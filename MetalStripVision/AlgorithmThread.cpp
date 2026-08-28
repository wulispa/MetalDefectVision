#include "AlgorithmThread.h"
#include "Logger.h"
#include "HalconAlgorithm.h"
#include "CameraHK1.h"
#include <chrono>
#include <windows.h>

AlgorithmThread::AlgorithmThread(int cameraId,
                                 ThreadSafeQueue<ImageTask>& inputQueue,
                                 ThreadSafeQueue<DispatchTask>& resultQueue,
                                 std::shared_ptr<IAlgorithm> tradAlgo,
                                 std::shared_ptr<YOLOAlgorithm> aiAlgo,
                                 PLCInterface* plc,
                                 CameraHK1* camera)
    : m_cameraId(cameraId)
    , m_inputQueue(inputQueue)
    , m_resultQueue(resultQueue)
    , m_tradAlgo(tradAlgo)
    , m_aiAlgo(aiAlgo)
    , m_plc(plc)
    , m_camera(camera)
{
}

void AlgorithmThread::updateAIThresholds(const std::vector<AIDefectThreshold>& thresholds)
{
    std::lock_guard<std::mutex> lock(m_thresholdMutex);
    m_thresholdMap.clear();
    for (const auto& t : thresholds) {
        m_thresholdMap[t.classId] = t;
    }
}

bool AlgorithmThread::passesThreshold(int classId, const cv::Rect& box) const
{
    std::lock_guard<std::mutex> lock(m_thresholdMutex);

    // 没有配置任何阈值，默认通过
    if (m_thresholdMap.empty()) return true;

    auto it = m_thresholdMap.find(classId);
    if (it == m_thresholdMap.end()) {
        // 没有为此类别配置阈值，默认通过
        return true;
    }

    const auto& threshold = it->second;
    bool widthOk = (box.width >= threshold.minWidth) && (box.width <= threshold.maxWidth);
    bool heightOk = (box.height >= threshold.minHeight) && (box.height <= threshold.maxHeight);

    return widthOk && heightOk;
}

AlgorithmThread::~AlgorithmThread() {
    stop();
}

void AlgorithmThread::start() {
    if (m_running.load()) return;

    m_stop.store(false);
    m_running.store(true);

    m_thread = std::thread(&AlgorithmThread::run, this);
    ::SetThreadPriority(m_thread.native_handle(), THREAD_PRIORITY_HIGHEST);
    Logger::instance().info("AlgorithmThread " + std::to_string(m_cameraId) + " started (priority=HIGHEST)");
}

void AlgorithmThread::stop() {
    m_stop.store(true);
    m_running.store(false);

    if (m_thread.joinable()) {
        m_thread.join();
    }

    Logger::instance().info("AlgorithmThread " + std::to_string(m_cameraId) + " stopped");
}

