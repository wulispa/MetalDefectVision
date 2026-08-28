#include "PLC_Interface.h"
#include "Logger.h"
#include <errno.h>
#include <chrono>
#include <algorithm>
#include <map>

#pragma comment(lib, "Ws2_32.lib")

PLCInterface::PLCInterface(const std::string& ip, int port, int slave_id)
    : ip_(ip), port_(port), slave_id_(slave_id), ctx_(nullptr),
      connected_flag_(false), stop_threads_(false), heartbeat_val_(0),
      log_counter_(0), last_log_time_(std::chrono::steady_clock::now())
{
}

PLCInterface::~PLCInterface() {
    stop();
}

bool PLCInterface::start() {
    stop_threads_ = false;
    if (monitor_thread_.joinable()) return true; // 防止重复启动

    monitor_thread_ = std::thread(&PLCInterface::monitor_loop, this);
    return true;
}

void PLCInterface::stop() {
    stop_threads_ = true;
    queue_cv_.notify_one();  // 唤醒可能在等待的线程

    if (monitor_thread_.joinable()) {
        monitor_thread_.join();
    }

    std::lock_guard<std::mutex> lock(modbus_mtx_);
    cleanup_ctx();
}

bool PLCInterface::is_connected() const {
    return connected_flag_.load();
}

bool PLCInterface::try_connect() {
    std::lock_guard<std::mutex> lock(modbus_mtx_);

    cleanup_ctx();

    ctx_ = modbus_new_tcp(ip_.c_str(), port_);
    if (!ctx_) return false;

    modbus_set_slave(ctx_, slave_id_);

    // 响应超时200ms（工业PLC响应快）
    struct timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = 200000;
    modbus_set_response_timeout(ctx_, timeout.tv_sec, timeout.tv_usec);

    if (modbus_connect(ctx_) == -1) {
        int wsa_err = WSAGetLastError();

        // 日志降频：每5秒打印一次
        if (should_log()) {
            Logger::instance().error(
                std::string("[PLC] connect failed: ") +
                modbus_strerror(errno) +
                " | WSA error=" + std::to_string(wsa_err) +
                " | retry_count=" + std::to_string(log_counter_.load())
            );
        }
        log_counter_++;

        modbus_free(ctx_);
        ctx_ = nullptr;
        return false;
    }

    log_counter_ = 0;
    connected_flag_ = true;
    return true;
}

void PLCInterface::cleanup_ctx() {
    if (ctx_) {
        modbus_close(ctx_);
        modbus_free(ctx_);
        ctx_ = nullptr;
    }
    connected_flag_ = false;
}

bool PLCInterface::should_log() {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_log_time_).count();
    if (elapsed >= 5) {
        last_log_time_ = now;
        return true;
    }
    return false;
}

// ===== 核心：非阻塞队列写入 =====
bool PLCInterface::set_ccd_result(int ccd_id, DetectionResult result) {
    int address = 700 + ccd_id;
    uint16_t val = static_cast<uint16_t>(result);

    // 每次都入队，PLC需要每个工件都收到信号
    {
        std::lock_guard<std::mutex> lk(queue_mtx_);
        write_queue_.push({address, val});
    }
    queue_cv_.notify_one();  // 唤醒PLC线程立即发送

    return true;
}

// ===== PLC主循环 =====
void PLCInterface::monitor_loop() {
    auto last_heartbeat = std::chrono::steady_clock::now();

    while (!stop_threads_) {
        // 1. 断线重连
        if (!connected_flag_) {
            if (!try_connect()) {
                // 等待1秒或被stop唤醒
                std::unique_lock<std::mutex> lk(queue_mtx_);
                queue_cv_.wait_for(lk, std::chrono::seconds(1), [this] {
                    return stop_threads_.load();
                });
                continue;
            }
            Logger::instance().info("[PLC] Connected to " + ip_ + ":" + std::to_string(port_));
            last_heartbeat = std::chrono::steady_clock::now();
        }

        // 2. 处理写入队列（优先级最高，立刻发）
        bool had_business = process_write_queue();

        // 3. 心跳：固定500ms间隔，不受业务影响
        auto now = std::chrono::steady_clock::now();
        if (now - last_heartbeat >= std::chrono::milliseconds(500)) {
            do_heartbeat();
            last_heartbeat = now;
        }

        // 4. 等待：全部用 cv.wait_for，notify 一来立刻唤醒
        //    有业务：等20ms节拍（但notify立刻打断）
        //    空闲：等500ms（但notify立刻打断）
        auto wait_timeout = had_business
            ? std::chrono::milliseconds(20)
            : std::chrono::milliseconds(500);

        {
            std::unique_lock<std::mutex> lk(queue_mtx_);
            queue_cv_.wait_for(lk, wait_timeout, [this] {
                return !write_queue_.empty() || stop_threads_.load();
            });
        }
    }
}

bool PLCInterface::process_write_queue() {
    // 取出所有待发送任务
    std::queue<PLCWriteTask> tasks;
    {
        std::lock_guard<std::mutex> lk(queue_mtx_);
        tasks.swap(write_queue_);
    }

    if (tasks.empty()) return false;

    // 按地址合并：同一地址只保留最后一条
    std::map<int, uint16_t> merged;
    while (!tasks.empty()) {
        auto& t = tasks.front();
        merged[t.address] = t.value;  // 同地址后者覆盖前者
        tasks.pop();
    }

    // 批量发送
    flush_batch_writes(merged);
    return true;
}

void PLCInterface::flush_batch_writes(const std::map<int, uint16_t>& writes) {
    if (writes.empty()) return;

    std::lock_guard<std::mutex> lock(modbus_mtx_);
    if (!ctx_) return;

    // 查找连续地址段，批量写入
    auto it = writes.begin();
    while (it != writes.end()) {
        int start_addr = it->first;
        uint16_t regs[8];  // 最多一次写8个连续寄存器
        regs[0] = it->second;
        int count = 1;

        // 收集连续地址
        auto next = std::next(it);
        while (next != writes.end() && next->first == start_addr + count && count < 8) {
            regs[count] = next->second;
            count++;
            next++;
        }

        // 批量写（count=1时退化为单写）
        int rc = modbus_write_registers(ctx_, start_addr, count, regs);
        if (rc == -1) {
            Logger::instance().error("[PLC] Batch write failed at D" + std::to_string(start_addr));
            cleanup_ctx();
            return;
        }

        it = next;
    }
}

void PLCInterface::do_heartbeat() {
    std::lock_guard<std::mutex> lock(modbus_mtx_);
    if (!ctx_) return;

    heartbeat_val_ = (heartbeat_val_ == 0) ? 1 : 0;
    if (modbus_write_register(ctx_, 700, heartbeat_val_) == -1) {
        Logger::instance().error("[PLC] Heartbeat failed");
        cleanup_ctx();
    }
}
