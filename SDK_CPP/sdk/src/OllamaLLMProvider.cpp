#include "../include/OllamaLLMProvider.hpp"
#include "../include/util/logger.hpp"
#include <httplib.h>
#include <jsoncpp/json/json.h>
#include <jsoncpp/json/reader.h>
#include <jsoncpp/json/writer.h>
namespace chat_sdk {
bool OllamaLLMProvider::initModel(
    const std::map<std::string, std::string> &configMap) {
    auto it = configMap.find("model_name");
    if (it == configMap.end()) {
        ERR("model_name is not found in configMap");
        return false;
    }
    _modelName = it->second;

    it = configMap.find("model_desc");
    if (it == configMap.end()) {
    }
    _modelDesc = it->second;

    it = configMap.find("endpoint");
    if (it == configMap.end()) {
        ERR("endpoint is not found in configMap");
        return false;
    }
    _endpoint = it->second;

    _isAvailable = true;
    INFO("OllamaLLMProvider initModel success, endpoint: {}", _endpoint);
    return true;
}
// 检查模型是否可用
bool OllamaLLMProvider::isAvailable() const { return _isAvailable; }
// 获取模型名称
std::string OllamaLLMProvider::GetModelName() const { return _modelName; }
// 获取模型描述
std::string OllamaLLMProvider::GetModelDesc() const { return _modelDesc; }

std::string OllamaLLMProvider::GetEndpoint() const { return _endpoint; }

std::string OllamaLLMProvider::sendMessage(
    const std::vector<Message> &messages,
    const std::map<std::string, std::string> &params) {

    // 模型是否可用
    if (!_isAvailable) {
        ERR("OllamaLLMProvider sendMessage error, model is not available");
        return "";
    }

    // 构造历史消息
    Json::Value historyMessages(Json::arrayValue);
    for (const auto &message : messages) {
        Json::Value messageJson;
        messageJson["role"] = message._role;
        messageJson["content"] = message._content;
        historyMessages.append(messageJson);
    }

    // 解析参数
    double temperature = 0.7;
    int maxTokens = 2048;
    if (params.find("max_tokens") != params.end()) {
        maxTokens = std::stoi(params.at("max_tokens"));
    }
    if (params.find("temperature") != params.end()) {
        temperature = std::stod(params.at("temperature"));
    }

    // 构造请求体
    Json::Value options;
    options["temperature"] = temperature;
    options["num_ctx"] = maxTokens;
    Json::Value requestBody;
    requestBody["model"] = _modelName;
    requestBody["options"] = options;
    requestBody["messages"] = historyMessages;
    requestBody["stream"] = false;

    // 序列化请求体
    std::string requestBodyStr;
    Json::StreamWriterBuilder builder;
    requestBodyStr = Json::writeString(builder, requestBody);

    // 构造请求头
    httplib::Headers headers = {{"Content-Type", "application/json"}};

    // 构造客户端
    httplib::Client client(_endpoint);
    client.set_connection_timeout(30, 0);
    client.set_read_timeout(30, 0);

    // 发送请求
    auto res =
        client.Post("/api/chat", headers, requestBodyStr, "application/json");
    if (res->status != 200) {
        ERR("OllamaLLMProvider sendMessage error, status code: {}, body: {}",
            res->status, res->body);
        return "";
    }

    // 解析响应体
    Json::Value responseJson;
    Json::CharReaderBuilder reader;
    std::istringstream streamBody(res->body);
    std::string errmsg;
    if (!Json::parseFromStream(reader, streamBody, &responseJson, &errmsg)) {
        ERR("OllamaLLMProvider sendMessage error, parse response body error: "
            "{}",
            errmsg);
        return "";
    }

    // 提取响应内容
    /*
{
"model":"deepseek-r1:1.5b",
"created_at":"2026-07-28T14:18:52.392773507Z",
"message":{
    "role":"assistant",
    "content":"\n\n您好！我是由中国的深度求索（DeepSeek）公司开发的智能助手DeepSeek-R1。如您有任何任何问题，我会尽我所能为您提供帮助。"
},
"done":true,
"done_reason":"stop",
"total_duration":1537829750,
"load_duration":160483256,
"prompt_eval_count":6,
"prompt_eval_duration":42615000,
"eval_count":40,
"eval_duration":1330123000}
*/
    if (!responseJson.isMember("message") ||
        !responseJson["message"].isMember("content") ||
        !responseJson["message"]["content"].isString()) {
        ERR("OllamaLLMProvider sendMessage error, response body is not valid");
        return "";
    }

    std::string response = responseJson["message"]["content"].asString();
    INFO("OllamaLLMProvider sendMessage success, response: {}", response);
    return response;
}

std::string OllamaLLMProvider::sendMessageStream(
    const std::vector<Message> &messages,
    const std::map<std::string, std::string> &params,
    std::function<void(const std::string &, bool)> callback) {
    // 模型是否可用
    if (!_isAvailable) {
        ERR("OllamaLLMProvider sendMessage error, model is not available");
        return "";
    }

    // 构造历史消息
    Json::Value historyMessages(Json::arrayValue);
    for (const auto &message : messages) {
        Json::Value messageJson;
        messageJson["role"] = message._role;
        messageJson["content"] = message._content;
        historyMessages.append(messageJson);
    }

    // 解析参数
    double temperature = 0.7;
    int maxTokens = 2048;
    if (params.find("max_tokens") != params.end()) {
        maxTokens = std::stoi(params.at("max_tokens"));
    }
    if (params.find("temperature") != params.end()) {
        temperature = std::stod(params.at("temperature"));
    }

    // 构造请求体
    Json::Value options;
    options["temperature"] = temperature;
    options["num_ctx"] = maxTokens;
    Json::Value requestBody;
    requestBody["model"] = _modelName;
    requestBody["options"] = options;
    requestBody["messages"] = historyMessages;
    requestBody["stream"] = true;

    // 序列化请求体
    std::string requestBodyStr;
    Json::StreamWriterBuilder builder;
    requestBodyStr = Json::writeString(builder, requestBody);

    // 构造请求头
    httplib::Headers headers = {{"Content-Type", "application/json"}};

    // 构造客户端
    httplib::Client client(_endpoint);
    client.set_connection_timeout(30, 0);
    client.set_read_timeout(30, 0);

    // 构造请求对象
    httplib::Request req;
    req.path = "/api/chat";
    req.body = requestBodyStr;
    req.headers = headers;
    req.method = "POST";

    // 流式处理的变量
    std::string replyContent = "";    // 接受流式内容的缓冲区
    bool gotError = false;            // 是否收到错误信息
    std::string errorContent = "";    // 错误内容
    std::string responseContent = ""; // 完整响应内容
    int statusCode = 0;               // 响应状态码
    bool streamFinished = false;      // 流式响应是否完成

    req.response_handler = [&](const httplib::Response &resp) {
        statusCode = resp.status;
        if (statusCode != 200) {
            gotError = true;
            errorContent =
                "OllamaLLMProvider sendMessageStream response status error: " +
                std::to_string(statusCode);

            ERR("OllamaLLMProvider sendMessageStream response status error: {}",
                statusCode);
            return false;
        }
        return true;
    };

    // 设置流式响应体处理
    req.content_receiver = [&](const char *data, size_t size, size_t offset,
                               size_t totalLength) {
        if (gotError) {
            return false;
        }
        replyContent.append(data, size);
        // DBG("OllamaLLMProvider sendMessageStream response body: {}",
        //     replyContent);
        int pos = 0;
        while ((pos = replyContent.find("\n")) != std::string::npos) {
            std::string line = replyContent.substr(0, pos);
            replyContent.erase(0, pos + 1);
            pos = 0;

            // 解析响应体
            Json::Value responseJson;
            Json::CharReaderBuilder builder;
            std::istringstream responseStream(line);
            std::string errorMsg = "";
            if (!Json::parseFromStream(builder, responseStream, &responseJson,
                                       &errorMsg)) {
                ERR("OllamaLLMProvider sendMessageStream error, response "
                    "body is not valid: {}",
                    errorMsg);
                return false;
            }
            // INFO("responseStream: {}", line);
            if (responseJson.isMember("done")) {
                if (responseJson["done"] == true) {
                    streamFinished = true;
                    callback("", true);
                    return true;
                }
            }
            // 检查响应体是否包含message字段
            if (!responseJson.isMember("message") ||
                !responseJson["message"].isMember("content")) {
                ERR("OllamaLLMProvider sendMessageStream error, response "
                    "body is not valid: {}",
                    errorMsg);
                return false;
            }
            // 提取message字段
            std::string message = responseJson["message"]["content"].asString();
            responseContent += message;
            // callback(message, false);
        }

        return true;
    };
    // 发送请求
    auto res = client.send(req);
    if (!res) {
        ERR("OllamaLLMProvider sendMessageStream error, check your "
            "network...");
        return "";
    }

    if (!streamFinished) {
        callback("", true);
        return responseContent;
    }
    return responseContent;
}
} // namespace chat_sdk
