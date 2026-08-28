#pragma execution_character_set("utf-8")

#include "AlgorithmROIManager.h"
#include "Logger.h"
#include <fstream>
#include <filesystem>
#include <stdexcept>
#include <QFile>
#include <QString>

namespace fs = std::filesystem;

// 获取单例实例
AlgorithmROIManager& AlgorithmROIManager::instance() {
    static AlgorithmROIManager instance;
    return instance;
}

// 加载配置文件
bool AlgorithmROIManager::loadConfig(const std::string& configFile) {
    // 快速失败：如果已经加载过，直接返回
    if (m_loaded) {
        return true;
    }

    m_configFile = configFile;

    // 检查文件是否存在
    if (!QFile::exists(QString::fromStdString(configFile))) {
        Logger::instance().warn("ROI config file not found: " + configFile +
                               ", using memory defaults. Please format config if needed.");
        createDefaultConfig();
        m_loaded = true;
        return true;
    }

    // 尝试解析XML文件
    std::ifstream file(configFile);
    if (!file.is_open()) {
        Logger::instance().error("Failed to open ROI config file: " + configFile +
                                ", using memory defaults.");
        createDefaultConfig();
        m_loaded = true;
        return true;
    }

    try {
        Logger::instance().info("Loading ROI config from: " + configFile);

        std::string line;
        int currentCameraId = 0;
        UserProfile* currentProfile = nullptr;
        bool inCamera = false;
        bool inUserProfile = false;
        bool inROI = false;

        // AI阈值解析状态
        bool inAIThresholds = false;
        int aiCameraId = 0;
        AIDefectThreshold currentDefect;
        bool inAIDefect = false;

        while (std::getline(file, line)) {
            // 去除空白字符
            line.erase(0, line.find_first_not_of(" \t\r\n"));
            line.erase(line.find_last_not_of(" \t\r\n") + 1);

            if (line.empty()) continue;

            // 解析XML标签
            // AI阈值解析（优先处理，避免与常规Camera标签混淆）
            if (line.find("<AIThresholds>") != std::string::npos) {
                inAIThresholds = true;
            }
            else if (line.find("</AIThresholds>") != std::string::npos) {
                inAIThresholds = false;
                aiCameraId = 0;
            }
            else if (inAIThresholds && line.find("<Camera") != std::string::npos) {
                size_t pos = line.find("id=\"");
                if (pos != std::string::npos) {
                    pos += 4;
                    size_t endPos = line.find("\"", pos);
                    if (endPos != std::string::npos) {
                        aiCameraId = std::stoi(line.substr(pos, endPos - pos));
                    }
                }
            }
            else if (inAIThresholds && line.find("</Camera>") != std::string::npos) {
                aiCameraId = 0;
                inAIDefect = false;
            }
            else if (inAIThresholds && aiCameraId > 0 && line.find("<Defect") != std::string::npos) {
                currentDefect = AIDefectThreshold();
                // 提取classId
                size_t pos = line.find("classId=\"");
                if (pos != std::string::npos) {
                    pos += 9;
                    size_t endPos = line.find("\"", pos);
                    if (endPos != std::string::npos) {
                        currentDefect.classId = std::stoi(line.substr(pos, endPos - pos));
                    }
                }
                // 提取name
                pos = line.find("name=\"");
                if (pos != std::string::npos) {
                    pos += 6;
                    size_t endPos = line.find("\"", pos);
                    if (endPos != std::string::npos) {
                        currentDefect.className = QString::fromStdString(line.substr(pos, endPos - pos));
                    }
                }
                inAIDefect = true;
            }
            else if (inAIDefect && line.find("</Defect>") != std::string::npos) {
                m_aiThresholds[aiCameraId].push_back(currentDefect);
                inAIDefect = false;
            }
            else if (inAIDefect) {
                // 解析缺陷阈值字段
                auto extractInt = [](const std::string& l, const std::string& tag) -> int {
                    size_t s = l.find(tag);
                    if (s != std::string::npos) {
                        s = l.find(">", s) + 1;
                        size_t e = l.find("<", s);
                        if (s != std::string::npos && e != std::string::npos) {
                            try { return std::stoi(l.substr(s, e - s)); } catch (...) {}
                        }
                    }
                    return 0;
                };
                if (line.find("<MinWidth>") != std::string::npos) currentDefect.minWidth = extractInt(line, "<MinWidth>");
                else if (line.find("<MaxWidth>") != std::string::npos) currentDefect.maxWidth = extractInt(line, "<MaxWidth>");
                else if (line.find("<MinHeight>") != std::string::npos) currentDefect.minHeight = extractInt(line, "<MinHeight>");
                else if (line.find("<MaxHeight>") != std::string::npos) currentDefect.maxHeight = extractInt(line, "<MaxHeight>");
            }
            // 常规配置解析
            else if (line.find("<Camera") != std::string::npos) {
                // 提取id属性
                size_t pos = line.find("id=\"");
                if (pos != std::string::npos) {
                    pos += 4;
                    size_t endPos = line.find("\"", pos);
                    if (endPos != std::string::npos) {
                        currentCameraId = std::stoi(line.substr(pos, endPos - pos));
                    }
                }
                inCamera = true;
            }
            else if (line.find("</Camera>") != std::string::npos) {
                inCamera = false;
                currentCameraId = 0;
            }
            else if (line.find("<UserProfile") != std::string::npos) {
                if (currentCameraId > 0 && currentCameraId <= getCameraCount()) {
                    // 创建新的UserProfile
                    std::string profileName = "未命名";
                    bool isDefault = false;

                    // 提取name属性
                    size_t pos = line.find("name=\"");
                    if (pos != std::string::npos) {
                        pos += 6;
                        size_t endPos = line.find("\"", pos);
                        if (endPos != std::string::npos) {
                            profileName = line.substr(pos, endPos - pos);
                        }
                    }

                    // 提取isDefault属性
                    pos = line.find("isDefault=\"");
                    if (pos != std::string::npos) {
                        pos += 11;
                        size_t endPos = line.find("\"", pos);
                        if (endPos != std::string::npos) {
                            std::string isDefaultStr = line.substr(pos, endPos - pos);
                            isDefault = (isDefaultStr == "true");
                        }
                    }

                    m_cameraProfiles[currentCameraId].push_back(UserProfile(profileName));
                    m_cameraProfiles[currentCameraId].back().isDefault = isDefault;
                    currentProfile = &m_cameraProfiles[currentCameraId].back();
                }
                inUserProfile = true;
            }
            else if (line.find("</UserProfile>") != std::string::npos) {
                inUserProfile = false;
                currentProfile = nullptr;
            }
            else if (line.find("<ROI") != std::string::npos) {
                // 检查ROI是否启用
                size_t pos = line.find("enabled=\"");
                if (pos != std::string::npos && currentProfile) {
                    pos += 9;
                    size_t endPos = line.find("\"", pos);
                    if (endPos != std::string::npos) {
                        std::string enabledStr = line.substr(pos, endPos - pos);
                        currentProfile->roi.enabled = (enabledStr == "true");
                    }
                }

                // 检查是否是自闭合标签
                if (line.find("/>") != std::string::npos) {
                    // ROI未启用，没有坐标信息
                    inROI = false;
                } else {
                    inROI = true;
                }
            }
            else if (line.find("</ROI>") != std::string::npos) {
                inROI = false;
            }
            // AlgorithmType 已删除 - 算法类型由 config.xml 控制
            else if (line.find("<ModelName>") != std::string::npos && currentProfile) {
                size_t start = line.find(">") + 1;
                size_t end = line.find("<", start);
                if (start != std::string::npos && end != std::string::npos) {
                    currentProfile->modelName = line.substr(start, end - start);
                }
            }
            else if (line.find("<ProfileImage>") != std::string::npos && currentProfile) {
                size_t start = line.find(">") + 1;
                size_t end = line.find("<", start);
                if (start != std::string::npos && end != std::string::npos) {
                    currentProfile->profileImagePath = line.substr(start, end - start);
                }
            }
            // 解析阈值参数（传统算法）
            else if (line.find("<ThresholdMin>") != std::string::npos && currentProfile) {
                size_t start = line.find(">") + 1;
                size_t end = line.find("<", start);
                if (start != std::string::npos && end != std::string::npos) {
                    currentProfile->thresholdMin = std::stod(line.substr(start, end - start));
                }
            }
            else if (line.find("<ThresholdMax>") != std::string::npos && currentProfile) {
                size_t start = line.find(">") + 1;
                size_t end = line.find("<", start);
                if (start != std::string::npos && end != std::string::npos) {
                    currentProfile->thresholdMax = std::stod(line.substr(start, end - start));
                }
            }
            else if (inROI && currentProfile) {
                // 解析ROI坐标
                if (line.find("<Row1>") != std::string::npos) {
                    size_t start = line.find(">") + 1;
                    size_t end = line.find("<", start);
                    if (start != std::string::npos && end != std::string::npos) {
                        currentProfile->roi.row1 = std::stod(line.substr(start, end - start));
                    }
                }
                else if (line.find("<Column1>") != std::string::npos) {
                    size_t start = line.find(">") + 1;
                    size_t end = line.find("<", start);
                    if (start != std::string::npos && end != std::string::npos) {
                        currentProfile->roi.column1 = std::stod(line.substr(start, end - start));
                    }
                }
                else if (line.find("<Row2>") != std::string::npos) {
                    size_t start = line.find(">") + 1;
                    size_t end = line.find("<", start);
                    if (start != std::string::npos && end != std::string::npos) {
                        currentProfile->roi.row2 = std::stod(line.substr(start, end - start));
                    }
                }
                else if (line.find("<Column2>") != std::string::npos) {
                    size_t start = line.find(">") + 1;
                    size_t end = line.find("<", start);
                    if (start != std::string::npos && end != std::string::npos) {
                        currentProfile->roi.column2 = std::stod(line.substr(start, end - start));
                    }
                }
            }
        }

        file.close();

        // 验证加载结果
        bool hasValidProfiles = false;
        for (int camId = 1; camId <= getCameraCount(); ++camId) {
            if (!m_cameraProfiles[camId].empty()) {
                hasValidProfiles = true;
                break;
            }
        }

        if (!hasValidProfiles) {
            Logger::instance().warn("No valid profiles found in ROI config, using defaults");
            createDefaultConfig();
        } else {
            Logger::instance().info("ROI config loaded successfully");
        }

    } catch (const std::exception& e) {
        Logger::instance().error("Failed to parse ROI config XML: " + std::string(e.what()) +
                                ", using memory defaults.");
        m_cameraProfiles.clear();
        createDefaultConfig();
    }

    m_loaded = true;
    return true;
}

