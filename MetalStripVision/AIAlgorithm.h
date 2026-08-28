#pragma once

#include "IAlgorithm.h"
#include <string>
#include <memory>
#include <vector>

// AI 算法配置
struct AIConfig : public AlgorithmConfig {
    std::string modelPath;           // 模型文件路径
    std::string configPath;          // 配置文件路径
    int inputWidth = 640;            // 输入图像宽度
    int inputHeight = 640;           // 输入图像高度
    float confidenceThreshold = 0.5; // 置信度阈值
    float nmsThreshold = 0.4;        // NMS 阈值
    bool useGPU = true;              // 是否使用 GPU
    int gpuDeviceId = 0;             // GPU 设备 ID
};

// 检测结果
struct Detection {
    cv::Rect bbox;          // 边界框
    float confidence;       // 置信度
    int classId;            // 类别 ID
    std::string className;  // 类别名称
};

// AI 算法结果（扩展）
struct AIAlgorithmResult : public AlgorithmResult {
    std::vector<Detection> detections;  // 检测结果列表

    AIAlgorithmResult() : AlgorithmResult() {}
};

// AI 算法抽象基类
class AIAlgorithm : public IAlgorithm {
public:
    virtual ~AIAlgorithm() = default;

    // 加载模型
    virtual bool loadModel(const std::string& modelPath, const std::string& configPath = "") = 0;

    // 设置推理参数
    virtual void setInferenceParams(float confThreshold, float nmsThreshold) = 0;

    // 获取检测结果
    virtual std::vector<Detection> getDetections() const = 0;

    // 预处理（钩子方法，子类可重写）
    virtual cv::Mat preprocess(const cv::Mat& image) {
        return image;  // 默认不做预处理
    }

    // 后处理（钩子方法，子类可重写）
    virtual std::vector<Detection> postprocess(const void* rawOutput, int imgWidth, int imgHeight) {
        return std::vector<Detection>();  // 默认返回空
    }

protected:
    AIConfig m_config;
    std::vector<Detection> m_detections;
    bool m_modelLoaded = false;
};
