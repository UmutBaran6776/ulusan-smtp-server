#ifndef IMAP_SERVER_H
#define IMAP_SERVER_H
#include <string>
#include <thread>
#include <atomic>
#include <vector>
#include <winsock2.h>
#include <ws2tcpip.h>
#include "auth.h"
#include "mail_store.h"

class ImapServer {
private:
    int port;
    SOCKET listenSocket;
    std::atomic<bool> running;
    std::thread serverThread;
    AuthManager& auth;
    MailStore& store;
    void serverLoop();
    void handleClient(SOCKET clientSocket, std::string clientAddr);
    void sendResp(SOCKET s, const std::string& r);
    std::string recvLine(SOCKET s);
    std::string flagsStr(int f);
    int parseFlags(const std::string& s);
    bool parseSeqSet(const std::string& ss, int max, std::vector<int>& out);
public:
    ImapServer(int port, AuthManager& auth, MailStore& store);
    ~ImapServer();
    bool start();
    void stop();
};
#endif
