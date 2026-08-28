#pragma once

#include "IAlgorithm.h"
#include "HalconCpp.h"
#include <memory>
#include <mutex>

using namespace HalconCpp;

// Halcon 传统算法实现
class HalconAlgorithm : public IAlgorithm {
public:
    // ROI配置结构体（公开，供外部获取）
    struct ROIConfig {
        double row1;
        double column1;
        double row2;
        double column2;
        bool enabled;

        ROIConfig() : row1(801.302), column1(0), row2(912.413), column2(2048), enabled(true) {}
    };

    HalconAlgorithm();
    ~HalconAlgorithm() override;

    // 禁止拷贝
    HalconAlgorithm(const HalconAlgorithm&) = delete;
    HalconAlgorithm& operator=(const HalconAlgorithm&) = delete;

    // 实现 IAlgorithm 接口
    bool initialize(const AlgorithmConfig* config) override;
    AlgorithmResult process(const cv::Mat& image) override;
    AlgorithmResult process_cam2(const cv::Mat& image);  // 相机2传统算法处理
    AlgorithmResult process_opencv(const cv::Mat& image); // OpenCV版传统算法（相机1）
    AlgorithmType getType() const override { return AlgorithmType::HALCON_TRADITIONAL; }
    std::string getName() const override { return "Halcon Traditional Algorithm"; }
    void release() override;
    bool isInitialized() const override { return m_initialized; }

    // 动态更新ROI参数（线程安全）
    void updateROI(double row1, double column1, double row2, double column2) override;

    // 动态更新阈值参数（线程安全）
    void updateThreshold(double thresholdMin, double thresholdMax);

    // 获取当前ROI配置
    ROIConfig getROI() const { return m_roiConfig; }

    // 获取当前阈值配置
    std::pair<double, double> getThreshold() const { return { m_thresholdMin, m_thresholdMax }; }


private:

    // 辅助方法
    HObject cvMatToHObject(const cv::Mat& cv_img);

private:
    bool m_initialized;
    mutable std::mutex m_roiMutex;  // 保护ROI参数的互斥锁
    ROIConfig m_roiConfig;  // ROI配置实例

    // 阈值参数（用于缺陷判断区间）
    double m_thresholdMin = 0.0;
    double m_thresholdMax = 100.0;
};
