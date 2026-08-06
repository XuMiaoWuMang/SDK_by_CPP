# AI_SDK_CPP SDK 使用文档

> 本文档描述 AI_SDK_CPP 的目录结构、核心数据结构、核心类接口（方法签名、参数类型与名称、返回值），以及典型使用流程。

## 1. SDK 概述

AI_SDK_CPP 是一个 C++17 的智能聊天 SDK，统一封装了云端大模型（DeepSeek / Gemini / ChatGPT）与本地 Ollama 模型的调用，并提供基于 SQLite 的会话持久化、会话与消息管理、流式/全量消息发送、统一日志等功能。

SDK 已安装至系统（头文件位于 `/usr/local/include/ai_cpp_sdk`，静态库 `libAI_SDK_CPP`）。源码目录结构与安装目录一致：

```
sdk/include/
├── common.hpp            # 公共数据结构（Message/Config/ModelInfo/SessionInfo）
├── ChatSDK.hpp           # SDK 统一入口（推荐使用）
├── SessionManager.hpp    # 会话管理
├── DataManager.hpp       # SQLite 持久化管理
├── LLMManager.hpp        # 模型管理（Provider 注册与调度）
├── LLMProvider.hpp       # LLM Provider 抽象接口
├── DeepSeekProvider.hpp  # DeepSeek 云端实现
├── GeminiProvider.hpp    # Gemini 云端实现
├── ChatGPTProvider.hpp   # ChatGPT(OpenAI) 云端实现
├── OllamaLLMProvider.hpp # 本地 Ollama 实现
└── util/logger.hpp       # 日志模块
```

## 2. 公共数据结构（common.hpp）

命名空间：`chat_sdk`

### 2.1 Message —— 消息

```cpp
class Message {
  public:
    Message(const std::string &role = "", const std::string &content = "");
  public:
    std::string _messageId;         // 消息ID
    std::string _role;              // 角色: "user" 或 "assistant"
    std::time_t _sendTimeTimestamp; // 发送时间戳, 单位: 秒
    std::string _content;           // 消息内容
};
```

| 成员 | 类型 | 说明 |
|---|---|---|
| `_messageId` | std::string | 消息唯一 ID |
| `_role` | std::string | 消息角色，`user` 或 `assistant` |
| `_sendTimeTimestamp` | std::time_t | 发送时间戳（秒） |
| `_content` | std::string | 消息内容 |

### 2.2 Config —— 模型公共配置（基类）

```cpp
struct Config {
    std::string _modelName;    // 模型名称
    double _temperature = 0.7; // 温度参数
    int _maxTokens = 2048;     // 最大token数
    std::string _modelDesc;    // 模型描述
    virtual ~Config() = default; // 用于 dynamic_cast 区分具体类型
};
```

### 2.3 RemoteConfig —— 云端模型配置

```cpp
struct RemoteConfig : Config {
    std::string _apiKey; // API key
};
```

### 2.4 OllamaConfig —— 本地 Ollama 模型配置

```cpp
struct OllamaConfig : Config {
    std::string _endpoint; // 模型端点 (base url), 如 http://127.0.0.1:11434
};
```

### 2.5 ModelInfo —— 模型信息

```cpp
struct ModelInfo {
    std::string _modelName;    // 模型名称
    std::string _modelDesc;    // 模型描述
    std::string _provider;     // 模型提供方
    std::string _endpoint;     // 模型端点 (base url)
    bool _isAvailable = false; // 是否可用

    ModelInfo(std::string modelName = "", std::string modelDesc = "",
              std::string provider = "", std::string endpoint = "");
};
```

### 2.6 SessionInfo —— 会话信息

```cpp
struct SessionInfo {
    std::string _sessionId;               // 会话ID
    std::string _modelName;               // 模型名称
    std::vector<Message> _messages;       // 会话消息列表
    std::time_t _createTimeTimestamp;     // 创建时间戳, 单位: 秒
    std::time_t _lastUpdateTimeTimestamp; // 最后更新时间戳, 单位: 秒

    SessionInfo(const std::string &_modelName);
    SessionInfo() = default;
};
```

---

## 3. 统一入口：ChatSDK（推荐）

头文件：`ai_cpp_sdk/ChatSDK.hpp`，命名空间：`chat_sdk`

