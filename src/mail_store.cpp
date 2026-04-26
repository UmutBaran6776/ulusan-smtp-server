#include "mail_store.h"
#include "logger.h"
#include "utils.h"
#include <fstream>
#include <sstream>
#include <filesystem>
#include <algorithm>

namespace fs = std::filesystem;

// ==================== Yardimci Path Fonksiyonlari ====================

std::string MailStore::getInboxPath(const std::string& username) {
    return baseDir + "\\mailboxes\\" + username + "\\inbox";
}

std::string MailStore::getSentPath(const std::string& username) {
    return baseDir + "\\mailboxes\\" + username + "\\sent";
}

std::string MailStore::sanitizeFilename(const std::string& name) {
    std::string result;
    for (char c : name) {
        if (isalnum(c) || c == '_' || c == '-' || c == '.') {
            result += c;
        } else if (c == ' ') {
            result += '_';
        }
    }
    if (result.empty()) result = "mail";
    return result;
}

// ==================== EML Dosyasi Parse ====================
// Basit bir .eml parser. From, To, Subject, Date ve body ayristirir.
Mail MailStore::parseEmlFile(const std::string& filepath) {
    Mail mail;
    std::ifstream file(filepath);
    if (!file.is_open()) return mail;

    mail.filename = filepath;
    bool inBody = false;
    std::string line;
    std::ostringstream bodyStream;

    while (std::getline(file, line)) {
        if (!inBody) {
            if (line.empty() || line == "\r") {
                inBody = true;
                continue;
            }
            if (utils::startsWithCI(line, "From: ")) {
                mail.from = utils::trim(line.substr(6));
            } else if (utils::startsWithCI(line, "To: ")) {
                mail.to = utils::trim(line.substr(4));
            } else if (utils::startsWithCI(line, "Subject: ")) {
                mail.subject = utils::trim(line.substr(9));
            } else if (utils::startsWithCI(line, "Date: ")) {
                mail.date = utils::trim(line.substr(6));
            } else if (utils::startsWithCI(line, "Message-ID: ")) {
                mail.messageId = utils::trim(line.substr(12));
            }
        } else {
            bodyStream << line << "\n";
        }
    }
    mail.body = bodyStream.str();
    file.close();
    return mail;
}

// ==================== Constructor ====================
MailStore::MailStore(const std::string& dataDir) : baseDir(dataDir) {
    fs::create_directories(baseDir + "\\mailboxes");
    LOG_INFO("MailStore: Mail deposu baslatildi: " + baseDir);
}

// ==================== Posta Kutusu Olustur ====================
bool MailStore::createMailbox(const std::string& username) {
    std::lock_guard<std::mutex> lock(mtx);
    try {
        fs::create_directories(getInboxPath(username));
        fs::create_directories(getSentPath(username));
        LOG_INFO("MailStore: Posta kutusu olusturuldu: " + username);
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("MailStore: Posta kutusu olusturulamadi: " + std::string(e.what()));
        return false;
    }
}

// ==================== Mail Kaydet ====================
// Mail'i .eml formatiyla dosyaya kaydeder.
bool MailStore::saveMail(const std::string& username, const Mail& mail, bool isSent) {
    std::lock_guard<std::mutex> lock(mtx);

    std::string dir = isSent ? getSentPath(username) : getInboxPath(username);
    fs::create_directories(dir);

    std::string filename = utils::getTimestampForFilename() + "_" +
                          sanitizeFilename(mail.subject.substr(0, 20)) + ".eml";
    std::string filepath = dir + "\\" + filename;

    std::ofstream file(filepath);
    if (!file.is_open()) {
        LOG_ERROR("MailStore: Mail kaydedilemedi: " + filepath);
        return false;
    }

    file << "Message-ID: " << mail.messageId << "\r\n";
    file << "From: " << mail.from << "\r\n";
    file << "To: " << mail.to << "\r\n";
    file << "Subject: " << mail.subject << "\r\n";
    file << "Date: " << mail.date << "\r\n";
    file << "\r\n";
    file << mail.body;
    file.close();

    LOG_INFO("MailStore: Mail kaydedildi: " + filepath);
    return true;
}

// ==================== Gelen Kutusunu Yukle ====================
std::vector<Mail> MailStore::loadInbox(const std::string& username) {
    std::lock_guard<std::mutex> lock(mtx);
    std::vector<Mail> mails;

    std::string path = getInboxPath(username);
    if (!fs::exists(path)) return mails;

    // Dosyalari sirali yukle
    std::vector<fs::directory_entry> entries;
    for (const auto& entry : fs::directory_iterator(path)) {
        if (entry.path().extension() == ".eml") {
            entries.push_back(entry);
        }
    }

    // Tarihe gore sirala (dosya adina gore - timestamp iceriyor)
    std::sort(entries.begin(), entries.end(),
        [](const fs::directory_entry& a, const fs::directory_entry& b) {
            return a.path().filename() > b.path().filename(); // Yeniden eskiye
        });

    for (const auto& entry : entries) {
        Mail mail = parseEmlFile(entry.path().string());
        if (!mail.from.empty()) {
            mails.push_back(mail);
        }
    }
    return mails;
}

