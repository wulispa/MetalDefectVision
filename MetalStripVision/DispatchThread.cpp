#include "DispatchThread.h"
#include "Logger.h"
#include <chrono>
#include <windows.h>
#include <QPainter>
#include <QImage>
#include <QFont>

DispatchThread::DispatchThread(int cameraId,
                               ThreadSafeQueue<DispatchTask>& inputQueue,
                               ThreadSafeQueue<UIDisplayTask>& uiQueue,
                               ThreadSafeQueue<IOSaveTask>& ioQueue,
                               std::shared_ptr<YOLOAlgorithm> aiAlgo)
    : m_cameraId(cameraId)
    , m_inputQueue(inputQueue)
    , m_uiQueue(uiQueue)
    , m_ioQueue(ioQueue)
    , m_aiAlgo(aiAlgo)
{
}

DispatchThread::~DispatchThread() {
    stop();
}

void DispatchThread::updateAIThresholds(const std::vector<AIDefectThreshold>& thresholds)
{
    std::lock_guard<std::mutex> lock(m_thresholdMutex);
    m_thresholdMap.clear();
    for (const auto& t : thresholds) {
        m_thresholdMap[t.classId] = t;
    }
}

void DispatchThread::setPartNumber(const std::string& partNumber)
{
    std::lock_guard<std::mutex> lock(m_partNumberMutex);
    m_partNumber = partNumber;
    Logger::instance().debug("DispatchThread " + std::to_string(m_cameraId) +
                            " part number updated: " + partNumber);
}

bool DispatchThread::shouldDrawDetection(int classId, const cv::Rect& box) const
{
    std::lock_guard<std::mutex> lock(m_thresholdMutex);

    // 如果没有配置任何阈值，默认绘制所有检测框
    if (m_thresholdMap.empty()) return true;

    auto it = m_thresholdMap.find(classId);
    if (it == m_thresholdMap.end()) {
        // 没有为此类别配置阈值，默认绘制
        return true;
    }

    const auto& threshold = it->second;
    int w = box.width;
    int h = box.height;

    // 宽度和高度都在阈值范围内才绘制
    bool widthOk = (w >= threshold.minWidth) && (w <= threshold.maxWidth);
    bool heightOk = (h >= threshold.minHeight) && (h <= threshold.maxHeight);

    return widthOk && heightOk;
}

void DispatchThread::start() {
    if (m_running.load()) return;

    m_stop.store(false);
    m_running.store(true);

    m_thread = std::thread(&DispatchThread::run, this);
    ::SetThreadPriority(m_thread.native_handle(), THREAD_PRIORITY_BELOW_NORMAL);
    Logger::instance().info("DispatchThread " + std::to_string(m_cameraId) + " started (priority=BELOW_NORMAL)");
}

void DispatchThread::stop() {
    m_stop.store(true);
    m_running.store(false);

    if (m_thread.joinable()) {
        m_thread.join();
    }

    Logger::instance().info("DispatchThread " + std::to_string(m_cameraId) + " stopped");
}

