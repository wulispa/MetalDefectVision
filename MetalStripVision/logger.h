#pragma once

#include <string>
#include <mutex>
#include <fstream>

class Logger
{
public:

    static Logger& instance();

    void init();
    void setEnabled(bool enabled);  // 设置是否启用日志
    void setDebugEnabled(bool enabled);  // 设置是否启用DEBUG级别日志
    bool isDebugEnabled() const;  // 检查是否启用DEBUG级别日志

    void info(const std::string& msg);
    void warn(const std::string& msg);
    void error(const std::string& msg);
    void debug(const std::string& msg);

private:

    Logger();
    ~Logger();

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    void log(const std::string& level, const std::string& msg);
    std::string getTime();
    std::string getDate();

    void checkDateChange();

private:

    std::ofstream logFile;
    std::mutex mtx;
    bool m_enabled = true;  // 日志开关，默认启用
    bool m_debugEnabled = false;  // DEBUG级别日志开关，默认关闭

    std::string currentDate;
};