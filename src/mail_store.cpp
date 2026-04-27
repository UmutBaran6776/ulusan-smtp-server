#include "mail_store.h"
#include "logger.h"
#include "utils.h"
#include <fstream>
#include <sstream>
#include <filesystem>
#include <algorithm>
#include <regex>

namespace fs = std::filesystem;

// ==================== Yardimci Path Fonksiyonlari ====================

std::string MailStore::getInboxPath(const std::string& username) {
    return baseDir + "\\mailboxes\\" + username + "\\inbox";
}

std::string MailStore::getSentPath(const std::string& username) {
    return baseDir + "\\mailboxes\\" + username + "\\sent";
}

std::string MailStore::getFolderPath(const std::string& username, const std::string& folder) {
    std::string folderLower = folder;
    std::transform(folderLower.begin(), folderLower.end(), folderLower.begin(), ::tolower);

    if (folderLower == "inbox") {
        return getInboxPath(username);
    } else if (folderLower == "sent") {
        return getSentPath(username);
    }
    // Varsayilan olarak inbox
    return getInboxPath(username);
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

// ==================== Flag Islemleri ====================
// Dosya adi formati: 20260427_120000_123_U0005_test_konu.eml
// Flag'ler ayri bir .flags dosyasinda saklanir

int MailStore::parseFlagsFromFilename(const std::string& filepath) {
    // .flags dosyasindan oku
    std::string flagFile = filepath + ".flags";
    std::ifstream file(flagFile);
    if (!file.is_open()) {
        return FLAG_RECENT; // Yeni mail, varsayilan flag
    }
    int flags = 0;
    file >> flags;
    file.close();
    return flags;
}

std::string MailStore::flagsToSuffix(int flags) {
    std::string suffix;
    if (flags & FLAG_SEEN) suffix += "S";
    if (flags & FLAG_DELETED) suffix += "D";
    if (flags & FLAG_FLAGGED) suffix += "F";
    if (flags & FLAG_ANSWERED) suffix += "A";
    if (flags & FLAG_DRAFT) suffix += "T";
    return suffix;
}

int MailStore::parseUidFromFilename(const std::string& filename) {
    // Dosya adi formati: ...._U0005_konu.eml
    // U ile baslayan 4+ haneli UID aranir
    size_t uPos = filename.find("_U");
    if (uPos != std::string::npos) {
        size_t start = uPos + 2;
        size_t end = filename.find('_', start);
        if (end == std::string::npos) {
            end = filename.find('.', start);
        }
        if (end != std::string::npos && end > start) {
            try {
                return std::stoi(filename.substr(start, end - start));
            } catch (...) {}
        }
    }
    return 0;
}

// ==================== UID Sayac Yonetimi ====================

int MailStore::loadUidCounter(const std::string& username, const std::string& folder) {
    std::string key = username + "/" + folder;
    auto it = uidCounters.find(key);
    if (it != uidCounters.end()) {
        return it->second;
    }

    // Dosyadan oku
    std::string counterFile = getFolderPath(username, folder) + "\\.uidnext";
    std::ifstream file(counterFile);
    int uid = 1;
    if (file.is_open()) {
        file >> uid;
        file.close();
    } else {
        // Mevcut dosyalardan en yuksek UID'yi bul
        auto files = getSortedMailFiles(getFolderPath(username, folder));
        for (const auto& f : files) {
            int fileUid = parseUidFromFilename(f);
            if (fileUid >= uid) uid = fileUid + 1;
        }
    }

    uidCounters[key] = uid;
    return uid;
}

void MailStore::saveUidCounter(const std::string& username, const std::string& folder, int uid) {
    std::string key = username + "/" + folder;
    uidCounters[key] = uid;

    std::string counterFile = getFolderPath(username, folder) + "\\.uidnext";
    std::ofstream file(counterFile, std::ios::trunc);
    if (file.is_open()) {
        file << uid;
        file.close();
    }
}

int MailStore::getNextUid(const std::string& username, const std::string& folder) {
    int uid = loadUidCounter(username, folder);
    saveUidCounter(username, folder, uid + 1);
    return uid;
}

// ==================== Sirali Dosya Listesi ====================

std::vector<std::string> MailStore::getSortedMailFiles(const std::string& dirPath) {
    std::vector<std::string> files;
    if (!fs::exists(dirPath)) return files;

    for (const auto& entry : fs::directory_iterator(dirPath)) {
        if (entry.path().extension() == ".eml") {
            files.push_back(entry.path().string());
        }
    }

    // Dosya adina gore sirala (timestamp iceriyor - eskiden yeniye)
    std::sort(files.begin(), files.end());
    return files;
}

// ==================== EML Dosyasi Parse ====================
// Basit bir .eml parser. From, To, Subject, Date ve body ayristirir.
Mail MailStore::parseEmlFile(const std::string& filepath) {
    Mail mail;
    std::ifstream file(filepath);
    if (!file.is_open()) return mail;

    mail.filename = filepath;
    mail.uid = parseUidFromFilename(filepath);
    mail.flags = parseFlagsFromFilename(filepath);

    bool inBody = false;
    std::string line;
    std::ostringstream bodyStream;

    while (std::getline(file, line)) {
        // \r temizle
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        if (!inBody) {
            if (line.empty()) {
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

    std::string folder = isSent ? "sent" : "inbox";
    int uid = loadUidCounter(username, folder);
    saveUidCounter(username, folder, uid + 1);

    // UID'yi dosya adina ekle
    char uidBuf[16];
    snprintf(uidBuf, sizeof(uidBuf), "U%04d", uid);

    std::string filename = utils::getTimestampForFilename() + "_" +
                          std::string(uidBuf) + "_" +
                          sanitizeFilename(mail.subject.substr(0, 20)) + ".eml";
    std::string filepath = dir + "\\" + filename;

    std::ofstream file(filepath, std::ios::binary);
    if (!file.is_open()) {
        LOG_ERROR("MailStore: Mail kaydedilemedi: " + filepath);
        return false;
    }

    file << "Message-ID: " << mail.messageId << "\r\n";
    file << "From: " << mail.from << "\r\n";
    file << "To: " << mail.to << "\r\n";
    file << "Subject: " << mail.subject << "\r\n";
    file << "Date: " << mail.date << "\r\n";
    file << "MIME-Version: 1.0\r\n";
    file << "Content-Type: text/plain; charset=UTF-8\r\n";
    file << "\r\n";
    file << mail.body;
    file.close();

    // Yeni mail icin varsayilan flag: RECENT
    int initialFlags = FLAG_RECENT;
    if (isSent) {
        initialFlags = FLAG_SEEN; // Gonderilen mailler okunmus sayilir
    }
    std::string flagFile = filepath + ".flags";
    std::ofstream ff(flagFile, std::ios::trunc);
    if (ff.is_open()) {
        ff << initialFlags;
        ff.close();
    }

    LOG_INFO("MailStore: Mail kaydedildi (UID=" + std::to_string(uid) + "): " + filepath);
    return true;
}

// ==================== Klasor Bazli Mail Yukleme ====================

std::vector<Mail> MailStore::loadFolder(const std::string& username, const std::string& folder) {
    std::lock_guard<std::mutex> lock(mtx);
    std::vector<Mail> mails;

    std::string path = getFolderPath(username, folder);
    if (!fs::exists(path)) return mails;

    auto files = getSortedMailFiles(path);

    for (const auto& filepath : files) {
        Mail mail = parseEmlFile(filepath);
        if (!mail.from.empty()) {
            mails.push_back(mail);
        }
    }
    return mails;
}

// ==================== Gelen Kutusunu Yukle ====================
std::vector<Mail> MailStore::loadInbox(const std::string& username) {
    return loadFolder(username, "inbox");
}

// ==================== Gonderilen Kutusunu Yukle ====================
std::vector<Mail> MailStore::loadSent(const std::string& username) {
    return loadFolder(username, "sent");
}

// ==================== Belirli Mail Getir ====================
Mail MailStore::getMail(const std::string& username, int index, bool isSent) {
    auto mails = isSent ? loadSent(username) : loadInbox(username);
    if (index >= 0 && index < (int)mails.size()) {
        return mails[index];
    }
    return Mail{};
}

// ==================== Ham EML Icerigi ====================
std::string MailStore::getRawMail(const std::string& username, int index, bool isSent) {
    std::string folder = isSent ? "sent" : "inbox";
    return getRawMailFromFolder(username, folder, index);
}

std::string MailStore::getRawMailFromFolder(const std::string& username,
                                            const std::string& folder, int index) {
    std::lock_guard<std::mutex> lock(mtx);
    std::string path = getFolderPath(username, folder);
    if (!fs::exists(path)) return "";

    auto files = getSortedMailFiles(path);

    if (index >= 0 && index < (int)files.size()) {
        std::ifstream file(files[index]);
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

    auto files = getSortedMailFiles(path);

    if (index >= 0 && index < (int)files.size()) {
        return (size_t)fs::file_size(files[index]);
    }
    return 0;
}

// ==================== Mail Sil ====================
bool MailStore::deleteMail(const std::string& username, int index, bool isSent) {
    std::lock_guard<std::mutex> lock(mtx);

    std::string path = isSent ? getSentPath(username) : getInboxPath(username);
    if (!fs::exists(path)) return false;

    auto files = getSortedMailFiles(path);

    if (index >= 0 && index < (int)files.size()) {
        try {
            fs::remove(files[index]);
            // Flag dosyasini da sil
            std::string flagFile = files[index] + ".flags";
            if (fs::exists(flagFile)) {
                fs::remove(flagFile);
            }
            LOG_INFO("MailStore: Mail silindi: " + files[index]);
            return true;
        } catch (...) {
            return false;
        }
    }
    return false;
}

// ==================== Mail Sayisi ====================
int MailStore::getMailCount(const std::string& username, bool isSent) {
    std::string folder = isSent ? "sent" : "inbox";
    return getMailCountInFolder(username, folder);
}

int MailStore::getMailCountInFolder(const std::string& username, const std::string& folder) {
    std::lock_guard<std::mutex> lock(mtx);
    std::string path = getFolderPath(username, folder);
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

// ==================== Flag Yonetimi ====================

bool MailStore::setMailFlags(const std::string& username, const std::string& folder,
                             int index, int flags) {
    std::lock_guard<std::mutex> lock(mtx);
    std::string path = getFolderPath(username, folder);
    if (!fs::exists(path)) return false;

    auto files = getSortedMailFiles(path);
    if (index < 0 || index >= (int)files.size()) return false;

    std::string flagFile = files[index] + ".flags";
    std::ofstream ff(flagFile, std::ios::trunc);
    if (ff.is_open()) {
        ff << flags;
        ff.close();
        return true;
    }
    return false;
}

bool MailStore::addMailFlags(const std::string& username, const std::string& folder,
                              int index, int flagsToAdd) {
    std::lock_guard<std::mutex> lock(mtx);
    std::string path = getFolderPath(username, folder);
    if (!fs::exists(path)) return false;

    auto files = getSortedMailFiles(path);
    if (index < 0 || index >= (int)files.size()) return false;

    int currentFlags = parseFlagsFromFilename(files[index]);
    int newFlags = currentFlags | flagsToAdd;

    std::string flagFile = files[index] + ".flags";
    std::ofstream ff(flagFile, std::ios::trunc);
    if (ff.is_open()) {
        ff << newFlags;
        ff.close();
        return true;
    }
    return false;
}

bool MailStore::removeMailFlags(const std::string& username, const std::string& folder,
                                 int index, int flagsToRemove) {
    std::lock_guard<std::mutex> lock(mtx);
    std::string path = getFolderPath(username, folder);
    if (!fs::exists(path)) return false;

    auto files = getSortedMailFiles(path);
    if (index < 0 || index >= (int)files.size()) return false;

    int currentFlags = parseFlagsFromFilename(files[index]);
    int newFlags = currentFlags & ~flagsToRemove;

    std::string flagFile = files[index] + ".flags";
    std::ofstream ff(flagFile, std::ios::trunc);
    if (ff.is_open()) {
        ff << newFlags;
        ff.close();
        return true;
    }
    return false;
}

int MailStore::getMailFlags(const std::string& username, const std::string& folder, int index) {
    std::lock_guard<std::mutex> lock(mtx);
    std::string path = getFolderPath(username, folder);
    if (!fs::exists(path)) return 0;

    auto files = getSortedMailFiles(path);
    if (index < 0 || index >= (int)files.size()) return 0;

    return parseFlagsFromFilename(files[index]);
}

// ==================== EXPUNGE ====================
// \Deleted isaretli mailleri kaldir, silinen mesaj numaralarini dondur

std::vector<int> MailStore::expungeMails(const std::string& username, const std::string& folder) {
    std::lock_guard<std::mutex> lock(mtx);
    std::vector<int> expunged;

    std::string path = getFolderPath(username, folder);
    if (!fs::exists(path)) return expunged;

    auto files = getSortedMailFiles(path);

    // Sondan basa dogru sil (index kaymamasini saglar)
    for (int i = (int)files.size() - 1; i >= 0; i--) {
        int flags = parseFlagsFromFilename(files[i]);
        if (flags & FLAG_DELETED) {
            try {
                fs::remove(files[i]);
                // Flag dosyasini da sil
                std::string flagFile = files[i] + ".flags";
                if (fs::exists(flagFile)) {
                    fs::remove(flagFile);
                }
                expunged.push_back(i + 1); // 1-based
                LOG_INFO("MailStore: EXPUNGE - mail silindi: " + files[i]);
            } catch (...) {
                LOG_ERROR("MailStore: EXPUNGE - silinemedi: " + files[i]);
            }
        }
    }

    // Silinenleri kucukten buyuge sirala (IMAP protokolu icin)
    std::sort(expunged.begin(), expunged.end());
    return expunged;
}

// ==================== APPEND ====================
// IMAP APPEND komutu ile mail ekleme

bool MailStore::appendMail(const std::string& username, const std::string& folder,
                            const std::string& rawMessage, int flags) {
    std::lock_guard<std::mutex> lock(mtx);

    std::string dir = getFolderPath(username, folder);
    fs::create_directories(dir);

    int uid = loadUidCounter(username, folder);
    saveUidCounter(username, folder, uid + 1);

    char uidBuf[16];
    snprintf(uidBuf, sizeof(uidBuf), "U%04d", uid);

    // Mesajdan Subject cikart
    std::string subject = "appended";
    std::istringstream iss(rawMessage);
    std::string line;
    while (std::getline(iss, line)) {
        if (line.empty() || line == "\r") break;
        if (utils::startsWithCI(line, "Subject: ")) {
            subject = utils::trim(line.substr(9));
            break;
        }
    }

    std::string filename = utils::getTimestampForFilename() + "_" +
                          std::string(uidBuf) + "_" +
                          sanitizeFilename(subject.substr(0, 20)) + ".eml";
    std::string filepath = dir + "\\" + filename;

    std::ofstream file(filepath, std::ios::binary);
    if (!file.is_open()) {
        LOG_ERROR("MailStore: APPEND basarisiz: " + filepath);
        return false;
    }
    file << rawMessage;
    file.close();

    // Flag kaydet
    std::string flagFile = filepath + ".flags";
    std::ofstream ff(flagFile, std::ios::trunc);
    if (ff.is_open()) {
        ff << flags;
        ff.close();
    }

    LOG_INFO("MailStore: APPEND basarili (UID=" + std::to_string(uid) + "): " + filepath);
    return true;
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
