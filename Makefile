# ============================================================
# Ulusan Sigorta SMTP Sunucusu - Makefile
# Bil314 - Bilgisayar Aglari Projesi
# ============================================================

CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O2 -static -static-libgcc -static-libstdc++
LDFLAGS = -lws2_32

SRCDIR = src
SOURCES = $(SRCDIR)/main.cpp \
          $(SRCDIR)/auth.cpp \
          $(SRCDIR)/mail_store.cpp \
          $(SRCDIR)/smtp_server.cpp \
          $(SRCDIR)/imap_server.cpp

TARGET = ulusan_smtp.exe

all: $(TARGET)

$(TARGET): $(SOURCES)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SOURCES) $(LDFLAGS)

clean:
	del /Q $(TARGET) 2>nul

.PHONY: all clean
