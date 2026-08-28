#pragma once

#include <QObject>
#include <thread>
#include <atomic>
#include <memory>
#include <vector>
#include "ThreadSafeQueue.h"
#include "ImageTask.h"
#include "ConfigManager.h"

class DetectionLogDatabase;

/**
 * @brief IO保存线程池
 *
 * 从IO队列获取任务，保存图片和记录数据库。
 * 特点：不能丢数据，所有结果必须完整保存。
 * 使用多线程并行处理，提升IO吞吐量。
 */
class IOThread : public QObject {
    Q_OBJECT

public:
    /**
     * @brief 构造函数
     * @param inputQueue 输入队列（IO保存任务）
     * @param database 数据库实例
     * @param threadCount 工作线程数量（默认3）
     */
    IOThread(ThreadSafeQueue<IOSaveTask>& inputQueue,
             DetectionLogDatabase* database,
             int threadCount = 3);

    ~IOThread();

    /**
     * @brief 设置图片保存配置
     */
    void setImageSaveConfig(const ImageSaveConfig& config) {
        m_saveConfig = config;
    }

    /**
     * @brief 启动线程池
     */
    void start();

    /**
     * @brief 停止线程池
     */
    void stop();

    /**
     * @brief 是否正在运行
     */
    bool isRunning() const { return m_running.load(); }

    /**
     * @brief 获取线程数量
     */
    int getThreadCount() const { return m_threadCount; }

signals:
    /**
     * @brief 记录添加信号（用于UI记录列表）
     */
    void recordAdded(const QString& recordText);

    /**
     * @brief 保存完成信号
     */
    void saveCompleted(int cameraId, unsigned int frameNum, bool success);

private:
    void workerThread();  // 工作线程函数
    void processTask(IOSaveTask& task);
    void saveImage(IOSaveTask& task);
    void recordToDatabase(IOSaveTask& task);
    QString generateRecordText(IOSaveTask& task);

    ThreadSafeQueue<IOSaveTask>& m_inputQueue;
    DetectionLogDatabase* m_database;

    std::vector<std::thread> m_workers;  // 工作线程池
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_stop{false};

    ImageSaveConfig m_saveConfig;
    int m_threadCount;  // 线程数量

    // 统计计数器（用于监控）
    std::atomic<int> m_processedCount{0};
    std::atomic<int> m_errorCount{0};
};
