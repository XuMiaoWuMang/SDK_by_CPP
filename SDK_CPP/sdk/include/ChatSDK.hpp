#pragma once
#include "LLMManager.hpp"
#include "SessionManager.hpp"
#include "common.hpp"

namespace chat_sdk {
class ChatSDK {
  public:
    // 初始化模型管理器
    bool initLLMManager(const std::vector<std::shared_ptr<Config>> &configMap);
    // 创建会话
    std::string createSession(const std::string &modelName);
    // 删除会话
    bool deleteSession(const std::string &sessionId);
    // 获取会话列表
    std::vector<std::string> getSessionList();
    // 获取指定会话
    std::shared_ptr<SessionInfo> getSession(const std::string &sessionId);
    // 获取可用的模型列表
    std::vector<std::string> getModelList();
    // 发送消息
    std::string sendMessage(const std::string &sessionId,
                            const std::string &message);
    // 发送消息 - 流式返回
    std::string
    sendMessageStream(const std::string &sessionId, const std::string &message,
                      std::function<void(const std::string &, bool)> callback);

  private:
    // 注册模型列表
    bool
    registerModelList(const std::vector<std::shared_ptr<Config>> &configMap);
    // 初始化模型列表
    bool initModelList(const std::vector<std::shared_ptr<Config>> &configMap);
    // 初始化云端模型
    bool initRemoteModel(const std::shared_ptr<RemoteConfig> &config);
    // 初始化本地模型
    bool initOllamaModel(const std::shared_ptr<OllamaConfig> &config);

  private:
    bool _isInit = false;           // 是否初始化
    LLMManager _llmManager;         // 模型管理器
    SessionManager _sessionManager; // 会话管理器
    std::unordered_map<std::string, std::shared_ptr<Config>>
        _modelInfoMap; // 模型配置映射表
};
} // namespace chat_sdk