**初始化流程**：构造 `ChatSDK` → 调用 `initLLMManager` 注册模型 → 即可创建会话与收发消息。

### 3.1 方法说明

| 方法 | 参数 | 返回值 | 说明 |
|---|---|---|---|
| `initLLMManager` | `const std::vector<std::shared_ptr<Config>> &configMap` | bool | 初始化模型管理器，注册并初始化所有模型；成功返回 true |
| `createSession` | `const std::string &modelName` | std::string | 创建会话；返回会话 ID，失败返回空串 |
| `deleteSession` | `const std::string &sessionId` | bool | 删除会话；成功返回 true |
| `getSessionList` | 无 | `std::vector<std::shared_ptr<SessionInfo>>` | 获取全部会话 |
| `getSession` | `const std::string &sessionId` | `std::shared_ptr<SessionInfo>` | 获取指定会话；不存在返回 `nullptr` |
| `getModelList` | 无 | `std::vector<ModelInfo>` | 获取可用模型列表 |
| `sendMessage` | `const std::string &sessionId`, `const std::string &message` | std::string | 全量发送消息；返回模型完整回复，失败返回空串 |
| `sendMessageStream` | `const std::string &sessionId`, `const std::string &message`, `std::function<void(const std::string &, bool)> callback` | std::string | 流式发送消息；增量数据通过 `callback` 回调返回 |

### 3.2 流式回调语义

`sendMessageStream` 的 `callback` 签名：`void(const std::string &data, bool finished)`

| 参数 | 类型 | 说明 |
|---|---|---|
| `data` | const std::string & | 模型返回的**增量**数据（每次回调追加一段） |
| `finished` | bool | 是否为最后一轮；为 `true` 表示模型回复完毕 |

> 注意：流式调用可能在未回调 `finished=true` 的情况下结束（模型流异常时），调用方需自行兜底收尾。

---

## 4. 会话管理：SessionManager

头文件：`ai_cpp_sdk/SessionManager.hpp`，命名空间：`chat_sdk`

由 `ChatSDK` 内部使用，负责会话的创建、查询、增删与持久化。一般场景无需直接调用。

| 方法 | 参数 | 返回值 | 说明 |
|---|---|---|---|
| 构造函数 | `const std::string &dbName = "chatDB.db"` | - | 指定数据库文件名（SQLite） |
| `createSession` | `const std::string &modelName` | std::string | 创建会话，返回会话 ID |
| `getSessionInfo` | `const std::string &sessionId` | `std::shared_ptr<SessionInfo>` | 获取会话信息 |
| `addMessage` | `const std::string &sessionId`, `const Message &message` | bool | 向指定会话添加消息 |
| `getMessages` | `const std::string &sessionId` | `std::vector<Message>` | 获取指定会话的历史消息 |
| `updateTimestamp` | `const std::string &sessionId` | void | 更新会话的最后更新时间戳 |
| `getSessionList` | 无 | `std::vector<std::shared_ptr<SessionInfo>>` | 获取会话列表 |
| `deleteSession` | `const std::string &sessionId` | bool | 删除会话 |
| `clearSessions` | 无 | bool | 清空所有会话 |
| `getSessionCount` | 无 | int64_t | 获取会话总数 |

---

## 5. 数据持久化：DataManager

头文件：`ai_cpp_sdk/DataManager.hpp`，命名空间：`chat_sdk`

基于 SQLite 的持久化层，被 `SessionManager` 内部使用，一般无需直接调用。

### 5.1 生命周期

| 方法 | 参数 | 返回值 | 说明 |
|---|---|---|---|
| 构造函数 | `const std::string &dbName` | - | 打开（或创建）SQLite 数据库 |
| 析构函数 | - | - | 关闭数据库连接 |

### 5.2 Session 相关

| 方法 | 参数 | 返回值 | 说明 |
|---|---|---|---|
| `InsertSession` | `const SessionInfo &sessionInfo` | bool | 插入会话 |
| `GetSession` | `const std::string &sessionId` | `std::shared_ptr<SessionInfo>` | 获取指定会话 |
| `UpdateSessionTimestamp` | `const std::string &sessionId`, `std::time_t lastUpdateTimeTimestamp` | bool | 更新会话时间戳 |
| `DeleteSession` | `const std::string &sessionId` | bool | 删除会话 |
| `GetAllSessionsIds` | 无 | `std::vector<std::string>` | 获取所有会话 ID |
| `GetAllSessions` | 无 | `std::vector<std::shared_ptr<SessionInfo>>` | 获取所有会话 |
| `GetSessionsCount` | 无 | size_t | 获取会话总数 |
| `DeleteAllSessions` | 无 | bool | 删除所有会话 |