// 保存配置文件
bool AlgorithmROIManager::saveConfig(const std::string& configFile) {
    // 使用加载时保存的文件路径，确保保存到同一个文件
    std::string targetFile = m_configFile.empty() ? configFile : m_configFile;
    if (targetFile.empty()) {
        targetFile = "config/algorithm_roi_profiles.xml";
    }

    try {
        // 确保目录存在
        size_t lastSlash = targetFile.find_last_of("/\\");
        if (lastSlash != std::string::npos) {
            std::string dirPath = targetFile.substr(0, lastSlash);
            if (!ensureDirectoryExists(dirPath)) {
                Logger::instance().error("Failed to create config directory: " + dirPath);
                return false;
            }
        }

        std::ofstream file(targetFile);
        if (!file.is_open()) {
            Logger::instance().error("Failed to create config file: " + targetFile);
            return false;
        }

        // 写入XML头部
        file << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
        file << "<AlgorithmROIProfiles>\n";

        // 遍历所有相机
        for (int camId = 1; camId <= getCameraCount(); ++camId) {
            file << "    <Camera id=\"" << camId << "\">\n";

            // 遍历该相机的所有用户集
            auto it = m_cameraProfiles.find(camId);
            if (it != m_cameraProfiles.end()) {
                for (const auto& profile : it->second) {
                    file << "        <UserProfile name=\"" << profile.name
                         << "\" isDefault=\"" << (profile.isDefault ? "true" : "false") << "\">\n";
                    // AlgorithmType 已删除 - 算法类型由 config.xml 控制

                    // 写入ROI配置
                    file << "            <ROI enabled=\"" << (profile.roi.enabled ? "true" : "false") << "\">\n";
                    if (profile.roi.enabled) {
                        file << "                <Row1>" << profile.roi.row1 << "</Row1>\n";
                        file << "                <Column1>" << profile.roi.column1 << "</Column1>\n";
                        file << "                <Row2>" << profile.roi.row2 << "</Row2>\n";
                        file << "                <Column2>" << profile.roi.column2 << "</Column2>\n";
                    }
                    file << "            </ROI>\n";

                    // 写入阈值参数（传统算法）
                    file << "            <ThresholdMin>" << profile.thresholdMin << "</ThresholdMin>\n";
                    file << "            <ThresholdMax>" << profile.thresholdMax << "</ThresholdMax>\n";

                    // 写入模型名称（AI算法）
                    if (!profile.modelName.empty()) {
                        file << "            <ModelName>" << profile.modelName << "</ModelName>\n";
                    }

                    // 写入示例图片路径
                    file << "            <ProfileImage>" << profile.profileImagePath << "</ProfileImage>\n";
                    file << "        </UserProfile>\n";
                }
            }

            file << "    </Camera>\n";
        }

        // 写入AI缺陷尺寸阈值配置
        if (!m_aiThresholds.empty()) {
            file << "    <AIThresholds>\n";
            for (const auto& [camId, defects] : m_aiThresholds) {
                if (defects.empty()) continue;
                file << "        <Camera id=\"" << camId << "\">\n";
                for (const auto& d : defects) {
                    file << "            <Defect classId=\"" << d.classId
                         << "\" name=\"" << d.className.toStdString() << "\">\n";
                    file << "                <MinWidth>" << d.minWidth << "</MinWidth>\n";
                    file << "                <MaxWidth>" << d.maxWidth << "</MaxWidth>\n";
                    file << "                <MinHeight>" << d.minHeight << "</MinHeight>\n";
                    file << "                <MaxHeight>" << d.maxHeight << "</MaxHeight>\n";
                    file << "            </Defect>\n";
                }
                file << "        </Camera>\n";
            }
            file << "    </AIThresholds>\n";
        }

        file << "</AlgorithmROIProfiles>\n";
        file.close();

        Logger::instance().info("Algorithm ROI config saved successfully to: " + targetFile);
        return true;

    } catch (const std::exception& e) {
        Logger::instance().error("Error saving config file: " + std::string(e.what()));
        return false;
    }
}

