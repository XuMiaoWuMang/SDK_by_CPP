#include "ChatServer.hpp"

#include <ai_cpp_sdk/util/logger.hpp>
#include <gflags/gflags.h>
#include <spdlog/spdlog.h>

#include <atomic>
#include <cctype>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

// ===================== 版本号 =====================
static const char *kVersion = "1.0.0";

// ===================== 命令行参数定义 (gflags) =====================
// 服务器参数
DEFINE_string(server_ip, "0.0.0.0", "服务器绑定的IP地址");
DEFINE_int32(server_port, 8080, "服务器监听的端口号");
DEFINE_string(log_level, "INFO",
              "日志级别: TRACE / DEBUG / INFO / WARN / ERROR / FATAL");

// 模型通用参数
DEFINE_double(temperature, 0.7, "模型温度参数, 取值范围 [0, 2]");
DEFINE_int64(max_tokens, 2048, "模型生成的最大token数, 不能为负数");

// 云端模型参数 (API Key 不通过命令行提供, 直接从环境变量获取)
DEFINE_string(deepseek_model_name, "deepseek-v4-flash",
              "DeepSeek 云端模型名称");
DEFINE_string(gemini_model_name, "gemini-3.5-flash", "Gemini 云端模型名称");
DEFINE_string(openai_model_name, "gpt-3.5-turbo", "ChatGPT 云端模型名称");

// 本地 Ollama 模型参数
DEFINE_string(ollama_model_name, "deepseek-r1:1.5b", "本地 Ollama 模型名称");
DEFINE_string(ollama_endpoint, "http://127.0.0.1:11434",
              "本地 Ollama 服务端点地址");
DEFINE_string(ollama_model_desc,
              "本地部署 deepseek-r1:1.5b 模型, 采用专家混合架构, "
              "专注于深度理解与推理",
              "本地 Ollama 模型描述");

// 配置文件
DEFINE_string(config_file, "ChatServer.conf",
              "配置文件路径, 若不存在则自动生成默认配置文件");

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

// 去除字符串首尾空白字符
static std::string Trim(const std::string &s) {
    const size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) {
        return "";
    }
    const size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

// 按分隔符拆分字符串, 自动忽略空项, 返回各段首尾去空白后的结果
static std::vector<std::string> Split(const std::string &src, char delim) {
    std::vector<std::string> parts;
    std::string cur;
    for (const char c : src) {
        if (c == delim) {
            if (!cur.empty()) {
                parts.push_back(Trim(cur));
                cur.clear();
            }
        } else {
            cur += c;
        }
    }
    if (!cur.empty()) {
        parts.push_back(Trim(cur));
    }
    return parts;
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

// 检查命令行中是否设置了某个 flag (支持 --name 与 --name=value 两种形式)
static bool HasCmdLineFlag(int argc, char **argv, const std::string &name) {
    const std::string exact = "--" + name;
    const std::string prefix = exact + "=";
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == exact || arg.rfind(prefix, 0) == 0) {
            return true;
        }
    }
    return false;
}

// 获取命令行中 --name=value 形式 flag 的值, 未设置时返回空串
static std::string GetCmdLineFlagValue(int argc, char **argv,
                                       const std::string &name) {
    const std::string prefix = "--" + name + "=";
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg.rfind(prefix, 0) == 0) {
            return arg.substr(prefix.size());
        }
    }
    return "";
}

