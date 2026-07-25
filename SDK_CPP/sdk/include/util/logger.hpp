#pragma once
#include <mutex>
#include <spdlog/async.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>
namespace sdk_logger {
// 封装spdlog接口
class Logger {
  public:
    // 单例模式
    static std::shared_ptr<spdlog::logger> getLogger();
    static void initLogger(const std::string &logFileName,
                           const std::string &logFilePath,
                           spdlog::level::level_enum level);

  private:
    Logger();
    // 禁用拷贝构造函数和赋值
    Logger(Logger &) = delete;
    Logger &operator=(Logger &) = delete;

  private:
    static std::shared_ptr<spdlog::logger> _logger; // 日志记录器
    static std::mutex _mutex;                       // 互斥锁
};
// 使用宏调用日志接口, 格式化输出内容, [文件名:行号]format
// TRACE: 跟踪日志, 用于调试
// DBG: 调试日志, 用于调试
// INFO: 信息日志, 用于普通信息
// WARN: 警告日志, 用于警告信息
// ERR: 错误日志, 用于错误信息
// FATAL: 致命错误日志, 用于致命错误信息
#define TRACE(format, ...)                                                     \
    sdk_logger::Logger::getLogger()->trace(std::string("[{:>10s}:{:<4d}]") +   \
                                               format,                         \
                                           __FILE__, __LINE__, ##__VA_ARGS__)
#define DBG(format, ...)                                                       \
    sdk_logger::Logger::getLogger()->debug(std::string("[{:>10s}:{:<4d}]") +   \
                                               format,                         \
                                           __FILE__, __LINE__, ##__VA_ARGS__)
#define INFO(format, ...)                                                      \
    sdk_logger::Logger::getLogger()->info(std::string("[{:>10s}:{:<4d}]") +    \
                                              format,                          \
                                          __FILE__, __LINE__, ##__VA_ARGS__)
#define WARN(format, ...)                                                      \
    sdk_logger::Logger::getLogger()->warn(std::string("[{:>10s}:{:<4d}]") +    \
                                              format,                          \
                                          __FILE__, __LINE__, ##__VA_ARGS__)
#define ERR(format, ...)                                                       \
    sdk_logger::Logger::getLogger()->error(std::string("[{:>10s}:{:<4d}]") +   \
                                               format,                         \
                                           __FILE__, __LINE__, ##__VA_ARGS__)
#define FATAL(format, ...)                                                     \
    sdk_logger::Logger::getLogger()->fatal(std::string("[{:>10s}:{:<4d}]") +   \
                                               format,                         \
                                           __FILE__, __LINE__, ##__VA_ARGS__)

} // namespace sdk_logger