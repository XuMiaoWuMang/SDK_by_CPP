#pragma once
#include <ai_cpp_sdk/ChatSDK.hpp>
#include <httplib.h>
#include <memory>
namespace chat_server {
class ServerConfig {
  public:
    std::string server_ip = "0.0.0.0";
    int server_port = 8080;

    std::string deepseek_model_name = "deepseek-v4-flash";
    std::string deepseek_api_key;
    std::string gemini_model_name = "gemini-3.5-flash";
    std::string gemini_api_key;
    std::string openai_model_name = "gpt-3.5-turbo";
    std::string openai_api_key;

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