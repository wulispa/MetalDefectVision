#pragma once

#include <opencv2/opencv.hpp>
#include <chrono>
#include <QString>
#include <QStringList>
#include <QDateTime>
#include "IAlgorithm.h"

/**
 * @brief 图像任务（采图 -> 算法队列）
 */
struct ImageTask {
    int cameraId = 0;
    cv::Mat image;
    unsigned int frameNum = 0;
    std::chrono::high_resolution_clock::time_point captureTime;      // GetImage完成后的时间
    std::chrono::high_resolution_clock::time_point captureStartTime; // GetImage开始前的时间

    ImageTask() = default;

    ImageTask(int camId, const cv::Mat& img, unsigned int frame)
        : cameraId(camId), image(img.clone()), frameNum(frame)
        , captureTime(std::chrono::high_resolution_clock::now())
    {}
};

/**
 * @brief 算法结果任务（算法 -> UI队列 / IO队列）
 */
struct AlgorithmResultTask {
    // 基本信息
    int cameraId = 0;
    cv::Mat image;                  // 待绘制图像
    cv::Mat originalImage;          // 原始图像备份
    unsigned int frameNum = 0;

    // 算法结果
    AlgorithmResult tradResult;     // 传统算法结果
    AlgorithmResult aiResult;       // AI算法结果
    bool tradProcessed = false;     // 是否执行了传统算法
    bool aiProcessed = false;       // 是否执行了AI算法

    // 综合结果
    bool hasDefect = false;
    int defectCount = 0;
    QStringList defectTypes;
    QString algorithmTypeDisplay;
    double processingTime = 0.0;

    // 时间戳（用于性能统计）
    std::chrono::high_resolution_clock::time_point captureStartTime; // GetImage开始前
    std::chrono::high_resolution_clock::time_point captureTime;      // GetImage完成后
    std::chrono::high_resolution_clock::time_point algorithmEndTime;

    // AI检测结果（用于绘制）
    std::vector<int> detectionClassIds;
    std::vector<cv::Rect> detectionBoxes;

    // Pin缺陷标志（Camera2专用）
    bool hasPinDefect = false;

    AlgorithmResultTask() = default;

    /**
     * @brief 计算从采图开始(GetImage)到IO发送完成的总时间（毫秒）
     */
    double getTotalTimeMs() const {
        auto start = captureStartTime.time_since_epoch().count() > 0 ? captureStartTime : captureTime;
        if (start.time_since_epoch().count() == 0 ||
            algorithmEndTime.time_since_epoch().count() == 0) {
            return 0.0;
        }
        return std::chrono::duration<double, std::milli>(algorithmEndTime - start).count();
    }

    /**
     * @brief 获取算法类型显示字符串
     */
    static QString getAlgorithmTypeDisplay(bool tradProcessed, bool aiProcessed) {
        if (tradProcessed && aiProcessed) return "Traditional+AI";
        if (tradProcessed) return "Traditional";
        if (aiProcessed) return "AI";
        return "None";
    }
};

/**
 * @brief 分发任务（算法线程 -> 分发线程）
 * 使用移动语义避免clone
 */
struct DispatchTask {
    int cameraId = 0;
    unsigned int frameNum = 0;

    // 图像数据（使用移动语义传递，避免clone）
    cv::Mat image;              // 算法处理后的图像
    cv::Mat originalImage;      // 原始图像

    // 算法结果
    bool hasDefect = false;
    int defectCount = 0;
    QStringList defectTypes;
    QString algorithmTypeDisplay;
    double processingTime = 0.0;

    // 绘制数据
    AlgorithmResult tradResult;
    bool tradProcessed = false;
    bool aiProcessed = false;
    std::vector<int> detectionClassIds;
    std::vector<cv::Rect> detectionBoxes;

    // 时间戳
    std::chrono::high_resolution_clock::time_point captureTime;

    // Camera2专用
    bool hasPinDefect = false;

    DispatchTask() = default;
};

/**
 * @brief UI显示任务（算法 -> UI队列）
 * 队列满时丢弃旧帧，保持最新
 */
struct UIDisplayTask {
    int cameraId = 0;
    cv::Mat annotatedImage;
    cv::Mat originalImage;       // 原始分辨率图像（用于参数设置对话框ROI绘制）
    bool hasDefect = false;
    bool algorithmFailed = false;
    unsigned int frameNum = 0;
};

/**
 * @brief IO保存任务（算法 -> IO队列）
 * 不能丢数据，必须完整保存
 */
struct IOSaveTask {
    int cameraId = 0;
    cv::Mat annotatedImage;
    cv::Mat originalImage;
    bool hasDefect = false;
    int defectCount = 0;
    QStringList defectTypes;
    QString algorithmTypeDisplay;
    double processingTime = 0.0;
    unsigned int frameNum = 0;
    std::chrono::high_resolution_clock::time_point captureTime;

    // 新增字段：统一时间戳和保存路径
    QDateTime timestamp;           // 任务创建时生成，图片和数据库共用
    QString savedImagePath;        // 保存后的图片路径（用于数据库记录）
    std::string originalFormat = "bmp";  // 原图格式：bmp, tiff 等

    // 料号（来自用户集1的名称，用于按料号分类存储）
    std::string partNumber;
};