void AlgorithmThread::run() {
    while (!m_stop.load()) {
        ImageTask task;
        // 使用100ms超时，避免无限阻塞
        if (!m_inputQueue.pop(task, 100)) {
            continue;
        }

        auto t0 = std::chrono::high_resolution_clock::now();  // 开始计时
        auto algoStartTime = t0;

        // 计算队列等待时间（从采图到开始处理）
        double queueWaitTime = std::chrono::duration<double, std::milli>(t0 - task.captureTime).count();

        // 创建结果任务
        AlgorithmResultTask result;
        result.cameraId = task.cameraId;
        result.frameNum = task.frameNum;
        result.captureStartTime = task.captureStartTime;
        result.captureTime = task.captureTime;

        // 先执行算法（使用task.image），后再处理图像转移，减少一次clone
        auto t1 = std::chrono::high_resolution_clock::now();

        // 根据相机ID调用不同的处理逻辑
        try {
            switch (m_cameraId) {
            case 1:
                processCamera1(task, result);
                break;
            case 2:
                processCamera2(task, result);
                break;
            case 3:
                processCamera3(task, result);
                break;
            default:
                Logger::instance().error("Unknown camera ID: " + std::to_string(m_cameraId));
                continue;
            }
        } catch (const std::exception& e) {
            Logger::instance().error("AlgorithmThread " + std::to_string(m_cameraId) +
                                    " exception: " + std::string(e.what()));
            emit errorOccurred(m_cameraId, QString::fromStdString(e.what()));
            continue;
        }

        auto t2 = std::chrono::high_resolution_clock::now();  // 算法完成
        double algoTime = std::chrono::duration<double, std::milli>(t2 - t1).count();

        result.processingTime = std::chrono::duration<double, std::milli>(t2 - algoStartTime).count();

        // ========== 关键路径：立即发送PLC信号 ==========
        sendPLCResult(m_cameraId, result.hasDefect);

        auto t3 = std::chrono::high_resolution_clock::now();  // PLC发送完成
        double plcTime = std::chrono::duration<double, std::milli>(t3 - t2).count();

        // Camera2 发送Pin信号（Camera3已禁用PLC Pin发送）
        double pinTime = 0.0;
        if (m_cameraId == 2) {
            bool quepinDefect = checkQuepinDefect(result);
            sendPLCPinSignal(result.hasDefect, quepinDefect);
            auto t4 = std::chrono::high_resolution_clock::now();
            pinTime = std::chrono::duration<double, std::milli>(t4 - t3).count();
        }

        // Total终点：所有IO发送完成之后（包含Pin信号）
        result.algorithmEndTime = std::chrono::high_resolution_clock::now();

        // 图像转移放到关键路径之后，clone不再计入检测时间
        result.image = std::move(task.image);
        result.originalImage = result.image.clone();

        // ========== 异步路径：推送到结果队列（由DispatchThread处理）==========
        DispatchTask dispatchTask;
        dispatchTask.cameraId = result.cameraId;
        dispatchTask.frameNum = result.frameNum;
        dispatchTask.image = std::move(result.image);                    // 移动，不clone
        dispatchTask.originalImage = std::move(result.originalImage);    // 移动，不clone
        dispatchTask.hasDefect = result.hasDefect;
        dispatchTask.defectCount = result.defectCount;
        dispatchTask.defectTypes = result.defectTypes;
        dispatchTask.algorithmTypeDisplay = result.algorithmTypeDisplay;
        dispatchTask.processingTime = result.processingTime;
        dispatchTask.tradResult = result.tradResult;
        dispatchTask.tradProcessed = result.tradProcessed;
        dispatchTask.aiProcessed = result.aiProcessed;
        dispatchTask.detectionClassIds = result.detectionClassIds;
        dispatchTask.detectionBoxes = result.detectionBoxes;
        dispatchTask.captureTime = result.captureTime;
        dispatchTask.hasPinDefect = result.hasPinDefect;

        m_resultQueue.push(std::move(dispatchTask));

        auto pushEnd = std::chrono::high_resolution_clock::now();  // 队列推送完成
        double pushTime = std::chrono::duration<double, std::milli>(pushEnd - result.algorithmEndTime).count();

        // 更新统计
        m_totalCount++;
        if (result.hasDefect) {
            m_defectCount++;
        }

        // 计算CAP耗时（GetImage）
        double capTime = std::chrono::duration<double, std::milli>(task.captureTime - task.captureStartTime).count();

        // 输出分段计时日志（debug级别）
        double totalTime = result.getTotalTimeMs();
        Logger::instance().debug(QString("[TIME] Camera%1: CAP=%2ms, Queue=%3ms, Algo=%4ms, PLC=%5ms, Pin=%6ms | Total=%7ms | %8")
            .arg(m_cameraId)
            .arg(capTime, 0, 'f', 1)
            .arg(queueWaitTime, 0, 'f', 1)
            .arg(algoTime, 0, 'f', 1)
            .arg(plcTime, 0, 'f', 1)
            .arg(pinTime, 0, 'f', 1)
            .arg(totalTime, 0, 'f', 1)
            .arg(result.hasDefect ? "NG" : "OK").toStdString());

        emit algorithmCompleted(m_cameraId, result.hasDefect, result.processingTime);
    }
}

