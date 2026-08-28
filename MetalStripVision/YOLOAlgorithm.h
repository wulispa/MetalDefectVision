#pragma once

#include "AIAlgorithm.h"
#include "include/yolo_api/AIMODEL.h"
#include <QString>
#include <QFile>
#include <QDir>
#include <unordered_map>
#include <vector>
#include <opencv2/opencv.hpp>

// 缺陷颜色配置
struct DefectColor {
    int classId;
    cv::Scalar color;      // OpenCV颜色 (B, G, R)
    QString name;          // 颜色名称
};

// YOLO 配置（继承 AIConfig）
struct YOLOConfig : public AIConfig {
    int srcWidth = 4096;                     // 源图片宽度（相机图片宽度，src_w）
    int srcHeight = 3000;                    // 源图片高度（相机图片高度，src_h）
    QString basePath;                       // 模型配置根目录
    QString colorConfigFile;                // 颜色配置文件路径
    float confThreshold = 0.1f;            // 置信度阈值
    float iouThreshold = 0.1f;             // NMS IOU阈值
    int inputWidth = 1280;                   // 模型训练输入宽度（dst_w）
    int inputHeight = 1280;                  // 模型训练输入高度（dst_h）

};

// YOLO 算法实现（基于DLL接口）
class YOLOAlgorithm : public AIAlgorithm {
public:
    YOLOAlgorithm();
    ~YOLOAlgorithm() override;

    // 实现 IAlgorithm 接口
    bool initialize(const AlgorithmConfig* config) override;
    AlgorithmResult process(const cv::Mat& image) override;
    AlgorithmType getType() const override { return AlgorithmType::YOLO_DETECTION; }
    std::string getName() const override { return "YOLO Detection Algorithm (DLL)"; }
    void release() override;
    bool isInitialized() const override { return m_initialized; }

    // 实现 AIAlgorithm 接口
    bool loadModel(const std::string& modelPath, const std::string& configPath = "") override;
    void setInferenceParams(float confThreshold, float nmsThreshold) override;
    std::vector<Detection> getDetections() const override;

    // YOLO 特有方法
    void setClassNames(const std::vector<std::string>& names);

    // 设置源图片尺寸（相机图片尺寸）
    void setSrcImageSize(int width, int height);

    // 根据模型名称加载模型（如 "Down_metal", "Up_metal"）
    bool loadModelByName(const QString& modelName);

    // 获取类别名称
    QString getClassName(int classId) const;

    // 获取所有类别名称
    const std::unordered_map<int, QString>& getAllClassNames() const { return classNames; }

    // 获取当前加载的模型名称
    QString getCurrentModelName() const { return m_currentModelName; }

    // 设置料号（用于构建模型路径：Model/{料号}/Trt/xxx.trt）
    void setPartNumber(const QString& partNumber);
    QString getPartNumber() const { return m_partNumber; }

    // 检查料号目录下的模型是否存在
    bool checkModelExists(const QString& modelName) const;

    // 加载颜色配置文件
    bool loadColorConfig(const QString& configPath);

    // 根据classId获取颜色
    cv::Scalar getClassColor(int classId) const;

protected:
    cv::Mat preprocess(const cv::Mat& image) override;
    std::vector<Detection> postprocess(const void* rawOutput, int imgWidth, int imgHeight) override;

private:
    bool m_initialized;
    YOLOConfig m_yoloConfig;
    AIModelInterFace* model;
    std::unordered_map<int, QString> classNames;  // 类别名称映射
    std::unordered_map<int, cv::Scalar> classColors;  // 类别颜色映射
    std::vector<Detection> m_detections;           // 当前检测结果
    QString m_currentModelName;                    // 当前加载的模型名称
    QString m_partNumber;                          // 当前料号（用于模型路径）
};
