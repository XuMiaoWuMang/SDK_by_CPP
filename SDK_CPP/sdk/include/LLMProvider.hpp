#pragma once
#include "common.hpp"
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace chat_sdk {
class LLMProvider {
  public:
    // 初始化模型
    virtual bool
    initModel(const std::map<std::string, std::string> &configMap) = 0;
    // 检查模型是否可用
    virtual bool isAvailable() const = 0;
    // 获取模型名称
    virtual std::string GetModelName() const = 0;
    // 获取模型描述
    virtual std::string GetModelDesc() const = 0;
    // 发送消息 - 全量返回
    virtual std::string
    sendMessage(const std::vector<Message> &messages,
                const std::map<std::string, std::string> &params) = 0;
    // 发送消息 - 流式返回
    virtual std::string sendMessageStream(
        const std::vector<Message> &messages,
        const std::map<std::string, std::string> &params,
        std::function<void(const std::string &, bool)>
            callback) = 0; // callback:
                           // 对于每次返回应该如何处理。参数一：增量数据；参数二：是否为最后一轮。

  protected:
    bool _isAvailable = false;
    std::string _api_key;   // API key
    std::string _modelName; // 模型名称
    std::string _endpoint;  // 模型端点 (base url)
};
} // namespace chat_sdk
