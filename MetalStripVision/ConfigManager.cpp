#pragma execution_character_set("utf-8")

#include "ConfigManager.h"
#include "Logger.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <QFile>
#include <QDir>
#include <QString>

ConfigManager& ConfigManager::instance() {
    static ConfigManager instance;
    return instance;
}

bool ConfigManager::loadConfig(const std::string& configFile) {
    m_cameraConfigs.clear();

    // 简单的 XML 解析实现
    std::ifstream file(configFile);
    if (!file.is_open()) {
        Logger::instance().error("Failed to open config file: " + configFile);
        return false;
    }

    std::string line;
    CameraConfig currentConfig;
    bool inCamera = false;
    int currentCameraId = 0;

    while (std::getline(file, line)) {
        // 去除空白字符
        line.erase(0, line.find_first_not_of(" \t\r\n"));
        line.erase(line.find_last_not_of(" \t\r\n") + 1);

        if (line.empty()) continue;

        // 解析 XML 标签
        if (line.find("<Camera") != std::string::npos) {
            // 提取 id 属性
            size_t pos = line.find("id=\"");
            if (pos != std::string::npos) {
                pos += 4;
                size_t endPos = line.find("\"", pos);
                if (endPos != std::string::npos) {
                    currentCameraId = std::stoi(line.substr(pos, endPos - pos));
                }
            }
            // 重置currentConfig，确保所有字段都是默认值
            currentConfig = CameraConfig();
            currentConfig.id = currentCameraId;
            inCamera = true;
        }
        else if (line.find("<SerialNumber>") != std::string::npos) {
            size_t start = line.find(">") + 1;
            size_t end = line.find("<", start);
            if (start != std::string::npos && end != std::string::npos) {
                currentConfig.serialNumber = line.substr(start, end - start);
            }
        }
        else if (line.find("<UseTraditionalAlgo>") != std::string::npos) {
            size_t start = line.find(">") + 1;
            size_t end = line.find("<", start);
            if (start != std::string::npos && end != std::string::npos) {
                std::string val = line.substr(start, end - start);
                currentConfig.useTraditionalAlgo = (val == "true");
            }
        }
        else if (line.find("<UseAIAlgo>") != std::string::npos) {
            size_t start = line.find(">") + 1;
            size_t end = line.find("<", start);
            if (start != std::string::npos && end != std::string::npos) {
                std::string val = line.substr(start, end - start);
                currentConfig.useAIAlgo = (val == "true");
            }
        }
        else if (line.find("<TriggerMode>") != std::string::npos) {
            size_t start = line.find(">") + 1;
            size_t end = line.find("<", start);
            if (start != std::string::npos && end != std::string::npos) {
                currentConfig.triggerMode = std::stoi(line.substr(start, end - start));
            }
        }
        else if (line.find("<Enabled>") != std::string::npos) {
            size_t start = line.find(">") + 1;
            size_t end = line.find("<", start);
            if (start != std::string::npos && end != std::string::npos) {
                std::string enabledStr = line.substr(start, end - start);
                currentConfig.enabled = (enabledStr == "true");
            }
        }
        else if (line.find("<OriginalFormat>") != std::string::npos) {
            size_t start = line.find(">") + 1;
            size_t end = line.find("<", start);
            if (start != std::string::npos && end != std::string::npos) {
                currentConfig.originalFormat = line.substr(start, end - start);
            }
        }
        else if (line.find("</Camera>") != std::string::npos) {
            m_cameraConfigs.push_back(currentConfig);
            inCamera = false;
        }
        else if (line.find("<SavePath>") != std::string::npos) {
            size_t start = line.find(">") + 1;
            size_t end = line.find("<", start);
            if (start != std::string::npos && end != std::string::npos) {
                m_imageSaveConfig.savePath = line.substr(start, end - start);
            }
        }
        else if (line.find("<EnableDateFolder>") != std::string::npos) {
            size_t start = line.find(">") + 1;
            size_t end = line.find("<", start);
            if (start != std::string::npos && end != std::string::npos) {
                std::string enabledStr = line.substr(start, end - start);
                m_imageSaveConfig.enableDateFolder = (enabledStr == "true");
            }
        }
        else if (line.find("<SaveOriginalImage>") != std::string::npos) {
            size_t start = line.find(">") + 1;
            size_t end = line.find("<", start);
            if (start != std::string::npos && end != std::string::npos) {
                std::string enabledStr = line.substr(start, end - start);
                m_imageSaveConfig.saveOriginalImage = (enabledStr == "true");
            }
        }
        else if (line.find("<SaveDefectImage>") != std::string::npos) {
            size_t start = line.find(">") + 1;
            size_t end = line.find("<", start);
            if (start != std::string::npos && end != std::string::npos) {
                std::string enabledStr = line.substr(start, end - start);
                m_imageSaveConfig.saveDefectImage = (enabledStr == "true");
            }
        }
        else if (line.find("<CompressDefectImage>") != std::string::npos) {
            size_t start = line.find(">") + 1;
            size_t end = line.find("<", start);
            if (start != std::string::npos && end != std::string::npos) {
                std::string enabledStr = line.substr(start, end - start);
                m_imageSaveConfig.compressDefectImage = (enabledStr == "true");
            }
        }
        else if (line.find("<TestModeEnabled>") != std::string::npos) {
            size_t start = line.find(">") + 1;
            size_t end = line.find("<", start);
            if (start != std::string::npos && end != std::string::npos) {
                std::string enabledStr = line.substr(start, end - start);
                m_testModeConfig.enabled = (enabledStr == "true");
            }
        }
        else if (line.find("<TestImagePath>") != std::string::npos) {
            size_t start = line.find(">") + 1;
            size_t end = line.find("<", start);
            if (start != std::string::npos && end != std::string::npos) {
                m_testModeConfig.imageBasePath = line.substr(start, end - start);
            }
        }
        else if (line.find("<TestLoopImages>") != std::string::npos) {
            size_t start = line.find(">") + 1;
            size_t end = line.find("<", start);
            if (start != std::string::npos && end != std::string::npos) {
                std::string enabledStr = line.substr(start, end - start);
                m_testModeConfig.loopImages = (enabledStr == "true");
            }
        }
        else if (line.find("<TestImageDelayMs>") != std::string::npos) {
            size_t start = line.find(">") + 1;
            size_t end = line.find("<", start);
            if (start != std::string::npos && end != std::string::npos) {
                m_testModeConfig.imageDelayMs = std::stoi(line.substr(start, end - start));
            }
        }
        else if (line.find("<SystemLogEnabled>") != std::string::npos) {
            size_t start = line.find(">") + 1;
            size_t end = line.find("<", start);
            if (start != std::string::npos && end != std::string::npos) {
                std::string enabledStr = line.substr(start, end - start);
                m_systemLogConfig.enabled = (enabledStr == "true");
            }
        }
        else if (line.find("<DebugLogEnabled>") != std::string::npos) {
            size_t start = line.find(">") + 1;
            size_t end = line.find("<", start);
            if (start != std::string::npos && end != std::string::npos) {
                std::string enabledStr = line.substr(start, end - start);
                m_systemLogConfig.debugEnabled = (enabledStr == "true");
            }
        }
        // PLC配置解析
        else if (line.find("<PLCEnabled>") != std::string::npos) {
            size_t start = line.find(">") + 1;
            size_t end = line.find("<", start);
            if (start != std::string::npos && end != std::string::npos) {
                std::string enabledStr = line.substr(start, end - start);
                m_plcConfig.enabled = (enabledStr == "true");
            }
        }
        else if (line.find("<PLCIP>") != std::string::npos) {
            size_t start = line.find(">") + 1;
            size_t end = line.find("<", start);
            if (start != std::string::npos && end != std::string::npos) {
                m_plcConfig.ip = line.substr(start, end - start);
            }
        }
        else if (line.find("<PLCPort>") != std::string::npos) {
            size_t start = line.find(">") + 1;
            size_t end = line.find("<", start);
            if (start != std::string::npos && end != std::string::npos) {
                m_plcConfig.port = std::stoi(line.substr(start, end - start));
            }
        }
        else if (line.find("<PLCSlaveId>") != std::string::npos) {
            size_t start = line.find(">") + 1;
            size_t end = line.find("<", start);
            if (start != std::string::npos && end != std::string::npos) {
                m_plcConfig.slaveId = std::stoi(line.substr(start, end - start));
            }
        }
        // 自启动配置解析
        else if (line.find("<AutoStartEnabled>") != std::string::npos) {
            size_t start = line.find(">") + 1;
            size_t end = line.find("<", start);
            if (start != std::string::npos && end != std::string::npos) {
                std::string enabledStr = line.substr(start, end - start);
                m_autoStartConfig.enabled = (enabledStr == "true");
            }
        }
    }

    file.close();

    // 只有在系统日志启用时才记录配置加载信息
    Logger::instance().info("Config loaded. Camera count: " +
                             std::to_string(m_cameraConfigs.size()));
    for (const auto& config : m_cameraConfigs) {
        Logger::instance().info("Camera " + std::to_string(config.id) +
                                 " - Algorithm: " + config.getAlgorithmTypeDisplay());
    }

    return true;
}

