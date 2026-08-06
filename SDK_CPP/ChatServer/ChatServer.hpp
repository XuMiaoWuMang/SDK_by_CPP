#pragma once
#include <ai_cpp_sdk/ChatSDK.hpp>
#include <httplib.h>
#include <memory>
namespace chat_server {
class ServerConfig {
  public:
    std::string server_ip = "0.0.0.0";
    int server_port = 8080;

    // 数据目录 (相对路径锚定可执行文件上一级) 与解析后的数据库完整路径
    std::string data_dir = "data";
    std::string db_path = "chatDB.db";

    // 云端模型 (deepseek / gemini / chatgpt), 每条目含 _provider 与 _apiKey
    std::vector<chat_sdk::RemoteConfig> remote_configs;
    // 本地 Ollama 模型
    std::vector<chat_sdk::OllamaConfig> ollama_configs;

    double temperature = 0.7; // 温度
    size_t max_tokens = 1024; // 最大令牌数
};

class ChatServer {
  public:
    ChatServer(const ServerConfig &config);
    ~ChatServer();

    void start();           // 启动服务器
    void stop();            // 停止服务器
    bool isRunning() const; // 是否正在运行
    void setHttpRouter();   // 设置HTTP路由

    // 构造错误响应
    std::string BuildResponse(const std::string &errMsg, bool success = false);
    // 处理创建会话请求
    void handleCreateSession(const httplib::Request &req,
                             httplib::Response &res);
    // 处理删除会话请求
    void handleDeleteSession(const httplib::Request &req,
                             httplib::Response &res);
    // 处理获取会话列表请求
    void handleGetSessionList(const httplib::Request &req,
                              httplib::Response &res);
    // 处理获取历史消息请求
    void handleGetHistoryMessages(const httplib::Request &req,
                                  httplib::Response &res);
    // 处理获取模型列表请求
    void handleGetModelList(const httplib::Request &req,
                            httplib::Response &res);
    // 处理全量发送消息请求
    void handleSendMessage(const httplib::Request &req, httplib::Response &res);
    // 处理增量发送消息请求
    void handleIncrementalSendMessage(const httplib::Request &req,
                                      httplib::Response &res);

  private:
    ServerConfig _config;
    std::atomic<bool> _isRunning = {false};
    std::shared_ptr<chat_sdk::ChatSDK> _chat_sdk; // 聊天SDK
    std::unique_ptr<httplib::Server> _chatServer; // 服务器实例
};
} // namespace chat_server