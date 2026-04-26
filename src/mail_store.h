#ifndef MAIL_STORE_H
#define MAIL_STORE_H

#include <string>
#include <vector>
#include <map>
#include <mutex>

/*
 * MailStore - Dosya Tabanli E-posta Depolama
 * Her kullanici icin data/mailboxes/<username>/inbox ve sent dizinleri olusturulur.
 * E-postalar .eml dosyalari olarak saklanir.
 * Mail yonlendirme (forwarding) destegi vardir.
 */

struct Mail {
    std::string messageId;
    std::string from;
    std::string to;
    std::string subject;
    std::string body;
    std::string date;
    std::string filename;
};

class MailStore {
private:
    std::string baseDir;
    std::mutex mtx;

    // Mail yonlendirme tablosu: kaynak -> hedef listesi
    std::map<std::string, std::vector<std::string>> forwardingRules;

    std::string getInboxPath(const std::string& username);
    std::string getSentPath(const std::string& username);
    std::string sanitizeFilename(const std::string& name);
    Mail parseEmlFile(const std::string& filepath);

public:
    MailStore(const std::string& dataDir);

    bool createMailbox(const std::string& username);
    bool saveMail(const std::string& username, const Mail& mail, bool isSent = false);
    std::vector<Mail> loadInbox(const std::string& username);
    std::vector<Mail> loadSent(const std::string& username);
    Mail getMail(const std::string& username, int index, bool isSent = false);
    std::string getRawMail(const std::string& username, int index, bool isSent = false);
    size_t getMailSize(const std::string& username, int index, bool isSent = false);
    bool deleteMail(const std::string& username, int index, bool isSent = false);
    int getMailCount(const std::string& username, bool isSent = false);
    long long getMailboxSize(const std::string& username);

    // Mail yonlendirme
    void addForwardingRule(const std::string& source, const std::string& target);
    std::vector<std::string> getForwardingTargets(const std::string& username);
    bool hasForwarding(const std::string& username);
};

#endif // MAIL_STORE_H
