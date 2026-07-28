#pragma once
#include "LLMProvider.hpp"
#include <memory>
#include <unordered_map>
namespace chat_sdk {
class LLMManager {
  public:
    // 注册LLMProvider
    bool registerProvider(const std::string &name,
                          std::unique_ptr<LLMProvider> provider);
    // 初始化模型
    bool initModel(const std::string &name,
                   const std::map<std::string, std::string> &modelParams);
    // 获取可用模型
    std::vector<std::string> getAvailableModels() const;
    // 检查模型是否可用
    bool isModelAvailable(const std::string &name) const;
    // 发送消息 - 全量
    std::string sendMessage(const std::string &name,
                            const std::vector<Message> &messages,
                            const std::map<std::string, std::string> &params);
    // 发送消息 - 流式
    std::string sendMessageStream(
        const std::string &name, const std::vector<Message> &messages,
        const std::map<std::string, std::string> &params,
        const std::function<void(const std::string &, bool)> &callback);

  private:
    std::unordered_map<std::string, std::unique_ptr<LLMProvider>> _providers;
    std::unordered_map<std::string, ModelInfo> _modelInfos;
};

} // namespace chat_sdk