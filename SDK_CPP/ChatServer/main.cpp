#include "ChatServer.hpp"

#include <ai_cpp_sdk/util/logger.hpp>
#include <gflags/gflags.h>
#include <jsoncpp/json/reader.h>
#include <jsoncpp/json/value.h>
#include <jsoncpp/json/writer.h>
#include <spdlog/spdlog.h>

#include <atomic>
#include <cctype>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits.h>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

// ===================== 版本号 =====================
static const char *kVersion = "1.0.0";

// ===================== 命令行标量参数 (gflags) =====================
// 参数优先级: 命令行(显式设置) > JSON 配置文件 > 内置默认值
// 哨兵默认值(-1/空串)用于区分"命令行是否显式设置", 未设置时以配置文件/默认值为准
DEFINE_string(server_ip, "", "服务器绑定的IP地址");
DEFINE_int32(server_port, -1, "服务器监听的端口号");
DEFINE_string(log_level, "",
              "日志级别: TRACE / DEBUG / INFO / WARN / ERROR / FATAL");
DEFINE_double(temperature, -1.0, "模型温度参数, 取值范围 [0, 2]");
DEFINE_int64(max_tokens, -1, "模型生成的最大token数, 不能为负数");
DEFINE_string(log_file, "",
              "日志输出: stdout(控制台) 或日志目录/文件路径");
DEFINE_string(config_file, "",
              "配置文件路径 (默认: 可执行文件上一级目录下的 env.conf)");

// ===================== 全局状态 =====================
static std::atomic<bool> g_stopFlag{false};

// 信号处理函数
static void SignalHandler(int /*signal*/) { g_stopFlag.store(true); }

// 获取环境变量, 不存在时返回空串
static std::string GetEnv(const char *name) {
    const char *value = std::getenv(name);
    return value == nullptr ? std::string() : std::string(value);
}

// 字符串转大写
static std::string ToUpper(std::string str) {
    std::transform(str.begin(), str.end(), str.begin(),
                   [](unsigned char c) { return std::toupper(c); });
    return str;
}

// 解析日志级别, 非法值回退到 INFO
static spdlog::level::level_enum ParseLogLevel(const std::string &level) {
    const std::string upper = ToUpper(level);
    if (upper == "TRACE") {
        return spdlog::level::trace;
    }
    if (upper == "DEBUG") {
        return spdlog::level::debug;
    }
    if (upper == "WARN" || upper == "WARNING") {
        return spdlog::level::warn;
    }
    if (upper == "ERROR") {
        return spdlog::level::err;
    }
    if (upper == "FATAL" || upper == "CRITICAL") {
        return spdlog::level::critical;
    }
    return spdlog::level::info;
}

// 检查文件是否存在
static bool FileExists(const std::string &path) {
    std::ifstream file(path);
    return file.good();
}

// 获取可执行文件所在目录的上一级目录 (通过 /proc/self/exe 解析真实路径,
// 与启动时的工作目录无关, 保证从任意目录启动都能定位到 ChatServer 目录)
static std::string GetExecutableParentDir() {
    char buf[PATH_MAX];
    const ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0) {
        return ".";
    }
    buf[n] = '\0';
    const std::string exe(buf);
    const size_t pos = exe.find_last_of('/');
    const std::string dir =
        (pos == std::string::npos) ? "." : exe.substr(0, pos);
    const size_t parent = dir.find_last_of('/');
    return (parent == std::string::npos) ? "." : dir.substr(0, parent);
}

// 默认配置文件路径: <可执行文件上一级目录>/env.conf
static std::string GetDefaultConfigPath() {
    return GetExecutableParentDir() + "/env.conf";
}

// 判断命令行是否显式设置了某 flag (排除 gflags 内置默认值)
static bool FlagWasSet(const std::string &name) {
    gflags::CommandLineFlagInfo info;
    return gflags::GetCommandLineFlagInfo(name.c_str(), &info) &&
           !info.is_default;
}

// 解析日志输出路径 (锚定可执行文件上一级目录):
//   "stdout" 或空 -> 控制台; 以 ".log" 结尾 -> 视为完整文件路径;
//   其余 -> 视为目录, 自动拼接默认日志文件名 AIChatServer.log;
//   相对路径均基于 baseDir (exe 上一级), 绝对路径直接使用
static std::string ResolveLogPath(const std::string &logFile,
                                  const std::string &baseDir) {
    if (logFile.empty() || logFile == "stdout") {
        return "stdout";
    }
    std::string path = logFile;
    if (path[0] != '/') {
        path = baseDir + "/" + path;
    }
    if (path.size() >= 4 &&
        path.compare(path.size() - 4, 4, ".log") == 0) {
        return path;
    }
    return path + "/AIChatServer.log";
}

