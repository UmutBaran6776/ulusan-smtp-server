#ifndef IMAP_SERVER_H
#define IMAP_SERVER_H

#include <string>
#include <thread>
#include <winsock2.h>
#include "auth.h"
#include "mail_store.h"

class ImapServer {
private:
    int port;
    SOCKET listenSocket;
    bool running;
    std::thread serverThread;
    
    AuthManager& auth;
    MailStore& store;

    void serverLoop();
    void handleClient(SOCKET clientSocket, std::string clientAddr);
    void sendResponse(SOCKET sock, const std::string& response);
    std::string recvLine(SOCKET sock);

public:
    ImapServer(int port, AuthManager& auth, MailStore& store);
    ~ImapServer();

    bool start();
    void stop();
};

#endif // IMAP_SERVER_H
