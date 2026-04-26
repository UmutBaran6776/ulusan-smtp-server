#include "smtp_server.h"
#include "logger.h"
#include "utils.h"
#include "base64.h"
#include <iostream>
#include <sstream>
#include <vector>

// ==================== Constructor / Destructor ====================

SmtpServer::SmtpServer(int p, AuthManager& a, MailStore& s)
    : port(p), listenSocket(INVALID_SOCKET), running(false), auth(a), store(s) {
}

SmtpServer::~SmtpServer() {
    stop();
}

// ==================== Sunucuyu Baslat ====================

bool SmtpServer::start() {
    listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listenSocket == INVALID_SOCKET) {
        LOG_ERROR("SMTP: Socket olusturulamadi: " + std::to_string(WSAGetLastError()));
        return false;
    }

    // Port yeniden kullanimi icin
    int opt = 1;
    setsockopt(listenSocket, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(port);

    if (bind(listenSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        LOG_ERROR("SMTP: Bind hatasi port " + std::to_string(port) + ": " +
                  std::to_string(WSAGetLastError()));
        closesocket(listenSocket);
        return false;
    }

    if (listen(listenSocket, SOMAXCONN) == SOCKET_ERROR) {
        LOG_ERROR("SMTP: Listen hatasi: " + std::to_string(WSAGetLastError()));
        closesocket(listenSocket);
        return false;
    }

    running = true;
    serverThread = std::thread(&SmtpServer::serverLoop, this);
    LOG_INFO("SMTP: Sunucu baslatildi, port " + std::to_string(port));
    return true;
}

// ==================== Sunucuyu Durdur ====================

void SmtpServer::stop() {
    running = false;
    if (listenSocket != INVALID_SOCKET) {
        closesocket(listenSocket);
        listenSocket = INVALID_SOCKET;
    }
    if (serverThread.joinable()) {
        serverThread.join();
    }
    LOG_INFO("SMTP: Sunucu durduruldu.");
}

// ==================== Ana Sunucu Dongusu ====================
// Yeni baglantilar icin dinler ve her baglanti icin thread olusturur.

void SmtpServer::serverLoop() {
    while (running) {
        sockaddr_in clientAddr{};
        int addrLen = sizeof(clientAddr);

        SOCKET clientSocket = accept(listenSocket, (sockaddr*)&clientAddr, &addrLen);
        if (clientSocket == INVALID_SOCKET) {
            if (running) {
                LOG_ERROR("SMTP: Accept hatasi: " + std::to_string(WSAGetLastError()));
            }
            continue;
        }

        char addrStr[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &clientAddr.sin_addr, addrStr, sizeof(addrStr));
        std::string clientAddrString = std::string(addrStr) + ":" +
                                       std::to_string(ntohs(clientAddr.sin_port));

        // Her istemci icin ayri thread
        std::thread clientThread(&SmtpServer::handleClient, this, clientSocket, clientAddrString);
        clientThread.detach();
    }
}

// ==================== Yanit Gonder ====================

void SmtpServer::sendResponse(SOCKET sock, const std::string& response) {
    std::string msg = response + "\r\n";
    send(sock, msg.c_str(), (int)msg.size(), 0);
    LOG_DEBUG("SMTP TX: " + response);
}

// ==================== Satir Oku ====================
// SMTP protokolunde her komut \r\n ile biter.

std::string SmtpServer::recvLine(SOCKET sock) {
    std::string line;
    char ch;
    while (true) {
        int result = recv(sock, &ch, 1, 0);
        if (result <= 0) return ""; // Baglanti kapandi
        if (ch == '\n') {
            // \r\n'i temizle
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            return line;
        }
        line += ch;
        if (line.size() > 4096) return line; // Cok uzun satir korumasi
    }
}

// ==================== Istemci Baglantisini Yonet ====================
// SMTP durum makinesi: INIT -> GREETED -> AUTHENTICATED -> MAIL_FROM -> RCPT_TO -> DATA

void SmtpServer::handleClient(SOCKET clientSocket, std::string clientAddr) {
    LOG_INFO("SMTP: Yeni baglanti: " + clientAddr);

    // Durum makinesi durumlari
    enum class State {
        INIT, GREETED, AUTH_USER, AUTH_PASS,
        AUTHENTICATED, MAIL_FROM, RCPT_TO, DATA
    };

    State state = State::INIT;
    std::string authUsername;
    std::string mailFrom;
    std::vector<std::string> rcptTo;
    std::string mailData;

    // 220 karsilama mesaji
    sendResponse(clientSocket, "220 ulusansigorta.com.tr ESMTP Ulusan Sigorta Mail Server Ready");

    while (true) {
        // DATA durumunda satir satir oku
        if (state == State::DATA) {
            std::string line = recvLine(clientSocket);
            if (line.empty() && mailData.empty()) {
                // Baglanti kapandi
                break;
            }

            if (line == ".") {
                // DATA sonu - maili kaydet
                // Mail iceriginden Subject cikart
                std::string subject = "Konu Yok";
                std::istringstream dataStream(mailData);
                std::string dataLine;
                std::string bodyContent;
                bool inHeaders = true;

                while (std::getline(dataStream, dataLine)) {
                    if (inHeaders) {
                        std::string trimmed = utils::trim(dataLine);
                        if (trimmed.empty()) {
                            inHeaders = false;
                            continue;
                        }
                        if (utils::startsWithCI(dataLine, "Subject: ")) {
                            subject = utils::trim(dataLine.substr(9));
                        }
                    } else {
                        bodyContent += dataLine + "\n";
                    }
                }

                // Eger header yoksa, tum data body olsun
                if (bodyContent.empty()) {
                    bodyContent = mailData;
                }

                Mail mail;
                mail.messageId = utils::generateMessageId();
                mail.from = mailFrom;
                mail.subject = subject;
                mail.body = bodyContent;
                mail.date = utils::getRFC2822Date();

                // Her aliciya kaydet
                for (const auto& recipient : rcptTo) {
                    mail.to = recipient;
                    std::string rcptUser = utils::getUsernameFromEmail(
                        utils::extractEmail(recipient));
                    if (auth.userExists(rcptUser)) {
                        store.saveMail(rcptUser, mail, false); // inbox

                        // Mail yonlendirme kontrolu
                        auto targets = store.getForwardingTargets(rcptUser);
                        for (const auto& target : targets) {
                            if (auth.userExists(target)) {
                                store.saveMail(target, mail, false);
                                LOG_INFO("SMTP: Mail yonlendirildi: " +
                                         rcptUser + " -> " + target);
                            }
                        }
                    }
                }

                // Gonderenin sent klasorune kaydet
                std::string senderUser = utils::getUsernameFromEmail(
                    utils::extractEmail(mailFrom));
                mail.to = rcptTo.empty() ? "" : rcptTo[0];
                store.saveMail(senderUser, mail, true); // sent

                sendResponse(clientSocket, "250 OK: Mesaj teslim edildi");
                LOG_INFO("SMTP: Mail teslim edildi: " + mailFrom + " -> " +
                         std::to_string(rcptTo.size()) + " alici");

                // Durumu sifirla
                state = State::AUTHENTICATED;
                mailFrom.clear();
                rcptTo.clear();
                mailData.clear();
            } else {
                mailData += line + "\r\n";
            }
            continue;
        }

        // Normal komut oku
        std::string line = recvLine(clientSocket);
        if (line.empty()) break; // Baglanti kapandi

        std::string cmd = utils::trim(line);
        std::string cmdUpper = utils::toUpper(cmd);

        LOG_DEBUG("SMTP RX [" + clientAddr + "]: " + cmd);

        // ---- AUTH LOGIN: Kullanici adi bekleniyor ----
        if (state == State::AUTH_USER) {
            authUsername = base64::decode(utils::trim(cmd));
            sendResponse(clientSocket, "334 UGFzc3dvcmQ6"); // "Password:" base64
            state = State::AUTH_PASS;
            continue;
        }

        // ---- AUTH LOGIN: Sifre bekleniyor ----
        if (state == State::AUTH_PASS) {
            std::string password = base64::decode(utils::trim(cmd));
            if (auth.authenticate(authUsername, password)) {
                sendResponse(clientSocket, "235 2.7.0 Kimlik dogrulama basarili");
                state = State::AUTHENTICATED;
                LOG_INFO("SMTP: Auth basarili: " + authUsername + " [" + clientAddr + "]");
            } else {
                sendResponse(clientSocket, "535 5.7.8 Kimlik dogrulama basarisiz");
                state = State::GREETED;
                LOG_WARN("SMTP: Auth basarisiz: " + authUsername + " [" + clientAddr + "]");
            }
            continue;
        }

        // ---- EHLO / HELO ----
        if (utils::startsWithCI(cmd, "EHLO") || utils::startsWithCI(cmd, "HELO")) {
            std::string domain = (cmd.size() > 5) ? utils::trim(cmd.substr(5)) : "unknown";
            sendResponse(clientSocket, "250-ulusansigorta.com.tr Merhaba " + domain);
            sendResponse(clientSocket, "250-AUTH LOGIN PLAIN");
            sendResponse(clientSocket, "250-SIZE 10485760");
            sendResponse(clientSocket, "250 OK");
            state = State::GREETED;
            continue;
        }

        // ---- AUTH LOGIN ----
        if (utils::startsWithCI(cmd, "AUTH LOGIN")) {
            if (state != State::GREETED) {
                sendResponse(clientSocket, "503 Once EHLO gonderin");
                continue;
            }
            // Eger AUTH LOGIN'den sonra base64 veri varsa
            if (cmd.size() > 11) {
                authUsername = base64::decode(utils::trim(cmd.substr(11)));
                sendResponse(clientSocket, "334 UGFzc3dvcmQ6"); // "Password:"
                state = State::AUTH_PASS;
            } else {
                sendResponse(clientSocket, "334 VXNlcm5hbWU6"); // "Username:"
                state = State::AUTH_USER;
            }
            continue;
        }

        // ---- AUTH PLAIN ----
        if (utils::startsWithCI(cmd, "AUTH PLAIN")) {
            if (state != State::GREETED) {
                sendResponse(clientSocket, "503 Once EHLO gonderin");
                continue;
            }
            std::string encoded = utils::trim(cmd.substr(11));
            std::string decoded = base64::decode(encoded);
            // Format: \0username\0password
            size_t first = decoded.find('\0');
            size_t second = decoded.find('\0', first + 1);
            if (first != std::string::npos && second != std::string::npos) {
                std::string username = decoded.substr(first + 1, second - first - 1);
                std::string password = decoded.substr(second + 1);
                if (auth.authenticate(username, password)) {
                    authUsername = username;
                    sendResponse(clientSocket, "235 2.7.0 Kimlik dogrulama basarili");
                    state = State::AUTHENTICATED;
                } else {
                    sendResponse(clientSocket, "535 5.7.8 Kimlik dogrulama basarisiz");
                }
            } else {
                sendResponse(clientSocket, "535 5.7.8 Gecersiz kimlik bilgisi");
            }
            continue;
        }

        // ---- MAIL FROM ----
        if (utils::startsWithCI(cmd, "MAIL FROM:")) {
            if (state != State::AUTHENTICATED) {
                sendResponse(clientSocket, "530 5.7.0 Once kimlik dogrulamasi yapin");
                continue;
            }
            mailFrom = utils::trim(cmd.substr(10));
            sendResponse(clientSocket, "250 OK");
            state = State::MAIL_FROM;
            continue;
        }

        // ---- RCPT TO ----
        if (utils::startsWithCI(cmd, "RCPT TO:")) {
            if (state != State::MAIL_FROM && state != State::RCPT_TO) {
                sendResponse(clientSocket, "503 Once MAIL FROM gonderin");
                continue;
            }
            std::string recipient = utils::trim(cmd.substr(8));
            std::string rcptUser = utils::getUsernameFromEmail(
                utils::extractEmail(recipient));

            if (!auth.userExists(rcptUser)) {
                sendResponse(clientSocket, "550 5.1.1 Kullanici bulunamadi: " + recipient);
                continue;
            }
            rcptTo.push_back(recipient);
            sendResponse(clientSocket, "250 OK");
            state = State::RCPT_TO;
            continue;
        }

        // ---- DATA ----
        if (cmdUpper == "DATA") {
            if (state != State::RCPT_TO || rcptTo.empty()) {
                sendResponse(clientSocket, "503 Once RCPT TO gonderin");
                continue;
            }
            sendResponse(clientSocket, "354 Mail icerigini girin; tek basina . ile bitirin");
            state = State::DATA;
            mailData.clear();
            continue;
        }

        // ---- RSET ----
        if (cmdUpper == "RSET") {
            mailFrom.clear();
            rcptTo.clear();
            mailData.clear();
            state = (state == State::INIT) ? State::INIT : State::AUTHENTICATED;
            sendResponse(clientSocket, "250 OK");
            continue;
        }

        // ---- NOOP ----
        if (cmdUpper == "NOOP") {
            sendResponse(clientSocket, "250 OK");
            continue;
        }

        // ---- QUIT ----
        if (cmdUpper == "QUIT") {
            sendResponse(clientSocket, "221 ulusansigorta.com.tr Gule gule");
            break;
        }

        // ---- Bilinmeyen komut ----
        sendResponse(clientSocket, "500 Bilinmeyen komut: " + cmd);
    }

    closesocket(clientSocket);
    LOG_INFO("SMTP: Baglanti kapatildi: " + clientAddr);
}