// 获取指定相机的所有用户集名称
std::vector<std::string> AlgorithmROIManager::getUserProfiles(int cameraId) const {
    std::vector<std::string> profiles;
    auto it = m_cameraProfiles.find(cameraId);
    if (it != m_cameraProfiles.end()) {
        for (const auto& profile : it->second) {
            profiles.push_back(profile.name);
        }
    }
    return profiles;
}

// 添加新用户集
bool AlgorithmROIManager::addUserProfile(int cameraId, const std::string& profileName) {
    // 检查用户集名称是否有效
    if (!isValidProfileName(profileName)) {
        Logger::instance().error("Invalid profile name: " + profileName);
        return false;
    }

    // 检查用户集是否已存在
    auto it = m_cameraProfiles.find(cameraId);
    if (it != m_cameraProfiles.end()) {
        for (const auto& profile : it->second) {
            if (profile.name == profileName) {
                Logger::instance().warn("Profile already exists: " + profileName);
                return false;
            }
        }

        // 检查数量限制
        if (it->second.size() >= getMaxProfilesPerCamera()) {
            Logger::instance().warn("Maximum profiles reached for camera " + std::to_string(cameraId));
            return false;
        }
    }

    // 创建新用户集（算法类型由 config.xml 控制）
    UserProfile newProfile(profileName);
    // 注意：用户集不再绑定图片，图片功能移到相机级别

    // 如果是第一个用户集，自动设为默认
    if (m_cameraProfiles[cameraId].empty()) {
        newProfile.isDefault = true;
    }

    m_cameraProfiles[cameraId].push_back(newProfile);
    Logger::instance().info("Added profile: " + profileName + " for camera " + std::to_string(cameraId));
    return true;
}

