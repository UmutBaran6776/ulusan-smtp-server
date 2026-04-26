#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <filesystem>
#include <cstdlib>

#include "utils.h"
#include "base64.h"
#include "logger.h"
#include "auth.h"
#include "mail_store.h"
#include "smtp_server.h"
#include "pop3_server.h"

namespace fs = std::filesystem;

// ==================== Global Degiskenler ====================
static const int SMTP_PORT = 25;
static const int POP3_PORT = 11;
static const std::string DOMAIN = "ulusansigorta.com.tr";

std::string currentUser = "";
UserRole currentRole = UserRole::USER;

// ==================== Yardimci Fonksiyonlar ====================

void clearScreen() {
    system("cls");
}

void pressEnter() {
    std::cout << "\n  Devam etmek icin ENTER'a basin...";
    std::cin.ignore(10000, '\n');
    std::cin.get();
}

void printHeader(const std::string& title) {
    clearScreen();
    std::cout << "\n";
    std::cout << "  ============================================================\n";
    std::cout << "       ULUSAN SIGORTA - MAIL SUNUCUSU\n";
    std::cout << "  ============================================================\n";
    std::cout << "       " << title << "\n";
    std::cout << "  ------------------------------------------------------------\n\n";
}

// ==================== E-posta Gonder ====================

void sendEmailMenu(AuthManager& auth, MailStore& store) {
    printHeader("E-POSTA GONDER");

    std::string toUser, subject, body, line;

    std::cout << "  Alici kullanici adi: ";
    std::getline(std::cin, toUser);
    toUser = utils::trim(toUser);

    if (!auth.userExists(toUser)) {
        std::cout << "\n  [HATA] Kullanici bulunamadi: " << toUser << "\n";
        pressEnter();
        return;
    }

    std::cout << "  Konu: ";
    std::getline(std::cin, subject);

    std::cout << "  Mesaj (bos satir ile bitirin):\n";
    std::cout << "  --------------------------------\n";
    while (true) {
        std::cout << "  ";
        std::getline(std::cin, line);
        if (line.empty()) break;
        body += line + "\n";
    }

    Mail mail;
    mail.messageId = utils::generateMessageId();
    mail.from = currentUser + "@" + DOMAIN;
    mail.to = toUser + "@" + DOMAIN;
    mail.subject = subject;
    mail.body = body;
    mail.date = utils::getRFC2822Date();

    // Alicinin inbox'ina kaydet
    store.saveMail(toUser, mail, false);
    // Gonderenin sent'ine kaydet
    store.saveMail(currentUser, mail, true);

    std::cout << "\n  [OK] E-posta basariyla gonderildi!\n";
    std::cout << "       Kime: " << toUser << "@" << DOMAIN << "\n";
    LOG_INFO("Menu: Mail gonderildi: " + currentUser + " -> " + toUser);
    pressEnter();
}

// ==================== Gelen Kutusu ====================

void inboxMenu(MailStore& store) {
    while (true) {
        printHeader("GELEN KUTUSU - " + currentUser + "@" + DOMAIN);

        auto mails = store.loadInbox(currentUser);
        if (mails.empty()) {
            std::cout << "  Gelen kutunuz bos.\n";
            pressEnter();
            return;
        }

        std::cout << "  No  Gonderen                Konu                    Tarih\n";
        std::cout << "  --- --------------------- ----------------------- ---------------------\n";
        for (int i = 0; i < (int)mails.size(); i++) {
            std::string from = mails[i].from;
            if (from.size() > 21) from = from.substr(0, 18) + "...";
            std::string subj = mails[i].subject;
            if (subj.size() > 23) subj = subj.substr(0, 20) + "...";
            std::string date = mails[i].date;
            if (date.size() > 21) date = date.substr(0, 21);

            printf("  %-3d %-21s %-23s %s\n", i + 1, from.c_str(), subj.c_str(), date.c_str());
        }

        std::cout << "\n  Numara girin (oku), 's' (sil), '0' (geri): ";
        std::string input;
        std::getline(std::cin, input);
        input = utils::trim(input);

        if (input == "0" || input.empty()) return;

        if (input[0] == 's' || input[0] == 'S') {
            std::cout << "  Silinecek mail no: ";
            std::getline(std::cin, input);
            int idx = std::stoi(input) - 1;
            if (store.deleteMail(currentUser, idx, false)) {
                std::cout << "  [OK] Mail silindi.\n";
            } else {
                std::cout << "  [HATA] Mail silinemedi.\n";
            }
            pressEnter();
            continue;
        }

        int idx = std::stoi(input) - 1;
        if (idx < 0 || idx >= (int)mails.size()) {
            std::cout << "  [HATA] Gecersiz numara.\n";
            pressEnter();
            continue;
        }

        // Maili goster
        printHeader("MAIL OKU");
        const Mail& m = mails[idx];
        std::cout << "  Gonderen : " << m.from << "\n";
        std::cout << "  Alici    : " << m.to << "\n";
        std::cout << "  Konu     : " << m.subject << "\n";
        std::cout << "  Tarih    : " << m.date << "\n";
        std::cout << "  Mesaj ID : " << m.messageId << "\n";
        std::cout << "  ------------------------------------------------------------\n";
        std::cout << "\n" << m.body << "\n";
        pressEnter();
    }
}

