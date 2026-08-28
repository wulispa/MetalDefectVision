#pragma once

#include <string>
#include <vector>
#include <opencv2/opencv.hpp>

// 相机配置结构
struct CameraConfig {
    int id;
    std::string serialNumber;
    bool useTraditionalAlgo = false;  // true = 启用传统算法
    bool useAIAlgo = false;           // true = 启用AI算法
    int triggerMode;
    bool enabled;
    std::string originalFormat = "bmp";  // 相机原始格式：bmp, tiff 等

    // 辅助方法：获取显示用的算法类型字符串
    std::string getAlgorithmTypeDisplay() const {
        if (useTraditionalAlgo && useAIAlgo) return "Traditional+AI";
        if (useTraditionalAlgo) return "Traditional";
        if (useAIAlgo) return "AI";
        return "Unknown";
    }

    // 辅助方法：是否启用用户集（只要启用传统算法就启用用户集）
    bool isUserSetEnabled() const {
        return useTraditionalAlgo;
    }
};

// 图片保存配置
struct ImageSaveConfig {
    std::string savePath;
    bool enableDateFolder;
    bool saveOriginalImage = false;    // 是否保存原图（未标注的原始图）
    bool saveDefectImage = true;       // 是否保存缺陷图（标注后的图）
    bool compressDefectImage = true;   // 缺陷图是否压缩成jpg（false则用原图格式）
};

// AI检测框尺寸显示配置
struct BBoxSizeDisplayConfig {
    double pixelsPerMm;      // 每毫米对应的像素数（3mm = 350像素 → 350/3 ≈ 116.67）
    double fontScale;        // 字体大小
    int fontThickness;       // 字体粗细
    cv::Scalar textColor;    // 文字颜色（红色）

    BBoxSizeDisplayConfig()
        : pixelsPerMm(350.0 / 3.0)  // 3mm = 350像素
        , fontScale(1)
        , fontThickness(2)
        , textColor(0, 0, 255)      // BGR格式红色
    {}
};

// 测试模式配置
struct TestModeConfig {
    bool enabled = false;                    // 是否启用测试模式
    std::string imageBasePath = "test_img"; // 测试图片基础路径
    bool loopImages = true;                  // 是否循环读取图片
    int imageDelayMs = 100;                  // 图片读取间隔（毫秒）
};

// 系统日志配置
struct SystemLogConfig {
    bool enabled = true;        // 是否启用系统日志，默认启用
    bool debugEnabled = false;  // 是否启用DEBUG级别日志，默认关闭
};

// PLC配置
struct PLCConfig {
    bool enabled = true;            // 是否启用PLC通信
    std::string ip = "192.168.1.10"; // PLC IP地址
    int port = 502;                 // Modbus TCP端口
    int slaveId = 4;                // 从站ID
};

// 自启动配置
struct AutoStartConfig {
    bool enabled = false;           // 是否开机自启动
};

// 配置管理器（单例模式）
class ConfigManager {
public:
    static ConfigManager& instance();

    // 禁止拷贝和赋值
    ConfigManager(const ConfigManager&) = delete;
    ConfigManager& operator=(const ConfigManager&) = delete;

    // 加载配置文件
    bool loadConfig(const std::string& configFile = "config/config.xml");

    // 获取相机配置
    std::vector<CameraConfig> getCameraConfigs() const;
    // 获取图片保存配置
    ImageSaveConfig getImageSaveConfig() const;

    // 获取测试模式配置
    TestModeConfig getTestModeConfig() const;

    // 获取系统日志配置
    SystemLogConfig getSystemLogConfig() const;

    // 获取PLC配置
    PLCConfig getPLCConfig() const;

    // 获取自启动配置
    AutoStartConfig getAutoStartConfig() const;

    // 获取指定相机是否启用用户集
    bool isCameraUserSetEnabled(int cameraId) const;

    // 获取指定相机的配置
    const CameraConfig& getCameraConfig(int cameraId) const;

    // 格式化配置（从模板复制）
    bool resetToDefault();

    // 检查配置文件状态
    enum class ConfigStatus { OK, Missing, Corrupted };
    static ConfigStatus checkConfigStatus(const std::string& configFile);

private:
    ConfigManager() = default;

    std::vector<CameraConfig> m_cameraConfigs;
    ImageSaveConfig m_imageSaveConfig;
    TestModeConfig m_testModeConfig;
    SystemLogConfig m_systemLogConfig;
    PLCConfig m_plcConfig;
    AutoStartConfig m_autoStartConfig;
};
