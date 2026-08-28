#pragma once

#include <string>
#include <opencv2/opencv.hpp>

// 算法ROI配置结构
struct AlgorithmROIConfig {
    double row1;
    double column1;
    double row2;
    double column2;
    bool enabled;

    AlgorithmROIConfig()
        : row1(0), column1(0), row2(0), column2(0), enabled(false) {}

    AlgorithmROIConfig(double r1, double c1, double r2, double c2)
        : row1(r1), column1(c1), row2(r2), column2(c2), enabled(true) {}

    // 验证ROI坐标是否有效
    bool isValid() const {
        return enabled && row2 > row1 && column2 > column1;
    }

    // 获取ROI宽度
    double getWidth() const {
        return enabled ? (column2 - column1) : 0.0;
    }

    // 获取ROI高度
    double getHeight() const {
        return enabled ? (row2 - row1) : 0.0;
    }
};

// 用户集配置结构
struct UserProfile {
    std::string name;
    bool isDefault;
    // 注意：algorithmType 已删除 - 算法类型由 config.xml 中的 useTraditionalAlgo/useAIAlgo 控制
    AlgorithmROIConfig roi;
    std::string modelName;  // AI算法模型名称（预留）
    std::string profileImagePath;

    // 传统算法阈值参数（用于缺陷判断区间）
    double thresholdMin;  // 阈值下限
    double thresholdMax;  // 阈值上限

    UserProfile()
        : isDefault(false),
          thresholdMin(0.0), thresholdMax(100.0) {}

    UserProfile(const std::string& profileName)
        : name(profileName), isDefault(false),
          thresholdMin(0.0), thresholdMax(100.0) {}
};

// 注意：AIAlgorithmConfig 已在 IAlgorithm.h 中定义，此处不再重复定义