// ==================== Gonderilen Kutusunu Yukle ====================
std::vector<Mail> MailStore::loadSent(const std::string& username) {
    std::lock_guard<std::mutex> lock(mtx);
    std::vector<Mail> mails;

    std::string path = getSentPath(username);
    if (!fs::exists(path)) return mails;

    std::vector<fs::directory_entry> entries;
    for (const auto& entry : fs::directory_iterator(path)) {
        if (entry.path().extension() == ".eml") {
            entries.push_back(entry);
        }
    }

    std::sort(entries.begin(), entries.end(),
        [](const fs::directory_entry& a, const fs::directory_entry& b) {
            return a.path().filename() > b.path().filename();
        });

    for (const auto& entry : entries) {
        Mail mail = parseEmlFile(entry.path().string());
        if (!mail.from.empty()) {
            mails.push_back(mail);
        }
    }
    return mails;
}

// ==================== Belirli Mail Getir ====================
Mail MailStore::getMail(const std::string& username, int index, bool isSent) {
    auto mails = isSent ? loadSent(username) : loadInbox(username);
    if (index >= 0 && index < (int)mails.size()) {
        return mails[index];
    }
    return Mail{};
}

// ==================== Ham EML İcerigi ====================
std::string MailStore::getRawMail(const std::string& username, int index, bool isSent) {
    std::lock_guard<std::mutex> lock(mtx);
    std::string path = isSent ? getSentPath(username) : getInboxPath(username);
    if (!fs::exists(path)) return "";

    std::vector<fs::directory_entry> entries;
    for (const auto& entry : fs::directory_iterator(path)) {
        if (entry.path().extension() == ".eml") entries.push_back(entry);
    }
    std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
        return a.path().filename() > b.path().filename();
    });

    if (index >= 0 && index < (int)entries.size()) {
        std::ifstream file(entries[index].path());
        if (file) {
            std::ostringstream ss;
            ss << file.rdbuf();
            return ss.str();
        }
    }
    return "";
}

// ==================== Mail Boyutu ====================
size_t MailStore::getMailSize(const std::string& username, int index, bool isSent) {
    std::lock_guard<std::mutex> lock(mtx);
    std::string path = isSent ? getSentPath(username) : getInboxPath(username);
    if (!fs::exists(path)) return 0;

    std::vector<fs::directory_entry> entries;
    for (const auto& entry : fs::directory_iterator(path)) {
        if (entry.path().extension() == ".eml") entries.push_back(entry);
    }
    std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
        return a.path().filename() > b.path().filename();
    });

    if (index >= 0 && index < (int)entries.size()) {
        return fs::file_size(entries[index].path());
    }
    return 0;
}

// ==================== Mail Sil ====================
bool MailStore::deleteMail(const std::string& username, int index, bool isSent) {
    std::lock_guard<std::mutex> lock(mtx);

    std::string path = isSent ? getSentPath(username) : getInboxPath(username);
    if (!fs::exists(path)) return false;

    std::vector<fs::directory_entry> entries;
    for (const auto& entry : fs::directory_iterator(path)) {
        if (entry.path().extension() == ".eml") {
            entries.push_back(entry);
        }
    }

    std::sort(entries.begin(), entries.end(),
        [](const fs::directory_entry& a, const fs::directory_entry& b) {
            return a.path().filename() > b.path().filename();
        });

    if (index >= 0 && index < (int)entries.size()) {
        try {
            fs::remove(entries[index].path());
            LOG_INFO("MailStore: Mail silindi: " + entries[index].path().string());
            return true;
        } catch (...) {
            return false;
        }
    }
    return false;
}

// ==================== Mail Sayisi ====================
int MailStore::getMailCount(const std::string& username, bool isSent) {
    std::lock_guard<std::mutex> lock(mtx);

    std::string path = isSent ? getSentPath(username) : getInboxPath(username);
    if (!fs::exists(path)) return 0;

    int count = 0;
    for (const auto& entry : fs::directory_iterator(path)) {
        if (entry.path().extension() == ".eml") {
            count++;
        }
    }
    return count;
}

// ==================== Posta Kutusu Boyutu ====================
long long MailStore::getMailboxSize(const std::string& username) {
    std::lock_guard<std::mutex> lock(mtx);
    long long totalSize = 0;

    std::string inboxPath = getInboxPath(username);
    std::string sentPath = getSentPath(username);

    auto calcSize = [&](const std::string& path) {
        if (!fs::exists(path)) return;
        for (const auto& entry : fs::directory_iterator(path)) {
            if (entry.is_regular_file()) {
                totalSize += entry.file_size();
            }
        }
    };

    calcSize(inboxPath);
    calcSize(sentPath);
    return totalSize;
}

// ==================== Mail Yonlendirme ====================

// Yonlendirme kurali ekle: source'a gelen mailler target'a da iletilir
void MailStore::addForwardingRule(const std::string& source, const std::string& target) {
    forwardingRules[source].push_back(target);
    LOG_INFO("MailStore: Yonlendirme eklendi: " + source + " -> " + target);
}

// Belirli bir kullanici icin yonlendirme hedeflerini getir
std::vector<std::string> MailStore::getForwardingTargets(const std::string& username) {
    auto it = forwardingRules.find(username);
    if (it != forwardingRules.end()) {
        return it->second;
    }
    return {};
}

// Kullanicinin yonlendirme kurali var mi
bool MailStore::hasForwarding(const std::string& username) {
    return forwardingRules.find(username) != forwardingRules.end();
}