// ==================== Gonderilen Kutusu ====================

void sentMenu(MailStore& store) {
    printHeader("GONDERILEN KUTUSU - " + currentUser + "@" + DOMAIN);

    auto mails = store.loadSent(currentUser);
    if (mails.empty()) {
        std::cout << "  Gonderilen kutunuz bos.\n";
        pressEnter();
        return;
    }

    std::cout << "  No  Alici                  Konu                    Tarih\n";
    std::cout << "  --- --------------------- ----------------------- ---------------------\n";
    for (int i = 0; i < (int)mails.size(); i++) {
        std::string to = mails[i].to;
        if (to.size() > 21) to = to.substr(0, 18) + "...";
        std::string subj = mails[i].subject;
        if (subj.size() > 23) subj = subj.substr(0, 20) + "...";
        std::string date = mails[i].date;
        if (date.size() > 21) date = date.substr(0, 21);

        printf("  %-3d %-21s %-23s %s\n", i + 1, to.c_str(), subj.c_str(), date.c_str());
    }

    std::cout << "\n  Numara girin (oku) veya '0' (geri): ";
    std::string input;
    std::getline(std::cin, input);
    input = utils::trim(input);

    if (input == "0" || input.empty()) return;

    int idx = std::stoi(input) - 1;
    if (idx >= 0 && idx < (int)mails.size()) {
        printHeader("GONDERILEN MAIL");
        const Mail& m = mails[idx];
        std::cout << "  Gonderen : " << m.from << "\n";
        std::cout << "  Alici    : " << m.to << "\n";
        std::cout << "  Konu     : " << m.subject << "\n";
        std::cout << "  Tarih    : " << m.date << "\n";
        std::cout << "  ------------------------------------------------------------\n";
        std::cout << "\n" << m.body << "\n";
    }
    pressEnter();
}

// ==================== Kullanici Yonetimi (Admin) ====================

void userManagementMenu(AuthManager& auth, MailStore& store) {
    while (true) {
        printHeader("KULLANICI YONETIMI (ADMIN)");

        auto users = auth.listUsers();
        std::cout << "  Kayitli Kullanicilar (" << users.size() << "):\n";
        std::cout << "  ---------------------------------------------------------\n";
        std::cout << "  No  Kullanici            E-posta                    Rol\n";
        std::cout << "  --- ------------------- -------------------------- ------\n";
        for (int i = 0; i < (int)users.size(); i++) {
            std::string roleStr = (users[i].role == UserRole::ADMIN) ? "ADMIN" : "USER";
            printf("  %-3d %-19s %-26s %s\n", i + 1,
                   users[i].username.c_str(), users[i].email.c_str(), roleStr.c_str());
        }

        std::cout << "\n  [1] Yeni Kullanici Ekle\n";
        std::cout << "  [2] Kullanici Sil\n";
        std::cout << "  [0] Geri\n";
        std::cout << "\n  Seciminiz: ";

        std::string input;
        std::getline(std::cin, input);
        input = utils::trim(input);

        if (input == "0") return;

        if (input == "1") {
            std::string uname, pass, dname, roleInput;
            std::cout << "\n  Kullanici adi: ";
            std::getline(std::cin, uname);
            std::cout << "  Sifre: ";
            std::getline(std::cin, pass);
            std::cout << "  Gorunen ad: ";
            std::getline(std::cin, dname);
            std::cout << "  Rol (admin/user): ";
            std::getline(std::cin, roleInput);

            UserRole role = (utils::toLower(roleInput) == "admin") ?
                            UserRole::ADMIN : UserRole::USER;

            if (auth.registerUser(uname, pass, role, dname)) {
                store.createMailbox(uname);
                std::cout << "\n  [OK] Kullanici olusturuldu: " << uname << "@" << DOMAIN << "\n";
            } else {
                std::cout << "\n  [HATA] Kullanici olusturulamadi (zaten mevcut olabilir).\n";
            }
            pressEnter();
        } else if (input == "2") {
            std::string uname;
            std::cout << "\n  Silinecek kullanici adi: ";
            std::getline(std::cin, uname);
            if (uname == currentUser) {
                std::cout << "\n  [HATA] Kendinizi silemezsiniz!\n";
            } else if (auth.deleteUser(uname)) {
                std::cout << "\n  [OK] Kullanici silindi: " << uname << "\n";
            } else {
                std::cout << "\n  [HATA] Kullanici bulunamadi.\n";
            }
            pressEnter();
        }
    }
}

