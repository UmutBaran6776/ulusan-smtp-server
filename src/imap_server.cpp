#include "imap_server.h"
#include "logger.h"
#include "utils.h"
#include "base64.h"
#include <iostream>
#include <sstream>
#include <vector>
#include <algorithm>
#include <chrono>

ImapServer::ImapServer(int p, AuthManager& a, MailStore& s)
    : port(p), listenSocket(INVALID_SOCKET), running(false), auth(a), store(s) {}
ImapServer::~ImapServer() { stop(); }

bool ImapServer::start() {
    listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listenSocket == INVALID_SOCKET) return false;
    int opt = 1;
    setsockopt(listenSocket, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));
    sockaddr_in sa{}; sa.sin_family = AF_INET; sa.sin_addr.s_addr = INADDR_ANY; sa.sin_port = htons(port);
    if (bind(listenSocket, (sockaddr*)&sa, sizeof(sa)) == SOCKET_ERROR) { closesocket(listenSocket); return false; }
    if (listen(listenSocket, SOMAXCONN) == SOCKET_ERROR) { closesocket(listenSocket); return false; }
    running = true;
    serverThread = std::thread(&ImapServer::serverLoop, this);
    LOG_INFO("IMAP: Sunucu baslatildi, port " + std::to_string(port));
    return true;
}

void ImapServer::stop() {
    running = false;
    if (listenSocket != INVALID_SOCKET) { closesocket(listenSocket); listenSocket = INVALID_SOCKET; }
    if (serverThread.joinable()) serverThread.join();
}

void ImapServer::serverLoop() {
    while (running) {
        sockaddr_in ca{}; int al = sizeof(ca);
        SOCKET cs = accept(listenSocket, (sockaddr*)&ca, &al);
        if (cs == INVALID_SOCKET) continue;
        char ab[INET_ADDRSTRLEN]; inet_ntop(AF_INET, &ca.sin_addr, ab, sizeof(ab));
        std::string addr = std::string(ab) + ":" + std::to_string(ntohs(ca.sin_port));
        std::thread(&ImapServer::handleClient, this, cs, addr).detach();
    }
}

void ImapServer::sendResp(SOCKET s, const std::string& r) {
    // TX log - literal data icindeki satir sonlarini gosterme
    std::string logMsg = r.substr(0, 200);
    if (r.size() > 200) logMsg += "...(" + std::to_string(r.size()) + " bytes)";
    // Newline'lari kaldir log icin
    for (auto& ch : logMsg) if (ch == '\r' || ch == '\n') ch = ' ';
    LOG_DEBUG("IMAP TX: " + logMsg);
    std::string m = r + "\r\n";
    int total = (int)m.size();
    int sent = 0;
    while (sent < total) {
        int n = send(s, m.c_str() + sent, total - sent, 0);
        if (n <= 0) break;
        sent += n;
    }
}

std::string ImapServer::recvLine(SOCKET s) {
    std::string l; char c;
    while (true) {
        int r = recv(s, &c, 1, 0);
        if (r <= 0) return "";
        if (c == '\n') { if (!l.empty() && l.back() == '\r') l.pop_back(); return l; }
        l += c;
        if (l.size() > 8192) return l;
    }
}

std::string ImapServer::flagsStr(int f) {
    std::string r;
    if (f & FLAG_SEEN) r += "\\Seen ";
    if (f & FLAG_DELETED) r += "\\Deleted ";
    if (f & FLAG_FLAGGED) r += "\\Flagged ";
    if (f & FLAG_ANSWERED) r += "\\Answered ";
    if (f & FLAG_DRAFT) r += "\\Draft ";
    if (f & FLAG_RECENT) r += "\\Recent ";
    if (!r.empty() && r.back() == ' ') r.pop_back();
    return r;
}

int ImapServer::parseFlags(const std::string& s) {
    int f = 0;
    std::string u = utils::toUpper(s);
    if (u.find("\\SEEN") != std::string::npos) f |= FLAG_SEEN;
    if (u.find("\\DELETED") != std::string::npos) f |= FLAG_DELETED;
    if (u.find("\\FLAGGED") != std::string::npos) f |= FLAG_FLAGGED;
    if (u.find("\\ANSWERED") != std::string::npos) f |= FLAG_ANSWERED;
    if (u.find("\\DRAFT") != std::string::npos) f |= FLAG_DRAFT;
    return f;
}