// 解析数据目录: 相对路径锚定可执行文件上一级, 绝对路径直接使用
static std::string ResolveDataDir(const std::string &dataDir,
                                  const std::string &baseDir) {
    if (dataDir.empty() || dataDir[0] == '/') {
        return dataDir.empty() ? baseDir : dataDir;
    }
    return baseDir + "/" + dataDir;
}

// 加载 JSON 配置文件
static bool LoadJsonConfig(const std::string &path, Json::Value &root,
                           std::string &errMsg) {
    std::ifstream file(path);
    if (!file.is_open()) {
        errMsg = "无法打开配置文件: " + path;
        return false;
    }
    Json::CharReaderBuilder builder;
    std::string errs;
    if (!Json::parseFromStream(builder, file, &root, &errs)) {
        errMsg = "配置文件不是合法的 JSON (第 " + std::to_string(1) +
                 " 处错误): " + errs;
        return false;
    }
    return true;
}

// 从 JSON 配置文件填充 ServerConfig (不含命令行覆盖)
static bool ParseJsonConfig(const Json::Value &root,
                            chat_server::ServerConfig &config,
                            std::string &logLevel, std::string &logFile,
                            std::string &errMsg) {
    // server 节: ip / port / log_level / log_file
    const Json::Value &serverObj =
        root.get("server", Json::Value(Json::objectValue));
    if (!serverObj.isObject()) {
        errMsg = "配置项 \"server\" 必须是对象";
        return false;
    }
    config.server_ip = serverObj.get("ip", "0.0.0.0").asString();
    config.server_port = serverObj.get("port", 8080).asInt();
    logLevel = serverObj.get("log_level", "INFO").asString();
    logFile = serverObj.get("log_file", "stdout").asString();
    config.data_dir = serverObj.get("data_dir", "data").asString();

    // 全局默认模型参数
    const double globalTemp = root.get("temperature", 0.7).asDouble();
    const int64_t globalMax = root.get("max_tokens", 2048).asInt64();
    if (globalMax < 0) {
        errMsg = "全局 max_tokens 不能为负数, 当前值: " +
                 std::to_string(globalMax);
        return false;
    }
    config.temperature = globalTemp;
    config.max_tokens = static_cast<size_t>(globalMax);

    // 模型列表
    const Json::Value &models =
        root.get("models", Json::Value(Json::arrayValue));
    if (!models.isArray()) {
        errMsg = "配置项 \"models\" 必须是数组";
        return false;
    }
    for (const auto &m : models) {
        if (!m.isObject()) {
            continue;
        }
        const std::string provider = m.get("provider", "").asString();
        const std::string name = m.get("name", "").asString();
        if (name.empty()) {
            errMsg = "模型条目的 \"name\" 不能为空";
            return false;
        }
        // 每模型可覆盖全局温度与 max_tokens
        double temp = m.isMember("temperature")
                          ? m["temperature"].asDouble()
                          : config.temperature;
        int64_t maxTok = m.isMember("max_tokens")
                             ? m["max_tokens"].asInt64()
                             : static_cast<int64_t>(config.max_tokens);
        if (maxTok < 0) {
            errMsg = "模型 " + name + " 的 max_tokens 不能为负数";
            return false;
        }

        if (provider == "ollama") {
            // 本地模型: 必须提供 endpoint
            chat_sdk::OllamaConfig ollamaConfig;
            ollamaConfig._modelName = name;
            ollamaConfig._endpoint = m.get("endpoint", "").asString();
            ollamaConfig._modelDesc = m.get("desc", "").asString();
            ollamaConfig._temperature = temp;
            ollamaConfig._maxTokens = static_cast<int>(maxTok);
            config.ollama_configs.push_back(ollamaConfig);
        } else if (provider == "deepseek" || provider == "gemini" ||
                   provider == "chatgpt" || provider == "openai") {
            // 云端模型: API Key 规则为条目 api_key 优先, 为空回退环境变量 (SDK 处理)
            chat_sdk::RemoteConfig remoteConfig;
            remoteConfig._provider =
                (provider == "openai") ? "chatgpt" : provider;
            remoteConfig._modelName = name;
            remoteConfig._temperature = temp;
            remoteConfig._maxTokens = static_cast<int>(maxTok);
            remoteConfig._modelDesc = m.get("desc", "").asString();
            remoteConfig._apiKey = m.get("api_key", "").asString();
            if (remoteConfig._apiKey.empty()) {
                std::cerr << "[WARN] 云端模型 " << name
                          << " 未配置 api_key, 将回退环境变量 (若未设置则不可用)"
                          << std::endl;
            }
            config.remote_configs.push_back(remoteConfig);
        } else {
            errMsg = "未知的模型 provider: \"" + provider +
                     "\", 可选: ollama / deepseek / gemini / chatgpt";
            return false;
        }
    }
    return true;
}