### 5.3 Message 相关

| 方法 | 参数 | 返回值 | 说明 |
|---|---|---|---|
| `InsertMessage` | `const std::string &sessionId`, `const Message &message` | bool | 向指定会话插入消息 |
| `GetAllMessages` | `const std::string &sessionId` | `std::vector<Message>` | 获取指定会话的所有消息 |
| `DeleteMessages` | `const std::string &sessionId` | bool | 删除指定会话的历史消息 |

---

## 6. 模型管理：LLMManager

头文件：`ai_cpp_sdk/LLMManager.hpp`，命名空间：`chat_sdk`

由 `ChatSDK` 内部使用，负责 Provider 注册、模型初始化与消息调度。

| 方法 | 参数 | 返回值 | 说明 |
|---|---|---|---|
| `registerProvider` | `const std::string &name`, `std::unique_ptr<LLMProvider> provider` | bool | 注册一个 Provider 到指定名称 |
| `initModel` | `const std::string &name`, `const std::map<std::string, std::string> &modelParams` | bool | 用参数表初始化指定模型 |
| `getAvailableModels` | 无 | `std::vector<ModelInfo>` | 获取可用模型列表 |
| `isModelAvailable` | `const std::string &name` | bool | 检查模型是否可用 |
| `sendMessage` | `name`, `const std::vector<Message> &messages`, `const std::map<std::string, std::string> &params` | std::string | 全量发送消息 |
| `sendMessageStream` | `name`, `messages`, `params`, `const std::function<void(const std::string &, bool)> &callback` | std::string | 流式发送消息 |

> `modelParams` / `params` 为键值字符串表，具体键名由各 Provider 的 `initModel` 实现定义（如 `api_key`、`model`、`endpoint` 等）。

---

## 7. LLM Provider 抽象接口与实现

头文件：`ai_cpp_sdk/LLMProvider.hpp`，命名空间：`chat_sdk`

### 7.1 抽象接口 LLMProvider

```cpp
class LLMProvider {
  public:
    virtual bool initModel(const std::map<std::string, std::string> &configMap) = 0;
    virtual bool isAvailable() const = 0;
    virtual std::string GetModelName() const = 0;
    virtual std::string GetModelDesc() const = 0;
    virtual std::string sendMessage(const std::vector<Message> &messages,
                                    const std::map<std::string, std::string> &params) = 0;
    virtual std::string sendMessageStream(
        const std::vector<Message> &messages,
        const std::map<std::string, std::string> &params,
        std::function<void(const std::string &, bool)> callback) = 0;
};
```

| 方法 | 参数 | 返回值 | 说明 |
|---|---|---|---|
| `initModel` | `const std::map<std::string, std::string> &configMap` | bool | 用配置键值表初始化模型（含 API Key、端点等） |
| `isAvailable` | 无 | bool | 模型当前是否可用 |
| `GetModelName` | 无 | std::string | 模型名称 |
| `GetModelDesc` | 无 | std::string | 模型描述 |
| `sendMessage` | `messages`, `params` | std::string | 全量发送，返回完整回复 |
| `sendMessageStream` | `messages`, `params`, `callback` | std::string | 流式发送；`callback(data, finished)`：参数一为增量数据，参数二为是否最后一轮 |

### 7.2 内置实现

| 类 | 头文件 | 说明 |
|---|---|---|
| `DeepSeekProvider` | `DeepSeekProvider.hpp` | DeepSeek 云端模型（需 API Key） |
| `GeminiProvider` | `GeminiProvider.hpp` | Gemini 云端模型（需 API Key） |
| `ChatGPTProvider` | `ChatGPTProvider.hpp` | ChatGPT / OpenAI 兼容云端模型（需 API Key） |
| `OllamaLLMProvider` | `OllamaLLMProvider.hpp` | 本地 Ollama 模型（无需 API Key），额外提供 `std::string GetEndpoint() const` 返回模型端点 |

