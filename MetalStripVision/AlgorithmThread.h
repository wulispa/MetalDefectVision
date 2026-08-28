#pragma once

#include <QObject>
#include <thread>
#include <atomic>
#include <memory>
#include "ThreadSafeQueue.h"
#include "ImageTask.h"
#include "IAlgorithm.h"
#include "AIAlgorithm.h"
#include "YOLOAlgorithm.h"
#include "PLC_Interface.h"
#include "AlgorithmROIManager.h"
#include "ConfigManager.h"
#include "AIDefectThreshold.h"

class CameraHK1;  // 前向声明

/**
 * @brief 算法线程
 *
 * 从图像队列获取任务，执行算法处理，立即发送PLC信号，
 * 然后将结果推送到分发队列。
 *
 * 关键路径：采图 -> 算法 -> PLC（必须在200ms内完成）
 * 异步路径：算法 -> 分发队列 -> DispatchThread
 *
 * 三个相机的处理逻辑不同：
 * - Camera1: 传统算法
 * - Camera2: 传统算法 + AI算法
 * - Camera3: AI算法
 */
class AlgorithmThread : public QObject {
    Q_OBJECT

public:
    /**
     * @brief 构造函数
     * @param cameraId 相机ID (1, 2, 3)
     * @param inputQueue 输入图像队列
     * @param resultQueue 算法结果队列（分发到DispatchThread）
     * @param tradAlgo 传统算法实例（Camera1, Camera2使用）
     * @param aiAlgo AI算法实例（Camera2, Camera3使用）
     * @param plc PLC接口
     */
    AlgorithmThread(int cameraId,
                    ThreadSafeQueue<ImageTask>& inputQueue,
                    ThreadSafeQueue<DispatchTask>& resultQueue,
                    std::shared_ptr<IAlgorithm> tradAlgo,
                    std::shared_ptr<YOLOAlgorithm> aiAlgo,
                    PLCInterface* plc,
                    CameraHK1* camera);

    ~AlgorithmThread();

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
     * @brief 设置AI算法实例（用于Camera2的pin检测）
     */
    void setAIAlgo(std::shared_ptr<YOLOAlgorithm> aiAlgo) {
        m_aiAlgo = aiAlgo;
    }

    /**
     * @brief 更新AI缺陷尺寸阈值（线程安全）
     * @param thresholds 阈值列表
     */
    void updateAIThresholds(const std::vector<AIDefectThreshold>& thresholds);

signals:
    /**
     * @brief 算法处理完成信号
     */
    void algorithmCompleted(int cameraId, bool hasDefect, double processingTime);

    /**
     * @brief 错误信号
     */
    void errorOccurred(int cameraId, const QString& error);

private:
    void run();

    // 三个相机的不同处理逻辑
    void processCamera1(const ImageTask& task, AlgorithmResultTask& result);
    void processCamera2(const ImageTask& task, AlgorithmResultTask& result);
    void processCamera3(const ImageTask& task, AlgorithmResultTask& result);

    // PLC发送
    void sendPLCResult(int cameraId, bool hasDefect);
    void sendPLCPinSignal(bool hasDefect, bool quepinDefect);  // Camera2/3发送quepin信号

    // 检查quepin缺陷（Camera2/3专用）
    bool checkQuepinDefect(const AlgorithmResultTask& result);

    // AI阈值筛选（在算法线程中过滤，确保NG判定和绘制一致）
    bool passesThreshold(int classId, const cv::Rect& box) const;

private:
    int m_cameraId;
    ThreadSafeQueue<ImageTask>& m_inputQueue;
    ThreadSafeQueue<DispatchTask>& m_resultQueue;  // 结果队列（分发到DispatchThread）

    std::shared_ptr<IAlgorithm> m_tradAlgo;
    std::shared_ptr<YOLOAlgorithm> m_aiAlgo;
    PLCInterface* m_plc;
    CameraHK1* m_camera;

    // AI阈值筛选（与DispatchThread使用相同逻辑）
    std::map<int, AIDefectThreshold> m_thresholdMap;  // classId -> 阈值
    mutable std::mutex m_thresholdMutex;

    std::thread m_thread;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_stop{false};

    // 统计
    std::atomic<int> m_totalCount{0};
    std::atomic<int> m_defectCount{0};
};