std::vector<CameraConfig> ConfigManager::getCameraConfigs() const {
    return m_cameraConfigs;
}

ImageSaveConfig ConfigManager::getImageSaveConfig() const {
    return m_imageSaveConfig;
}

TestModeConfig ConfigManager::getTestModeConfig() const {
    return m_testModeConfig;
}

SystemLogConfig ConfigManager::getSystemLogConfig() const {
    return m_systemLogConfig;
}

PLCConfig ConfigManager::getPLCConfig() const {
    return m_plcConfig;
}

AutoStartConfig ConfigManager::getAutoStartConfig() const {
    return m_autoStartConfig;
}

bool ConfigManager::isCameraUserSetEnabled(int cameraId) const {
    for (const auto& config : m_cameraConfigs) {
        if (config.id == cameraId) {
            return config.useTraditionalAlgo;
        }
    }
    return false;
}

const CameraConfig& ConfigManager::getCameraConfig(int cameraId) const {
    static CameraConfig emptyConfig;
    for (const auto& config : m_cameraConfigs) {
        if (config.id == cameraId) {
            return config;
        }
    }
    return emptyConfig;
}

// 格式化配置（从模板复制）
bool ConfigManager::resetToDefault() {
    const std::string templateFile = "config_template/config.xml";
    const std::string targetFile = "config/config.xml";

    Logger::instance().info("Resetting system config from template: " + templateFile);

    // 检查模板文件是否存在
    if (!QFile::exists(QString::fromStdString(templateFile))) {
        Logger::instance().error("Template config file not found: " + templateFile);
        return false;
    }

    // 确保config目录存在
    QString targetDir = QString::fromStdString("config");
    if (!QDir().exists(targetDir)) {
        if (!QDir().mkpath(targetDir)) {
            Logger::instance().error("Failed to create config directory");
            return false;
        }
    }

    // 如果目标文件已存在，先删除
    if (QFile::exists(QString::fromStdString(targetFile))) {
        if (!QFile::remove(QString::fromStdString(targetFile))) {
            Logger::instance().error("Failed to remove old config file: " + targetFile);
            return false;
        }
    }

    // 从模板复制文件
    if (!QFile::copy(QString::fromStdString(templateFile),
                     QString::fromStdString(targetFile))) {
        Logger::instance().error("Failed to copy template system config to: " + targetFile);
        return false;
    }

    Logger::instance().info("System config reset successfully from template");

    // 重新加载配置
    return loadConfig(targetFile);
}

