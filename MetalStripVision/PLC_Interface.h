#ifndef PLC_INTERFACE_H
#define PLC_INTERFACE_H

#include <string>
#include <cstdint>
#include <thread>
#include <mutex>
#include <atomic>
#include <queue>
#include <condition_variable>
#include <map>
#include <modbus/modbus.h>

// 定义检测结果枚举 (确保在 cpp 中引用一致)
enum class DetectionResult : uint16_t {
    IDLE = 0,
    OK = 1,
    NG = 2
};

// PLC写入任务
struct PLCWriteTask {
    int address;        // 寄存器地址 (如 701)
    uint16_t value;     // 要写入的值
};

class PLCInterface {
public:
    PLCInterface(const std::string& ip, int port = 502, int slave_id = 1);
    ~PLCInterface();

    // 禁止拷贝
    PLCInterface(const PLCInterface&) = delete;
    PLCInterface& operator=(const PLCInterface&) = delete;

    bool start();
    void stop();
    bool is_connected() const;

    // 写入结果到 D701-D704 (对应 ccd_id 1-4)
    // 改为队列模式：非阻塞，立即返回
    bool set_ccd_result(int ccd_id, DetectionResult result);

private:
    bool try_connect();
    void cleanup_ctx();              // 内部清理，不带锁
    void monitor_loop();             // PLC主循环（心跳+队列发送）
    bool process_write_queue();      // 批量处理队列中的写入任务，返回是否有业务
    void do_heartbeat();             // 发送心跳
    void flush_batch_writes(const std::map<int, uint16_t>& writes);

private:
    std::string ip_;
    int port_;
    int slave_id_;

    modbus_t* ctx_;
    std::mutex modbus_mtx_;          // 仅保护 ctx_ 的连接/断开操作

    std::atomic<bool> connected_flag_;
    std::atomic<bool> stop_threads_;

    std::thread monitor_thread_;
    uint16_t heartbeat_val_;

    // === 写入队列（核心改造） ===
    std::mutex queue_mtx_;
    std::condition_variable queue_cv_;
    std::queue<PLCWriteTask> write_queue_;

    // === 日志降频 ===
    std::atomic<uint64_t> log_counter_;      // 连接失败计数
    std::chrono::steady_clock::time_point last_log_time_;
    bool should_log();                        // 每5秒允许打印一次
};

#endif // PLC_INTERFACE_H
