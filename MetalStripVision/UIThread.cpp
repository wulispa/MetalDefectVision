#include "UIThread.h"
#include "Logger.h"
#include <chrono>

UIThread::UIThread(ThreadSafeQueue<UIDisplayTask>& inputQueue)
    : m_inputQueue(inputQueue)
{
}

UIThread::~UIThread() {
    stop();
}

void UIThread::start() {
    if (m_running.load()) return;

    m_stop.store(false);
    m_running.store(true);

    m_thread = std::thread(&UIThread::run, this);
    Logger::instance().info("UIThread started");
}

void UIThread::stop() {
    m_stop.store(true);
    m_running.store(false);

    if (m_thread.joinable()) {
        m_thread.join();
    }

    Logger::instance().info("UIThread stopped");
}

void UIThread::run() {
    while (!m_stop.load()) {
        UIDisplayTask task;
        if (!m_inputQueue.pop(task, 50)) {
            continue;
        }

        emit imageUpdated(task.cameraId, task.annotatedImage, task.hasDefect, task.originalImage);
        emit defectStatusUpdated(task.cameraId, task.hasDefect, task.algorithmFailed);
    }
}
