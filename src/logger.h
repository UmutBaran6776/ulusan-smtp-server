#ifndef LOGGER_H
#define LOGGER_H

#include <string>
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>
#include <deque>
#include "utils.h"

/*
 * Logger - Thread-safe loglama modulu
 * Dosyaya ve konsola log yazar.
 * Singleton pattern kullanilir.
 */

enum class LogLevel {
    INFO,
    WARNING,
    ERR,
    DEBUG
};

class Logger {
private:
    std::ofstream logFile;
    std::string logFilePath;
    std::mutex mtx;
    bool consoleOutput;

    Logger() : consoleOutput(false) {}

public:
    static Logger& getInstance() {
        static Logger instance;
        return instance;
    }

    void init(const std::string& filename, bool console = false) {
        std::lock_guard<std::mutex> lock(mtx);
        logFilePath = filename;
        if (logFile.is_open()) logFile.close();
        logFile.open(filename, std::ios::app);
        consoleOutput = console;
    }

    void log(LogLevel level, const std::string& message) {
        std::lock_guard<std::mutex> lock(mtx);

        std::string levelStr;
        switch (level) {
            case LogLevel::INFO:    levelStr = "INFO "; break;
            case LogLevel::WARNING: levelStr = "WARN "; break;
            case LogLevel::ERR:     levelStr = "ERROR"; break;
            case LogLevel::DEBUG:   levelStr = "DEBUG"; break;
        }

        std::ostringstream oss;
        oss << "[" << utils::getCurrentTimestamp() << "] [" << levelStr << "] " << message;
        std::string logLine = oss.str();

        if (logFile.is_open()) {
            logFile << logLine << std::endl;
            logFile.flush();
        }

        if (consoleOutput) {
            std::cout << logLine << std::endl;
        }
    }

    void info(const std::string& msg)    { log(LogLevel::INFO, msg); }
    void warning(const std::string& msg) { log(LogLevel::WARNING, msg); }
    void error(const std::string& msg)   { log(LogLevel::ERR, msg); }
    void debug(const std::string& msg)   { log(LogLevel::DEBUG, msg); }

    // Son N satir logu oku (admin paneli icin)
    std::string readLogs(int lastN = 30) {
        std::lock_guard<std::mutex> lock(mtx);
        std::ifstream inFile(logFilePath);
        if (!inFile.is_open()) return "Log dosyasi bulunamadi.\n";

        std::deque<std::string> lines;
        std::string line;
        while (std::getline(inFile, line)) {
            lines.push_back(line);
            if ((int)lines.size() > lastN) {
                lines.pop_front();
            }
        }
        inFile.close();

        std::ostringstream oss;
        for (const auto& l : lines) {
            oss << l << "\n";
        }
        return oss.str();
    }

    ~Logger() {
        if (logFile.is_open()) logFile.close();
    }

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;
};

#define LOG_INFO(msg) Logger::getInstance().info(msg)
#define LOG_WARN(msg) Logger::getInstance().warning(msg)
#define LOG_ERROR(msg) Logger::getInstance().error(msg)
#define LOG_DEBUG(msg) Logger::getInstance().debug(msg)

#endif // LOGGER_H
