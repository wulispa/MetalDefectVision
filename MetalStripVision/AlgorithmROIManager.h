#pragma once

#include "AlgorithmROIConfig.h"
#include "AIDefectThreshold.h"
#include <map>
#include <vector>
#include <string>
#include <opencv2/opencv.hpp>

// 算法ROI配置管理器（单例模式）
class AlgorithmROIManager {
public:
    // 获取单例实例
    static AlgorithmROIManager& instance();

    // 禁止拷贝和赋值
    AlgorithmROIManager(const AlgorithmROIManager&) = delete;
    AlgorithmROIManager& operator=(const AlgorithmROIManager&) = delete;

    // ========== 配置文件加载/保存 ==========

    // 加载配置文件
    bool loadConfig(const std::string& configFile = "config/algorithm_roi_profiles.xml");

    // 保存配置文件
    bool saveConfig(const std::string& configFile = "config/algorithm_roi_profiles.xml");

    // 格式化配置（从模板复制）
    bool resetToDefault();

    // 检查配置文件状态
    enum class ConfigStatus { OK, Missing, Corrupted };
    static ConfigStatus checkConfigStatus(const std::string& configFile);

    // ========== 用户集管理（每个相机最多6个）==========

    // 获取指定相机的所有用户集名称
    std::vector<std::string> getUserProfiles(int cameraId) const;

    // 添加新用户集（算法类型由 config.xml 控制）
    bool addUserProfile(int cameraId, const std::string& profileName);

    // 重命名用户集（会同步重命名对应的示例图片）
    bool renameUserProfile(int cameraId, const std::string& oldName, const std::string& newName);

    // 删除用户集（会同步删除对应的示例图片）
    bool deleteUserProfile(int cameraId, const std::string& profileName);

    // 设置默认用户集
    bool setDefaultProfile(int cameraId, const std::string& profileName);

    // 获取默认用户集名称
    std::string getDefaultProfile(int cameraId) const;

    // ========== 配置获取/更新 ==========

    // 获取指定相机的指定用户集配置
    UserProfile getUserProfile(int cameraId, const std::string& profileName) const;

    // 更新指定相机的指定用户集配置
    bool updateUserProfile(int cameraId, const std::string& profileName, const UserProfile& profile);

    // ========== 示例图片管理 ==========

    // 保存示例图片
    bool saveProfileImage(int cameraId, const std::string& profileName, const cv::Mat& image);

    // 加载示例图片
    cv::Mat loadProfileImage(int cameraId, const std::string& profileName) const;

    // 检查示例图片是否存在
    bool hasProfileImage(int cameraId, const std::string& profileName) const;

    // ========== 工具方法 ==========

    // 获取相机数量（固定为3）
    static int getCameraCount() { return 3; }

    // 每个相机最大用户集数量
    static int getMaxProfilesPerCamera() { return 20; }

    // 检查用户集名称是否有效
    static bool isValidProfileName(const std::string& name);

    // ========== AI缺陷阈值配置 ==========

    // 加载AI阈值配置
    bool loadAIThresholds(std::map<int, std::vector<AIDefectThreshold>>& cameraThresholds);

    // 保存AI阈值配置
    bool saveAIThresholds(const std::map<int, std::vector<AIDefectThreshold>>& cameraThresholds);

private:
    AlgorithmROIManager() = default;
    ~AlgorithmROIManager() = default;

    // 配置加载状态
    bool m_loaded = false;

    // 获取示例图片路径
    std::string getProfileImagePath(int cameraId, const std::string& profileName) const;

    // 确保目录存在
    bool ensureDirectoryExists(const std::string& dirPath) const;

    // 创建默认配置（如果配置文件不存在）
    void createDefaultConfig();

    // 数据存储：cameraId -> 用户集列表
    std::map<int, std::vector<UserProfile>> m_cameraProfiles;

    // AI缺陷尺寸阈值：cameraId -> 阈值列表
    std::map<int, std::vector<AIDefectThreshold>> m_aiThresholds;

    // 配置文件路径
    std::string m_configFile;
};
