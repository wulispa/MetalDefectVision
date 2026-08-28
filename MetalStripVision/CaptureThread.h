#pragma once

#include <QObject>
#include <thread>
#include <atomic>
#include <memory>
#include <vector>
#include <string>
#include "ThreadSafeQueue.h"
#include "ImageTask.h"
#include "CameraHK1.h"

/**
 * @brief 采图线程
 *
 * 负责从相机采集图像并放入图像队列。
 * 支持正常模式和测试模式。
 */
class CaptureThread : public QObject {
    Q_OBJECT

public:
    /**
     * @brief 构造函数
     * @param cameraId 相机ID (1, 2, 3)
     * @param camera 相机实例指针
     * @param outputQueue 输出队列
     */
    CaptureThread(int cameraId,
                  CameraHK1* camera,
                  ThreadSafeQueue<ImageTask>& outputQueue);

    ~CaptureThread();

    /**
     * @brief 启动线程（线程启动后处于暂停状态，需要调用resume()开始采集）
     */
    void start();

    /**
     * @brief 停止线程
     */
    void stop();

    /**
     * @brief 暂停采集
     */
    void pause();

    /**
     * @brief 恢复采集
     */
    void resume();

    /**
     * @brief 设置测试模式
     * @param testMode 是否为测试模式
     * @param testImages 测试图片路径列表
     * @param delayMs 测试模式下图片间隔（毫秒）
     */
    void setTestMode(bool testMode,
                     const std::vector<std::string>& testImages = {},
                     int delayMs = 100);

    /**
     * @brief 设置触发模式
     * @param triggerMode 1=软触发, 2=硬触发
     */
    void setTriggerMode(int triggerMode) { m_triggerMode = triggerMode; }

    /**
     * @brief 是否正在运行
     */
    bool isRunning() const { return m_running.load(); }

signals:
    /**
     * @brief 采图失败信号
     */
    void captureFailed(int cameraId, const QString& error);

    /**
     * @brief 采图计数更新
     */
    void frameCountUpdated(int cameraId, int count);

private:
    void run();
    cv::Mat getNextTestImage();

    int m_cameraId;
    CameraHK1* m_camera;
    ThreadSafeQueue<ImageTask>& m_outputQueue;

    std::thread m_thread;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_stop{false};

    // 测试模式
    bool m_testMode = false;
    std::vector<std::string> m_testImages;
    size_t m_testImageIndex = 0;
    int m_testDelayMs = 100;
    int m_frameCount = 0;

    // 触发模式：1=软触发, 2=硬触发
    int m_triggerMode = 2;  // 默认硬触发
};
