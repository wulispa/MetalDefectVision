#include "Logger.h"

#include <chrono>
#include <iomanip>
#include <sstream>
#include <ctime>
#include <filesystem>
#include <iostream>

Logger& Logger::instance()
{
    static Logger logger;
    return logger;
}

Logger::Logger()
{
}

Logger::~Logger()
{
    if (logFile.is_open())
        logFile.close();
}

void Logger::init()
{
    std::lock_guard<std::mutex> lock(mtx);

    currentDate = getDate();

    // 使用Log/system_log文件夹存储系统日志
    std::string logDir = "Log/system_log/";

    // 确保日志目录存在
    if (!std::filesystem::exists(logDir)) {
        std::error_code ec;
        std::filesystem::create_directories(logDir, ec);
        if (ec) {
            // 目录创建失败，回退到根目录
            logDir = "";
        }
    }

    std::string filename = logDir + "system_log_" + currentDate + ".txt";

    if (logFile.is_open())
        logFile.close();

    logFile.open(filename, std::ios::app);
}

void Logger::info(const std::string& msg)
{
    log("INFO", msg);
}

void Logger::warn(const std::string& msg)
{
    log("WARN", msg);
}

void Logger::error(const std::string& msg)
{
    log("ERROR", msg);
}

void Logger::debug(const std::string& msg)
{
    // 只有在启用DEBUG模式时才记录
    if (!m_debugEnabled)
        return;
    log("DEBUG", msg);
}

void Logger::setEnabled(bool enabled)
{
    std::lock_guard<std::mutex> lock(mtx);
    m_enabled = enabled;
}

void Logger::setDebugEnabled(bool enabled)
{
    std::lock_guard<std::mutex> lock(mtx);
    m_debugEnabled = enabled;
}

bool Logger::isDebugEnabled() const
{
    return m_debugEnabled;
}

void Logger::log(const std::string& level, const std::string& msg)
{
    std::lock_guard<std::mutex> lock(mtx);

    // 如果日志未启用，则不记录
    if (!m_enabled)
        return;

    checkDateChange();

    if (!logFile.is_open())
        return;

    logFile << "[" << getTime() << "] "
        << "[" << level << "] "
        << msg << std::endl;
}

std::string Logger::getTime()
{
    auto now = std::chrono::system_clock::now();
    auto itt = std::chrono::system_clock::to_time_t(now);

    std::tm tm;

#ifdef _WIN32
    localtime_s(&tm, &itt);
#else
    localtime_r(&itt, &tm);
#endif

    std::stringstream ss;

    ss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");

    return ss.str();
}

std::string Logger::getDate()
{
    auto now = std::chrono::system_clock::now();
    auto itt = std::chrono::system_clock::to_time_t(now);

    std::tm tm;

#ifdef _WIN32
    localtime_s(&tm, &itt);
#else
    localtime_r(&itt, &tm);
#endif

    std::stringstream ss;

    ss << std::put_time(&tm, "%Y_%m_%d");

    return ss.str();
}

void Logger::checkDateChange()
{
     std::string today = getDate();

    if (today != currentDate)
    {
        currentDate = today;

        if (logFile.is_open())
            logFile.close();

        // 使用Log/system_log文件夹存储系统日志
        std::string logDir = "Log/system_log/";

        // 确保日志目录存在
        if (!std::filesystem::exists(logDir)) {
            std::error_code ec;
            std::filesystem::create_directories(logDir, ec);
            if (ec) {
                // 目录创建失败，回退到根目录
                logDir = "";
            }
        }

        std::string filename = logDir + "system_log_" + currentDate + ".txt";

        logFile.open(filename, std::ios::app);
    }
}