// 重命名用户集
bool AlgorithmROIManager::renameUserProfile(int cameraId, const std::string& oldName,
                                           const std::string& newName) {
    // 检查新名称是否有效
    if (!isValidProfileName(newName)) {
        Logger::instance().error("Invalid profile name: " + newName);
        return false;
    }

    auto it = m_cameraProfiles.find(cameraId);
    if (it == m_cameraProfiles.end()) {
        Logger::instance().warn("No profiles found for camera " + std::to_string(cameraId));
        return false;
    }

    // 查找并重命名用户集
    for (auto& profile : it->second) {
        if (profile.name == oldName) {
            // 用户集不再绑定图片，只更新名称
            profile.name = newName;
            Logger::instance().info("Renamed profile from " + oldName + " to " + newName);
            return true;
        }
    }

    Logger::instance().warn("Profile not found: " + oldName);
    return false;
}

// 删除用户集
bool AlgorithmROIManager::deleteUserProfile(int cameraId, const std::string& profileName) {
    auto it = m_cameraProfiles.find(cameraId);
    if (it == m_cameraProfiles.end()) {
        Logger::instance().warn("No profiles found for camera " + std::to_string(cameraId));
        return false;
    }

    // 检查是否为最后一个用户集
    if (it->second.size() <= 1) {
        Logger::instance().warn("Cannot delete the last profile");
        return false;
    }

    // 查找并删除用户集
    bool wasDefault = false;
    for (auto profileIt = it->second.begin(); profileIt != it->second.end(); ++profileIt) {
        if (profileIt->name == profileName) {
            wasDefault = profileIt->isDefault;

            // 用户集不再绑定图片，直接删除
            it->second.erase(profileIt);

            // 如果删除的是默认用户集，设置第一个为默认
            if (wasDefault && !it->second.empty()) {
                it->second[0].isDefault = true;
            }

            Logger::instance().info("Deleted profile: " + profileName);
            return true;
        }
    }

    Logger::instance().warn("Profile not found: " + profileName);
    return false;
}

