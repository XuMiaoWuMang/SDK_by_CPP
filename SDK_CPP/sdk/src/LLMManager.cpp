#include "../include/LLMManager.hpp"
#include "../include/common.hpp"
#include "../include/util/logger.hpp"
namespace chat_sdk {
// 注册LLMProvider
bool LLMManager::registerProvider(const std::string &name,
                                  std::unique_ptr<LLMProvider> provider) {
    if (!provider) {
        ERR("registerProvider: provider is null, model name: {}", name);
        return false;
    }
    _providers[name] = std::move(provider);

    _modelInfos[name] = ModelInfo(name);

    INFO("registerProvider: model name: {}", name);
    return true;
}
// 初始化模型
bool LLMManager::initModel(
    const std::string &name,
    const std::map<std::string, std::string> &modelParams) {
    auto provider = _providers.find(name);
    if (provider == _providers.end()) {
        ERR("initModel: provider not found, model name: {}", name);
        return false;
    }
    bool isInit = provider->second->initModel(modelParams);
    if (!isInit) {
        ERR("initModel: initModel failed, model name: {}", name);
        return false;
    } else {
        _modelInfos[name]._modelDesc = provider->second->GetModelDesc();
        _modelInfos[name]._isAvailable = true;
    }
    return isInit;
}
// 获取可用模型
std::vector<ModelInfo> LLMManager::getAvailableModels() const {
    std::vector<ModelInfo> availableModels;
    for (const auto &model : _modelInfos) {
        if (model.second._isAvailable) {
            availableModels.push_back(model.second);
        }
    }
    return availableModels;
}
// 检查模型是否可用
bool LLMManager::isModelAvailable(const std::string &name) const {
    auto it = _modelInfos.find(name);
    if (it == _modelInfos.end()) {
        return false;
    }
    return it->second._isAvailable;
}
// 发送消息 - 全量
std::string
LLMManager::sendMessage(const std::string &name,
                        const std::vector<Message> &messages,
                        const std::map<std::string, std::string> &params) {
    // 检查模型是否可用
    if (!isModelAvailable(name)) {
        ERR("sendMessage: model not available, model name: {}", name);
        return "";
    }
    return _providers[name]->sendMessage(messages, params);
}
// 发送消息 - 流式
std::string LLMManager::sendMessageStream(
    const std::string &name, const std::vector<Message> &messages,
    const std::map<std::string, std::string> &params,
    const std::function<void(const std::string &, bool)> &callback) {
    // 检查模型是否可用
    if (!isModelAvailable(name)) {
        ERR("sendMessageStream: model not available, model name: {}", name);
        return "";
    }
    return _providers[name]->sendMessageStream(messages, params, callback);
}
} // namespace chat_sdk