// 对配置参数进行安全检查
static bool ValidateConfig(const chat_server::ServerConfig &config,
                           std::string &errMsg) {
    // 1. 温度值必须在 [0, 2] 范围内
    if (config.temperature < 0.0 || config.temperature > 2.0) {
        errMsg = "温度参数 temperature 必须在 [0, 2] 范围内, 当前值: " +
                 std::to_string(config.temperature);
        return false;
    }
    // 2. 服务器端口号必须在合法范围内
    if (config.server_port <= 0 || config.server_port > 65535) {
        errMsg = "服务器端口号 server_port 必须在 [1, 65535] 范围内, 当前值: " +
                 std::to_string(config.server_port);
        return false;
    }
    // 3. 至少注册了一个模型 (本地或云端)
    if (config.ollama_configs.empty() && config.remote_configs.empty()) {
        errMsg = "未配置任何模型: 请在配置文件的 models 中添加模型";
        return false;
    }
    // 4. 云端模型必须有 API Key (配置条目 api_key 或对应环境变量)
    for (const auto &remote : config.remote_configs) {
        bool hasKey = !remote._apiKey.empty();
        if (!hasKey) {
            if (remote._provider == "gemini") {
                hasKey = !GetEnv("GEMINI_API_KEY").empty();
            } else if (remote._provider == "chatgpt") {
                hasKey = !GetEnv("CHATGPT_API_KEY").empty() ||
                         !GetEnv("OPENAI_API_KEY").empty();
            } else {
                hasKey = !GetEnv("DEEPSEEK_API_KEY").empty();
            }
        }
        if (!hasKey) {
            errMsg = "云端模型 " + remote._modelName +
                     " 未配置 API Key, 请设置 api_key 或对应环境变量";
            return false;
        }
    }
    // 5. Ollama 配置参数不能为空
    for (const auto &ollama : config.ollama_configs) {
        if (ollama._modelName.empty()) {
            errMsg = "Ollama 模型名称不能为空";
            return false;
        }
        if (ollama._endpoint.empty()) {
            errMsg = "Ollama 服务端点不能为空, 请检查模型 " +
                     ollama._modelName + " 的 endpoint 配置";
            return false;
        }
    }
    return true;
}

// 打印版本号
static void PrintVersion() {
    std::cout << "AIChatServer version " << kVersion << std::endl;
}