// ==================== Sunucu Loglari (Admin) ====================

void viewLogsMenu() {
    printHeader("SUNUCU LOGLARI");
    std::string logs = Logger::getInstance().readLogs(40);
    std::cout << logs << "\n";
    pressEnter();
}

// ==================== Sifre Degistir ====================

void changePasswordMenu(AuthManager& auth) {
    printHeader("SIFRE DEGISTIR");

    std::string oldPw, newPw, confirmPw;
    std::cout << "  Mevcut sifre: ";
    std::getline(std::cin, oldPw);
    std::cout << "  Yeni sifre: ";
    std::getline(std::cin, newPw);
    std::cout << "  Yeni sifre (tekrar): ";
    std::getline(std::cin, confirmPw);

    if (newPw != confirmPw) {
        std::cout << "\n  [HATA] Sifreler uyusmuyor!\n";
    } else if (auth.changePassword(currentUser, oldPw, newPw)) {
        std::cout << "\n  [OK] Sifre basariyla degistirildi.\n";
    } else {
        std::cout << "\n  [HATA] Mevcut sifre yanlis.\n";
    }
    pressEnter();
}

// ==================== Ana Menu (Giris Sonrasi) ====================

void mainMenu(AuthManager& auth, MailStore& store) {
    while (true) {
        int inboxCount = store.getMailCount(currentUser, false);
        int sentCount = store.getMailCount(currentUser, true);

        printHeader("ANA MENU");
        std::string roleStr = (currentRole == UserRole::ADMIN) ? " [ADMIN]" : "";
        std::cout << "  Hosgeldiniz, " << currentUser << "@" << DOMAIN << roleStr << "\n";
        std::cout << "  Gelen: " << inboxCount << " | Gonderilen: " << sentCount << "\n\n";

        std::cout << "  [1] E-posta Gonder\n";
        std::cout << "  [2] Gelen Kutusu (" << inboxCount << ")\n";
        std::cout << "  [3] Gonderilen Kutusu (" << sentCount << ")\n";
        std::cout << "  [4] Sifre Degistir\n";
        if (currentRole == UserRole::ADMIN) {
            std::cout << "  [5] Kullanici Yonetimi\n";
            std::cout << "  [6] Sunucu Loglari\n";
        }
        std::cout << "  [0] Cikis Yap\n";
        std::cout << "\n  Seciminiz: ";

        std::string input;
        std::getline(std::cin, input);
        input = utils::trim(input);

        if (input == "0") {
            LOG_INFO("Menu: Cikis yapildi: " + currentUser);
            currentUser = "";
            return;
        }
        else if (input == "1") sendEmailMenu(auth, store);
        else if (input == "2") inboxMenu(store);
        else if (input == "3") sentMenu(store);
        else if (input == "4") changePasswordMenu(auth);
        else if (input == "5" && currentRole == UserRole::ADMIN) userManagementMenu(auth, store);
        else if (input == "6" && currentRole == UserRole::ADMIN) viewLogsMenu();
    }
}

// ==================== Giris Ekrani ====================

bool loginMenu(AuthManager& auth, MailStore& store) {
    while (true) {
        printHeader("GIRIS");

        std::cout << "  [1] Giris Yap\n";
        std::cout << "  [2] Kayit Ol\n";
        std::cout << "  [0] Programi Kapat\n";
        std::cout << "\n  Seciminiz: ";

        std::string input;
        std::getline(std::cin, input);
        input = utils::trim(input);

        if (input == "0") return false;

        if (input == "1") {
            std::string uname, pass;
            std::cout << "\n  Kullanici adi: ";
            std::getline(std::cin, uname);
            std::cout << "  Sifre: ";
            std::getline(std::cin, pass);

            if (auth.authenticate(uname, pass)) {
                currentUser = uname;
                currentRole = auth.getUserRole(uname);
                User* user = auth.getUser(uname);
                std::cout << "\n  [OK] Giris basarili! Hosgeldiniz, "
                          << (user ? user->displayName : uname) << "\n";
                std::this_thread::sleep_for(std::chrono::seconds(1));
                mainMenu(auth, store);
            } else {
                std::cout << "\n  [HATA] Kullanici adi veya sifre yanlis!\n";
                pressEnter();
            }
        } else if (input == "2") {
            std::string uname, pass, pass2, dname;
            std::cout << "\n  Kullanici adi: ";
            std::getline(std::cin, uname);
            std::cout << "  Gorunen ad (Ad Soyad): ";
            std::getline(std::cin, dname);
            std::cout << "  Sifre: ";
            std::getline(std::cin, pass);
            std::cout << "  Sifre (tekrar): ";
            std::getline(std::cin, pass2);

            if (pass != pass2) {
                std::cout << "\n  [HATA] Sifreler uyusmuyor!\n";
            } else if (auth.registerUser(uname, pass, UserRole::USER, dname)) {
                store.createMailbox(uname);
                std::cout << "\n  [OK] Kayit basarili! E-posta adresiniz: "
                          << uname << "@" << DOMAIN << "\n";
                std::cout << "       Giris yapabilirsiniz.\n";
            } else {
                std::cout << "\n  [HATA] Kullanici adi zaten alinmis.\n";
            }
            pressEnter();
        }
    }
}

