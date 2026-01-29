#pragma once

#include <string>
#include <fstream>
#include <mutex>
#include <sstream>
#include <chrono>
#include <iomanip>
#include <ctime>
#include <windows.h>

class DebugLog
{
public:
    static DebugLog& Instance()
    {
        static DebugLog instance;
        return instance;
    }

    void Log(const std::string& level, const std::string& message)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        
        if (!m_file.is_open())
        {
            // Get executable directory
            char path[MAX_PATH];
            GetModuleFileNameA(nullptr, path, MAX_PATH);
            std::string exe_path(path);
            size_t last_slash = exe_path.find_last_of("\\/");
            if (last_slash != std::string::npos)
            {
                exe_path = exe_path.substr(0, last_slash + 1);
            }
            m_log_path = exe_path + "wiimote_bridge.log";
            m_file.open(m_log_path, std::ios::out | std::ios::app);
        }

        if (m_file.is_open())
        {
            auto now = std::chrono::system_clock::now();
            auto time = std::chrono::system_clock::to_time_t(now);
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                now.time_since_epoch()) % 1000;

            struct tm local_time;
            localtime_s(&local_time, &time);

            m_file << std::put_time(&local_time, "%Y-%m-%d %H:%M:%S")
                   << "." << std::setfill('0') << std::setw(3) << ms.count()
                   << " [" << level << "] " << message << std::endl;
            m_file.flush();
        }
    }

    void Info(const std::string& message) { Log("INFO", message); }
    void Error(const std::string& message) { Log("ERROR", message); }
    void Debug(const std::string& message) { Log("DEBUG", message); }
    void Notice(const std::string& message) { Log("NOTICE", message); }

    std::string GetLogPath() const { return m_log_path; }

private:
    DebugLog() = default;
    ~DebugLog() { if (m_file.is_open()) m_file.close(); }
    
    DebugLog(const DebugLog&) = delete;
    DebugLog& operator=(const DebugLog&) = delete;

    std::ofstream m_file;
    std::mutex m_mutex;
    std::string m_log_path;
};

// Convenience macros
#define LOG_INFO(msg) DebugLog::Instance().Info(msg)
#define LOG_ERROR(msg) DebugLog::Instance().Error(msg)
#define LOG_DEBUG(msg) DebugLog::Instance().Debug(msg)
#define LOG_NOTICE(msg) DebugLog::Instance().Notice(msg)

// Helper for formatting
inline std::string LogFormat(const char* format, ...)
{
    char buffer[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    return std::string(buffer);
}