---

## 8. 日志模块：util/logger.hpp

命名空间：`sdk_logger`

### 8.1 Logger 类

| 方法 | 参数 | 返回值 | 说明 |
|---|---|---|---|
| `Logger::getLogger` | 无 | `std::shared_ptr<spdlog::logger>` | 获取全局日志器（单例） |
| `Logger::initLogger` | `const std::string &logFileName`, `const std::string &logFilePath`, `spdlog::level::level_enum level` | void | 初始化日志器：文件前缀名、输出路径（如 `"stdout"`）、日志级别 |

### 8.2 日志宏

日志宏自动附加 `[文件名:行号]` 前缀，支持 `spdlog` 的 `fmt` 格式化语法（`{}` 占位）。

| 宏 | 级别 | 用途 |
|---|---|---|
| `TRACE(format, ...)` | trace | 跟踪日志，调试用 |
| `DBG(format, ...)` | debug | 调试日志 |
| `INFO(format, ...)` | info | 普通信息 |
| `WARN(format, ...)` | warn | 警告 |
| `ERR(format, ...)` | error | 错误 |
| `FATAL(format, ...)` | fatal | 致命错误 |

```cpp
INFO("server running on ip: {}, port: {}", ip, port);
ERR("init failed: {}", errMsg);
```

---

## 9. 典型使用流程

```cpp
#include <ai_cpp_sdk/ChatSDK.hpp>
#include <ai_cpp_sdk/util/logger.hpp>
#include <memory>
#include <vector>

using namespace chat_sdk;

int main() {
    // 1. 初始化日志（可选）
    sdk_logger::Logger::initLogger("MyApp", "stdout", spdlog::level::info);

    // 2. 构造 SDK 并注册模型
    ChatSDK sdk;

    // 云端 DeepSeek 模型
    auto deepseek = std::make_shared<RemoteConfig>();
    deepseek->_apiKey = "sk-xxxx";
    deepseek->_modelName = "deepseek-v4-flash";
    deepseek->_temperature = 0.7;
    deepseek->_maxTokens = 2048;

    // 本地 Ollama 模型
    auto ollama = std::make_shared<OllamaConfig>();
    ollama->_modelName = "deepseek-r1:1.5b";
    ollama->_endpoint = "http://127.0.0.1:11434";
    ollama->_temperature = 0.7;

    std::vector<std::shared_ptr<Config>> configs = {deepseek, ollama};
    if (!sdk.initLLMManager(configs)) {
        return -1; // 初始化失败
    }

    // 3. 创建会话
    std::string sessionId = sdk.createSession("deepseek-v4-flash");
    if (sessionId.empty()) {
        return -1;
    }

    // 4. 全量发送消息
    std::string reply = sdk.sendMessage(sessionId, "你好，请自我介绍");
    INFO("回复: {}", reply);

    // 5. 流式发送消息
    sdk.sendMessageStream(
        sessionId, "再详细一点",
        [](const std::string &data, bool finished) {
            std::cout << data; // 逐段输出增量内容
            if (finished) {
                std::cout << "\n[流结束]" << std::endl;
            }
        });

    // 6. 查询会话与历史
    auto list = sdk.getSessionList();      // 所有会话
    auto info = sdk.getSession(sessionId); // 指定会话(含消息列表)

    // 7. 清理
    sdk.deleteSession(sessionId);
    return 0;
}
```

## 10. 注意事项

1. **初始化顺序**：必须先调用 `initLLMManager` 注册模型，再创建会话/发送消息。
2. **API Key**：云端模型（DeepSeek/Gemini/ChatGPT）必须配置有效的 `_apiKey`，否则模型不可用。
3. **流式回调**：`sendMessageStream` 的 `finished=true` 表示最后一轮；模型流异常时可能不回调 `finished=true`，调用方需自行兜底。
4. **时间戳单位**：所有时间戳字段单位均为**秒**。
5. **线程安全**：`SessionManager` / `DataManager` 内部有互斥锁保护，可跨线程调用；但流式回调可能在 SDK 内部线程触发。
6. **编译链接**：使用时需链接 `libAI_SDK_CPP` 及其依赖（OpenSSL、spdlog、jsoncpp、sqlite3、fmt）。
