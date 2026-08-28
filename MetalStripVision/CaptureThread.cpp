#include "CaptureThread.h"
#include "Logger.h"
#include <chrono>
#include <QDir>

CaptureThread::CaptureThread(int cameraId,
                             CameraHK1* camera,
                             ThreadSafeQueue<ImageTask>& outputQueue)
    : m_cameraId(cameraId)
    , m_camera(camera)
    , m_outputQueue(outputQueue)
{
}

CaptureThread::~CaptureThread() {
    stop();
}

void CaptureThread::start() {
    if (m_running.load()) return;

    m_stop.store(false);
    // 不自动设置 m_running = true，等待用户点击"开始检测"后调用 resume()
    m_frameCount = 0;
    m_testImageIndex = 0;

    m_thread = std::thread(&CaptureThread::run, this);
    Logger::instance().info("CaptureThread " + std::to_string(m_cameraId) + " started (paused)");
}

void CaptureThread::stop() {
    m_stop.store(true);
    m_running.store(false);

    if (m_thread.joinable()) {
        m_thread.join();
    }

    Logger::instance().info("CaptureThread " + std::to_string(m_cameraId) + " stopped");
}

void CaptureThread::pause() {
    m_running.store(false);
    Logger::instance().info("CaptureThread " + std::to_string(m_cameraId) + " paused");
}

void CaptureThread::resume() {
    m_running.store(true);
    Logger::instance().info("CaptureThread " + std::to_string(m_cameraId) + " resumed");
}

void CaptureThread::setTestMode(bool testMode,
                                const std::vector<std::string>& testImages,
                                int delayMs) {
    m_testMode = testMode;
    m_testImages = testImages;
    m_testDelayMs = delayMs;
    m_testImageIndex = 0;
}

void CaptureThread::run() {
    Logger::instance().info("CaptureThread " + std::to_string(m_cameraId) + " running, testMode=" +
                           std::string(m_testMode ? "true" : "false"));

    auto lastCaptureTime = std::chrono::high_resolution_clock::now();
    static constexpr int LOG_INTERVAL_MS = 1000;  // 每秒输出一次
    auto lastLogTime = lastCaptureTime;

    while (!m_stop.load()) {
        // 等待运行信号
        if (!m_running.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            lastCaptureTime = std::chrono::high_resolution_clock::now();  // 重置计时
            continue;
        }

        auto captureStart = std::chrono::high_resolution_clock::now();

        ImageTask task;
        task.cameraId = m_cameraId;

        if (m_testMode) {
            // 测试模式：从文件读取
            std::this_thread::sleep_for(std::chrono::milliseconds(m_testDelayMs));
            task.image = getNextTestImage();
            task.frameNum = static_cast<unsigned int>(m_frameCount);

            if (task.image.empty()) {
                continue;
            }
        } else {
            // 正常模式：从相机采集
            // 软触发模式需要先发送触发命令
            if (m_triggerMode == 1) {
                if (m_camera->TriggerSoftware() != MV_OK) {
                    emit captureFailed(m_cameraId, QString("Soft trigger failed"));
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    continue;
                }
            }

            if (!m_camera || !m_camera->GetImage(task.image, task.frameNum)) {
                emit captureFailed(m_cameraId, QString("GetImage failed"));
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
        }

        // 拿到图后的时间点
        auto afterGetImage = std::chrono::high_resolution_clock::now();

        // 设置采图时间戳（以拿到图为基准，不含等待硬触发时间）
        task.captureTime = afterGetImage;
        task.captureStartTime = captureStart;

        // 放入队列
        if (!m_outputQueue.push(task)) {
            Logger::instance().warn("CaptureThread " + std::to_string(m_cameraId) +
                                   " queue full, frame dropped");
        }

        m_frameCount++;
        emit frameCountUpdated(m_cameraId, m_frameCount);

        // 计算采图耗时和间隔（每秒输出一次，debug级别）
        auto now = std::chrono::high_resolution_clock::now();
        auto logElapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastLogTime).count();
        if (logElapsed >= LOG_INTERVAL_MS) {
            double getImageTime = std::chrono::duration<double, std::milli>(afterGetImage - captureStart).count();
            double processTime = std::chrono::duration<double, std::milli>(now - afterGetImage).count();
            double intervalTime = std::chrono::duration<double, std::milli>(now - lastCaptureTime).count();
            size_t queueDepth = m_outputQueue.size();

            Logger::instance().debug(QString("[CAP] Camera%1: GetImage=%2ms, Process=%3ms, Interval=%4ms, Queue=%5/10")
                .arg(m_cameraId)
                .arg(getImageTime, 0, 'f', 1)
                .arg(processTime, 0, 'f', 1)
                .arg(intervalTime, 0, 'f', 1)
                .arg(queueDepth).toStdString());

            lastLogTime = now;
        }
        lastCaptureTime = now;
    }
}

cv::Mat CaptureThread::getNextTestImage() {
    if (m_testImages.empty()) {
        return cv::Mat();
    }

    // 循环读取测试图片
    if (m_testImageIndex >= m_testImages.size()) {
        m_testImageIndex = 0;
    }

    const std::string& path = m_testImages[m_testImageIndex];
    m_testImageIndex++;

    cv::Mat image = cv::imread(path, cv::IMREAD_COLOR);
    if (image.empty()) {
        Logger::instance().warn("CaptureThread " + std::to_string(m_cameraId) +
                               " failed to load test image: " + path);
    }

    return image;
}
