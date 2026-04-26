#include "pop3_server.h"
#include "logger.h"
#include "utils.h"
#include <iostream>
#include <sstream>
#include <vector>
#include <set>

// ==================== Constructor / Destructor ====================

Pop3Server::Pop3Server(int p, AuthManager& a, MailStore& s)
    : port(p), listenSocket(INVALID_SOCKET), running(false), auth(a), store(s) {
}

Pop3Server::~Pop3Server() {
    stop();
}

// ==================== Sunucuyu Baslat ====================

bool Pop3Server::start() {
    listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listenSocket == INVALID_SOCKET) {
        LOG_ERROR("POP3: Socket olusturulamadi: " + std::to_string(WSAGetLastError()));
        return false;
    }

    int opt = 1;
    setsockopt(listenSocket, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(port);

    if (bind(listenSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        LOG_ERROR("POP3: Bind hatasi port " + std::to_string(port) + ": " +
                  std::to_string(WSAGetLastError()));
        closesocket(listenSocket);
        return false;
    }

    if (listen(listenSocket, SOMAXCONN) == SOCKET_ERROR) {
        LOG_ERROR("POP3: Listen hatasi: " + std::to_string(WSAGetLastError()));
        closesocket(listenSocket);
        return false;
    }

    running = true;
    serverThread = std::thread(&Pop3Server::serverLoop, this);
    LOG_INFO("POP3: Sunucu baslatildi, port " + std::to_string(port));
    return true;
}

// ==================== Sunucuyu Durdur ====================

void Pop3Server::stop() {
    running = false;
    if (listenSocket != INVALID_SOCKET) {
        closesocket(listenSocket);
        listenSocket = INVALID_SOCKET;
    }
    if (serverThread.joinable()) {
        serverThread.join();
    }
    LOG_INFO("POP3: Sunucu durduruldu.");
}

// ==================== Ana Sunucu Dongusu ====================

void Pop3Server::serverLoop() {
    while (running) {
        sockaddr_in clientAddr{};
        int addrLen = sizeof(clientAddr);

        SOCKET clientSocket = accept(listenSocket, (sockaddr*)&clientAddr, &addrLen);
        if (clientSocket == INVALID_SOCKET) {
            if (running) {
                LOG_ERROR("POP3: Accept hatasi: " + std::to_string(WSAGetLastError()));
            }
            continue;
        }

        char addrStr[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &clientAddr.sin_addr, addrStr, sizeof(addrStr));
        std::string clientAddrString = std::string(addrStr) + ":" +
                                       std::to_string(ntohs(clientAddr.sin_port));

        std::thread clientThread(&Pop3Server::handleClient, this, clientSocket, clientAddrString);
        clientThread.detach();
    }
}

// ==================== Yanit Gonder ====================

void Pop3Server::sendResponse(SOCKET sock, const std::string& response) {
    std::string msg = response + "\r\n";
    send(sock, msg.c_str(), (int)msg.size(), 0);
    LOG_DEBUG("POP3 TX: " + response);
}

// ==================== Satir Oku ====================

std::string Pop3Server::recvLine(SOCKET sock) {
    std::string line;
    char ch;
    while (true) {
        int result = recv(sock, &ch, 1, 0);
        if (result <= 0) return "";
        if (ch == '\n') {
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            return line;
        }
        line += ch;
        if (line.size() > 4096) return line;
    }
}

// ==================== Istemci Baglantisini Yonet ====================
// POP3 durum makinesi: AUTHORIZATION -> TRANSACTION -> UPDATE

void Pop3Server::handleClient(SOCKET clientSocket, std::string clientAddr) {
    LOG_INFO("POP3: Yeni baglanti: " + clientAddr);

    enum class State { AUTHORIZATION, TRANSACTION, UPDATE };
    State state = State::AUTHORIZATION;

    std::string username;
    std::vector<Mail> mails;
    std::set<int> deletedIndices; // Silinmek uzere isaretlenen mailler

    // Karsilama
    sendResponse(clientSocket, "+OK Ulusan Sigorta POP3 Sunucusu hazir");

    while (true) {
        std::string line = recvLine(clientSocket);
        if (line.empty()) break;

        std::string cmd = utils::trim(line);
        std::string cmdUpper = utils::toUpper(cmd);

        LOG_DEBUG("POP3 RX [" + clientAddr + "]: " + cmd);

        // ---- USER ----
        if (utils::startsWithCI(cmd, "USER ")) {
            if (state != State::AUTHORIZATION) {
                sendResponse(clientSocket, "-ERR Zaten giris yapildi");
                continue;
            }
            username = utils::trim(cmd.substr(5));
            if (auth.userExists(username)) {
                sendResponse(clientSocket, "+OK Kullanici kabul edildi, sifreyi girin");
            } else {
                sendResponse(clientSocket, "-ERR Kullanici bulunamadi");
                username.clear();
            }
            continue;
        }

        // ---- PASS ----
        if (utils::startsWithCI(cmd, "PASS ")) {
            if (state != State::AUTHORIZATION || username.empty()) {
                sendResponse(clientSocket, "-ERR Once USER komutunu gonderin");
                continue;
            }
            std::string password = utils::trim(cmd.substr(5));
            if (auth.authenticate(username, password)) {
                state = State::TRANSACTION;
                mails = store.loadInbox(username);
                sendResponse(clientSocket, "+OK Giris basarili, " +
                            std::to_string(mails.size()) + " mesaj mevcut");
                LOG_INFO("POP3: Giris basarili: " + username);
            } else {
                sendResponse(clientSocket, "-ERR Yanlis sifre");
                username.clear();
                LOG_WARN("POP3: Giris basarisiz: " + username);
            }
            continue;
        }

        // ---- Asagidaki komutlar TRANSACTION durumunda calismali ----
        if (state != State::TRANSACTION) {
            sendResponse(clientSocket, "-ERR Once giris yapin");
            continue;
        }

        // ---- STAT ----
        if (cmdUpper == "STAT") {
            int count = 0;
            long long totalSize = 0;
            for (int i = 0; i < (int)mails.size(); i++) {
                if (deletedIndices.find(i) == deletedIndices.end()) {
                    count++;
                    totalSize += (long long)mails[i].body.size();
                }
            }
            sendResponse(clientSocket, "+OK " + std::to_string(count) + " " +
                        std::to_string(totalSize));
            continue;
        }

        // ---- LIST ----
        if (cmdUpper == "LIST" || utils::startsWithCI(cmd, "LIST ")) {
            // LIST n - belirli bir mesaj
            if (cmd.size() > 5) {
                int idx = std::stoi(utils::trim(cmd.substr(5))) - 1;
                if (idx < 0 || idx >= (int)mails.size() ||
                    deletedIndices.find(idx) != deletedIndices.end()) {
                    sendResponse(clientSocket, "-ERR Mesaj bulunamadi");
                } else {
                    sendResponse(clientSocket, "+OK " + std::to_string(idx + 1) + " " +
                                std::to_string(mails[idx].body.size()));
                }
            } else {
                // LIST - tum mesajlar
                int count = 0;
                for (int i = 0; i < (int)mails.size(); i++) {
                    if (deletedIndices.find(i) == deletedIndices.end()) count++;
                }
                sendResponse(clientSocket, "+OK " + std::to_string(count) + " mesaj");
                for (int i = 0; i < (int)mails.size(); i++) {
                    if (deletedIndices.find(i) == deletedIndices.end()) {
                        std::string listLine = std::to_string(i + 1) + " " +
                                              std::to_string(mails[i].body.size());
                        sendResponse(clientSocket, listLine);
                    }
                }
                sendResponse(clientSocket, ".");
            }
            continue;
        }

        // ---- RETR n ----
        if (utils::startsWithCI(cmd, "RETR ")) {
            int idx = std::stoi(utils::trim(cmd.substr(5))) - 1;
            if (idx < 0 || idx >= (int)mails.size() ||
                deletedIndices.find(idx) != deletedIndices.end()) {
                sendResponse(clientSocket, "-ERR Mesaj bulunamadi");
            } else {
                const Mail& mail = mails[idx];
                long long size = mail.body.size();
                sendResponse(clientSocket, "+OK " + std::to_string(size) + " octets");
                sendResponse(clientSocket, "From: " + mail.from);
                sendResponse(clientSocket, "To: " + mail.to);
                sendResponse(clientSocket, "Subject: " + mail.subject);
                sendResponse(clientSocket, "Date: " + mail.date);
                sendResponse(clientSocket, "Message-ID: " + mail.messageId);
                sendResponse(clientSocket, "");
                // Body'yi satir satir gonder
                std::istringstream bodyStream(mail.body);
                std::string bodyLine;
                while (std::getline(bodyStream, bodyLine)) {
                    // Byte-stuffing: "." ile baslayan satirlarin basina "." ekle
                    if (!bodyLine.empty() && bodyLine[0] == '.') {
                        bodyLine = "." + bodyLine;
                    }
                    sendResponse(clientSocket, bodyLine);
                }
                sendResponse(clientSocket, ".");
            }
            continue;
        }

        // ---- DELE n ----
        if (utils::startsWithCI(cmd, "DELE ")) {
            int idx = std::stoi(utils::trim(cmd.substr(5))) - 1;
            if (idx < 0 || idx >= (int)mails.size()) {
                sendResponse(clientSocket, "-ERR Mesaj bulunamadi");
            } else if (deletedIndices.find(idx) != deletedIndices.end()) {
                sendResponse(clientSocket, "-ERR Mesaj zaten silinmek uzere isaretlendi");
            } else {
                deletedIndices.insert(idx);
                sendResponse(clientSocket, "+OK Mesaj " + std::to_string(idx + 1) +
                            " silinmek uzere isaretlendi");
            }
            continue;
        }

        // ---- RSET ----
        if (cmdUpper == "RSET") {
            deletedIndices.clear();
            sendResponse(clientSocket, "+OK Silme isaretleri kaldirildi");
            continue;
        }

        // ---- NOOP ----
        if (cmdUpper == "NOOP") {
            sendResponse(clientSocket, "+OK");
            continue;
        }

        // ---- QUIT ----
        if (cmdUpper == "QUIT") {
            // UPDATE durumu: isaretlenen mesajlari sil
            if (!deletedIndices.empty()) {
                // Buyukten kucuge sil ki indeksler bozulmasin
                std::vector<int> sorted(deletedIndices.begin(), deletedIndices.end());
                std::sort(sorted.rbegin(), sorted.rend());
                for (int idx : sorted) {
                    store.deleteMail(username, idx, false);
                }
                LOG_INFO("POP3: " + std::to_string(deletedIndices.size()) +
                        " mesaj silindi: " + username);
            }
            sendResponse(clientSocket, "+OK Ulusan Sigorta POP3 sunucusu kapaniyor");
            break;
        }

        // ---- Bilinmeyen komut ----
        sendResponse(clientSocket, "-ERR Bilinmeyen komut");
    }

    closesocket(clientSocket);
    LOG_INFO("POP3: Baglanti kapatildi: " + clientAddr);
}