// 打印使用帮助
static void PrintHelp(const char *program) {
    std::cout
        << "============================================================\n"
        << " AIChatServer - 基于 AI_SDK_CPP 的智能聊天 HTTP 服务器\n"
        << "============================================================\n"
        << "\n"
        << "用法: " << program << " [选项]\n"
        << "\n"
        << "参数选项说明 (优先级: 命令行 > 配置文件 > 默认值):\n"
        << "  --server_ip=<ip>            服务器绑定的IP地址\n"
        << "  --server_port=<port>        服务器监听的端口号\n"
        << "  --log_level=<level>         日志级别: TRACE / DEBUG / INFO / "
           "WARN / ERROR / FATAL\n"
        << "  --temperature=<value>       全局模型温度, 取值范围 [0, 2]\n"
        << "  --max_tokens=<value>        全局最大token数, 不能为负数\n"
        << "  --log_file=<path>           日志输出: stdout(控制台) / 日志目录 "
           "/ 文件路径\n"
        << "  --config_file=<path>        配置文件路径\n"
        << "                              默认: 可执行文件上一级目录下的 "
           "env.conf\n"
        << "  -h, --help                  显示本帮助信息\n"
        << "  -v, --version               显示版本号\n"
        << "\n"
        << "配置文件 (env.conf, JSON 格式):\n"
        << "  服务器参数: server.ip / server.port / server.log_level /\n"
        << "              server.log_file (stdout 或日志目录/文件路径) /\n"
        << "              server.data_dir (数据目录, 默认 data)\n"
        << "  日志与数据目录: 相对路径锚定可执行文件上一级, 绝对路径直接用\n"
        << "  字段级配置编写说明: 见同目录《配置说明.md》\n"
        << "  全局默认模型参数: temperature / max_tokens (模型条目可覆盖)\n"
        << "  模型列表: models 数组, 每条目字段:\n"
        << "    provider   模型提供方: ollama(本地) / deepseek / gemini / "
           "chatgpt\n"
        << "    name       模型名称\n"
        << "    endpoint   仅 ollama 需要, 本地服务端点\n"
        << "    api_key    可选, 云端模型 API Key, 覆盖同名环境变量\n"
        << "    desc       可选, 模型描述\n"
        << "    temperature / max_tokens  可选, 覆盖全局值\n"
        << "  未找到配置文件时使用内置默认值启动; 配置模板请参考 "
           "env.conf.example\n"
        << "\n"
        << "环境变量 (云端模型 API Key, 配置文件 api_key 可覆盖):\n"
        << "    DEEPSEEK_API_KEY    DeepSeek 模型 API Key\n"
        << "    CHATGPT_API_KEY     ChatGPT 模型 API Key (兼容 "
           "OPENAI_API_KEY)\n"
        << "    GEMINI_API_KEY      Gemini 模型 API Key\n"
        << "\n"
        << "使用示例:\n"
        << "  " << program << "                                    # 使用配置文件启动\n"
        << "  " << program << " --server_port=9090                 # 命令行覆盖端口\n"
        << "  " << program
        << " --config_file=/path/to/env.conf  # 指定配置文件\n"
        << "  " << program << " -h / --help                         # 显示帮助\n"
        << "  " << program << " -v / --version                      # 显示版本号\n"
        << "\n"
        << "HTTP 接口说明 (默认地址 http://<server_ip>:<server_port>):\n"
        << "  POST   /api/session                      创建会话, body: "
           "{\"model\": \"deepseek-v4-flash\"}\n"
        << "  DELETE /api/session/<session_id>         删除会话\n"
        << "  GET    /api/sessions                     获取会话列表\n"
        << "  GET    /api/session/<session_id>/history 获取指定会话的历史消息\n"
        << "  GET    /api/models                       获取可用模型列表\n"
        << "  POST   /api/message                      全量发送消息, body: "
           "{\"sessionId\": \"xx\", \"message\": \"你好\"}\n"
        << "  POST   /api/message/async                流式发送消息 (SSE), "
           "body 同上\n"
        << "\n"
        << "接口统一返回 JSON, 字段: success(是否成功), message(提示信息), "
           "data(业务数据)\n"
        << "流式接口 /api/message/async 返回格式: data: <增量内容>\\n\\n, "
           "结束标志: data: [DONE]\\n\\n\n"
        << "\n"
        << "版本: " << kVersion << std::endl;
}

