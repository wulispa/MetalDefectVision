#pragma once

#include <QObject>
#include <thread>
#include <atomic>
#include <memory>
#include <mutex>
#include <map>
#include "ThreadSafeQueue.h"
#include "ImageTask.h"
#include "YOLOAlgorithm.h"
#include "ConfigManager.h"
#include "AIDefectThreshold.h"

/**
 * @brief 分发线程
 *
 * 从算法结果队列获取任务，执行绘制、缩放操作，
 * 然后分发到UI队列和IO队列。
 *
 * 每个相机独立一个DispatchThread实例。
 *
 * 职责：
 * - 绘制检测结果（从AlgorithmThread移过来）
 * - 图像缩放（减轻UI线程负担）
 * - 分发到UI/IO队列
 */
class DispatchThread : public QObject {
    Q_OBJECT

public:
    /**
     * @brief 构造函数
     * @param cameraId 相机ID (1, 2, 3)
     * @param inputQueue 输入队列（算法结果）
     * @param uiQueue UI显示队列
     * @param ioQueue IO保存队列
     * @param aiAlgo AI算法实例（用于获取类别名称，Camera2/3使用）
     */
    DispatchThread(int cameraId,
                   ThreadSafeQueue<DispatchTask>& inputQueue,
                   ThreadSafeQueue<UIDisplayTask>& uiQueue,
                   ThreadSafeQueue<IOSaveTask>& ioQueue,
                   std::shared_ptr<YOLOAlgorithm> aiAlgo);

    ~DispatchThread();

    /**
     * @brief 启动线程
     */
    void start();

    /**
     * @brief 停止线程
     */
    void stop();

    /**
     * @brief 是否正在运行
     */
    bool isRunning() const { return m_running.load(); }

    /**
     * @brief 设置AI算法实例
     */
    void setAIAlgo(std::shared_ptr<YOLOAlgorithm> aiAlgo) {
        m_aiAlgo = aiAlgo;
    }

    /**
     * @brief 更新AI缺陷尺寸阈值（线程安全）
     */
    void updateAIThresholds(const std::vector<AIDefectThreshold>& thresholds);

    /**
     * @brief 设置当前料号（线程安全）
     * @param partNumber 料号名称
     */
    void setPartNumber(const std::string& partNumber);

signals:
    /**
     * @brief 分发完成信号
     */
    void dispatchCompleted(int cameraId);

private:
    void run();

    // 绘制函数（从AlgorithmThread移过来）
    cv::Mat drawDetectionResults(const DispatchTask& task);
    void drawCamera1Results(cv::Mat& image, const DispatchTask& task);
    void drawCamera2Results(cv::Mat& image, const DispatchTask& task);
    void drawCamera3Results(cv::Mat& image, const DispatchTask& task);

    // 中文文字绘制（使用QPainter）
    void drawChineseText(cv::Mat& image, const QString& text,
                         const cv::Point& position,
                         const cv::Scalar& color, int fontSize = 24);

    // AI阈值筛选
    bool shouldDrawDetection(int classId, const cv::Rect& box) const;

private:
    int m_cameraId;
    ThreadSafeQueue<DispatchTask>& m_inputQueue;
    ThreadSafeQueue<UIDisplayTask>& m_uiQueue;
    ThreadSafeQueue<IOSaveTask>& m_ioQueue;
    std::shared_ptr<YOLOAlgorithm> m_aiAlgo;

    // AI阈值筛选
    std::map<int, AIDefectThreshold> m_thresholdMap;  // classId -> 阈值
    mutable std::mutex m_thresholdMutex;              // 保护m_thresholdMap

    // 尺寸显示配置
    BBoxSizeDisplayConfig m_bboxConfig;

    // 料号（用于图片保存路径分类）
    std::string m_partNumber;
    mutable std::mutex m_partNumberMutex;             // 保护m_partNumber

    std::thread m_thread;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_stop{false};
};