// 检查配置文件状态
ConfigManager::ConfigStatus ConfigManager::checkConfigStatus(const std::string& configFile) {
    // 检查文件是否存在
    if (!QFile::exists(QString::fromStdString(configFile))) {
        return ConfigStatus::Missing;
    }

    // 尝试解析XML
    try {
        std::ifstream file(configFile);
        if (!file.is_open()) {
            return ConfigStatus::Corrupted;
        }

        // 读取整个文件内容
        std::string content((std::istreambuf_iterator<char>(file)),
                           std::istreambuf_iterator<char>());
        file.close();

        // 检查是否为空
        if (content.empty()) {
            return ConfigStatus::Corrupted;
        }

        bool hasValidTag = false;
        bool hasValidRoot = false;

        // 根据文件名判断期望的根标签
        std::string expectedRoot = "<Configuration>";
        std::string expectedRootEnd = "</Configuration>";
        if (configFile.find("defect_colors") != std::string::npos) {
            expectedRoot = "<DefectColors>";
            expectedRootEnd = "</DefectColors>";
        }

        // 检查 XML 声明
        if (content.find("<?xml") != std::string::npos) {
            hasValidTag = true;
        }

        // 检查根标签
        if (content.find(expectedRoot) != std::string::npos) {
            hasValidRoot = true;
        }

        // 检查根标签闭合
        bool hasRootEnd = (content.find(expectedRootEnd) != std::string::npos);

        if (!hasValidTag || !hasValidRoot || !hasRootEnd) {
            return ConfigStatus::Corrupted;
        }

        // 简单的标签匹配检查：统计开始和结束标签数量
        int openTags = 0, closeTags = 0;
        size_t pos = 0;
        while ((pos = content.find("<", pos)) != std::string::npos) {
            size_t endPos = content.find(">", pos);
            if (endPos == std::string::npos) break;

            std::string tag = content.substr(pos, endPos - pos + 1);

            // 跳过注释、声明和自闭合标签
            if (tag.find("<?") != std::string::npos ||
                tag.find("<!--") != std::string::npos ||
                tag.find("/>") != std::string::npos) {
                pos = endPos + 1;
                continue;
            }

            if (tag.find("</") != std::string::npos) {
                closeTags++;
            } else if (tag.find("<") != std::string::npos && tag.find("<!") == std::string::npos) {
                openTags++;
            }
            pos = endPos + 1;
        }

        // 标签数量应该匹配（允许一些误差，因为可能有自闭合标签）
        if (openTags < 1 || closeTags < 1 || std::abs(openTags - closeTags) > 5) {
            return ConfigStatus::Corrupted;
        }

    } catch (...) {
        return ConfigStatus::Corrupted;
    }

    return ConfigStatus::OK;
}
