#include "../include/ChatSDK.hpp"
#include "../include/ChatGPTProvider.hpp"
#include "../include/DeepSeekProvider.hpp"
#include "../include/GeminiProvider.hpp"
#include "../include/OllamaLLMProvider.hpp"
#include "../include/util/logger.hpp"
#include <cstdlib>
#include <memory>
#include <unordered_map>
#include <unordered_set>
namespace chat_sdk {
// 构造函数: 指定数据库文件路径
ChatSDK::ChatSDK(const std::string &dbPath) : _sessionManager(dbPath) {}
// 初始化模型管理器
bool ChatSDK::initLLMManager(
    const std::vector<std::shared_ptr<Config>> &configMap) {
    registerModelList(configMap);
    initModelList(configMap);

    _isInit = true;
    return true;
}
// 注册模型列表: 模型全部来自配置, 云端模型按 _provider 实例化对应 Provider,
// 本地模型使用 OllamaLLMProvider (LLMManager 以模型名为 key, 支持同 provider 多模型)
bool ChatSDK::registerModelList(
    const std::vector<std::shared_ptr<Config>> &configMap) {
    std::unordered_set<std::string> registeredModels;
    for (auto &config : configMap) {
        if (config == nullptr) {
            continue;
        }
        if (registeredModels.find(config->_modelName) !=
            registeredModels.end()) {
            WARN("model {} is already registered", config->_modelName);
            continue;
        }
        if (_llmManager.isModelAvailable(config->_modelName)) {
            WARN("model {} is already available", config->_modelName);
            continue;
        }
        registeredModels.insert(config->_modelName);

        std::unique_ptr<LLMProvider> provider;
        if (auto remote = std::dynamic_pointer_cast<RemoteConfig>(config)) {
            // 云端模型: 按 provider 选择对应实现
            if (remote->_provider == "gemini") {
                provider = std::make_unique<GeminiProvider>();
            } else if (remote->_provider == "chatgpt") {
                provider = std::make_unique<ChatGPTProvider>();
            } else {
                provider = std::make_unique<DeepSeekProvider>();
            }
        } else if (std::dynamic_pointer_cast<OllamaConfig>(config)) {
            provider = std::make_unique<OllamaLLMProvider>();
        } else {
            WARN("unknown config type, skip model {}", config->_modelName);
            continue;
        }

        if (!_llmManager.registerProvider(config->_modelName,
                                          std::move(provider))) {
            ERR("register model {} failed", config->_modelName);
            return false;
        }
        INFO("register model success: {}", config->_modelName);
    }
    return true;
}
// 初始化模型列表
bool ChatSDK::initModelList(
    const std::vector<std::shared_ptr<Config>> &configMap) {
    for (auto &model : configMap) {
        if (model == nullptr) {
            continue;
        }
        if (auto configPtr =
                std::dynamic_pointer_cast<OllamaConfig>(model)) {
            // 本地 Ollama 模型
            initOllamaModel(configPtr);
        } else if (auto configPtr =
                       std::dynamic_pointer_cast<RemoteConfig>(model)) {
            // 云端模型
            initRemoteModel(configPtr);
        }
    }
    return true;
}

// 初始化云端模型
bool ChatSDK::initRemoteModel(const std::shared_ptr<RemoteConfig> &config) {
    if (config == nullptr) {
        WARN("config is nullptr");
        return false;
    }
    if (_llmManager.isModelAvailable(config->_modelName)) {
        WARN("model {} is already available", config->_modelName);
        return false;
    }
    // API Key: 配置条目 api_key 优先, 为空时回退对应环境变量
    auto env = [](const char *name) {
        const char *value = std::getenv(name);
        return value == nullptr ? std::string() : std::string(value);
    };
    std::string apiKey = config->_apiKey;
    if (apiKey.empty()) {
        if (config->_provider == "gemini") {
            apiKey = env("GEMINI_API_KEY");
        } else if (config->_provider == "chatgpt") {
            apiKey = env("CHATGPT_API_KEY");
            if (apiKey.empty()) {
                apiKey = env("OPENAI_API_KEY");
            }
        } else {
            apiKey = env("DEEPSEEK_API_KEY");
        }
    }
    std::map<std::string, std::string> params;
    params["model"] = config->_modelName;
    params["temperature"] = std::to_string(config->_temperature);
    params["max_tokens"] = std::to_string(config->_maxTokens);
    params["api_key"] = apiKey;
    params["model_desc"] = config->_modelDesc;
    if (!_llmManager.initModel(config->_modelName, params)) {
        ERR("init model {} failed", config->_modelName);
        return false;
    }
    _modelInfoMap[config->_modelName] = config;
    INFO("init model {} success", config->_modelName);
    return true;
}
// 初始化本地模型
bool ChatSDK::initOllamaModel(const std::shared_ptr<OllamaConfig> &config) {
    if (config == nullptr) {
        WARN("config is nullptr");
        return false;
    }
    if (_llmManager.isModelAvailable(config->_modelName)) {
        WARN("model {} is already available", config->_modelName);
        return false;
    }
    std::map<std::string, std::string> params;
    params["model_name"] = config->_modelName;
    params["temperature"] = std::to_string(config->_temperature);
    params["max_tokens"] = std::to_string(config->_maxTokens);
    params["endpoint"] = config->_endpoint;
    params["model_desc"] = config->_modelDesc;
    _llmManager.initModel(config->_modelName, params);
    if (!_llmManager.isModelAvailable(config->_modelName)) {
        ERR("init model {} failed", config->_modelName);
        return false;
    }
    _modelInfoMap[config->_modelName] = config;
    INFO("init model {} success", config->_modelName);
    return true;
}

// 创建会话
std::string ChatSDK::createSession(const std::string &modelName) {
    if (!_isInit) {
        WARN("createSession error, isInit is false");
        return "";
    }
    std::string result = _sessionManager.createSession(modelName);
    if (result.empty()) {
        ERR("createSession error, modelName is empty");
        return "";
    }
    INFO("createSession success, sessionId: {}", result);
    return result;
}
// 删除会话
bool ChatSDK::deleteSession(const std::string &sessionId) {
    if (!_isInit) {
        WARN("deleteSession error, isInit is false");
        return false;
    }
    bool result = _sessionManager.deleteSession(sessionId);
    if (!result) {
        ERR("deleteSession error, sessionId is empty");
        return false;
    }
    INFO("deleteSession success, sessionId: {}", sessionId);
    return result;
}
// 获取会话列表
std::vector<std::shared_ptr<SessionInfo>> ChatSDK::getSessionList() {
    if (!_isInit) {
        WARN("getSessionList error, isInit is false");
        return {};
    }
    return _sessionManager.getSessionList();
}
// 获取指定会话
std::shared_ptr<SessionInfo> ChatSDK::getSession(const std::string &sessionId) {
    if (!_isInit) {
        WARN("getSession error, isInit is false");
        return nullptr;
    }
    std::shared_ptr<SessionInfo> result =
        _sessionManager.getSessionInfo(sessionId);
    if (result == nullptr) {
        ERR("getSession error, sessionId is empty");
        return nullptr;
    }
    INFO("getSession success, sessionId: {}", sessionId);
    return result;
}
// 获取可用的模型列表
std::vector<ModelInfo> ChatSDK::getModelList() {
    if (!_isInit) {
        WARN("getModelList error, isInit is false");
        return {};
    }
    std::vector<ModelInfo> modelList = _llmManager.getAvailableModels();
    if (modelList.empty()) {
        ERR("getModelList error, no available models");
        return {};
    }
    INFO("getModelList success, modelList size: {}", modelList.size());
    return modelList;
}
// 发送消息
std::string ChatSDK::sendMessage(const std::string &sessionId,
                                 const std::string &message) {
    if (!_isInit) {
        WARN("sendMessage error, isInit is false");
        return "";
    }
    auto sessionInfo = _sessionManager.getSessionInfo(sessionId);
    if (sessionInfo == nullptr) {
        ERR("sendMessage error, sessionId is empty");
        return "";
    }
    Message newMessage("user", message);

    _sessionManager.addMessage(sessionId, newMessage); // 添加消息到会话

    auto it = _modelInfoMap.find(sessionInfo->_modelName);
    if (it == _modelInfoMap.end()) {
        ERR("sendMessage error, modelName is empty");
        return "";
    }
    std::map<std::string, std::string> params; // 模型参数
    params["temperature"] = std::to_string(it->second->_temperature);
    params["max_tokens"] = std::to_string(it->second->_maxTokens);

    std::string result = _llmManager.sendMessage(
        sessionInfo->_modelName, sessionInfo->_messages, params);
    if (result.empty()) {
        ERR("sendMessage error, sessionId is empty");
        return "";
    }
    Message assistantMessage("assistant", result);
    _sessionManager.addMessage(sessionId, assistantMessage); // 添加消息到会话
    INFO("sendMessage success, sessionId: {}", sessionId);
    
    return result;
}
// 发送消息 - 流式返回
std::string ChatSDK::sendMessageStream(
    const std::string &sessionId, const std::string &message,
    std::function<void(const std::string &, bool)> callback) {
    if (!_isInit) {
        WARN("sendMessageStream error, isInit is false");
        return "";
    }
    auto sessionInfo = _sessionManager.getSessionInfo(sessionId);
    if (sessionInfo == nullptr) {
        ERR("sendMessageStream error, sessionId is empty");
        return "";
    }
    Message newMessage("user", message);

    _sessionManager.addMessage(sessionId, newMessage); // 添加消息到会话

    auto it = _modelInfoMap.find(sessionInfo->_modelName);
    if (it == _modelInfoMap.end()) {
        ERR("sendMessageStream error, modelName is empty");
        return "";
    }
    std::map<std::string, std::string> params; // 模型参数
    params["temperature"] = std::to_string(it->second->_temperature);
    params["max_tokens"] = std::to_string(it->second->_maxTokens);

    std::string result = _llmManager.sendMessageStream(
        sessionInfo->_modelName, sessionInfo->_messages, params, callback);

    Message assistantMessage("assistant", result);
    _sessionManager.addMessage(sessionId, assistantMessage); // 添加消息到会话
    if (result.empty()) {
        ERR("sendMessageStream error, sessionId is empty");
        return "";
    }
    INFO("sendMessageStream success, sessionId: {}", sessionId);
    return result;
}
} // namespace chat_sdk