// 设置默认用户集
bool AlgorithmROIManager::setDefaultProfile(int cameraId, const std::string& profileName) {
    auto it = m_cameraProfiles.find(cameraId);
    if (it == m_cameraProfiles.end()) {
        Logger::instance().warn("No profiles found for camera " + std::to_string(cameraId));
        return false;
    }

    // 取消所有默认标记
    for (auto& profile : it->second) {
        profile.isDefault = false;
    }

    // 设置新的默认用户集
    for (auto& profile : it->second) {
        if (profile.name == profileName) {
            profile.isDefault = true;
            Logger::instance().info("Set default profile: " + profileName);
            return true;
        }
    }

    Logger::instance().warn("Profile not found: " + profileName);
    return false;
}

// 获取默认用户集名称
std::string AlgorithmROIManager::getDefaultProfile(int cameraId) const {
    auto it = m_cameraProfiles.find(cameraId);
    if (it != m_cameraProfiles.end()) {
        for (const auto& profile : it->second) {
            if (profile.isDefault) {
                return profile.name;
            }
        }
        // 如果没有设置默认，返回第一个
        if (!it->second.empty()) {
            return it->second[0].name;
        }
    }
    return "";
}

// 获取指定相机的指定用户集配置
UserProfile AlgorithmROIManager::getUserProfile(int cameraId, const std::string& profileName) const {
    auto it = m_cameraProfiles.find(cameraId);
    if (it != m_cameraProfiles.end()) {
        for (const auto& profile : it->second) {
            if (profile.name == profileName) {
                return profile;
            }
        }
    }
    // 返回空配置
    return UserProfile();
}

// 更新指定相机的指定用户集配置
bool AlgorithmROIManager::updateUserProfile(int cameraId, const std::string& profileName,
                                            const UserProfile& profile) {
    auto it = m_cameraProfiles.find(cameraId);
    if (it == m_cameraProfiles.end()) {
        Logger::instance().warn("No profiles found for camera " + std::to_string(cameraId));
        return false;
    }

    for (auto& p : it->second) {
        if (p.name == profileName) {
            // 保存原有的 isDefault 标志，不被覆盖
            bool wasDefault = p.isDefault;

            p = profile;
            p.name = profileName;  // 确保名称不变
            p.isDefault = wasDefault;  // 恢复 isDefault 标志

            Logger::instance().info("Updated profile: " + profileName);
            return true;
        }
    }

    Logger::instance().warn("Profile not found: " + profileName);
    return false;
}

