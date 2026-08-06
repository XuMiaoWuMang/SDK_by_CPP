#include "../include/ChatGPTProvider.hpp"
#include "../include/util/logger.hpp"
#include <httplib.h>
#include <jsoncpp/json/json.h>
#include <jsoncpp/json/reader.h>
#include <jsoncpp/json/writer.h>
#include <sstream>

namespace chat_sdk {
bool ChatGPTProvider::initModel(
    const std::map<std::string, std::string> &configMap) {
    auto it = configMap.find("api_key");
    if (it == configMap.end()) {
        ERR("api_key is not found in configMap");
        return false;
    } else {
        _api_key = it->second;
    }

    it = configMap.find("base_url");
    if (it != configMap.end()) {
        _endpoint = it->second;
    } else {
        _endpoint = "https://api.openai.com";
    }

    it = configMap.find("model_name");
    if (it != configMap.end()) {
        _modelName = it->second;
    } else {
        it = configMap.find("model");
        _modelName = (it != configMap.end()) ? it->second : "gpt-3.5-turbo";
    }

    it = configMap.find("model_desc");
    if (it != configMap.end()) {
        _modelDesc = it->second;
    }

    _isAvailable = true;
    INFO("ChatGPTProvider initModel success, endpoint: {}", _endpoint);
    return true;
}
// 检查模型是否可用
bool ChatGPTProvider::isAvailable() const { return _isAvailable; }
// 获取模型名称
std::string ChatGPTProvider::GetModelName() const { return _modelName; }
// 获取模型描述: 配置 desc 优先, 空时使用默认文案
std::string ChatGPTProvider::GetModelDesc() const {
    if (!_modelDesc.empty()) {
        return _modelDesc;
    }
    return "由OpenAI公司打造的⼀款实用性强、中⽂优化的通用对话助⼿, "
           "适合日常问答与创作。";
}
// 发送消息 - 全量返回
std::string
ChatGPTProvider::sendMessage(const std::vector<Message> &messages,
                             const std::map<std::string, std::string> &params) {

    if (!_isAvailable) {
        ERR("ChatGPTProvider is not available");
        return "";
    }

    double temperature = 0.7;
    long max_input_tokens = 2048;

    auto it = params.find("temperature");
    if (it != params.end()) {
        temperature = std::stod(it->second);
    }
    it = params.find("max_input_tokens");
    if (it != params.end()) {
        max_input_tokens = std::stoi(it->second);
    }

    // 构造历史消息
    Json::Value Message_Array(Json::arrayValue);
    for (auto &msg : messages) {
        Json::Value message(Json::objectValue);
        message["role"] = msg._role;
        message["content"] = msg._content;
        Message_Array.append(message);
    }

    // 构造请求体
    Json::Value request(Json::objectValue);
    request["model"] = _modelName;
    request["input"] = Message_Array;
    request["temperature"] = temperature;
    request["max_input_tokens"] = max_input_tokens;
    request["stream"] = false;

    // 序列化请求体
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";

    std::string request_str = Json::writeString(builder, request);
    if (request_str.empty()) {
        ERR("Json::writeString failed");
        return "";
    }
    INFO("request_str: {}", request_str);

    // 使用httplib库创建客户端
    httplib::Client client(_endpoint.c_str());
    client.set_connection_timeout(30, 0); // 设置连接30秒超时
    client.set_read_timeout(60, 0);       // 设置读取60秒超时

    // 设置请求头
    httplib::Headers headers = {{"Authorization", "Bearer " + _api_key}};

    // 发送POST请求
    auto resp =
        client.Post("/v1/responses", headers, request_str, "application/json");
    if (!resp) {
        ERR("httplib::Post failed");
        return "ChatGPT response error";
    }

    INFO("ChatGPTProvider sendMessage response body: {}", resp->body);

    if (resp->status != 200) {
        ERR("httplib::Post failed, status: {}", resp->status);
        return "ChatGPT response status error";
    }
    INFO("ChatGPTProvider sendMessage response status: {}", resp->status);

    // 解析响应体
    Json::CharReaderBuilder reader;
    Json::Value response;
    std::string parseError;
    std::istringstream iss(resp->body);
    // 如果解析失败，返回错误
    if (!Json::parseFromStream(reader, iss, &response, &parseError)) {
        ERR("Json::parseFromStream failed, error: {}", parseError);
        return "ChatGPT response content parse error";
    }
    // 如果output字段不存在或者不是数组类型，返回错误
    if (!response.isMember("output") || !response["output"].isArray()) {
        ERR("Json::parseFromStream failed, output is not array type");
        return "ChatGPT response content parse error";
    }
    // 如果output的数组没有content字段或者content字段不是对象类型，返回错误
    if (!response["output"].isMember("content") ||
        !response["output"]["content"].isObject()) {
        ERR("Json::parseFromStream failed, output.content is not object "
            "type");
        return "ChatGPT response content parse error";
    }
    // 如果output的数组没有content字段或者content字段不是字符串类型，返回错误
    if (!response["output"]["content"][0].isMember("text") ||
        !response["output"]["content"][0]["text"].isString()) {
        ERR("Json::parseFromStream failed, output.content.text is not exist or "
            "not string type");
        return "ChatGPT response content parse error";
    }
    // 提取回复内容
    std::string replyContent =
        response["output"]["content"][0]["text"].asString();
    INFO("ChatGPTProvider sendMessage response content: {}", replyContent);
    return replyContent;
}
// 发送消息 - 流式返回
std::string ChatGPTProvider::sendMessageStream(
    const std::vector<Message> &messages,
    const std::map<std::string, std::string> &params,
    std::function<void(const std::string &, bool)> callback) {
    if (!_isAvailable) {
        ERR("ChatGPTProvider is not available");
        return "ChatGPTProvider ERROR";
    }

    // 历史消息
    Json::Value Message_Array(Json::arrayValue);
    for (auto &msg : messages) {
        Json::Value message(Json::objectValue);
        message["role"] = msg._role;
        message["content"] = msg._content;
        Message_Array.append(message);
    }
    // 请求参数
    std::string model = _modelName;
    if (params.find("model") != params.end()) {
        model = params.at("model");
    }
    double temperature = 0.7;
    if (params.find("temperature") != params.end()) {
        temperature = std::stod(params.at("temperature"));
    }
    int max_tokens = 2048;
    if (params.find("max_tokens") != params.end()) {
        max_tokens = std::stoi(params.at("max_tokens"));
    }

    // 构造请求体
    Json::Value request(Json::objectValue);
    request["model"] = model;
    request["messages"] = Message_Array;
    request["temperature"] = temperature;
    request["max_output_tokens"] = max_tokens;
    request["stream"] = true;

    // 构造请求头
    httplib::Headers headers = {{"Authorization", "Bearer " + _api_key},
                                {"Accept", "text/event-stream"}};
    // 构造客户端
    httplib::Client client(_endpoint.c_str());
    client.set_connection_timeout(30, 0); // 设置连接30秒超时
    client.set_read_timeout(600, 0);      // 设置读取600秒超时

    // 序列化
    Json::StreamWriterBuilder writer;
    std::string json_string = Json::writeString(writer, request);
    INFO("DeepSeekProvider sendMessageStream request body: {}", json_string);

    // 流式处理的变量
    std::string replyContent = "";    // 接受流式内容
    bool gotError = false;            // 是否收到错误信息
    std::string errorContent = "";    // 错误内容
    std::string responseContent = ""; // 响应内容
    int statusCode = 0;               // 响应状态码
    bool streamFinished = false;      // 流式响应是否完成

    // 创建请求对象
    httplib::Request req;
    req.path = "/v1/responses";
    req.body = json_string;
    req.headers = headers;
    req.method = "POST";

    // 设置响应头处理
    req.response_handler = [&](const httplib::Response &resp) {
        statusCode = resp.status;
        if (statusCode != 200) {
            ERR("DeepSeekProvider sendMessageStream response status error: {}",
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
        replyContent += std::string(data, size);
        // DBG("ChatGPTProvider sendMessageStream response body: {}",
        //     replyContent);
        int pos = 0;
        while ((pos = replyContent.find("\n\n")) != std::string::npos) {

            // 接受一个chunk内容
            std::string chunk = replyContent.substr(0, pos);
            DBG("ChatGPTProvider sendMessageStream response chunk: {}", chunk);
            replyContent.erase(0, pos + 2);
            // 解析chunk内容
            Json::Value chunkJson;
            Json::CharReaderBuilder reader;
            std::string errorMsg;
            std::istringstream chunkStream(chunk);
            if (!Json::parseFromStream(reader, chunkStream, &chunkJson,
                                       &errorMsg)) {
                ERR("Json::parseFromStream failed, chunk is not valid JSON, "
                    "error: {}",
                    errorMsg);
                return false;
            }

            std::istringstream eventStream(chunk);
            std::string line = "";
            std::string eventType = "";
            std::string eventData = "";
            // 循环处理时间类型和内容
            while (std::getline(eventStream, line)) {
                if (line.compare(0, 5, "data:") == 0) {
                    eventData = line.substr(6);
                } else if (line.compare(0, 6, "event: ") == 0) {
                    eventType = line.substr(7);
                }
            }

            // 分别处理不同类型的事件
            if (eventType == "response.output_text.delta") {
                if (chunkJson.isMember("delta") &&
                    chunkJson["delta"].isString()) {
                    std::string delta = chunkJson["delta"].asString();
                    callback(delta, false);
                }
            } else if (eventType == "response.output_item.done") {
                if (chunkJson.isMember("item") &&
                    chunkJson["item"].isObject() &&
                    chunkJson["item"].isMember("content") &&
                    chunkJson["item"]["content"].isArray() &&
                    chunkJson["item"]["content"].empty() &&
                    chunkJson["item"]["content"][0].isMember("text") &&
                    chunkJson["item"]["content"][0]["text"].isString()) {
                    responseContent +=
                        chunkJson["item"]["content"][0]["text"].asString();
                }
            } else if (eventType == "response.completed") {
                streamFinished = true;
                callback("", true);
                return true;
            }
        }
        return true;
    };

    // 发送请求
    auto response = client.send(
        req); // 注：send方法会阻塞，除非已经设置了content_receiver函数
    // 阻塞模式下，send的返回值表示响应结果，客户端收到服务端的完整响应之后，客户端才能获取具体的响应结果
    // 非阻塞模式下，send的返回值只表示请求是否成功发送，不表示响应结果，响应结果由response_handler函数处理

    // 若返回值为空，可能是因为网络问题、DNS解析失败等情况，导致请求失败
    if (!response) {
        ERR("ChatGPTProvider sendMessageStream send request failed, please "
            "check network connection");
        return "";
    }

    // 确保流式操作正常处理
    if (!streamFinished) {
        WARN("ChatGPTProvider sendMessageStream stream not finished");
        callback("", true);
    }
    DBG("ChatGPTProvider sendMessageStream response content: {}",
        responseContent);
    return responseContent;
};

} // namespace chat_sdk