int main(int argc, char **argv) {
    // 1. 优先处理帮助/版本参数 (需在 gflags 解析之前, 否则内置 --help
    // 会直接退出)
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            PrintHelp(argv[0]);
            return 0;
        }
        if (arg == "-v" || arg == "--version") {
            PrintVersion();
            return 0;
        }
    }

    // 2. 解析命令行标量参数
    gflags::SetUsageMessage("AIChatServer - 基于 AI_SDK_CPP 的智能聊天服务器, "
                            "使用 --help 查看详细帮助");
    gflags::ParseCommandLineFlags(&argc, &argv, true);

    // 3. 定位配置文件: 命令行 --config_file 优先, 否则默认在可执行文件
    //    上一级目录下
    const std::string configPath =
        FlagWasSet("config_file") ? FLAGS_config_file : GetDefaultConfigPath();

    // 4. 构建服务器配置: 内置默认值 -> JSON 配置文件 -> 命令行覆盖
    chat_server::ServerConfig config;
    std::string logLevel;
    std::string logFile = "stdout";
    bool configLoaded = false;
    if (FileExists(configPath)) {
        Json::Value root;
        std::string errMsg;
        if (!LoadJsonConfig(configPath, root, errMsg)) {
            std::cerr << "[FATAL] " << errMsg << std::endl;
            std::cerr << "[FATAL] 请参考配置模板: " << configPath << ".example"
                      << std::endl;
            return 1;
        }
        if (!ParseJsonConfig(root, config, logLevel, logFile, errMsg)) {
            std::cerr << "[FATAL] " << errMsg << std::endl;
            std::cerr << "[FATAL] 请参考配置模板: " << configPath << ".example"
                      << std::endl;
            return 1;
        }
        configLoaded = true;
    } else {
        // 配置文件不存在: 使用内置默认值启动
        config.server_ip = "0.0.0.0";
        config.server_port = 8080;
        config.temperature = 0.7;
        config.max_tokens = 2048;
        logLevel = "INFO";

        // 默认注册: 云端 deepseek-v4-flash + 本地 ollama deepseek-r1:1.5b
        chat_sdk::RemoteConfig deepseekConfig;
        deepseekConfig._provider = "deepseek";
        deepseekConfig._modelName = "deepseek-v4-flash";
        deepseekConfig._temperature = config.temperature;
        deepseekConfig._maxTokens = static_cast<int>(config.max_tokens);
        config.remote_configs.push_back(deepseekConfig);

        chat_sdk::OllamaConfig ollamaConfig;
        ollamaConfig._modelName = "deepseek-r1:1.5b";
        ollamaConfig._endpoint = "http://127.0.0.1:11434";
        ollamaConfig._modelDesc =
            "本地部署 deepseek-r1:1.5b 模型, 专注于深度理解与推理";
        ollamaConfig._temperature = config.temperature;
        ollamaConfig._maxTokens = static_cast<int>(config.max_tokens);
        config.ollama_configs.push_back(ollamaConfig);

        std::cerr << "[WARN] 未找到配置文件 " << configPath
                  << ", 已使用内置默认配置启动" << std::endl;
        std::cerr << "[WARN] 配置模板请参考: " << configPath << ".example"
                  << std::endl;
    }

    // 5. 命令行显式设置的参数覆盖配置文件
    if (FlagWasSet("server_ip")) {
        config.server_ip = FLAGS_server_ip;
    }
    if (FlagWasSet("server_port")) {
        config.server_port = FLAGS_server_port;
    }
    if (FlagWasSet("log_level")) {
        logLevel = FLAGS_log_level;
    }
    if (FlagWasSet("temperature")) {
        config.temperature = FLAGS_temperature;
    }
    if (FlagWasSet("max_tokens")) {
        if (FLAGS_max_tokens < 0) {
            std::cerr << "[FATAL] max_tokens 不能为负数, 当前值: "
                      << FLAGS_max_tokens << std::endl;
            return 1;
        }
        config.max_tokens = static_cast<size_t>(FLAGS_max_tokens);
    }
    if (FlagWasSet("log_file")) {
        logFile = FLAGS_log_file;
    }

    // 6. 配置参数安全检查
    std::string errMsg;
    if (!ValidateConfig(config, errMsg)) {
        std::cerr << "[FATAL] 配置参数检查失败: " << errMsg << std::endl;
        std::cerr << "请使用 " << argv[0] << " --help 查看参数说明"
                  << std::endl;
        return 1;
    }

    // 7. 初始化日志与数据目录: 相对路径锚定可执行文件上一级
    const std::string baseDir = GetExecutableParentDir();
    const std::string logPath = ResolveLogPath(logFile, baseDir);
    if (logPath != "stdout") {
        const std::filesystem::path parent =
            std::filesystem::path(logPath).parent_path();
        if (!parent.empty()) {
            std::error_code ec;
            std::filesystem::create_directories(parent, ec);
            if (ec) {
                std::cerr << "[WARN] 创建日志目录失败: " << ec.message()
                          << std::endl;
            }
        }
    }
    sdk_logger::Logger::initLogger("AIChatServer", logPath,
                                   ParseLogLevel(logLevel));

    // 数据目录: 确保存在, 并拼出数据库完整路径
    const std::string dataDir = ResolveDataDir(config.data_dir, baseDir);
    if (!dataDir.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(dataDir, ec);
        if (ec) {
            std::cerr << "[WARN] 创建数据目录失败: " << ec.message()
                      << std::endl;
        }
    }
    config.db_path = dataDir + "/chatDB.db";

    // 8. 启动服务器
    INFO("AIChatServer 启动中, 版本: {}, 监听地址: {}:{}, 日志级别: {}",
         kVersion, config.server_ip, config.server_port, logLevel);
    INFO("配置文件: {}, 已加载: {}", configPath, configLoaded);
    INFO("日志输出: {}", logPath);
    INFO("数据目录: {}, 数据库: {}", dataDir, config.db_path);
    INFO("云端模型数量: {}", config.remote_configs.size());
    for (const auto &remote : config.remote_configs) {
        INFO("  云端模型: {} (provider={}, key={})", remote._modelName,
             remote._provider,
             remote._apiKey.empty() ? "回退环境变量" : "配置提供");
    }
    INFO("本地模型数量: {}", config.ollama_configs.size());
    for (const auto &ollama : config.ollama_configs) {
        INFO("  本地模型: {} @ {}", ollama._modelName, ollama._endpoint);
    }
    chat_server::ChatServer server(config);
    server.start();

    // 9. 注册信号处理, 阻塞等待退出信号
    std::signal(SIGINT, SignalHandler);
    std::signal(SIGTERM, SignalHandler);
    while (!g_stopFlag.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    INFO("收到退出信号, 正在停止服务器...");
    server.stop();
    return 0;
}
