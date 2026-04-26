# Ulusan Sigorta - SMTP & POP3 Mail Sunucusu

**BİL314 - Bilgisayar Ağları Dersi Projesi**

| | |
|---|---|
| **Öğrenci 1** | Umut Baran Ulusan - 230206035 |
| **Öğrenci 2** | Berkay Demirci - 230206064 |
| **Dil** | C/C++ (Winsock API) |
| **Platform** | Windows |
| **Domain** | ulusansigorta.com |

## Proje Hakkında

Bu proje, Ulusan Sigorta şirketi çalışanları için sıfırdan geliştirilmiş bir **SMTP ve POP3 mail sunucusu**dur. C/C++ ile Winsock API kullanılarak Windows platformunda geliştirilmiştir.

### Özellikler

- **SMTP Sunucusu** (Port 25) — RFC 5321 uyumlu e-posta gönderimi
- **POP3 Sunucusu** (Port 11) — RFC 1939 uyumlu e-posta okuma
- **Kullanıcı Yetkilendirme** — Kayıt, giriş, admin/user rolleri
- **Konsol Menü Arayüzü** — İnteraktif kullanıcı arayüzü
- **Dosya Tabanlı Depolama** — .eml formatında mail saklama
- **Thread Desteği** — Çoklu bağlantı yönetimi
- **Loglama** — Tüm işlemlerin kaydı

## Derleme

### Gereksinimler
- MinGW g++ (C++17 desteği)
- Windows işletim sistemi

### Derleme Komutu

```bash
g++ -std=c++17 -Wall -O2 -o ulusan_smtp.exe src/main.cpp src/auth.cpp src/mail_store.cpp src/smtp_server.cpp src/pop3_server.cpp -lws2_32
```

Veya `build.bat` dosyasını çalıştırın.

## Kullanım

```
ulusan_smtp.exe
```

### Varsayılan Admin Hesabı
- **Kullanıcı:** admin
- **Şifre:** admin123

### SMTP Komutları (Telnet ile test)
```
telnet localhost 25
EHLO test
AUTH LOGIN
(base64 username)
(base64 password)
MAIL FROM:<user@ulusansigorta.com>
RCPT TO:<other@ulusansigorta.com>
DATA
Subject: Test
Bu bir test mailidir.
.
QUIT
```

### POP3 Komutları (Telnet ile test)
```
telnet localhost 11
USER admin
PASS admin123
STAT
LIST
RETR 1
QUIT
```

## Dizin Yapısı

```
ulusan-smtp-server/
├── src/
│   ├── main.cpp           # Ana program ve menü
│   ├── smtp_server.h/cpp  # SMTP sunucu
│   ├── pop3_server.h/cpp  # POP3 sunucu
│   ├── auth.h/cpp         # Kullanıcı yetkilendirme
│   ├── mail_store.h/cpp   # Mail depolama
│   ├── base64.h           # Base64 encode/decode
│   ├── logger.h           # Loglama
│   └── utils.h            # Yardımcı fonksiyonlar
├── data/
│   ├── users.dat          # Kullanıcı veritabanı
│   ├── server.log         # Sunucu logları
│   └── mailboxes/         # Posta kutuları
├── Makefile
├── build.bat
└── README.md
```

## Lisans

Bu proje eğitim amaçlı geliştirilmiştir.
