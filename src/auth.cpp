#include "auth.h"
#include "logger.h"
#include <fstream>
#include <sstream>
#include <filesystem>
#include <iostream>
#include <algorithm>

namespace fs = std::filesystem;

// ==================== DJB2 Hash Algoritmasi ====================
// Basit ve hizli bir hash fonksiyonu. Universite projesi icin yeterli.
std::string AuthManager::hashPassword(const std::string& password) {
    unsigned long hash = 5381;
    for (char c : password) {
        hash = ((hash << 5) + hash) + (unsigned char)c; // hash * 33 + c
    }
    std::ostringstream oss;
    oss << hash;
    return oss.str();
}

// ==================== Kullanicilari Dosyadan Yukle ====================
// Format: username|password_hash|role|displayName
void AuthManager::loadUsers() {
    std::ifstream file(dataFile);
    if (!file.is_open()) return;

    users.clear();
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;

        std::istringstream iss(line);
        std::string username, hash, roleStr, displayName;

        std::getline(iss, username, '|');
        std::getline(iss, hash, '|');
        std::getline(iss, roleStr, '|');
        std::getline(iss, displayName);

        if (username.empty() || hash.empty()) continue;

        User user;
        user.username = username;
        user.passwordHash = hash;
        user.role = (roleStr == "ADMIN") ? UserRole::ADMIN : UserRole::USER;
        user.displayName = displayName;
        user.email = username + "@ulusansigorta.com";

        users.push_back(user);
    }
    file.close();
    LOG_INFO("Auth: " + std::to_string(users.size()) + " kullanici yuklendi.");
}

// ==================== Kullanicilari Dosyaya Kaydet ====================
void AuthManager::saveUsers() {
    std::ofstream file(dataFile, std::ios::trunc);
    if (!file.is_open()) {
        LOG_ERROR("Auth: users.dat dosyasi yazilamadi!");
        return;
    }

    file << "# Ulusan Sigorta - Kullanici Veritabani" << std::endl;
    file << "# Format: username|password_hash|role|displayName" << std::endl;

    for (const auto& user : users) {
        std::string roleStr = (user.role == UserRole::ADMIN) ? "ADMIN" : "USER";
        file << user.username << "|"
             << user.passwordHash << "|"
             << roleStr << "|"
             << user.displayName << std::endl;
    }
    file.close();
}

// ==================== Constructor ====================
AuthManager::AuthManager(const std::string& dir) : dataDir(dir) {
    dataFile = dataDir + "\\users.dat";
    fs::create_directories(dataDir);
    loadUsers();
}

// ==================== Kullanici Kayit ====================
bool AuthManager::registerUser(const std::string& username, const std::string& password,
                               UserRole role, const std::string& displayName) {
    std::lock_guard<std::mutex> lock(mtx);

    // Kullanici adi kontrolu
    for (const auto& u : users) {
        if (u.username == username) {
            LOG_WARN("Auth: Kullanici zaten mevcut: " + username);
            return false;
        }
    }

    User newUser;
    newUser.username = username;
    newUser.passwordHash = hashPassword(password);
    newUser.role = role;
    newUser.displayName = displayName;
    newUser.email = username + "@ulusansigorta.com";

    users.push_back(newUser);
    saveUsers();

    LOG_INFO("Auth: Yeni kullanici kaydedildi: " + username + " (" + displayName + ")");
    return true;
}

// ==================== Kullanici Dogrulama ====================
bool AuthManager::authenticate(const std::string& username, const std::string& password) {
    std::lock_guard<std::mutex> lock(mtx);

    std::string hash = hashPassword(password);
    for (const auto& user : users) {
        if (user.username == username && user.passwordHash == hash) {
            LOG_INFO("Auth: Basarili giris: " + username);
            return true;
        }
    }
    LOG_WARN("Auth: Basarisiz giris denemesi: " + username);
    return false;
}

// ==================== Kullanici Bilgisi Getir ====================
User* AuthManager::getUser(const std::string& username) {
    for (auto& user : users) {
        if (user.username == username) {
            return &user;
        }
    }
    return nullptr;
}

// ==================== Kullanicilari Listele ====================
std::vector<User> AuthManager::listUsers() {
    std::lock_guard<std::mutex> lock(mtx);
    return users;
}

// ==================== Kullanici Sil ====================
bool AuthManager::deleteUser(const std::string& username) {
    std::lock_guard<std::mutex> lock(mtx);

    auto it = std::remove_if(users.begin(), users.end(),
        [&username](const User& u) { return u.username == username; });

    if (it != users.end()) {
        users.erase(it, users.end());
        saveUsers();
        LOG_INFO("Auth: Kullanici silindi: " + username);
        return true;
    }
    return false;
}

// ==================== Sifre Degistir ====================
bool AuthManager::changePassword(const std::string& username, const std::string& oldPassword,
                                  const std::string& newPassword) {
    std::lock_guard<std::mutex> lock(mtx);

    std::string oldHash = hashPassword(oldPassword);
    for (auto& user : users) {
        if (user.username == username && user.passwordHash == oldHash) {
            user.passwordHash = hashPassword(newPassword);
            saveUsers();
            LOG_INFO("Auth: Sifre degistirildi: " + username);
            return true;
        }
    }
    return false;
}

// ==================== Kullanici Var Mi ====================
bool AuthManager::userExists(const std::string& username) {
    std::lock_guard<std::mutex> lock(mtx);
    for (const auto& u : users) {
        if (u.username == username) return true;
    }
    return false;
}

// ==================== Varsayilan Admin Olustur ====================
void AuthManager::createDefaultAdmin() {
    if (!userExists("admin")) {
        registerUser("admin", "admin123", UserRole::ADMIN, "Sistem Yoneticisi");
    }
}

// ==================== Kullanici Rolu Getir ====================
UserRole AuthManager::getUserRole(const std::string& username) {
    std::lock_guard<std::mutex> lock(mtx);
    for (const auto& u : users) {
        if (u.username == username) return u.role;
    }
    return UserRole::USER;
}

// ==================== Kullanici Sayisi ====================
int AuthManager::getUserCount() {
    std::lock_guard<std::mutex> lock(mtx);
    return (int)users.size();
}