// 保存示例图片
bool AlgorithmROIManager::saveProfileImage(int cameraId, const std::string& profileName,
                                           const cv::Mat& image) {
    if (image.empty()) {
        Logger::instance().error("Cannot save empty image");
        return false;
    }

    // 使用相机级别的图片路径（忽略profileName参数）
    std::string imagePath = "config/reference_images/camera" + std::to_string(cameraId) + ".jpg";

    // 确保目录存在
    size_t lastSlash = imagePath.find_last_of("/\\");
    if (lastSlash != std::string::npos) {
        std::string dirPath = imagePath.substr(0, lastSlash);
        if (!ensureDirectoryExists(dirPath)) {
            Logger::instance().error("Failed to create directory: " + dirPath);
            return false;
        }
    }

    // 保存图片
    try {
        if (cv::imwrite(imagePath, image)) {
            Logger::instance().info("Saved camera reference image: " + imagePath);
            return true;
        } else {
            Logger::instance().error("Failed to save image: " + imagePath);
            return false;
        }
    } catch (const std::exception& e) {
        Logger::instance().error("Error saving image: " + std::string(e.what()));
        return false;
    }
}

// 加载示例图片
cv::Mat AlgorithmROIManager::loadProfileImage(int cameraId, const std::string& profileName) const {
    // 使用相机级别的图片路径，支持多种格式
    std::string basePath = "config/reference_images/camera" + std::to_string(cameraId);
    std::vector<std::string> extensions = { ".tiff", ".tif", ".jpg", ".jpeg", ".bmp", ".png" };

    std::string imagePath;
    for (const auto& ext : extensions) {
        std::string testPath = basePath + ext;
        if (fs::exists(testPath)) {
            imagePath = testPath;
            break;
        }
    }

    if (imagePath.empty()) {
        // 图片不存在是正常情况，返回空Mat而不是警告
        Logger::instance().info("Camera reference image not found for camera " + std::to_string(cameraId));
        return cv::Mat();
    }

    try {
        // 使用 IMREAD_UNCHANGED 保持原始格式（对TIFF等格式很重要）
        cv::Mat image = cv::imread(imagePath, cv::IMREAD_UNCHANGED);
        if (image.empty()) {
            Logger::instance().error("Failed to load image: " + imagePath);
            return cv::Mat();
        }

        Logger::instance().debug("Loaded reference image: " + imagePath +
            ", size=" + std::to_string(image.cols) + "x" + std::to_string(image.rows) +
            ", channels=" + std::to_string(image.channels()) +
            ", type=" + std::to_string(image.type()));

        // 如果是多通道图像但不是BGR，转换
        if (image.channels() == 4) {
            cv::Mat bgr;
            cv::cvtColor(image, bgr, cv::COLOR_BGRA2BGR);
            return bgr;
        }
        else if (image.channels() == 1 && image.depth() != CV_8U) {
            // 16位灰度图转8位
            cv::Mat normalized;
            image.convertTo(normalized, CV_8U, 255.0 / 65535.0);
            return normalized;
        }

        return image;

    }
    catch (const std::exception& e) {
        Logger::instance().error("Error loading image: " + std::string(e.what()));
        return cv::Mat();
    }
}

// 检查相机参考图片是否存在（忽略profileName，使用相机级别路径）
bool AlgorithmROIManager::hasProfileImage(int cameraId, const std::string& profileName) const {
    // 使用相机级别的图片路径（忽略profileName参数）
    std::string imagePath = "config/reference_images/camera" + std::to_string(cameraId) + ".jpg";
    return fs::exists(imagePath);
}

// 检查用户集名称是否有效
bool AlgorithmROIManager::isValidProfileName(const std::string& name) {
    if (name.empty() || name.length() > 50) {
        return false;
    }

    // 检查是否包含非法字符
    for (char c : name) {
        if (c == '<' || c == '>' || c == '"' || c == '/' || c == '\\' ||
            c == '|' || c == '?' || c == '*' || c == ':') {
            return false;
        }
    }

    return true;
}

