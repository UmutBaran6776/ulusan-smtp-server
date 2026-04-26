#ifndef UTILS_H
#define UTILS_H

#include <string>
#include <vector>
#include <algorithm>
#include <sstream>
#include <chrono>
#include <iomanip>
#include <ctime>
#include <cstdlib>
#include <winsock2.h>
#include <ws2tcpip.h>

// Linking: -lws2_32 (MinGW) veya #pragma comment(lib, "ws2_32.lib") (MSVC)

namespace utils {

// ==================== String Islemleri ====================

inline std::string ltrim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    return (start == std::string::npos) ? "" : s.substr(start);
}

inline std::string rtrim(const std::string& s) {
    size_t end = s.find_last_not_of(" \t\r\n");
    return (end == std::string::npos) ? "" : s.substr(0, end + 1);
}

inline std::string trim(const std::string& s) {
    return ltrim(rtrim(s));
}

inline std::string toUpper(const std::string& s) {
    std::string result = s;
    std::transform(result.begin(), result.end(), result.begin(), ::toupper);
    return result;
}

inline std::string toLower(const std::string& s) {
    std::string result = s;
    std::transform(result.begin(), result.end(), result.begin(), ::tolower);
    return result;
}

inline std::vector<std::string> split(const std::string& s, char delimiter) {
    std::vector<std::string> tokens;
    std::istringstream stream(s);
    std::string token;
    while (std::getline(stream, token, delimiter)) {
        tokens.push_back(token);
    }
    return tokens;
}

inline bool startsWithCI(const std::string& str, const std::string& prefix) {
    if (str.size() < prefix.size()) return false;
    return toUpper(str.substr(0, prefix.size())) == toUpper(prefix);
}

// ==================== E-posta Islemleri ====================

// <user@domain> -> user@domain
inline std::string extractEmail(const std::string& s) {
    size_t start = s.find('<');
    size_t end = s.find('>');
    if (start != std::string::npos && end != std::string::npos && end > start) {
        return s.substr(start + 1, end - start - 1);
    }
    return trim(s);
}

// user@domain -> user
inline std::string getUsernameFromEmail(const std::string& email) {
    size_t at = email.find('@');
    if (at != std::string::npos) {
        return email.substr(0, at);
    }
    return email;
}

// ==================== Zaman Islemleri ====================

inline std::string getCurrentTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    struct tm tm_now;
    localtime_s(&tm_now, &time_t_now);
    std::ostringstream oss;
    oss << std::put_time(&tm_now, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

inline std::string getRFC2822Date() {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    struct tm tm_now;
    localtime_s(&tm_now, &time_t_now);
    std::ostringstream oss;
    oss << std::put_time(&tm_now, "%a, %d %b %Y %H:%M:%S +0300");
    return oss.str();
}

inline std::string generateMessageId() {
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();
    std::ostringstream oss;
    oss << ms << "." << rand() << "@ulusansigorta.com";
    return oss.str();
}

inline std::string getTimestampForFilename() {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count() % 1000;
    struct tm tm_now;
    localtime_s(&tm_now, &time_t_now);
    std::ostringstream oss;
    oss << std::put_time(&tm_now, "%Y%m%d_%H%M%S") << "_" << ms;
    return oss.str();
}

// ==================== Winsock Islemleri ====================

inline bool initWinsock() {
    WSADATA wsaData;
    return WSAStartup(MAKEWORD(2, 2), &wsaData) == 0;
}

inline void cleanupWinsock() {
    WSACleanup();
}

} // namespace utils

#endif // UTILS_H
