#ifndef BASE64_H
#define BASE64_H

#include <string>
#include <vector>

/*
 * Base64 Encoding/Decoding
 * SMTP AUTH LOGIN mekanizmasi icin kullanilir.
 * RFC 4648 uyumlu implementasyon.
 */

namespace base64 {

static const std::string CHARS =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz"
    "0123456789+/";

inline std::string encode(const std::string& input) {
    std::string encoded;
    int val = 0, valb = -6;

    for (unsigned char c : input) {
        val = (val << 8) + c;
        valb += 8;
        while (valb >= 0) {
            encoded.push_back(CHARS[(val >> valb) & 0x3F]);
            valb -= 6;
        }
    }

    if (valb > -6) {
        encoded.push_back(CHARS[((val << 8) >> (valb + 8)) & 0x3F]);
    }

    while (encoded.size() % 4) {
        encoded.push_back('=');
    }

    return encoded;
}

inline std::string decode(const std::string& input) {
    std::string decoded;
    std::vector<int> T(256, -1);

    for (int i = 0; i < 64; i++) {
        T[(unsigned char)CHARS[i]] = i;
    }

    int val = 0, valb = -8;
    for (unsigned char c : input) {
        if (T[c] == -1) break;
        val = (val << 6) + T[c];
        valb += 6;
        if (valb >= 0) {
            decoded.push_back(char((val >> valb) & 0xFF));
            valb -= 8;
        }
    }

    return decoded;
}

} // namespace base64

#endif // BASE64_H
