#ifndef POP3_SERVER_H
#define POP3_SERVER_H

#include <string>
#include <thread>
#include <atomic>
#include <winsock2.h>
#include <ws2tcpip.h>
#include "auth.h"
#include "mail_store.h"

/*
 * Pop3Server - POP3 Sunucusu (RFC 1939)
 * Port 11 uzerinde dinler.
 * Desteklenen komutlar: USER, PASS, STAT, LIST, RETR, DELE, QUIT, NOOP, RSET
 * Her baglanti icin ayri thread olusturulur.
 */

class Pop3Server {
private:
    int port;
    SOCKET listenSocket;
    std::thread serverThread;
    std::atomic<bool> running;
    AuthManager& auth;
    MailStore& store;

    void serverLoop();
    void handleClient(SOCKET clientSocket, std::string clientAddr);
    void sendResponse(SOCKET sock, const std::string& response);
    std::string recvLine(SOCKET sock);

public:
    Pop3Server(int port, AuthManager& auth, MailStore& store);
    ~Pop3Server();

    bool start();
    void stop();
    bool isRunning() const { return running.load(); }
};

#endif // POP3_SERVER_H
