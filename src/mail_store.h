#ifndef MAIL_STORE_H
#define MAIL_STORE_H

#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <set>

/*
 * MailStore - Dosya Tabanli E-posta Depolama
 * Her kullanici icin data/mailboxes/<username>/inbox ve sent dizinleri olusturulur.
 * E-postalar .eml dosyalari olarak saklanir.
 * Mail yonlendirme (forwarding) destegi vardir.
 *
 * Flag Yonetimi:
 *   Her mail icin flag bilgisi dosya adinda kodlanir.
 *   Ornek: 20260427_120000_123_test_konu_FS.eml
 *          F = Flagged, S = Seen, D = Deleted
 */

// Mail flag bitleri (IMAP uyumlu)
enum MailFlags {
    FLAG_NONE    = 0,
    FLAG_SEEN    = (1 << 0),  // \Seen    - Okundu
    FLAG_DELETED = (1 << 1),  // \Deleted - Silinmek icin isaretlendi
    FLAG_FLAGGED = (1 << 2),  // \Flagged - Yildizli/Onemli
    FLAG_RECENT  = (1 << 3),  // \Recent  - Yeni gelen
    FLAG_DRAFT   = (1 << 4),  // \Draft   - Taslak
    FLAG_ANSWERED= (1 << 5)   // \Answered - Cevaplanmis
};

struct Mail {
    std::string messageId;
    std::string from;
    std::string to;
    std::string subject;
    std::string body;
    std::string date;
    std::string filename;  // Dosya yolu
    int uid;               // IMAP UID (benzersiz, artan)
    int flags;             // MailFlags birlesimleri
};

class MailStore {
private:
    std::string baseDir;
    std::mutex mtx;

    // Mail yonlendirme tablosu: kaynak -> hedef listesi
    std::map<std::string, std::vector<std::string>> forwardingRules;

    // UID sayaclari: kullanici+kutu -> sonraki UID
    std::map<std::string, int> uidCounters;

    std::string getInboxPath(const std::string& username);
    std::string getSentPath(const std::string& username);
    std::string getFolderPath(const std::string& username, const std::string& folder);
    std::string sanitizeFilename(const std::string& name);
    Mail parseEmlFile(const std::string& filepath);

    // Flag islemleri (dosya adi bazli)
    int parseFlagsFromFilename(const std::string& filename);
    std::string flagsToSuffix(int flags);
    int parseUidFromFilename(const std::string& filename);

    // UID sayac dosyasi
    int loadUidCounter(const std::string& username, const std::string& folder);
    void saveUidCounter(const std::string& username, const std::string& folder, int uid);

    // Sirali dosya listesi getir
    std::vector<std::string> getSortedMailFiles(const std::string& dirPath);

public:
    MailStore(const std::string& dataDir);

    bool createMailbox(const std::string& username);
    bool saveMail(const std::string& username, const Mail& mail, bool isSent = false);
    std::vector<Mail> loadInbox(const std::string& username);
    std::vector<Mail> loadSent(const std::string& username);
    std::vector<Mail> loadFolder(const std::string& username, const std::string& folder);
    Mail getMail(const std::string& username, int index, bool isSent = false);
    std::string getRawMail(const std::string& username, int index, bool isSent = false);
    std::string getRawMailFromFolder(const std::string& username, const std::string& folder, int index);
    size_t getMailSize(const std::string& username, int index, bool isSent = false);
    bool deleteMail(const std::string& username, int index, bool isSent = false);
    int getMailCount(const std::string& username, bool isSent = false);
    int getMailCountInFolder(const std::string& username, const std::string& folder);
    long long getMailboxSize(const std::string& username);

    // Flag yonetimi (IMAP icin)
    bool setMailFlags(const std::string& username, const std::string& folder, int index, int flags);
    bool addMailFlags(const std::string& username, const std::string& folder, int index, int flagsToAdd);
    bool removeMailFlags(const std::string& username, const std::string& folder, int index, int flagsToRemove);
    int getMailFlags(const std::string& username, const std::string& folder, int index);

    // UID yonetimi
    int getNextUid(const std::string& username, const std::string& folder);
    int getUidValidity() { return 1; } // Sabit, sunucu yeniden basladiginda da gecerli

    // EXPUNGE: \Deleted isaretli mailleri kaldir
    std::vector<int> expungeMails(const std::string& username, const std::string& folder);

    // APPEND: IMAP uzerinden mail ekleme
    bool appendMail(const std::string& username, const std::string& folder,
                    const std::string& rawMessage, int flags);

    // Mail yonlendirme
    void addForwardingRule(const std::string& source, const std::string& target);
    std::vector<std::string> getForwardingTargets(const std::string& username);
    bool hasForwarding(const std::string& username);
};

#endif // MAIL_STORE_H