void AlgorithmThread::processCamera1(const ImageTask& task, AlgorithmResultTask& result) {
    // ========== 空跑模式：算法1已禁用，始终输出OK ==========
    // 恢复时将 #if 0 改为 #if 1
#if 1
    // Camera1 只使用传统算法
    if (!m_tradAlgo || !m_tradAlgo->isInitialized()) {
        Logger::instance().warn("Camera1: Traditional algorithm not initialized");
        return;
    }

    // ROI参数已在检测启动时一次性加载，不再每帧读取

    // 执行算法
    auto halconAlgo = std::dynamic_pointer_cast<HalconAlgorithm>(m_tradAlgo);
    //result.tradResult = halconAlgo->process_opencv(task.image);  // OpenCV版（已禁用）
    result.tradResult = halconAlgo->process(task.image);  // Halcon版
    result.tradProcessed = true;

    if (result.tradResult.success) {
        result.hasDefect = result.tradResult.hasDefect;
        result.defectCount = result.tradResult.defectCount;
        result.algorithmTypeDisplay = "Traditional";
    }
#endif
    // hasDefect 默认为 false，即始终输出OK
}

void AlgorithmThread::processCamera2(const ImageTask& task, AlgorithmResultTask& result) {
    const CameraConfig& camConfig = ConfigManager::instance().getCameraConfig(2);

    // 传统算法处理已禁用（改用AI算法处理）
    // // 更新ROI参数
    // std::string defaultProfile = AlgorithmROIManager::instance().getDefaultProfile(2);
    // if (!defaultProfile.empty()) {
    //     UserProfile profile = AlgorithmROIManager::instance().getUserProfile(2, defaultProfile);
    //     if (profile.roi.enabled && m_tradAlgo) {
    //         m_tradAlgo->updateROI(profile.roi.row1, profile.roi.column1,
    //                               profile.roi.row2, profile.roi.column2);
    //     }
    // }
    //
    // // 传统算法处理（Camera2使用专门的process_cam2方法）
    // if (camConfig.useTraditionalAlgo && m_tradAlgo && m_tradAlgo->isInitialized()) {
    //     auto halconAlgo = std::dynamic_pointer_cast<HalconAlgorithm>(m_tradAlgo);
    //     if (halconAlgo) {
    //         result.tradResult = halconAlgo->process_cam2(task.image);
    //     }
    //     result.tradProcessed = true;
    //
    //     if (result.tradResult.success && result.tradResult.hasDefect) {
    //         result.hasDefect = true;
    //     }
    // }

    // AI算法处理
    if (camConfig.useAIAlgo && m_aiAlgo && m_aiAlgo->isInitialized()) {
        result.aiResult = m_aiAlgo->process(task.image);
        result.aiProcessed = true;

        if (result.aiResult.success) {
            auto detections = m_aiAlgo->getDetections();
            // 阈值筛选：只有通过阈值的缺陷才算NG
            for (const auto& det : detections) {
                if (!passesThreshold(det.classId, det.bbox)) continue;

                result.detectionClassIds.push_back(det.classId);
                result.detectionBoxes.push_back(det.bbox);

                QString className = m_aiAlgo->getClassName(det.classId);
                if (!result.defectTypes.contains(className)) {
                    result.defectTypes.append(className);
                }
            }
            // 只有通过阈值筛选的缺陷才算NG
            if (!result.detectionBoxes.empty()) {
                result.hasDefect = true;
            }
        }
    }

    // 综合结果
    result.defectCount = (result.tradProcessed && result.tradResult.hasDefect ? 1 : 0) +
                         (result.aiProcessed && !result.detectionBoxes.empty() ?
                          static_cast<int>(result.detectionBoxes.size()) : 0);
    result.algorithmTypeDisplay = AlgorithmResultTask::getAlgorithmTypeDisplay(
        result.tradProcessed, result.aiProcessed);
}

