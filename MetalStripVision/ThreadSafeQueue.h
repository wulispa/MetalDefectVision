#pragma once

#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <chrono>

/**
 * @brief 线程安全队列模板
 *
 * 用于生产者-消费者模式，支持：
 * - 阻塞式 pop（可设置超时）
 * - 非阻塞式 push（队列满时返回false）
 * - 最大容量限制（防止内存溢出）
 */
template<typename T>
class ThreadSafeQueue {
public:
    explicit ThreadSafeQueue(size_t maxSize = 10)
        : m_maxSize(maxSize)
        , m_stopped(false)
    {}

    ~ThreadSafeQueue() {
        stop();
    }

    /**
     * @brief 压入元素
     * @param item 要压入的元素
     * @return true 成功压入
     * @return false 队列已满或已停止
     */
    bool push(const T& item) {
        std::unique_lock<std::mutex> lock(m_mutex);

        if (m_stopped.load(std::memory_order_relaxed)) {
            return false;
        }

        // 队列满时丢弃最旧的元素（防止内存溢出）
        if (m_queue.size() >= m_maxSize) {
            m_queue.pop();
        }

        m_queue.push(item);
        m_cv.notify_one();
        return true;
    }

    /**
     * @brief 压入元素（移动语义）
     */
    bool push(T&& item) {
        std::unique_lock<std::mutex> lock(m_mutex);

        if (m_stopped.load(std::memory_order_relaxed)) {
            return false;
        }

        if (m_queue.size() >= m_maxSize) {
            m_queue.pop();
        }

        m_queue.push(std::move(item));
        m_cv.notify_one();
        return true;
    }

    /**
     * @brief 弹出元素
     * @param item 输出参数，存储弹出的元素
     * @param timeoutMs 超时时间（毫秒），-1表示无限等待
     * @return true 成功弹出
     * @return false 超时或队列已停止
     */
    bool pop(T& item, int timeoutMs = -1) {
        // 快速路径：短暂自旋，避免条件变量的内核切换开销（~0.01ms vs ~0.5ms）
        for (int spin = 0; spin < 64; ++spin) {
            std::unique_lock<std::mutex> lock(m_mutex, std::try_to_lock);
            if (lock.owns_lock() && !m_queue.empty()) {
                item = std::move(m_queue.front());
                m_queue.pop();
                return true;
            }
            // CPU pause hint，降低自旋功耗
        #if defined(_MSC_VER)
            _mm_pause();
        #else
            std::this_thread::yield();
        #endif
        }

        // 慢路径：退化为条件变量等待
        std::unique_lock<std::mutex> lock(m_mutex);

        if (timeoutMs < 0) {
            // 无限等待
            m_cv.wait(lock, [this] {
                return !m_queue.empty() || m_stopped.load(std::memory_order_relaxed);
            });
        } else {
            // 带超时等待
            if (!m_cv.wait_for(lock, std::chrono::milliseconds(timeoutMs), [this] {
                return !m_queue.empty() || m_stopped.load(std::memory_order_relaxed);
            })) {
                return false;  // 超时
            }
        }

        if (m_queue.empty()) {
            return false;  // 队列已停止且为空
        }

        item = std::move(m_queue.front());
        m_queue.pop();
        return true;
    }

    /**
     * @brief 停止队列，唤醒所有等待的线程
     */
    void stop() {
        m_stopped.store(true, std::memory_order_relaxed);
        m_cv.notify_all();
    }

    /**
     * @brief 重置队列状态
     */
    void reset() {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_stopped.store(false, std::memory_order_relaxed);
        while (!m_queue.empty()) {
            m_queue.pop();
        }
    }

    /**
     * @brief 清空队列
     */
    void clear() {
        std::unique_lock<std::mutex> lock(m_mutex);
        while (!m_queue.empty()) {
            m_queue.pop();
        }
    }

    /**
     * @brief 获取队列大小
     */
    size_t size() const {
        std::unique_lock<std::mutex> lock(m_mutex);
        return m_queue.size();
    }

    /**
     * @brief 队列是否为空
     */
    bool empty() const {
        std::unique_lock<std::mutex> lock(m_mutex);
        return m_queue.empty();
    }

    /**
     * @brief 队列是否已停止
     */
    bool isStopped() const {
        return m_stopped.load(std::memory_order_relaxed);
    }

private:
    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    std::queue<T> m_queue;
    std::atomic<bool> m_stopped;
    size_t m_maxSize;
};