// 获取相机参考图片路径（忽略profileName参数）
std::string AlgorithmROIManager::getProfileImagePath(int cameraId, const std::string& profileName) const {
    // 返回相机级别的图片路径（忽略profileName参数）
    return "config/reference_images/camera" + std::to_string(cameraId) + ".jpg";
}

// 确保目录存在
bool AlgorithmROIManager::ensureDirectoryExists(const std::string& dirPath) const {
    try {
        if (!fs::exists(dirPath)) {
            fs::create_directories(dirPath);
            Logger::instance().info("Created directory: " + dirPath);
        }
        return true;
    } catch (const std::exception& e) {
        Logger::instance().error("Failed to create directory: " + std::string(e.what()));
        return false;
    }
}

// 创建默认配置
void AlgorithmROIManager::createDefaultConfig() {
    Logger::instance().info("Creating default Algorithm ROI config");

    // 为每个相机创建默认用户集
    for (int camId = 1; camId <= getCameraCount(); ++camId) {
        UserProfile defaultProfile("默认配置");
        defaultProfile.isDefault = true;
        defaultProfile.profileImagePath = getProfileImagePath(camId, "默认配置");

        // 相机1和相机2使用传统算法，设置默认ROI
        if (camId == 1 || camId == 2) {
            defaultProfile.roi = AlgorithmROIConfig(801.302, 0, 912.413, 2048);
        } else {
            // 相机3使用AI算法
            defaultProfile.modelName = "yolo11n.pt";
            defaultProfile.roi.enabled = false;
        }

        m_cameraProfiles[camId].push_back(defaultProfile);
    }
}

// 格式化配置（从模板复制）
bool AlgorithmROIManager::resetToDefault() {
    const std::string templateFile = "config_template/algorithm_roi_profiles.xml";
    const std::string targetFile = "config/algorithm_roi_profiles.xml";

    Logger::instance().info("Resetting ROI config from template: " + templateFile);

    // 检查模板文件是否存在
    if (!QFile::exists(QString::fromStdString(templateFile))) {
        Logger::instance().error("Template config file not found: " + templateFile);
        return false;
    }

    // 确保config目录存在
    if (!ensureDirectoryExists("config")) {
        Logger::instance().error("Failed to create config directory");
        return false;
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
        Logger::instance().error("Failed to copy template ROI config to: " + targetFile);
        return false;
    }

    Logger::instance().info("ROI config reset successfully from template");

    // 重新加载配置
    m_cameraProfiles.clear();
    m_loaded = false;  // 重置加载标志，允许重新加载

    return loadConfig(targetFile);
}

// 检查配置文件状态
AlgorithmROIManager::ConfigStatus AlgorithmROIManager::checkConfigStatus(const std::string& configFile) {
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

        bool hasValidTag = (content.find("<?xml") != std::string::npos);
        bool hasROIProfiles = (content.find("<AlgorithmROIProfiles>") != std::string::npos);
        bool hasROIProfilesEnd = (content.find("</AlgorithmROIProfiles>") != std::string::npos);

        if (!hasValidTag || !hasROIProfiles || !hasROIProfilesEnd) {
            return ConfigStatus::Corrupted;
        }

        // 简单的标签匹配检查
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

        // 标签数量应该匹配
        if (openTags < 1 || closeTags < 1 || std::abs(openTags - closeTags) > 5) {
            return ConfigStatus::Corrupted;
        }

    } catch (...) {
        return ConfigStatus::Corrupted;
    }

    return ConfigStatus::OK;
}

// ========== AI缺陷阈值配置 ==========

bool AlgorithmROIManager::loadAIThresholds(std::map<int, std::vector<AIDefectThreshold>>& cameraThresholds)
{
    // 确保配置已加载
    if (!m_loaded) {
        loadConfig();
    }
    cameraThresholds = m_aiThresholds;
    return true;
}

bool AlgorithmROIManager::saveAIThresholds(const std::map<int, std::vector<AIDefectThreshold>>& cameraThresholds)
{
    // 更新内存中的阈值
    m_aiThresholds = cameraThresholds;

    // 重新保存整个配置文件（包含传统参数和AI阈值）
    return saveConfig();
}