void AlgorithmThread::processCamera3(const ImageTask& task, AlgorithmResultTask& result) {
    // Camera3 只使用AI算法
    if (!m_aiAlgo || !m_aiAlgo->isInitialized()) {
        Logger::instance().warn("Camera3: AI algorithm not initialized");
        return;
    }

    // 执行AI算法
    result.aiResult = m_aiAlgo->process(task.image);
    result.aiProcessed = true;

    if (result.aiResult.success) {
        auto detections = m_aiAlgo->getDetections();
        result.algorithmTypeDisplay = "AI";

        // 阈值筛选：只有通过阈值的缺陷才算NG
        for (const auto& det : detections) {
            if (!passesThreshold(det.classId, det.bbox)) continue;

            result.detectionClassIds.push_back(det.classId);
            result.detectionBoxes.push_back(det.bbox);

            QString className = m_aiAlgo->getClassName(det.classId);
            if (!result.defectTypes.contains(className)) {
                result.defectTypes.append(className);
            }
        }
        // 只有通过阈值筛选的缺陷才算NG
        result.hasDefect = !result.detectionBoxes.empty();
        result.defectCount = static_cast<int>(result.detectionBoxes.size());
    }
}

void AlgorithmThread::sendPLCResult(int cameraId, bool hasDefect) {
    // ===== IO发送方式：单次发送 =====
    if (!m_camera) {
        Logger::instance().error("IO: m_camera is NULL for cameraId=" + std::to_string(cameraId));
        return;
    }

    if (hasDefect) {
        m_camera->OutPutResult_NG();
        Logger::instance().debug("IO: cameraId=" + std::to_string(cameraId) + ", result=NG");
    } else {
        m_camera->OutPutResult_OK();
        Logger::instance().debug("IO: cameraId=" + std::to_string(cameraId) + ", result=OK");
    }

    // ===== 循环发送方式（已禁用） =====
    //const int REPEAT_COUNT = 5;
    //const int DELAY_MS = 200;
    //for (int i = 0; i < REPEAT_COUNT; i++) {
    //    if (hasDefect) {
    //        m_camera->OutPutResult_NG();
    //    } else {
    //        m_camera->OutPutResult_OK();
    //    }
    //    Logger::instance().info("IO: cameraId=" + std::to_string(cameraId) +
    //                           ", send_count=" + std::to_string(i + 1) + "/" + std::to_string(REPEAT_COUNT));
    //    if (i < REPEAT_COUNT - 1) {
    //        std::this_thread::sleep_for(std::chrono::milliseconds(DELAY_MS));
    //    }
    //}

    // ===== PLC发送方式（已禁用） =====
    //if (!m_plc) return;
    //int ccdId;
    //if (cameraId == 3) {
    //    ccdId = 4;
    //} else {
    //    ccdId = cameraId;
    //}
    //DetectionResult plcResult = hasDefect ? DetectionResult::NG : DetectionResult::OK;
    //m_plc->set_ccd_result(ccdId, plcResult);
}

void AlgorithmThread::sendPLCPinSignal(bool hasDefect, bool quepinDefect) {
    if (!m_plc) return;

    DetectionResult result = quepinDefect ? DetectionResult::NG : DetectionResult::OK;
    m_plc->set_ccd_result(3, result);
    Logger::instance().debug(QString("PLC Pin: quepin=%1, sent to D703 (ccdId=3)")
        .arg(quepinDefect ? "NG" : "OK").toStdString());
}

bool AlgorithmThread::checkQuepinDefect(const AlgorithmResultTask& result) {
    // AI算法检测到quepin类缺陷
    if (result.aiProcessed && m_aiAlgo) {
        for (int classId : result.detectionClassIds) {
            QString className = m_aiAlgo->getClassName(classId);
            if (className.toLower().contains("quepin")) {
                return true;
            }
        }
    }

    return false;
}
