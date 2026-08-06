#include "ChatServer.hpp"
#include <ai_cpp_sdk/util/logger.hpp>
#include <jsoncpp/json/reader.h>
#include <jsoncpp/json/value.h>
#include <jsoncpp/json/writer.h>
#include <sstream>
namespace chat_server {

ChatServer::ChatServer(const ServerConfig &config) { // 模型的配置数组
    _chat_sdk = std::make_shared<chat_sdk::ChatSDK>();

    // deepseek远端模型
    auto deepseekConfig = std::make_shared<chat_sdk::RemoteConfig>();
    deepseekConfig->_apiKey = config.deepseek_api_key;
    deepseekConfig->_modelName = "deepseek-v4-flash";
    deepseekConfig->_temperature = config.temperature;
    deepseekConfig->_maxTokens = config.max_tokens;

    // ollama本地模型
    // std::vector<std::shared_ptr<chat_sdk::OllamaConfig>> ollamaConfigs;
    std::vector<std::shared_ptr<chat_sdk::Config>>configLists; 
    for (auto &ollama : config.ollama_configs) {
        auto ollamaConfig = std::make_shared<chat_sdk::OllamaConfig>();
        ollamaConfig->_modelName = ollama._modelName;
        ollamaConfig->_endpoint = ollama._endpoint;
        ollamaConfig->_modelDesc =
            ollama._modelDesc;
        ollamaConfig->_temperature = config.temperature;
        ollamaConfig->_maxTokens = config.max_tokens;
        // ollamaConfigs.push_back(ollamaConfig);
        configLists.push_back(ollamaConfig);

    }
    configLists.push_back(deepseekConfig);


    // std::vector<std::shared_ptr<chat_sdk::Config>> configLists = {
    //     deepseekConfig,
    //     ollamaConfigs.begin(),
    //     ollamaConfigs.end()};

    if (!_chat_sdk->initLLMManager(configLists)) // 初始化LLMManager
    {
        ERR("initLLMManager failed !!!");
    }

    _config = config;
    _chatServer = std::make_unique<httplib::Server>();
}

void ChatServer::start() {
    if (_isRunning.load()) {
        WARN("server is running !!!");
        return;
    }
    setHttpRouter(); // 设置HTTP路由
    // 将请求路径的 / 映射为 /www，这样访问 http://localhost:8080/ 就会返回
    // ./www/index.html 文件
    _chatServer->set_base_dir("../www", "/"); // 设置前端页面目录

    std::thread serverThread([this]() {
        _chatServer->listen(_config.server_ip.c_str(), _config.server_port);
    });
    serverThread.detach();
    INFO("server running on ip: {}, port: {}", _config.server_ip,
         _config.server_port);
    _isRunning.store(true);
}

void ChatServer::stop() {
    if (!_isRunning.load()) {
        WARN("server is not running !!!");
        return;
    }
    if (_chatServer) {
        _chatServer->stop();
    }
    INFO("server stopped...");
    _isRunning.store(false);
}

bool ChatServer::isRunning() const { return _isRunning.load(); }

ChatServer::~ChatServer() { stop(); }

// 构造错误响应
std::string ChatServer::BuildResponse(const std::string &errMsg, bool success) {
    Json::StreamWriterBuilder writer;
    std::string responseStr;

    Json::Value errorJson;
    errorJson["success"] = success;
    errorJson["message"] = errMsg;
    responseStr = Json::writeString(writer, errorJson);
    return responseStr;
}
// 处理创建会话请求
void ChatServer::handleCreateSession(const httplib::Request &req,
                                     httplib::Response &res) {
    if (!isRunning()) {
        res.status = 500;
        res.set_content(BuildResponse("server is not running"),
                        "application/json");
        ERR("server is not running !!!");
        return;
    }

    // 解析请求体
    Json::Value requestBody;
    std::string errMsg;
    std::istringstream iss(req.body);
    Json::CharReaderBuilder builder;
    if (!Json::parseFromStream(builder, iss, &requestBody, &errMsg)) {
        res.status = 400;
        res.set_content(BuildResponse(errMsg), "application/json");
        ERR("parse request body failed: {}", errMsg);
        return;
    }

    // 创建会话
    auto sessionId = _chat_sdk->createSession(requestBody["model"].asString());
    if (sessionId.empty()) {
        res.status = 400;
        res.set_content(BuildResponse("create session failed"),
                        "application/json");
        ERR("create session failed !!!");
        return;
    }
    Json::Value dataJson(Json::objectValue);
    dataJson["session_id"] = sessionId;
    dataJson["model"] = requestBody["model"].asString();
    // 构造响应体
    Json::StreamWriterBuilder writer;
    Json::Value sessionJson;
    sessionJson["success"] = true;
    sessionJson["message"] = "create session success";
    sessionJson["data"] = dataJson;
    res.status = 200;
    res.set_content(Json::writeString(writer, sessionJson), "application/json");
    INFO("create session success: {}", sessionId);
    return;
}
// 处理删除会话请求
void ChatServer::handleDeleteSession(const httplib::Request &req,
                                     httplib::Response &res) {
    if (!isRunning()) {
        res.status = 500;
        res.set_content(BuildResponse("server is not running"),
                        "application/json");
        ERR("server is not running !!!");
        return;
    }

    // 获取路径参数
    std::string sessionId = req.matches[1];

    // 删除会话
    if (!_chat_sdk->deleteSession(sessionId)) {
        res.status = 400;
        res.set_content(BuildResponse("delete session failed"),
                        "application/json");
        ERR("delete session failed !!!");
        return;
    }

    // 构造响应体
    Json::StreamWriterBuilder writer;
    res.status = 200;
    res.set_content(BuildResponse("Delete Session Success", true),
                    "application/json");
    INFO("delete session success: {}", sessionId);
    return;
}
// 处理获取会话列表请求
void ChatServer::handleGetSessionList(const httplib::Request &req,
                                      httplib::Response &res) {
    if (!isRunning()) {
        res.status = 500;
        res.set_content(BuildResponse("server is not running"),
                        "application/json");
        ERR("server is not running !!!");
        return;
    }
    // 获取会话列表
    auto sessionList = _chat_sdk->getSessionList();
    Json::Value sessionJsonList(Json::arrayValue);
    for (auto &session : sessionList) {
        Json::Value sessionJson(Json::objectValue);
        sessionJson["id"] = session->_sessionId;
        sessionJson["model"] = session->_modelName;
        sessionJson["createTime"] = session->_createTimeTimestamp;
        sessionJson["lastUpdateTime"] = session->_lastUpdateTimeTimestamp;
        sessionJson["messageCount"] = session->_messages.size();
        if (!session->_messages.empty()) {
            // 取最后一条用户消息作为会话列表的展示内容
            for (auto it = session->_messages.rbegin();
                 it != session->_messages.rend(); ++it) {
                if (it->_role == "user") {
                    sessionJson["lastUserMessage"] = it->_content;
                    break;
                }
            }
        }

        sessionJsonList.append(sessionJson);
    }
    // 构造响应体
    Json::StreamWriterBuilder writer;
    Json::Value sessionJson;
    sessionJson["success"] = true;
    sessionJson["message"] = "get session list success";
    sessionJson["data"] = sessionJsonList;
    res.status = 200;
    res.set_content(Json::writeString(writer, sessionJson), "application/json");
    INFO("get session list success: {}", sessionList.size());
    return;
}
// 处理获取历史消息请求
void ChatServer::handleGetHistoryMessages(const httplib::Request &req,
                                          httplib::Response &res) {
    if (!isRunning()) {
        res.status = 500;
        res.set_content(BuildResponse("server is not running"),
                        "application/json");
        ERR("server is not running !!!");
        return;
    }
    // 获取路径参数
    std::string sessionId = req.matches[1];

    // 获取历史消息
    auto sessionInfo = _chat_sdk->getSession(sessionId);
    if (sessionInfo == nullptr) {
        res.status = 400;
        res.set_content(BuildResponse("get history messages failed"),
                        "application/json");
        ERR("get history messages failed !!!");
        return;
    }

    // 构造响应
    Json::Value dataJson(Json::arrayValue);
    for (auto &message : sessionInfo->_messages) {
        Json::Value messageJson(Json::objectValue);
        messageJson["id"] = message._messageId;
        messageJson["role"] = message._role;
        messageJson["content"] = message._content;
        messageJson["timestamp"] = message._sendTimeTimestamp;
        dataJson.append(messageJson);
    }
    Json::Value responseJson(Json::objectValue);
    responseJson["success"] = true;
    responseJson["message"] = "get history messages success";
    responseJson["data"] = dataJson;
    Json::StreamWriterBuilder writer;
    res.status = 200;
    res.set_content(Json::writeString(writer, responseJson),
                    "application/json");
    INFO("get history messages success: {}", sessionId);
    return;
}
// 处理获取模型列表请求
void ChatServer::handleGetModelList(const httplib::Request &req,
                                    httplib::Response &res) {
    if (!isRunning()) {
        res.status = 500;
        res.set_content(BuildResponse("server is not running"),
                        "application/json");
        ERR("server is not running !!!");
        return;
    }

    // 获取模型列表
    auto modelList = _chat_sdk->getModelList();
    if (modelList.empty()) {
        res.status = 400;
        res.set_content(BuildResponse("get model list failed"),
                        "application/json");
        ERR("get model list failed !!!");
        return;
    }
    Json::Value modelJsonList(Json::arrayValue);
    for (auto &model : modelList) {
        Json::Value modelJson(Json::objectValue);
        modelJson["model"] = model._modelName;
        modelJson["desc"] = model._modelDesc;
        modelJsonList.append(modelJson);
    }

    // 构造响应体
    Json::StreamWriterBuilder writer;
    Json::Value modelJson;
    modelJson["success"] = true;
    modelJson["message"] = "get model list success";
    modelJson["data"] = modelJsonList;
    res.status = 200;
    res.set_content(Json::writeString(writer, modelJson), "application/json");
    INFO("get model list success: {}", modelList.size());
    return;
}
// 处理全量发送消息请求
void ChatServer::handleSendMessage(const httplib::Request &req,
                                   httplib::Response &res) {
    if (!isRunning()) {
        res.status = 500;
        res.set_content(BuildResponse("server is not running"),
                        "application/json");
        ERR("server is not running !!!");
        return;
    }

    // 解析请求体
    Json::Value requestBody;
    std::string errMsg;
    std::istringstream iss(req.body);
    Json::CharReaderBuilder builder;
    if (!Json::parseFromStream(builder, iss, &requestBody, &errMsg)) {
        res.status = 400;
        res.set_content(BuildResponse(errMsg), "application/json");
        ERR("parse request body failed: {}", errMsg);
        return;
    }

    std::string sessionId = requestBody["sessionId"].asString();
    std::string message = requestBody["message"].asString();
    if (sessionId.empty() || message.empty()) {
        res.status = 400;
        res.set_content(BuildResponse("sessionId or message is empty"),
                        "application/json");
        ERR("sessionId or message is empty !!!");
        return;
    }
    // 发送消息
    std::string response = _chat_sdk->sendMessage(sessionId, message);

    if (response.empty()) {
        res.status = 400;
        res.set_content(BuildResponse("send message failed"),
                        "application/json");
        ERR("send message failed !!!");
        return;
    }
    Json::Value dataJson(Json::objectValue);
    dataJson["response"] = response;
    dataJson["sessionId"] = sessionId;

    Json::StreamWriterBuilder writer;
    Json::Value responseJson;
    responseJson["success"] = true;
    responseJson["message"] = "send message success";
    responseJson["data"] = dataJson;
    res.status = 200;
    res.set_content(Json::writeString(writer, responseJson),
                    "application/json");
    INFO("send message success: {}", response);
    return;
}
// 处理增量发送消息请求
void ChatServer::handleIncrementalSendMessage(const httplib::Request &req,
                                              httplib::Response &res) {
    if (!isRunning()) {
        res.status = 500;
        res.set_content(BuildResponse("server is not running"),
                        "application/json");
        ERR("server is not running !!!");
        return;
    }

    // 解析请求体
    Json::Value requestBody;
    std::string errMsg;
    std::istringstream iss(req.body);
    Json::CharReaderBuilder builder;
    if (!Json::parseFromStream(builder, iss, &requestBody, &errMsg)) {
        res.status = 400;
        res.set_content(BuildResponse(errMsg), "application/json");
        ERR("parse request body failed: {}", errMsg);
        return;
    }

    // 提取会话ID和消息
    std::string sessionId = requestBody["sessionId"].asString();
    std::string message = requestBody["message"].asString();

    res.set_header("Cache-Control", "no-cache"); // 无缓存，立即响应
    res.set_header("Connection", "keep-alive");  // 保持长连接

    // 设置流式响应
    res.set_chunked_content_provider(
        "text/event-stream",
        [this, sessionId, message](unsigned long outSize,
                                   httplib::DataSink &dataSink) -> bool {
            // 设置回调函数
            auto dataWrite = [&](const std::string &data,
                                 bool finished) -> void {
                // 构造响应数据，格式为：data: "data"
                // 其中，data为模型返回的增量数据
                // 为防止数据中包含"\n\n"等特殊字符破坏流式响应，使用Json::valueToQuotedString()函数转义
                std::string response =
                    "data: " + Json::valueToQuotedString(data.c_str()) + "\n\n";
                dataSink.write(response.c_str(),
                               response.size()); // 发送响应数据

                // finished若为true，说明模型响应完毕，发送[DONE]数据
                if (finished) {
                    response = "data: [DONE]\n\n";
                    dataSink.write(response.c_str(),
                                   response.size()); // 发送[DONE]数据
                    dataSink.done();                 // 标记流式响应结束
                }
            };
            dataWrite("", false); // 发送初始空数据，确保流式响应开始
            _chat_sdk->sendMessageStream(sessionId, message, dataWrite);
            return true;
        });
}

void ChatServer::setHttpRouter() {
    // 处理获取会话列表请求
    _chatServer->Get("/api/sessions", [this](const httplib::Request &req,
                                             httplib::Response &res) {
        this->handleGetSessionList(req, res);
    });
    // 处理获取模型列表请求
    _chatServer->Get("/api/models", [this](const httplib::Request &req,
                                           httplib::Response &res) {
        this->handleGetModelList(req, res);
    });
    // 处理创建会话请求
    _chatServer->Post("/api/session", [this](const httplib::Request &req,
                                             httplib::Response &res) {
        this->handleCreateSession(req, res);
    });
    // 处理获取历史消息请求
    _chatServer->Get(
        "/api/session/([^/]+)/history",
        [this](const httplib::Request &req, httplib::Response &res) {
            this->handleGetHistoryMessages(req, res);
        });

    // 处理全量发送消息请求
    _chatServer->Post("/api/message", [this](const httplib::Request &req,
                                             httplib::Response &res) {
        this->handleSendMessage(req, res);
    });
    // 处理增量发送消息请求
    _chatServer->Post("/api/message/async", [this](const httplib::Request &req,
                                                   httplib::Response &res) {
        this->handleIncrementalSendMessage(req, res);
    });
    // 处理删除会话请求
    _chatServer->Delete(
        "/api/session/([^/]+)",
        [this](const httplib::Request &req, httplib::Response &res) {
            this->handleDeleteSession(req, res);
        });
}

} // namespace chat_server
