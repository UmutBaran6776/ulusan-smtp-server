#ifndef AUTH_H
#define AUTH_H

#include <string>
#include <vector>
#include <mutex>

/*
 * AuthManager - Kullanici Yetkilendirme Modulu
 * Kullanici kayit, giris, rol yonetimi islemlerini gerceklestirir.
 * Kullanici verileri data/users.dat dosyasinda saklanir.
 * Sifreler DJB2 hash algoritmasi ile hashlenir.
 */

enum class UserRole {
    ADMIN,
    USER
};

struct User {
    std::string username;      // Kullanici adi (ornek: umut)
    std::string passwordHash;  // DJB2 hash
    UserRole role;             // ADMIN veya USER
    std::string displayName;   // Gorunen ad (ornek: Umut Baran Ulusan)
    std::string email;         // username@ulusansigorta.com.tr
};

class AuthManager {
private:
    std::vector<User> users;
    std::mutex mtx;
    std::string dataFile;
    std::string dataDir;

    std::string hashPassword(const std::string& password);
    void loadUsers();
    void saveUsers();

public:
    AuthManager(const std::string& dataDir);

    bool registerUser(const std::string& username, const std::string& password,
                      UserRole role, const std::string& displayName);
    bool authenticate(const std::string& username, const std::string& password);
    User* getUser(const std::string& username);
    std::vector<User> listUsers();
    bool deleteUser(const std::string& username);
    bool changePassword(const std::string& username, const std::string& oldPassword,
                        const std::string& newPassword);
    bool userExists(const std::string& username);
    void createDefaultAdmin();
    UserRole getUserRole(const std::string& username);
    int getUserCount();
};

#endif // AUTH_H
