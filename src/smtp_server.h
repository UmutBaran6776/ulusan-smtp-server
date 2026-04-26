#ifndef SMTP_SERVER_H
#define SMTP_SERVER_H

#include <string>
#include <thread>
#include <atomic>
#include <winsock2.h>
#include <ws2tcpip.h>
#include "auth.h"
#include "mail_store.h"

/*
 * SmtpServer - SMTP Sunucusu (RFC 5321)
 * Port 25 uzerinde dinler.
 * Desteklenen komutlar: EHLO, HELO, AUTH LOGIN, MAIL FROM,
 *                       RCPT TO, DATA, QUIT, RSET, NOOP
 * Her baglanti icin ayri thread olusturulur.
 */

class SmtpServer {
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
    SmtpServer(int port, AuthManager& auth, MailStore& store);
    ~SmtpServer();

    bool start();
    void stop();
    bool isRunning() const { return running.load(); }
};

#endif // SMTP_SERVER_H
