#pragma once
#include <ctime>
#include <string>
#include <vector>
namespace chat_sdk {

// 消息结构
class Message {
  public:
    Message(const std::string &role = "", const std::string &content = "")
        : _role(role), _sendTimeTimestamp(std::time(nullptr)),
          _content(content) {}

  public:
    std::string _messageId;         // 消息ID
    std::string _role;              // 角色，user或assistant
    std::time_t _sendTimeTimestamp; // 发送时间戳，单位为秒
    std::string _content;           // 消息内容
};

// 公共配置参数
struct Config {
    std::string _modelName;      // 模型名称
    double _temperature = 0.7;   // 温度参数
    int _maxTokens = 2048;       // 最大token数
    std::string _modelDesc;      // 模型描述
    virtual ~Config() = default; // 为了dynamic继承区分不同类型所配置的虚函数
};

// 远端模型需要api key
struct RemoteConfig : Config {
    std::string _apiKey;    // API key
    std::string _provider;  // 模型提供方: deepseek / gemini / chatgpt
    RemoteConfig() = default;
};

// 本地Ollama模型不需要api key
struct OllamaConfig : Config {
    std::string _endpoint; // 模型端点 (base url)
};
// LLM大语言模型信息结构
struct ModelInfo {
    std::string _modelName;    // 模型名称
    std::string _modelDesc;    // 模型描述
    std::string _provider;     // 模型提供方
    std::string _endpoint;     // 模型端点 (base url)
    bool _isAvailable = false; // 是否可用

    ModelInfo(std::string modelName = "", std::string modelDesc = "",
              std::string provider = "", std::string endpoint = "")
        : _modelName(modelName), _modelDesc(modelDesc), _provider(provider),
          _endpoint(endpoint) {}
};

// 会话信息结构
struct SessionInfo {
    std::string _sessionId;               // 会话ID
    std::string _modelName;               // 模型名称
    std::vector<Message> _messages;       // 会话消息列表
    std::time_t _createTimeTimestamp;     // 创建时间戳，单位为秒
    std::time_t _lastUpdateTimeTimestamp; // 最后更新时间戳，单位为秒

    SessionInfo(const std::string &_modelName) : _modelName(_modelName) {}
    SessionInfo() = default;
};
} // namespace chat_sdk