bool ImapServer::parseSeqSet(const std::string& ss, int mx, std::vector<int>& out) {
    out.clear();
    auto parts = utils::split(ss, ',');
    for (auto& p : parts) {
        p = utils::trim(p);
        size_t col = p.find(':');
        if (col != std::string::npos) {
            std::string sa = p.substr(0, col), sb = p.substr(col + 1);
            int a = (sa == "*") ? mx : std::stoi(sa);
            int b = (sb == "*") ? mx : std::stoi(sb);
            if (a > b) std::swap(a, b);
            for (int i = a; i <= b && i <= mx; i++) if (i >= 1) out.push_back(i);
        } else {
            int v = (p == "*") ? mx : std::stoi(p);
            if (v >= 1 && v <= mx) out.push_back(v);
        }
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return !out.empty();
}

void ImapServer::handleClient(SOCKET cs, std::string addr) {
    LOG_INFO("IMAP: Baglanti: " + addr);
    sendResp(cs, "* OK [CAPABILITY IMAP4rev1 AUTH=PLAIN] Ulusan Sigorta IMAP Ready");

    bool authed = false;
    std::string user, folder;

    while (true) {
        std::string line = recvLine(cs);
        if (line.empty()) break;
        LOG_DEBUG("IMAP RX: " + line);

        size_t sp = line.find(' ');
        if (sp == std::string::npos) continue;
        std::string tag = line.substr(0, sp);
        std::string rest = line.substr(sp + 1);
        std::string restUp = utils::toUpper(rest);

        // CAPABILITY
        if (utils::startsWithCI(rest, "CAPABILITY")) {
            sendResp(cs, "* CAPABILITY IMAP4rev1 AUTH=PLAIN IDLE NAMESPACE");
            sendResp(cs, tag + " OK CAPABILITY completed");
            continue;
        }
        // LOGOUT
        if (utils::startsWithCI(rest, "LOGOUT")) {
            sendResp(cs, "* BYE Ulusan Sigorta IMAP logging out");
            sendResp(cs, tag + " OK LOGOUT completed");
            break;
        }
        // NOOP
        if (utils::startsWithCI(rest, "NOOP")) {
            sendResp(cs, tag + " OK NOOP completed");
            continue;
        }
        // AUTHENTICATE PLAIN
        if (utils::startsWithCI(rest, "AUTHENTICATE PLAIN")) {
            // Eger veri ayni satirda gelmediyse, + gonder ve bir satir daha oku
            std::string encoded;
            std::string afterCmd = utils::trim(rest.substr(18));
            if (afterCmd.empty()) {
                sendResp(cs, "+");
                encoded = recvLine(cs);
                if (encoded.empty()) break;
                encoded = utils::trim(encoded);
            } else {
                encoded = afterCmd;
            }
            // Base64 decode: \0username\0password
            std::string decoded = base64::decode(encoded);
            std::string aUser, aPass;
            size_t first = decoded.find('\0');
            size_t second = (first != std::string::npos) ? decoded.find('\0', first + 1) : std::string::npos;
            if (first != std::string::npos && second != std::string::npos) {
                aUser = decoded.substr(first + 1, second - first - 1);
                aPass = decoded.substr(second + 1);
            }
            if (!aUser.empty() && auth.authenticate(aUser, aPass)) {
                authed = true; user = aUser;
                sendResp(cs, tag + " OK AUTHENTICATE completed");
                LOG_INFO("IMAP: Auth PLAIN: " + user);
            } else {
                sendResp(cs, tag + " NO AUTHENTICATE failed");
            }
            continue;
        }
        // LOGIN
        if (utils::startsWithCI(rest, "LOGIN")) {
            std::istringstream iss(rest); std::string c, u, p;
            iss >> c >> u >> p;
            auto strip = [](std::string& s) {
                if (s.size() >= 2 && s.front() == '"' && s.back() == '"') s = s.substr(1, s.size()-2);
            };
            strip(u); strip(p);
            if (auth.authenticate(u, p)) {
                authed = true; user = u;
                sendResp(cs, tag + " OK LOGIN completed");
                LOG_INFO("IMAP: Login: " + user);
            } else {
                sendResp(cs, tag + " NO LOGIN failed");
            }
            continue;
        }

        if (!authed) { sendResp(cs, tag + " BAD Please login first"); continue; }

        // NAMESPACE
        if (utils::startsWithCI(rest, "NAMESPACE")) {
            sendResp(cs, "* NAMESPACE ((\"\" \"/\")) NIL NIL");
            sendResp(cs, tag + " OK NAMESPACE completed");
            continue;
        }
        // LIST
        if (utils::startsWithCI(rest, "LIST")) {
            // Bos referans kontrolu
            if (rest.find("\"\" \"\"") != std::string::npos) {
                sendResp(cs, "* LIST (\\Noselect) \"/\" \"\"");
            } else {
                sendResp(cs, "* LIST (\\HasNoChildren) \"/\" \"INBOX\"");
                sendResp(cs, "* LIST (\\HasNoChildren \\Sent) \"/\" \"Sent\"");
            }
            sendResp(cs, tag + " OK LIST completed");
            continue;
        }
        // LSUB
        if (utils::startsWithCI(rest, "LSUB")) {
            sendResp(cs, "* LSUB () \"/\" \"INBOX\"");
            sendResp(cs, "* LSUB (\\Sent) \"/\" \"Sent\"");
            sendResp(cs, tag + " OK LSUB completed");
            continue;
        }
        // SELECT / EXAMINE
        if (utils::startsWithCI(rest, "SELECT") || utils::startsWithCI(rest, "EXAMINE")) {
            bool ro = utils::startsWithCI(rest, "EXAMINE");
            size_t s1 = rest.find(' ');
            std::string mb = (s1 != std::string::npos) ? utils::trim(rest.substr(s1+1)) : "INBOX";
            if (mb.front() == '"' && mb.back() == '"') mb = mb.substr(1, mb.size()-2);
            std::string fl = utils::toUpper(mb);
            if (fl == "INBOX") folder = "inbox";
            else if (fl == "SENT") folder = "sent";
            else folder = "inbox";

            int cnt = store.getMailCountInFolder(user, folder);
            int unseen = 0;
            auto mails = store.loadFolder(user, folder);
            for (auto& m : mails) if (!(m.flags & FLAG_SEEN)) unseen++;
            int uidnext = store.getNextUid(user, folder);

            sendResp(cs, "* " + std::to_string(cnt) + " EXISTS");
            sendResp(cs, "* 0 RECENT");
            sendResp(cs, "* FLAGS (\\Seen \\Answered \\Flagged \\Deleted \\Draft)");
            sendResp(cs, "* OK [PERMANENTFLAGS (\\Seen \\Answered \\Flagged \\Deleted \\Draft \\*)] Limited");
            if (unseen > 0) sendResp(cs, "* OK [UNSEEN 1] First unseen");
            sendResp(cs, "* OK [UIDVALIDITY 1] UIDs valid");
            sendResp(cs, "* OK [UIDNEXT " + std::to_string(uidnext) + "] Predicted next UID");
            sendResp(cs, tag + " OK [" + std::string(ro ? "READ-ONLY" : "READ-WRITE") + "] " +
                     std::string(ro ? "EXAMINE" : "SELECT") + " completed");
            continue;
        }
        // STATUS
        if (utils::startsWithCI(rest, "STATUS")) {
            size_t s1 = rest.find(' '); if (s1 == std::string::npos) { sendResp(cs, tag + " BAD"); continue; }
            std::string args = rest.substr(s1+1);
            size_t paren = args.find('(');
            std::string mb = utils::trim(args.substr(0, paren));
            if (mb.front() == '"' && mb.back() == '"') mb = mb.substr(1, mb.size()-2);
            std::string fl = utils::toUpper(mb);
            std::string f2 = (fl == "SENT") ? "sent" : "inbox";
            int cnt = store.getMailCountInFolder(user, f2);
            int uidnext = store.getNextUid(user, f2);
            auto mails = store.loadFolder(user, f2);
            int unseen = 0;
            for (auto& m : mails) if (!(m.flags & FLAG_SEEN)) unseen++;
            sendResp(cs, "* STATUS \"" + mb + "\" (MESSAGES " + std::to_string(cnt) +
                     " RECENT 0 UNSEEN " + std::to_string(unseen) +
                     " UIDVALIDITY 1 UIDNEXT " + std::to_string(uidnext) + ")");
            sendResp(cs, tag + " OK STATUS completed");
            continue;
        }
        // CLOSE
        if (utils::startsWithCI(rest, "CLOSE")) {
            if (!folder.empty()) store.expungeMails(user, folder);
            folder = "";
            sendResp(cs, tag + " OK CLOSE completed");
            continue;
        }
        // EXPUNGE
        if (utils::startsWithCI(rest, "EXPUNGE")) {
            auto exp = store.expungeMails(user, folder);
            for (int e : exp) sendResp(cs, "* " + std::to_string(e) + " EXPUNGE");
            sendResp(cs, tag + " OK EXPUNGE completed");
            continue;
        }
        // IDLE
        if (utils::startsWithCI(rest, "IDLE")) {
            sendResp(cs, "+ idling");
            int lastCount = store.getMailCountInFolder(user, folder);
            auto start = std::chrono::steady_clock::now();
            while (true) {
                fd_set fds; FD_ZERO(&fds); FD_SET(cs, &fds);
                timeval tv; tv.tv_sec = 3; tv.tv_usec = 0;
                int sel = select(0, &fds, NULL, NULL, &tv);
                if (sel > 0) {
                    std::string done = recvLine(cs);
                    if (done.empty() || utils::startsWithCI(done, "DONE")) break;
                }
                // Yeni mail kontrolu
                int curCount = store.getMailCountInFolder(user, folder);
                if (curCount != lastCount) {
                    sendResp(cs, "* " + std::to_string(curCount) + " EXISTS");
                    lastCount = curCount;
                }
                auto now = std::chrono::steady_clock::now();
                if (std::chrono::duration_cast<std::chrono::seconds>(now - start).count() > 300) break;
            }
            sendResp(cs, tag + " OK IDLE terminated");
            continue;
        }

        // Folder secilmemisse
        if (folder.empty() && (utils::startsWithCI(rest, "FETCH") ||
            utils::startsWithCI(rest, "UID") || utils::startsWithCI(rest, "SEARCH") ||
            utils::startsWithCI(rest, "STORE"))) {
            sendResp(cs, tag + " BAD No mailbox selected");
            continue;
        }

        // SEARCH / UID SEARCH
        if (utils::startsWithCI(rest, "SEARCH") || utils::startsWithCI(rest, "UID SEARCH")) {
            bool isUid = utils::startsWithCI(rest, "UID SEARCH");
            auto mails = store.loadFolder(user, folder);
            std::string result = "* SEARCH";
            std::string sUp = utils::toUpper(rest);
            for (int i = 0; i < (int)mails.size(); i++) {
                bool match = false;
                if (sUp.find("ALL") != std::string::npos) match = true;
                else if (sUp.find("UNSEEN") != std::string::npos) match = !(mails[i].flags & FLAG_SEEN);
                else if (sUp.find("SEEN") != std::string::npos) match = (mails[i].flags & FLAG_SEEN);
                else if (sUp.find("FLAGGED") != std::string::npos) match = (mails[i].flags & FLAG_FLAGGED);
                else if (sUp.find("DELETED") != std::string::npos) match = (mails[i].flags & FLAG_DELETED);
                else if (sUp.find("NOT DELETED") != std::string::npos) match = !(mails[i].flags & FLAG_DELETED);
                else match = true;
                if (match) {
                    int id = isUid ? mails[i].uid : (i + 1);
                    result += " " + std::to_string(id);
                }
            }
            sendResp(cs, result);
            sendResp(cs, tag + " OK SEARCH completed");
            continue;
        }

        // STORE / UID STORE
        if (utils::startsWithCI(rest, "STORE") || utils::startsWithCI(rest, "UID STORE")) {
            bool isUid = utils::startsWithCI(rest, "UID STORE");
            std::string args = rest.substr(isUid ? 10 : 6);
            std::istringstream iss(args);
            std::string seqStr, action, flagParen;
            iss >> seqStr >> action;
            std::getline(iss, flagParen);
            flagParen = utils::trim(flagParen);

            int flagBits = parseFlags(flagParen);
            auto mails = store.loadFolder(user, folder);
            int mx = (int)mails.size();
            std::vector<int> indices;

            if (isUid) {
                // UID tabanlı
                auto parts = utils::split(seqStr, ',');
                for (auto& p : parts) {
                    p = utils::trim(p);
                    for (int i = 0; i < mx; i++) {
                        size_t col = p.find(':');
                        if (col != std::string::npos) {
                            int a = (p.substr(0,col) == "*") ? 999999 : std::stoi(p.substr(0,col));
                            int b = (p.substr(col+1) == "*") ? 999999 : std::stoi(p.substr(col+1));
                            if (a > b) std::swap(a, b);
                            if (mails[i].uid >= a && mails[i].uid <= b) indices.push_back(i);
                        } else {
                            int v = (p == "*") ? 999999 : std::stoi(p);
                            if (mails[i].uid == v) indices.push_back(i);
                        }
                    }
                }
            } else {
                parseSeqSet(seqStr, mx, indices);
                for (auto& idx : indices) idx--; // 0-based
            }

            std::string actUp = utils::toUpper(action);
            for (int idx : indices) {
                if (idx < 0 || idx >= mx) continue;
                if (actUp.find("+FLAGS") != std::string::npos) {
                    store.addMailFlags(user, folder, idx, flagBits);
                } else if (actUp.find("-FLAGS") != std::string::npos) {
                    store.removeMailFlags(user, folder, idx, flagBits);
                } else {
                    store.setMailFlags(user, folder, idx, flagBits);
                }
                int nf = store.getMailFlags(user, folder, idx);
                int id = isUid ? mails[idx].uid : (idx + 1);
                std::string resp = "* " + std::to_string(isUid ? (idx+1) : id) + " FETCH (FLAGS (" + flagsStr(nf) + ")";
                if (isUid) resp += " UID " + std::to_string(mails[idx].uid);
                resp += ")";
                sendResp(cs, resp);
            }
            sendResp(cs, tag + " OK STORE completed");
            continue;
        }

        // FETCH / UID FETCH
        if (utils::startsWithCI(rest, "FETCH") || utils::startsWithCI(rest, "UID FETCH")) {
            bool isUid = utils::startsWithCI(rest, "UID FETCH");
            std::string args = rest.substr(isUid ? 10 : 6);
            size_t sp2 = args.find(' ');
            std::string seqStr = (sp2 != std::string::npos) ? args.substr(0, sp2) : args;
            std::string items = (sp2 != std::string::npos) ? args.substr(sp2 + 1) : "(FLAGS)";
            std::string itemsUp = utils::toUpper(items);

            auto mails = store.loadFolder(user, folder);
            int mx = (int)mails.size();
            std::vector<int> indices;

            if (isUid) {
                auto parts = utils::split(seqStr, ',');
                for (auto& p : parts) {
                    p = utils::trim(p);
                    for (int i = 0; i < mx; i++) {
                        size_t col = p.find(':');
                        if (col != std::string::npos) {
                            int a = (p.substr(0,col) == "*") ? 999999 : std::stoi(p.substr(0,col));
                            int b = (p.substr(col+1) == "*") ? 999999 : std::stoi(p.substr(col+1));
                            if (a > b) std::swap(a, b);
                            if (mails[i].uid >= a && mails[i].uid <= b) indices.push_back(i);
                        } else {
                            int v = (p == "*") ? 999999 : std::stoi(p);
                            if (mails[i].uid == v) indices.push_back(i);
                        }
                    }
                }
            } else {
                parseSeqSet(seqStr, mx, indices);
                for (auto& idx : indices) idx--;
            }

            bool wantFlags = itemsUp.find("FLAGS") != std::string::npos;
            bool wantUid = isUid || itemsUp.find("UID") != std::string::npos;
            bool wantEnv = itemsUp.find("ENVELOPE") != std::string::npos;
            bool wantSize = itemsUp.find("RFC822.SIZE") != std::string::npos;
            bool wantPeek = itemsUp.find("BODY.PEEK") != std::string::npos;
            bool wantHdr = itemsUp.find("HEADER") != std::string::npos;
            bool wantDate = itemsUp.find("INTERNALDATE") != std::string::npos;
            bool wantBS = itemsUp.find("BODYSTRUCTURE") != std::string::npos;
            // BODY[] istegi: RFC822 (ama RFC822.SIZE degil) veya BODY[] (ama BODY.PEEK degil)
            bool wantFullBody = false;
            if (itemsUp.find("BODY[]") != std::string::npos && !wantPeek) wantFullBody = true;
            if (itemsUp.find("RFC822") != std::string::npos && itemsUp.find("RFC822.SIZE") == std::string::npos) wantFullBody = true;

            for (int idx : indices) {
                if (idx < 0 || idx >= mx) continue;
                const Mail& m = mails[idx];
                int seqNum = idx + 1;

                // Literal data varsa ayri gonderilmesi lazim
                std::string raw;
                if (wantSize || wantFullBody || wantPeek || wantHdr || wantBS) {
                    raw = store.getRawMailFromFolder(user, folder, idx);
                }

                std::string resp = "* " + std::to_string(seqNum) + " FETCH (";
                bool first = true;
                auto addSep = [&]() { if (!first) resp += " "; first = false; };

                if (wantUid) { addSep(); resp += "UID " + std::to_string(m.uid); }
                if (wantFlags) { addSep(); resp += "FLAGS (" + flagsStr(m.flags) + ")"; }
                if (wantDate) {
                    addSep();
                    // IMAP INTERNALDATE formati: "dd-Mon-yyyy HH:MM:SS +ZZZZ"
                    // m.date RFC 2822: "Mon, 27 Apr 2026 13:58:19 +0300"
                    // Donustur
                    std::string idate = m.date;
                    // Basit donusum: gun, ay, yil ve saat cikart
                    std::istringstream ds(m.date);
                    std::string dow, dayS, monS, yearS, timeS, tzS;
                    ds >> dow >> dayS >> monS >> yearS >> timeS >> tzS;
                    // dow = "Mon," dayS = "27" monS = "Apr" yearS = "2026" timeS = "13:58:19" tzS = "+0300"
                    if (!dayS.empty() && dayS.back() == ',') dayS.pop_back();
                    if (!dow.empty() && dow.back() == ',') dow.pop_back();
                    if (!yearS.empty() && !timeS.empty()) {
                        idate = dayS + "-" + monS + "-" + yearS + " " + timeS + " " + tzS;
                    }
                    resp += "INTERNALDATE \"" + idate + "\"";
                }
                if (wantSize) { addSep(); resp += "RFC822.SIZE " + std::to_string(raw.size()); }

                if (wantPeek && wantHdr) {
                    // BODY.PEEK[HEADER]
                    std::string hdr;
                    size_t pos = raw.find("\r\n\r\n");
                    hdr = (pos != std::string::npos) ? raw.substr(0, pos + 4) : raw;
                    addSep();
                    resp += "BODY[HEADER] {" + std::to_string(hdr.size()) + "}";
                    resp += "\r\n"; // literal marker sonu
                    // Literal gonder + kapaniş
                    resp += hdr;
                    resp += ")";
                    sendResp(cs, resp);
                    // Seen isareti PEEK oldugu icin koymuyoruz
                } else if (wantFullBody) {
                    addSep();
                    resp += "BODY[] {" + std::to_string(raw.size()) + "}";
                    resp += "\r\n";
                    resp += raw;
                    resp += ")";
                    sendResp(cs, resp);
                    store.addMailFlags(user, folder, idx, FLAG_SEEN);
                } else if (wantPeek && !wantHdr) {
                    addSep();
                    resp += "BODY[] {" + std::to_string(raw.size()) + "}";
                    resp += "\r\n";
                    resp += raw;
                    resp += ")";
                    sendResp(cs, resp);
                } else {
                    resp += ")";
                    sendResp(cs, resp);
                }

                if (wantEnv) {
                    // Envelope ayri gonderilmez, yukarida resp icine eklenmeli
                    // Ama basitlik icin burada ignore ediyoruz - telefon genelde envelope istemiyor
                }
            }
            sendResp(cs, tag + " OK FETCH completed");
            continue;
        }

        // APPEND
        if (utils::startsWithCI(rest, "APPEND")) {
            // APPEND "INBOX" (\Seen) {size}
            size_t braceOpen = rest.find('{');
            if (braceOpen == std::string::npos) { sendResp(cs, tag + " BAD"); continue; }
            int sz = std::stoi(rest.substr(braceOpen + 1));
            int fl = parseFlags(rest);
            std::string mb = "inbox";
            if (utils::toUpper(rest).find("SENT") != std::string::npos) mb = "sent";
            sendResp(cs, "+ Ready for literal data");
            std::string data; data.reserve(sz);
            int received = 0;
            while (received < sz) {
                char buf[4096];
                int toRead = std::min(sz - received, 4096);
                int r = recv(cs, buf, toRead, 0);
                if (r <= 0) break;
                data.append(buf, r);
                received += r;
            }
            recvLine(cs); // trailing CRLF
            store.appendMail(user, mb, data, fl);
            sendResp(cs, tag + " OK APPEND completed");
            continue;
        }

        sendResp(cs, tag + " BAD Command not recognized");
    }
    closesocket(cs);
    LOG_INFO("IMAP: Baglanti kapatildi: " + addr);
}
