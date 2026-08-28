#pragma once

#include <QObject>
#include <thread>
#include <atomic>
#include <memory>
#include <map>
#include <chrono>
#include "ThreadSafeQueue.h"
#include "ImageTask.h"

/**
 * @brief UI显示线程
 *
 * 从UI队列获取算法结果，更新UI显示。
 * 每个相机独立150ms刷新率。
 */
class UIThread : public QObject {
    Q_OBJECT

public:
    explicit UIThread(ThreadSafeQueue<UIDisplayTask>& inputQueue);
    ~UIThread();

    void start();
    void stop();
    bool isRunning() const { return m_running.load(); }

signals:
    void imageUpdated(int cameraId, cv::Mat image, bool hasDefect, cv::Mat originalImage);
    void defectStatusUpdated(int cameraId, bool hasDefect, bool algorithmFailed);

private:
    void run();

    ThreadSafeQueue<UIDisplayTask>& m_inputQueue;

    std::thread m_thread;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_stop{false};
};
