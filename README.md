# Ulusan Sigorta - SMTP & IMAP Mail Sunucusu

**BİL314 - Bilgisayar Ağları Dersi Projesi**

| | |
|---|---|
| **Öğrenci 1** | Umut Baran Ulusan - 230206035 |
| **Öğrenci 2** | Berkay Demirci - 230206064 |
| **Dil** | C/C++ (Winsock API) |
| **Platform** | Windows |
| **Domain** | ulusansigorta.com.tr |

## Proje Hakkında

Bu proje, Ulusan Sigorta şirketi çalışanları için sıfırdan geliştirilmiş bir **SMTP ve IMAP mail sunucusu**dur. C/C++ ile Winsock API kullanılarak Windows platformunda geliştirilmiştir.

### Özellikler

- **SMTP Sunucusu** (Port 25) — RFC 5321 uyumlu e-posta gönderimi
- **IMAP Sunucusu** (Port 143) — RFC 3501 uyumlu e-posta okuma (telefondan erişim)
- **Kullanıcı Yetkilendirme** — Kayıt, giriş, admin/user rolleri
- **Konsol Menü Arayüzü** — İnteraktif kullanıcı arayüzü
- **Dosya Tabanlı Depolama** — .eml formatında mail saklama
- **Thread Desteği** — Çoklu bağlantı yönetimi
- **Loglama** — Tüm işlemlerin kaydı
- **Mail Yönlendirme** — Otomatik mail iletme kuralları

## Derleme

### Gereksinimler
- MinGW g++ (C++17 desteği)
- Windows işletim sistemi

### Derleme Komutu

```bash
g++ -std=c++17 -Wall -O2 -static -o ulusan_smtp.exe src/main.cpp src/auth.cpp src/mail_store.cpp src/smtp_server.cpp src/imap_server.cpp -lws2_32
```

Veya `build.bat` dosyasını çalıştırın.

## Kullanım

```
ulusan_smtp.exe
```

### Varsayılan Hesaplar

| Kullanıcı | E-posta | Şifre | Rol |
|---|---|---|---|
| admin | admin@ulusansigorta.com.tr | admin123 | ADMIN |
| info | info@ulusansigorta.com.tr | info123 | ADMIN |
| fatos | fatos@ulusansigorta.com.tr | fatos123 | USER |
| teknik | teknik@ulusansigorta.com.tr | teknik123 | USER |

### SMTP Komutları (Telnet ile test)
```
telnet localhost 25
EHLO test
AUTH LOGIN
(base64 username)
(base64 password)
MAIL FROM:<user@ulusansigorta.com.tr>
RCPT TO:<other@ulusansigorta.com.tr>
DATA
Subject: Test
Bu bir test mailidir.
.
QUIT
```

### IMAP Komutları (Telnet ile test)
```
telnet localhost 143
a1 LOGIN info info123
a2 SELECT INBOX
a3 FETCH 1:* (FLAGS ENVELOPE)
a4 UID SEARCH ALL
a5 UID FETCH 1:* (FLAGS RFC822.SIZE)
a6 LOGOUT
```

## Telefon Yapılandırması

Sunucu çalıştıktan sonra telefonunuzdaki mail uygulamasına (iPhone Mail, Gmail, Outlook) şu ayarlarla hesap ekleyin:

### Gelen Posta (IMAP)
| Ayar | Değer |
|---|---|
| Sunucu | 78.186.12.5 |
| Port | 143 |
| Güvenlik | Yok |
| Kullanıcı Adı | info |
| Şifre | info123 |

### Giden Posta (SMTP)
| Ayar | Değer |
|---|---|
| Sunucu | 78.186.12.5 |
| Port | 25 |
| Güvenlik | Yok |
| Kullanıcı Adı | info |
| Şifre | info123 |

> **Not:** Telefonun aynı ağda olması veya router'da port yönlendirme yapılmış olması gerekir.

## Dizin Yapısı

```
ulusan-smtp-server/
├── src/
│   ├── main.cpp           # Ana program ve menü
│   ├── smtp_server.h/cpp  # SMTP sunucu
│   ├── imap_server.h/cpp  # IMAP sunucu
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

## Ağ Yapılandırması

### Statik IP
- **Sunucu IP:** 78.186.12.5

### DNS Kayıtları (ulusansigorta.com.tr)

| Kayıt Tipi | Ad | Değer | Öncelik |
|---|---|---|---|
| A | mail | 78.186.12.5 | - |
| MX | @ | mail.ulusansigorta.com.tr | 10 |
| TXT | @ | v=spf1 ip4:78.186.12.5 ~all | - |

### Firewall / Port Yönlendirme

| Port | Protokol | Servis |
|---|---|---|
| 25 | TCP | SMTP |
| 143 | TCP | IMAP |

### Windows Firewall Kuralı
```powershell
netsh advfirewall firewall add rule name="SMTP" dir=in action=allow protocol=TCP localport=25
netsh advfirewall firewall add rule name="IMAP" dir=in action=allow protocol=TCP localport=143
```

## Lisans

Bu proje eğitim amaçlı geliştirilmiştir.
