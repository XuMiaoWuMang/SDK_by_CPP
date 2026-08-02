#pragma once
#include "LLMProvider.hpp"

namespace chat_sdk {
class ChatGPTProvider : public LLMProvider {
  public:
    // 初始化模型
    bool
    initModel(const std::map<std::string, std::string> &configMap) override;
    // 检查模型是否可用
    bool isAvailable() const override;
    // 获取模型名称
    std::string GetModelName() const override;
    // 获取模型描述
    std::string GetModelDesc() const override;
    // 发送消息 - 全量返回
    std::string
    sendMessage(const std::vector<Message> &messages,
                const std::map<std::string, std::string> &params) override;
    // 发送消息 - 流式返回
    std::string sendMessageStream(
        const std::vector<Message> &messages,
        const std::map<std::string, std::string> &params,
        std::function<void(const std::string &, bool)> callback) override;
};
} // namespace chat_sdk
