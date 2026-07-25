#include "../../include/util/logger.hpp"

namespace sdk_logger {

std::shared_ptr<spdlog::logger> sdk_logger::Logger::_logger = nullptr;
std::mutex sdk_logger::Logger::_mutex;
Logger::Logger() {}
// 初始化日志策略和文件路径
void Logger::initLogger(const std::string &logFileName,
                        const std::string &logFilePath,
                        spdlog::level::level_enum level) {
    if (nullptr == _logger) {
        std::lock_guard<std::mutex> lock(_mutex);
        if (nullptr == _logger) {
            // 设置全局刷新等级,当刷新等级 >= 日志等级时,刷新日志
            spdlog::flush_on(level);
            // 启动异步日志，将队列中的日志交给后台线程处理
            // 参数一: 队列大小
            // 参数二: 线程数
            spdlog::init_thread_pool(32768, 1);

            if ("stdout" == logFilePath) {
                // 创建一个带颜色的日志记录器
                _logger = spdlog::stdout_color_mt(logFileName);
            } else {
                // 创建一个日志记录器,并指定日志文件路径
                _logger = spdlog::basic_logger_mt<spdlog::async_factory>(
                    logFileName, logFilePath);
            }
            // 设置日志等级
            _logger->set_level(level);
            // 设置日志格式
            _logger->set_pattern("[%H:%M:%S.%e][%n][%l]%v");
        }
    }
}
std::shared_ptr<spdlog::logger> Logger::getLogger() { return _logger; }
} // namespace sdk_logger