// 若配置文件不存在, 则生成一份默认配置文件
static void EnsureConfigFile(const std::string &path) {
    if (FileExists(path)) {
        return;
    }
    std::ofstream file(path);
    if (!file.is_open()) {
        std::cerr << "警告: 无法生成配置文件 " << path << std::endl;
        return;
    }
    file << "# AIChatServer 默认配置文件 (gflags flagfile 格式)" << std::endl;
    file << "# 参数可通过命令行或本配置文件提供, 命令行参数优先级更高"
         << std::endl;
    file << "# 云端模型 API Key 不在此配置, 请通过环境变量提供:" << std::endl;
    file << "#   DEEPSEEK_API_KEY  (DeepSeek)" << std::endl;
    file << "#   CHATGPT_API_KEY   (ChatGPT, 兼容 OPENAI_API_KEY)" << std::endl;
    file << "#   GEMINI_API_KEY    (Gemini)" << std::endl;
    file << std::endl;
    file << "--server_ip=" << FLAGS_server_ip << std::endl;
    file << "--server_port=" << FLAGS_server_port << std::endl;
    file << "--log_level=" << FLAGS_log_level << std::endl;
    file << "--temperature=" << FLAGS_temperature << std::endl;
    file << "--max_tokens=" << FLAGS_max_tokens << std::endl;
    file << "--deepseek_model_name=" << FLAGS_deepseek_model_name << std::endl;
    file << "--gemini_model_name=" << FLAGS_gemini_model_name << std::endl;
    file << "--openai_model_name=" << FLAGS_openai_model_name << std::endl;
    file << "--ollama_model_name=" << FLAGS_ollama_model_name << std::endl;
    file << "--ollama_endpoint=" << FLAGS_ollama_endpoint << std::endl;
    file << "--ollama_model_desc=" << FLAGS_ollama_model_desc << std::endl;
    file.close();
    std::cout << "已生成默认配置文件: " << path << std::endl;
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
    // 2. 最大token数不能为负数 (ServerConfig 中为 size_t, 需在转换前校验)
    if (FLAGS_max_tokens < 0) {
        errMsg = "最大token数 max_tokens 不能为负数, 当前值: " +
                 std::to_string(FLAGS_max_tokens);
        return false;
    }
    // 3. 云端模型 API Key 至少有一个不为空
    if (config.deepseek_api_key.empty() && config.gemini_api_key.empty() &&
        config.openai_api_key.empty()) {
        errMsg = "至少需要配置一个云端模型的 API Key, 请设置环境变量 "
                 "DEEPSEEK_API_KEY / CHATGPT_API_KEY / GEMINI_API_KEY";
        return false;
    }
    // 4. 服务器端口号必须在合法范围内
    if (config.server_port <= 0 || config.server_port > 65535) {
        errMsg = "服务器端口号 server_port 必须在 [1, 65535] 范围内, 当前值: " +
                 std::to_string(config.server_port);
        return false;
    }
    // 5. Ollama 配置参数不能为空
    for (const auto &ollama : config.ollama_configs) {
        if (ollama._modelName.empty()) {
            errMsg = "Ollama 模型名称不能为空, 请检查 --ollama_model_name 参数";
            return false;
        }
        if (ollama._endpoint.empty()) {
            errMsg =
                "Ollama 服务端点地址不能为空, 请检查 --ollama_endpoint 参数";
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
        << "参数选项说明:\n"
        << "  --server_ip=<ip>            服务器绑定的IP地址 (默认: 0.0.0.0)\n"
        << "  --server_port=<port>        服务器监听的端口号 (默认: 8080)\n"
        << "  --log_level=<level>         日志级别 (默认: INFO)\n"
        << "                              可选: TRACE / DEBUG / INFO / WARN / "
           "ERROR / FATAL\n"
        << "  --temperature=<value>       模型温度参数, 取值范围 [0, 2] (默认: "
           "0.7)\n"
        << "  --max_tokens=<value>        模型生成的最大token数, 不能为负数 "
           "(默认: 2048)\n"
        << "  --deepseek_model_name=<n>   DeepSeek 云端模型名称 (默认: "
           "deepseek-v4-flash)\n"
        << "  --gemini_model_name=<n>     Gemini 云端模型名称 (默认: "
           "gemini-3.5-flash)\n"
        << "  --openai_model_name=<n>     ChatGPT 云端模型名称 (默认: "
           "gpt-3.5-turbo)\n"
        << "  --ollama_model_name=<n>     本地 Ollama 模型名称, 多个以逗号分隔 "
           "(默认: deepseek-r1:1.5b)\n"
        << "  --ollama_endpoint=<url>     本地 Ollama 服务端点, 多个以逗号分隔 "
           "(默认: http://127.0.0.1:11434)\n"
        << "                              数量不足时自动使用默认端点\n"
        << "  --ollama_model_desc=<desc>  本地 Ollama 模型描述, 多个以分号分隔\n"
        << "                              数量不足时描述留空\n"
        << "  --config_file=<path>        配置文件路径 (默认: "
           "ChatServer.conf)\n"
        << "                              若不存在会自动生成默认配置文件\n"
        << "  -h, --help                  显示本帮助信息\n"
        << "  -v, --version               显示版本号\n"
        << "\n"
        << "环境变量:\n"
        << "  云端模型 API Key 通过环境变量提供, 不通过命令行参数:\n"
        << "    DEEPSEEK_API_KEY    DeepSeek 模型 API Key\n"
        << "    CHATGPT_API_KEY     ChatGPT 模型 API Key (兼容 "
           "OPENAI_API_KEY)\n"
        << "    GEMINI_API_KEY      Gemini 模型 API Key\n"
        << "\n"
        << "使用示例:\n"
        << "  " << program
        << "                                    # 使用默认配置启动\n"
        << "  " << program << " --server_ip=0.0.0.0 --server_port=8080\n"
        << "  " << program << " --temperature=0.8 --max_tokens=4096\n"
        << "  " << program
        << " --ollama_model_name=qwen2.5:7b "
           "--ollama_endpoint=http://127.0.0.1:11434\n"
        << "  " << program
        << " --ollama_model_name=deepseek-r1:1.5b,qwen2.5:7b "
           "--ollama_endpoint=http://127.0.0.1:11434\n"
        << "                               # 配置多个本地模型\n"
        << "  " << program
        << " --config_file=ChatServer.conf       # 从配置文件加载参数\n"
        << "  " << program
        << " -h / --help                         # 显示帮助\n"
        << "  " << program
        << " -v / --version                      # 显示版本号\n"
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
        << "说明: Gemini / ChatGPT 等云端模型的 endpoint 使用 SDK "
           "内置默认端点, "
           "如需自定义请扩展 AI_SDK_CPP\n"
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

    // 2. 确定配置文件路径并加载 (命令行参数优先级高于配置文件)
    //    若命令行未显式指定 --flagfile, 则将 --flagfile 插入 argv 最前面,
    //    使 gflags 先加载配置文件, 再被命令行参数覆盖
    std::string configFile = FLAGS_config_file;
    const std::string cmdConfigFile =
        GetCmdLineFlagValue(argc, argv, "config_file");
    if (!cmdConfigFile.empty()) {
        configFile = cmdConfigFile;
    }
    std::vector<std::string> argList;
    std::vector<char *> argvPtrs;
    if (!HasCmdLineFlag(argc, argv, "flagfile")) {
        // 配置文件不存在则自动生成默认配置
        EnsureConfigFile(configFile);
        if (FileExists(configFile)) {
            argList.push_back(argv[0]);
            argList.push_back("--flagfile=" + configFile);
            for (int i = 1; i < argc; ++i) {
                argList.push_back(argv[i]);
            }
            argvPtrs.reserve(argList.size() + 1);
            for (auto &arg : argList) {
                argvPtrs.push_back(arg.data());
            }
            argvPtrs.push_back(nullptr);
            argc = static_cast<int>(argList.size());
            argv = argvPtrs.data();
        }
    }

    // 3. 解析命令行参数 (参数可能来自命令行, 也可能来自配置文件)
    gflags::SetUsageMessage("AIChatServer - 基于 AI_SDK_CPP 的智能聊天服务器, "
                            "使用 --help 查看详细帮助");
    gflags::ParseCommandLineFlags(&argc, &argv, true);

    // 4. 构建服务器配置
    chat_server::ServerConfig config;
    config.server_ip = FLAGS_server_ip;
    config.server_port = FLAGS_server_port;
    config.temperature = FLAGS_temperature;
    config.max_tokens = static_cast<size_t>(FLAGS_max_tokens);

    // 云端模型 (API Key 从环境变量获取)
    config.deepseek_model_name = FLAGS_deepseek_model_name;
    config.deepseek_api_key = GetEnv("DEEPSEEK_API_KEY");
    config.gemini_model_name = FLAGS_gemini_model_name;
    config.gemini_api_key = GetEnv("GEMINI_API_KEY");
    config.openai_model_name = FLAGS_openai_model_name;
    config.openai_api_key = GetEnv("CHATGPT_API_KEY");
    if (config.openai_api_key.empty()) {
        config.openai_api_key = GetEnv("OPENAI_API_KEY");
    }

    // 本地 Ollama 模型 (支持配置多个, 默认: deepseek-r1:1.5b)
    //   模型名称与端点以逗号分隔, 描述以分号分隔(描述中可能包含逗号)
    //   例: --ollama_model_name=a,b --ollama_endpoint=http://x,http://y
    //       --ollama_model_desc=描述A;描述B
    //   endpoint/描述可少于模型数量, 缺失项使用默认值
    const std::vector<std::string> ollamaNames = Split(FLAGS_ollama_model_name, ',');
    const std::vector<std::string> ollamaEndpoints = Split(FLAGS_ollama_endpoint, ',');
    const std::vector<std::string> ollamaDescs = Split(FLAGS_ollama_model_desc, ';');
    for (size_t i = 0; i < ollamaNames.size(); ++i) {
        chat_sdk::OllamaConfig ollamaConfig;
        ollamaConfig._modelName = ollamaNames[i];
        ollamaConfig._endpoint =
            (i < ollamaEndpoints.size() && !ollamaEndpoints[i].empty())
                ? ollamaEndpoints[i]
                : "http://127.0.0.1:11434";
        ollamaConfig._modelDesc =
            (i < ollamaDescs.size()) ? ollamaDescs[i] : "";
        ollamaConfig._temperature = FLAGS_temperature;
        ollamaConfig._maxTokens = static_cast<int>(FLAGS_max_tokens);
        config.ollama_configs.push_back(ollamaConfig);
    }

    // 5. 配置参数安全检查
    std::string errMsg;
    if (!ValidateConfig(config, errMsg)) {
        std::cerr << "[FATAL] 配置参数检查失败: " << errMsg << std::endl;
        std::cerr << "请使用 " << argv[0] << " --help 查看参数说明"
                  << std::endl;
        return 1;
    }

    // 6. 初始化日志
    sdk_logger::Logger::initLogger("AIChatServer", "stdout",
                                   ParseLogLevel(FLAGS_log_level));

    // 7. 启动服务器
    INFO("AIChatServer 启动中, 版本: {}, 监听地址: {}:{}, 日志级别: {}",
         kVersion, config.server_ip, config.server_port, FLAGS_log_level);
    INFO("默认云端模型: {}, 默认本地模型: {} ({})", config.deepseek_model_name,
         FLAGS_ollama_model_name, FLAGS_ollama_endpoint);
    chat_server::ChatServer server(config);
    server.start();

    // 8. 注册信号处理, 阻塞等待退出信号
    std::signal(SIGINT, SignalHandler);
    std::signal(SIGTERM, SignalHandler);
    while (!g_stopFlag.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    INFO("收到退出信号, 正在停止服务器...");
    server.stop();
    return 0;
}