// ==================== MAIN ====================

int main() {
    // Windows konsol UTF-8 ayari
    SetConsoleOutputCP(65001);
    SetConsoleCP(65001);

    // Calisma dizinini belirle
    std::string exePath = fs::current_path().string();
    std::string dataDir = exePath + "\\data";
    std::string logFile = dataDir + "\\server.log";

    // Dizinleri olustur
    fs::create_directories(dataDir);
    fs::create_directories(dataDir + "\\mailboxes");

    // Logger baslat
    Logger::getInstance().init(logFile, false);
    LOG_INFO("=== Ulusan Sigorta Mail Sunucusu Baslatiliyor ===");

    // Winsock baslat
    if (!utils::initWinsock()) {
        std::cerr << "  [KRITIK HATA] Winsock baslatilamadi!\n";
        return 1;
    }
    LOG_INFO("Winsock baslatildi.");

    // Modulleri olustur
    AuthManager auth(dataDir);
    MailStore store(dataDir);

    // Varsayilan sirket hesaplari
    auth.createDefaultAdmin();
    store.createMailbox("admin");

    // Sirket mail hesaplari
    if (!auth.userExists("bayram")) {
        auth.registerUser("bayram", "bayram123", UserRole::ADMIN, "Bayram Ulusan");
        store.createMailbox("bayram");
    }
    if (!auth.userExists("fatos")) {
        auth.registerUser("fatos", "fatos123", UserRole::USER, "Fatos Ulusan");
        store.createMailbox("fatos");
    }

    // SMTP ve POP3 sunucularini baslat
    SmtpServer smtp(SMTP_PORT, auth, store);
    Pop3Server pop3(POP3_PORT, auth, store);

    bool smtpOk = smtp.start();
    bool pop3Ok = pop3.start();

    clearScreen();
    std::cout << "\n";
    std::cout << "  ============================================================\n";
    std::cout << "       ULUSAN SIGORTA - MAIL SUNUCUSU v1.0\n";
    std::cout << "  ============================================================\n";
    std::cout << "       Bil314 - Bilgisayar Aglari Projesi\n";
    std::cout << "       Umut Baran Ulusan  - 230206035\n";
    std::cout << "       Berkay Demirci     - 230206064\n";
    std::cout << "  ------------------------------------------------------------\n";
    std::cout << "       Domain : " << DOMAIN << "\n";
    std::cout << "       IP     : 78.186.12.5\n";
    std::cout << "       SMTP   : Port " << SMTP_PORT
              << (smtpOk ? " [AKTIF]" : " [HATA]") << "\n";
    std::cout << "       POP3   : Port " << POP3_PORT
              << (pop3Ok ? " [AKTIF]" : " [HATA]") << "\n";
    std::cout << "  ------------------------------------------------------------\n";
    std::cout << "       Sirket Hesaplari:\n";
    std::cout << "         bayram@ulusansigorta.com.tr (admin / bayram123)\n";
    std::cout << "         fatos@ulusansigorta.com.tr  (user  / fatos123)\n";
    std::cout << "         admin                       (admin / admin123)\n";
    std::cout << "  ============================================================\n";

    std::this_thread::sleep_for(std::chrono::seconds(2));

    // Giris menusu
    loginMenu(auth, store);

    // Temizlik
    std::cout << "\n  Sunucu kapatiliyor...\n";
    smtp.stop();
    pop3.stop();
    utils::cleanupWinsock();
    LOG_INFO("=== Sunucu kapatildi ===");

    std::cout << "  Gule gule!\n\n";
    return 0;
}
