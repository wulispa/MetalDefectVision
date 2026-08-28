#pragma once

#include <opencv2/opencv.hpp>
#include <memory>
#include <string>
#include <vector>

// 算法结果结构体
struct AlgorithmResult {
    bool hasDefect;             // 是否检测到缺陷（true=NG, false=OK）
    int defectCount;            // 缺陷数量
    bool success;               // 是否成功
    std::string message;        // 结果消息
    cv::Mat visualization;      // 可视化图像（可选）
    double processingTime;      // 处理时间（毫秒）

    // 传统算法横向计算接口（预留）
    std::vector<std::vector<cv::Point>> defectCoordinates;  // 缺陷坐标点数组（二维）
    std::vector<double> defectLengths;                     // 缺陷长度数组（一维）

    // 传统算法绘制数据
    std::vector<int> abnormalLineCols;      // 异常线段横坐标数组（ColsArray）
    double roiRow1;                          // ROI 起始行（用于绘制线段高度）
    double roiRow2;                          // ROI 结束行
    std::vector<double> distanceTexts;       // 距离文本数组（Texts）
    std::vector<double> textRows;            // 文本行坐标（RowsText）
    std::vector<double> textCols;            // 文本列坐标（ColsText）

    // 相机2传统算法检测框数据
    double bboxRow1;                         // 检测框左上角行坐标
    double bboxCol1;                         // 检测框左上角列坐标
    double bboxRow2;                         // 检测框右下角行坐标
    double bboxCol2;                         // 检测框右下角列坐标
    bool hasBbox;                            // 是否有有效的检测框

    AlgorithmResult()
        : hasDefect(false), defectCount(0), success(false), processingTime(0.0),
          roiRow1(0), roiRow2(0), bboxRow1(0), bboxCol1(0), bboxRow2(0), bboxCol2(0), hasBbox(false) {}
};

// 算法配置基类
struct AlgorithmConfig {
    virtual ~AlgorithmConfig() = default;
};

// 传统算法配置结构
struct TraditionalAlgorithmConfig : public AlgorithmConfig {
    double roiRow1;
    double roiColumn1;
    double roiRow2;
    double roiColumn2;

    TraditionalAlgorithmConfig()
        : roiRow1(801.302), roiColumn1(0), roiRow2(912.413), roiColumn2(2048) {}

    TraditionalAlgorithmConfig(double r1, double c1, double r2, double c2)
        : roiRow1(r1), roiColumn1(c1), roiRow2(r2), roiColumn2(c2) {}
};

// AI算法配置结构（预留）
struct AIAlgorithmConfig : public AlgorithmConfig {
    std::string modelName;
    double confidenceThreshold;
    double nmsThreshold;

    AIAlgorithmConfig()
        : modelName("yolo11n.pt"), confidenceThreshold(0.5), nmsThreshold(0.45) {}

    AIAlgorithmConfig(const std::string& model, double confThresh, double nmsThresh)
        : modelName(model), confidenceThreshold(confThresh), nmsThreshold(nmsThresh) {}
};

// 算法类型枚举
enum class AlgorithmType {
    HALCON_TRADITIONAL,   // Halcon 传统算法
    YOLO_DETECTION,       // YOLO 检测算法
    UNKNOWN
};

// 算法接口基类
class IAlgorithm {
public:
    virtual ~IAlgorithm() = default;

    // 初始化算法
    virtual bool initialize(const AlgorithmConfig* config) = 0;

    // 处理单张图像
    virtual AlgorithmResult process(const cv::Mat& image) = 0;

    // 获取算法类型
    virtual AlgorithmType getType() const = 0;

    // 获取算法名称
    virtual std::string getName() const = 0;

    // 释放资源
    virtual void release() = 0;

    // 是否已初始化
    virtual bool isInitialized() const = 0;

    // 动态更新ROI参数（传统算法使用）
    virtual void updateROI(double row1, double column1, double row2, double column2) {
        // 默认空实现，子类可重写
        (void)row1; (void)column1; (void)row2; (void)column2;
    }
};
