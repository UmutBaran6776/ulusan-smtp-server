#include "imap_server.h"
#include "logger.h"
#include "utils.h"
#include <iostream>
#include <sstream>
#include <vector>

ImapServer::ImapServer(int p, AuthManager& a, MailStore& s)
    : port(p), listenSocket(INVALID_SOCKET), running(false), auth(a), store(s) {}

ImapServer::~ImapServer() { stop(); }

bool ImapServer::start() {
    listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listenSocket == INVALID_SOCKET) return false;

    int opt = 1;
    setsockopt(listenSocket, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(port);

    if (bind(listenSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        closesocket(listenSocket);
        return false;
    }
    if (listen(listenSocket, SOMAXCONN) == SOCKET_ERROR) {
        closesocket(listenSocket);
        return false;
    }

    running = true;
    serverThread = std::thread(&ImapServer::serverLoop, this);
    LOG_INFO("IMAP: Sunucu baslatildi, port " + std::to_string(port));
    return true;
}

void ImapServer::stop() {
    running = false;
    if (listenSocket != INVALID_SOCKET) {
        closesocket(listenSocket);
        listenSocket = INVALID_SOCKET;
    }
    if (serverThread.joinable()) serverThread.join();
    LOG_INFO("IMAP: Sunucu durduruldu.");
}

void ImapServer::serverLoop() {
    while (running) {
        sockaddr_in clientAddr{};
        int addrLen = sizeof(clientAddr);
        SOCKET clientSocket = accept(listenSocket, (sockaddr*)&clientAddr, &addrLen);
        if (clientSocket == INVALID_SOCKET) continue;

        char addrStr[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &clientAddr.sin_addr, addrStr, sizeof(addrStr));
        std::string clientAddrString = std::string(addrStr) + ":" + std::to_string(ntohs(clientAddr.sin_port));

        std::thread clientThread(&ImapServer::handleClient, this, clientSocket, clientAddrString);
        clientThread.detach();
    }
}

void ImapServer::sendResponse(SOCKET sock, const std::string& response) {
    std::string msg = response + "\r\n";
    send(sock, msg.c_str(), (int)msg.size(), 0);
    LOG_DEBUG("IMAP TX: " + response);
}

std::string ImapServer::recvLine(SOCKET sock) {
    std::string line;
    char ch;
    while (true) {
        int result = recv(sock, &ch, 1, 0);
        if (result <= 0) return "";
        if (ch == '\n') {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            return line;
        }
        line += ch;
        if (line.size() > 4096) return line;
    }
}

void ImapServer::handleClient(SOCKET clientSocket, std::string clientAddr) {
    LOG_INFO("IMAP: Yeni baglanti: " + clientAddr);
    sendResponse(clientSocket, "* OK IMAP4rev1 Ulusan Sigorta Mail Server Ready");

    bool authenticated = false;
    std::string currentUser = "";
    std::string currentMailbox = "";

    while (true) {
        std::string line = recvLine(clientSocket);
        if (line.empty()) break;
        
        LOG_DEBUG("IMAP RX [" + clientAddr + "]: " + line);

        size_t firstSpace = line.find(' ');
        if (firstSpace == std::string::npos) continue;

        std::string tag = line.substr(0, firstSpace);
        std::string cmdFull = line.substr(firstSpace + 1);
        std::string cmdUpper = utils::toUpper(cmdFull);

        if (utils::startsWithCI(cmdFull, "CAPABILITY")) {
            sendResponse(clientSocket, "* CAPABILITY IMAP4rev1 AUTH=PLAIN");
            sendResponse(clientSocket, tag + " OK CAPABILITY completed");
            continue;
        }
        
        if (utils::startsWithCI(cmdFull, "LOGOUT")) {
            sendResponse(clientSocket, "* BYE IMAP4rev1 Server logging out");
            sendResponse(clientSocket, tag + " OK LOGOUT completed");
            break;
        }

        if (utils::startsWithCI(cmdFull, "NOOP")) {
            sendResponse(clientSocket, tag + " OK NOOP completed");
            continue;
        }

        if (!authenticated) {
            if (utils::startsWithCI(cmdFull, "LOGIN")) {
                std::istringstream iss(cmdFull);
                std::string c, user, pass;
                iss >> c >> user >> pass;
                
                // Tirnak isaretlerini temizle
                if (user.size() >= 2 && user.front() == '"' && user.back() == '"') user = user.substr(1, user.size() - 2);
                if (pass.size() >= 2 && pass.front() == '"' && pass.back() == '"') pass = pass.substr(1, pass.size() - 2);

                if (auth.authenticate(user, pass)) {
                    authenticated = true;
                    currentUser = user;
                    sendResponse(clientSocket, tag + " OK LOGIN completed");
                    LOG_INFO("IMAP: Giris basarili: " + currentUser);
                } else {
                    sendResponse(clientSocket, tag + " NO LOGIN failed");
                }
            } else {
                sendResponse(clientSocket, tag + " BAD Command needs authentication");
            }
            continue;
        }

        // Authenticated Commands
        if (utils::startsWithCI(cmdFull, "LIST")) {
            sendResponse(clientSocket, "* LIST (\\HasNoChildren) \"/\" \"INBOX\"");
            sendResponse(clientSocket, tag + " OK LIST completed");
            continue;
        }

        if (utils::startsWithCI(cmdFull, "SELECT")) {
            currentMailbox = "INBOX";
            int count = store.getMailCount(currentUser, false);
            sendResponse(clientSocket, "* " + std::to_string(count) + " EXISTS");
            sendResponse(clientSocket, "* " + std::to_string(count) + " RECENT");
            sendResponse(clientSocket, "* OK [UIDVALIDITY 1] UIDs valid");
            sendResponse(clientSocket, tag + " OK [READ-WRITE] SELECT completed");
            continue;
        }

        // Cok basit bir FETCH ve UID FETCH implementasyonu
        if (utils::startsWithCI(cmdFull, "FETCH") || utils::startsWithCI(cmdFull, "UID FETCH")) {
            if (currentMailbox.empty()) {
                sendResponse(clientSocket, tag + " BAD No mailbox selected");
                continue;
            }

            int mailCount = store.getMailCount(currentUser, false);
            if (mailCount == 0) {
                sendResponse(clientSocket, tag + " OK FETCH completed");
                continue;
            }

            // Gelen mesajdaki bosluklara gore ayrilip FETCH edilen aralik bulunabilir.
            // Fakat Thunderbird genelde UID FETCH 1:* (FLAGS) veya UID FETCH X (BODY.PEEK[]) atar.
            // Biz basit bir projede butun maillere bakip eger body istenmisse donduruyoruz.
            
            bool wantBody = (cmdUpper.find("BODY") != std::string::npos);
            bool isUid = utils::startsWithCI(cmdFull, "UID FETCH");

            for (int i = 0; i < mailCount; ++i) {
                int id = i + 1; // 1-based index
                int uid = id;   // UID basitce ID ile ayni

                std::string flags = "\\Recent"; // Varsayilan
                
                std::string fetchResp = "* " + std::to_string(id) + " FETCH (";
                if (isUid) fetchResp += "UID " + std::to_string(uid) + " ";
                fetchResp += "FLAGS (" + flags + ")";

                if (wantBody) {
                    std::string rawMail = store.getRawMail(currentUser, i, false);
                    size_t size = rawMail.size();
                    if (size > 0) {
                        fetchResp += " RFC822.SIZE " + std::to_string(size);
                        fetchResp += " BODY[] {" + std::to_string(size) + "}\r\n";
                        sendResponse(clientSocket, fetchResp);
                        
                        // Body verisini \r\n yapiyi bozmadan dogrudan gonder
                        send(clientSocket, rawMail.c_str(), rawMail.size(), 0);
                        sendResponse(clientSocket, ")"); // Kapanis parantezi
                        continue; // Parantezi kapattik, bir sonrakine gec
                    }
                } else {
                    fetchResp += ")";
                    sendResponse(clientSocket, fetchResp);
                }
            }
            sendResponse(clientSocket, tag + " OK FETCH completed");
            continue;
        }
        
        // Diger komutlar
        sendResponse(clientSocket, tag + " BAD Command not supported in minimal IMAP");
    }

    closesocket(clientSocket);
    LOG_INFO("IMAP: Baglanti kapatildi: " + clientAddr);
}