void DispatchThread::run() {
    while (!m_stop.load()) {
        DispatchTask task;
        // 使用100ms超时，避免无限阻塞
        if (!m_inputQueue.pop(task, 100)) {
            continue;
        }

        auto t0 = std::chrono::high_resolution_clock::now();

        // ========== 1. 绘制检测结果 ==========
        cv::Mat annotatedImage = drawDetectionResults(task);

        auto t1 = std::chrono::high_resolution_clock::now();
        double drawTime = std::chrono::duration<double, std::milli>(t1 - t0).count();

        // ========== 2. 分发到UI队列 ==========
        UIDisplayTask uiTask;
        uiTask.cameraId = task.cameraId;
        uiTask.hasDefect = task.hasDefect;
        uiTask.frameNum = task.frameNum;
        uiTask.algorithmFailed = false;

        // 保存原始分辨率图像（用于参数设置对话框ROI绘制，避免缩放导致坐标偏移）
        uiTask.originalImage = task.originalImage.clone();

        // 预先缩放减轻UI负担
        constexpr int UI_TARGET_HEIGHT = 800;
        double scale = static_cast<double>(UI_TARGET_HEIGHT) / annotatedImage.rows;
        cv::resize(annotatedImage, uiTask.annotatedImage, cv::Size(), scale, scale, cv::INTER_NEAREST);

        m_uiQueue.push(std::move(uiTask));

        auto t2 = std::chrono::high_resolution_clock::now();
        double uiTime = std::chrono::duration<double, std::milli>(t2 - t1).count();

        // ========== 3. 分发到IO队列 ==========
        IOSaveTask ioTask;
        ioTask.cameraId = task.cameraId;
        ioTask.annotatedImage = annotatedImage.clone();  // 必须clone，annotatedImage还会被UI使用
        ioTask.originalImage = std::move(task.originalImage);  // 移动，不再clone
        ioTask.hasDefect = task.hasDefect;
        ioTask.defectCount = task.defectCount;
        ioTask.defectTypes = task.defectTypes;
        ioTask.algorithmTypeDisplay = task.algorithmTypeDisplay;
        ioTask.processingTime = task.processingTime;
        ioTask.frameNum = task.frameNum;
        ioTask.captureTime = task.captureTime;
        ioTask.timestamp = QDateTime::currentDateTime();

        // 获取相机的原图格式配置
        const CameraConfig& camConfig = ConfigManager::instance().getCameraConfig(m_cameraId);
        ioTask.originalFormat = camConfig.originalFormat;

        // 获取并设置料号（线程安全）
        {
            std::lock_guard<std::mutex> lock(m_partNumberMutex);
            ioTask.partNumber = m_partNumber;
        }

        m_ioQueue.push(std::move(ioTask));

        auto t3 = std::chrono::high_resolution_clock::now();
        double ioTime = std::chrono::duration<double, std::milli>(t3 - t2).count();
        double totalTime = std::chrono::duration<double, std::milli>(t3 - t0).count();

        // 输出计时日志（debug级别）
        Logger::instance().debug(QString("[DISPATCH] Camera%1: Draw=%2ms, UI=%3ms, IO=%4ms | Total=%5ms")
            .arg(m_cameraId)
            .arg(drawTime, 0, 'f', 1)
            .arg(uiTime, 0, 'f', 1)
            .arg(ioTime, 0, 'f', 1)
            .arg(totalTime, 0, 'f', 1).toStdString());

        emit dispatchCompleted(m_cameraId);
    }
}

// ========== 绘制函数实现 ==========

cv::Mat DispatchThread::drawDetectionResults(const DispatchTask& task) {
    cv::Mat annotatedImage = task.image.clone();
    if (annotatedImage.channels() == 1) {
        cv::cvtColor(annotatedImage, annotatedImage, cv::COLOR_GRAY2BGR);
    }

    switch (m_cameraId) {
        case 1:
            drawCamera1Results(annotatedImage, task);
            break;
        case 2:
            drawCamera2Results(annotatedImage, task);
            break;
        case 3:
            drawCamera3Results(annotatedImage, task);
            break;
    }

    return annotatedImage;
}

void DispatchThread::drawCamera1Results(cv::Mat& image, const DispatchTask& task) {
    if (!task.tradProcessed) return;

    const auto& tradResult = task.tradResult;

    // 绘制异常线段（红色竖线）
    if (!tradResult.abnormalLineCols.empty()) {
        for (int col : tradResult.abnormalLineCols) {
            cv::line(image,
                     cv::Point(col, static_cast<int>(tradResult.roiRow1)),
                     cv::Point(col, static_cast<int>(tradResult.roiRow2)),
                     cv::Scalar(0, 0, 255),  // 红色
                     3);  // 线宽
        }
    }

    // 绘制距离文本
    if (!tradResult.distanceTexts.empty() &&
        tradResult.distanceTexts.size() == tradResult.textRows.size() &&
        tradResult.distanceTexts.size() == tradResult.textCols.size()) {

        for (size_t i = 0; i < tradResult.distanceTexts.size(); ++i) {
            std::string text = std::to_string(tradResult.distanceTexts[i]);
            text = text.substr(0, text.find('.') + 3);  // 保留2位小数

            cv::putText(image, text,
                        cv::Point(static_cast<int>(tradResult.textCols[i]),
                                  static_cast<int>(tradResult.textRows[i])),
                        cv::FONT_HERSHEY_SIMPLEX, 1.5,
                        cv::Scalar(0, 0, 255),  // 红色
                        3);
        }
    }
}

