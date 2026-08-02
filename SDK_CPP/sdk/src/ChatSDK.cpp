#include "../include/ChatSDK.hpp"
#include "../include/DeepSeekProvider.hpp"
#include "../include/OllamaLLMProvider.hpp"
#include "../include/util/logger.hpp"
#include <memory>
#include <unordered_map>
#include <unordered_set>
namespace chat_sdk {
// 初始化模型管理器
bool ChatSDK::initLLMManager(
    const std::vector<std::shared_ptr<Config>> &configMap) {
    registerModelList(configMap);
    initModelList(configMap);

    _isInit = true;
    return true;
}
// 注册模型列表
bool ChatSDK::registerModelList(
    const std::vector<std::shared_ptr<Config>> &configMap) {
    // 若未注册deepseek-v4-flash，则注册
    if (!_llmManager.isModelAvailable("deepseek-v4-flash")) {
        auto provider = std::make_unique<DeepSeekProvider>();

        if (!_llmManager.registerProvider("deepseek-v4-flash",
                                          std::move(provider))) {
            ERR("register deepseek-v4-flash model failed");
            return false;
        }
        INFO("register deepseek-v4-flash model");
    }

    // 使用unordered_set 存储已注册的模型名称
    std::unordered_set<std::string> registeredModels;
    registeredModels.insert("deepseek-v4-flash");

    // 接下来循环注册用户传入的模型
    for (auto &config : configMap) {
        auto provider = std::make_unique<OllamaLLMProvider>();
        auto configPtr = std::dynamic_pointer_cast<OllamaConfig>(config);
        if (configPtr != nullptr) {
            // 若模型名称已注册，则跳过
            if (registeredModels.find(configPtr->_modelName) !=
                registeredModels.end()) {
                WARN("model {} is already registered", configPtr->_modelName);
                continue;
            }
            registeredModels.insert(configPtr->_modelName);

            if (_llmManager.isModelAvailable(configPtr->_modelName)) {
                WARN("model {} is already available", configPtr->_modelName);
                continue;
            }
            if (!_llmManager.registerProvider(configPtr->_modelName,
                                              std::move(provider))) {
                ERR("register user's model {} failed", configPtr->_modelName);
                return false;
            }
            INFO("register user's model success: {}", configPtr->_modelName);
        }
    }
    return true;
}
// 初始化模型列表
bool ChatSDK::initModelList(
    const std::vector<std::shared_ptr<Config>> &configMap) {
    for (auto &model : configMap) {
        if (model != nullptr) {

            if (model->_modelName == "deepseek-v4-flash") {
                auto configPtr = std::make_shared<RemoteConfig>();
                configPtr->_modelName = model->_modelName;
                configPtr->_apiKey = std::getenv("DEEPSEEK_API_KEY");
                initRemoteModel(configPtr);
            } else if (auto configPtr =
                           std::dynamic_pointer_cast<OllamaConfig>(model)) {
                // 若模型配置为Ollama模型，则初始化模型信息
                initOllamaModel(configPtr);
            } else if (auto configPtr =
                           std::dynamic_pointer_cast<RemoteConfig>(model)) {
                // 若模型配置为Remote模型，则初始化模型信息
                initRemoteModel(configPtr);
            }
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
    std::map<std::string, std::string> params;
    params["model"] = config->_modelName;
    params["temperature"] = std::to_string(config->_temperature).empty()
                                ? "0.7"
                                : std::to_string(config->_temperature);
    params["max_tokens"] = std::to_string(config->_maxTokens).empty()
                               ? "2048"
                               : std::to_string(config->_maxTokens);
    params["api_key"] = config->_apiKey;
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
std::vector<std::string> ChatSDK::getSessionList() {
    if (!_isInit) {
        WARN("getSessionList error, isInit is false");
        return {};
    }
    return _sessionManager.getSessionIds();
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
std::vector<std::string> ChatSDK::getModelList() {
    if (!_isInit) {
        WARN("getModelList error, isInit is false");
        return {};
    }
    std::vector<std::string> modelList = _llmManager.getAvailableModels();
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
    if (result.empty()) {
        ERR("sendMessageStream error, sessionId is empty");
        return "";
    }
    INFO("sendMessageStream success, sessionId: {}", sessionId);
    return result;
}
} // namespace chat_sdk
