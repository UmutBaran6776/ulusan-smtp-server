@echo off
echo ============================================================
echo  Ulusan Sigorta SMTP/IMAP Sunucusu - Derleme
echo  Bil314 - Bilgisayar Aglari Projesi
echo ============================================================

REM MinGW PATH ayari
set PATH=C:\msys64\mingw64\bin;%PATH%

echo Derleniyor...

g++ -std=c++17 -Wall -Wextra -O2 -static -static-libgcc -static-libstdc++ -o ulusan_smtp.exe ^
    src/main.cpp ^
    src/auth.cpp ^
    src/mail_store.cpp ^
    src/smtp_server.cpp ^
    src/imap_server.cpp ^
    -lws2_32

if %ERRORLEVEL% == 0 (
    echo.
    echo  [OK] Derleme basarili: ulusan_smtp.exe
    echo  Calistirmak icin: ulusan_smtp.exe
) else (
    echo.
    echo  [HATA] Derleme basarisiz!
)

echo.
pause
