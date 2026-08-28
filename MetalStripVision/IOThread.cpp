#include "IOThread.h"
#include "DetectionLogDatabase.h"
#include "Logger.h"
#include <QDateTime>
#include <QDir>
#include <QRegularExpression>

IOThread::IOThread(ThreadSafeQueue<IOSaveTask>& inputQueue,
                   DetectionLogDatabase* database,
                   int threadCount)
    : m_inputQueue(inputQueue)
    , m_database(database)
    , m_threadCount(threadCount > 0 ? threadCount : 3)
{
}

IOThread::~IOThread() {
    stop();
}

void IOThread::start() {
    if (m_running.load()) return;

    m_stop.store(false);
    m_running.store(true);
    m_processedCount = 0;
    m_errorCount = 0;

    // 创建多个工作线程
    m_workers.reserve(m_threadCount);
    for (int i = 0; i < m_threadCount; ++i) {
        m_workers.emplace_back(&IOThread::workerThread, this);
    }

    Logger::instance().info(QString("IOThread pool started with %1 threads").arg(m_threadCount).toStdString());
}

void IOThread::stop() {
    m_stop.store(true);
    m_running.store(false);

    // 停止队列，唤醒所有等待的线程
    m_inputQueue.stop();

    // 等待所有工作线程结束
    for (auto& worker : m_workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    m_workers.clear();

    Logger::instance().info(QString("IOThread pool stopped (processed: %1, errors: %2)")
        .arg(m_processedCount.load()).arg(m_errorCount.load()).toStdString());
}

void IOThread::workerThread() {
    auto lastLogTime = std::chrono::steady_clock::now();
    static constexpr int LOG_INTERVAL_MS = 1000;  // 每秒输出一次
    static std::atomic<int> logThreadCounter{0};

    // 只有第一个线程输出队列深度监控
    int threadId = logThreadCounter.fetch_add(1);

    while (!m_stop.load()) {
        // 队列深度监控（仅线程0输出，每秒一次）
        if (threadId == 0) {
            auto now = std::chrono::steady_clock::now();
            auto logElapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastLogTime).count();
            if (logElapsed >= LOG_INTERVAL_MS) {
                size_t queueSize = m_inputQueue.size();
                Logger::instance().debug(QString("IO Queue depth: %1 / 100").arg(queueSize).toStdString());
                lastLogTime = now;
            }
        }

        IOSaveTask task;
        // 使用超时等待
        if (!m_inputQueue.pop(task, 50)) {
            continue;  // 超时或队列停止，继续循环
        }

        // 处理任务
        processTask(task);
        m_processedCount++;
    }

    // 停止前处理队列中剩余的任务
    IOSaveTask remainingTask;
    while (m_inputQueue.pop(remainingTask, 0)) {
        processTask(remainingTask);
        m_processedCount++;
    }
}

void IOThread::processTask(IOSaveTask& task) {
    try {
        // 1. 保存图片（saveImage内部已检查hasDefect）
        saveImage(task);

        // 2. 记录数据库 - 只记录NG
        if (task.hasDefect && m_database) {
            recordToDatabase(task);
        }

        // 3. 发送完成信号
        emit saveCompleted(task.cameraId, task.frameNum, true);

    } catch (const std::exception& e) {
        m_errorCount++;
        Logger::instance().error(QString("IOThread process error: %1")
            .arg(e.what()).toStdString());
        emit saveCompleted(task.cameraId, task.frameNum, false);
    }
}

void IOThread::saveImage(IOSaveTask& task) {
    if (!m_saveConfig.saveDefectImage && !m_saveConfig.saveOriginalImage) {
        return;
    }

    // 只保存NG图片
    if (!task.hasDefect) {
        return;
    }

    // 使用任务中的统一时间戳
    QDateTime& timestamp = task.timestamp;
    QString dateStr = timestamp.toString("yyyyMMdd");
    QString timeStr = timestamp.toString("yyyyMMdd_HHmmss_zzz");

    // 获取料号并清理非法字符
    QString partNumber = QString::fromStdString(task.partNumber);
    if (partNumber.isEmpty()) {
        partNumber = "未知料号";
    }
    // 清理料号中的非法字符（防止路径问题）
    partNumber.remove(QRegularExpression("[<>:\"/\\|?*]"));

    // 基础路径：save/料号/日期/相机
    QString basePath = QString::fromStdString(m_saveConfig.savePath) +
                       "/" + partNumber +
                       "/" + dateStr +
                       "/Camera" + QString::number(task.cameraId);

    // 保存原图 - 使用相机配置的原始格式（bmp/tiff），不压缩
    if (m_saveConfig.saveOriginalImage) {
        QString originalPath = basePath + "/original";
        QDir().mkpath(originalPath);

        QString ext = QString::fromStdString(task.originalFormat);
        QString filename = QString("%1/Camera%2_%3.%4")
            .arg(originalPath)
            .arg(task.cameraId)
            .arg(timeStr)
            .arg(ext);

        // 原图不压缩，使用原始格式保存
        std::vector<int> params;
        if (ext == "tiff" || ext == "tif") {
            params = {cv::IMWRITE_TIFF_COMPRESSION, 1};  // 无压缩
        }
        // bmp 不需要额外参数

        if (!cv::imwrite(filename.toStdString(), task.originalImage, params)) {
            Logger::instance().error("IOThread: Failed to save original image: " +
                                   filename.toStdString());
        }
    }

    // 保存标注图 - 根据配置决定是否压缩
    if (m_saveConfig.saveDefectImage) {
        QString defectPath = basePath + "/defect";
        QDir().mkpath(defectPath);

        QString ext;
        std::vector<int> params;
        if (m_saveConfig.compressDefectImage) {
            ext = "jpg";
            params = {cv::IMWRITE_JPEG_QUALITY, 80};
        } else {
            // 不压缩时使用原图格式
            ext = QString::fromStdString(task.originalFormat);
            if (ext == "tiff" || ext == "tif") {
                params = {cv::IMWRITE_TIFF_COMPRESSION, 1};
            }
        }

        QString filename = QString("%1/Camera%2_%3.%4")
            .arg(defectPath)
            .arg(task.cameraId)
            .arg(timeStr)
            .arg(ext);

        if (!cv::imwrite(filename.toStdString(), task.annotatedImage, params)) {
            Logger::instance().error("IOThread: Failed to save defect image: " +
                                   filename.toStdString());
        }

        // 保存文件路径到任务中，供数据库记录使用
        task.savedImagePath = filename;
    }
}

void IOThread::recordToDatabase(IOSaveTask& task) {
    if (!m_database) return;

    // 使用保存时记录的图片路径
    QString imagePath = task.savedImagePath;

    // 插入数据库（DetectionLogDatabase支持多线程，每个线程独立连接）
    m_database->insertRecord(task.cameraId,
                             task.defectCount,
                             task.hasDefect,
                             imagePath,
                             task.algorithmTypeDisplay,
                             task.processingTime);

    // 发送记录信号
    QString recordText = generateRecordText(task);
    emit recordAdded(recordText);
}

QString IOThread::generateRecordText(IOSaveTask& task) {
    QString timeStr = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
    QString resultStr = task.hasDefect ? "NG" : "OK";

    QString text = QString("[%1] Camera%2 %3")
        .arg(timeStr)
        .arg(task.cameraId)
        .arg(resultStr);

    if (task.hasDefect && !task.defectTypes.isEmpty()) {
        text += QString(" 缺陷: %1").arg(task.defectTypes.join(", "));
    }

    return text;
}