void DispatchThread::drawCamera2Results(cv::Mat& image, const DispatchTask& task) {
    // 绘制传统算法检测框（黄色）
    if (task.tradProcessed && task.tradResult.hasBbox) {
        cv::rectangle(image,
                      cv::Point(static_cast<int>(task.tradResult.bboxCol1),
                                static_cast<int>(task.tradResult.bboxRow1)),
                      cv::Point(static_cast<int>(task.tradResult.bboxCol2),
                                static_cast<int>(task.tradResult.bboxRow2)),
                      cv::Scalar(0, 0, 255),  // 红色
                      3);
    }

    // 绘制AI检测框（按类别配色）
    if (!task.detectionBoxes.empty() && m_aiAlgo) {
        for (size_t i = 0; i < task.detectionBoxes.size(); ++i) {
            const auto& box = task.detectionBoxes[i];
            int classId = (i < task.detectionClassIds.size()) ? task.detectionClassIds[i] : 0;

            // AI阈值筛选
            if (!shouldDrawDetection(classId, box)) continue;

            cv::Scalar color = m_aiAlgo->getClassColor(classId);

            cv::rectangle(image,
                          cv::Point(box.x, box.y),
                          cv::Point(box.x + box.width, box.y + box.height),
                          color, 3);

            // 绘制类别标签
            if (i < task.detectionClassIds.size()) {
                QString className = m_aiAlgo->getClassName(task.detectionClassIds[i]);
                cv::putText(image, className.toStdString(),
                            cv::Point(box.x, box.y - 10),
                            cv::FONT_HERSHEY_SIMPLEX, 2.0,
                            color, 3);
            }

            // 绘制尺寸标签
            double widthMm = box.width / m_bboxConfig.pixelsPerMm;
            double heightMm = box.height / m_bboxConfig.pixelsPerMm;
            QString sizeLabel = QString("%1x%2").arg(widthMm, 0, 'f', 2)
                                                .arg(heightMm, 0, 'f', 2);
            cv::putText(image, sizeLabel.toStdString(),
                        cv::Point(box.x - 60, box.y + box.height + 25),
                        cv::FONT_HERSHEY_SIMPLEX,
                        m_bboxConfig.fontScale,
                        m_bboxConfig.textColor,
                        m_bboxConfig.fontThickness);
        }
    }
}

void DispatchThread::drawCamera3Results(cv::Mat& image, const DispatchTask& task) {
    // 绘制AI检测框（按类别配色）
    if (task.detectionBoxes.empty() || !m_aiAlgo) return;

    for (size_t i = 0; i < task.detectionBoxes.size(); ++i) {
        const auto& box = task.detectionBoxes[i];
        int classId = (i < task.detectionClassIds.size()) ? task.detectionClassIds[i] : 0;

        // AI阈值筛选
        if (!shouldDrawDetection(classId, box)) continue;

        cv::Scalar color = m_aiAlgo->getClassColor(classId);

        cv::rectangle(image,
                      cv::Point(box.x, box.y),
                      cv::Point(box.x + box.width, box.y + box.height),
                      color, 3);

        // 绘制类别标签
        if (i < task.detectionClassIds.size()) {
            QString className = m_aiAlgo->getClassName(task.detectionClassIds[i]);
            cv::putText(image, className.toStdString(),
                        cv::Point(box.x, box.y - 10),
                        cv::FONT_HERSHEY_SIMPLEX, 2.0,
                        color, 3);
        }

        // 绘制尺寸标签
        double widthMm = box.width / m_bboxConfig.pixelsPerMm;
        double heightMm = box.height / m_bboxConfig.pixelsPerMm;
        QString sizeLabel = QString("%1x%2").arg(widthMm, 0, 'f', 2)
                                            .arg(heightMm, 0, 'f', 2);
        cv::putText(image, sizeLabel.toStdString(),
                    cv::Point(box.x - 20, box.y + box.height + 25),
                    cv::FONT_HERSHEY_SIMPLEX,
                    m_bboxConfig.fontScale,
                    m_bboxConfig.textColor,
                    m_bboxConfig.fontThickness);
    }
}

void DispatchThread::drawChineseText(cv::Mat& image, const QString& text,
                                      const cv::Point& position,
                                      const cv::Scalar& color, int fontSize) {
    if (image.empty() || text.isEmpty()) return;

    // 确保是3通道BGR格式
    if (image.channels() == 1) {
        cv::cvtColor(image, image, cv::COLOR_GRAY2BGR);
    }

    // 转换为 QImage（共享数据，不复制）
    QImage qImage(image.data, image.cols, image.rows,
                  static_cast<int>(image.step), QImage::Format_BGR888);

    QPainter painter(&qImage);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);

    QFont font("Microsoft YaHei UI", fontSize, QFont::Bold);
    painter.setFont(font);

    // BGR -> RGB
    QColor textColor(static_cast<int>(color[2]),
                     static_cast<int>(color[1]),
                     static_cast<int>(color[0]));
    painter.setPen(textColor);

    painter.drawText(position.x, position.y + fontSize, text);
    painter.end();
}
