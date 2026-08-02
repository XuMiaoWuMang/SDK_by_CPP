#include "../include/DeepSeekProvider.hpp"
#include "../include/util/logger.hpp"
#include <httplib.h>
#include <jsoncpp/json/json.h>
#include <jsoncpp/json/reader.h>
#include <jsoncpp/json/writer.h>
namespace chat_sdk {
bool DeepSeekProvider::initModel(
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
        _endpoint = "https://api.deepseek.com";
    }

    it = configMap.find("model_name");
    if (it != configMap.end()) {
        _modelName = it->second;
    } else {
        _modelName = "deepseek-v4-flash";
    }

    _isAvailable = true;
    INFO("DeepSeekProvider initModel success, endpoint: {}", _endpoint);
    return true;
}
// 检查模型是否可用
bool DeepSeekProvider::isAvailable() const { return _isAvailable; }
// 获取模型名称
std::string DeepSeekProvider::GetModelName() const { return _modelName; }
// 获取模型描述
std::string DeepSeekProvider::GetModelDesc() const {
    return "由深度求索公司打造的⼀款实用性强、中⽂优化的通用对话助⼿, "
           "适合日常问答与创作。";
}
// 发送消息 - 全量返回
std::string DeepSeekProvider::sendMessage(
    const std::vector<Message> &messages,
    const std::map<std::string, std::string> &params) {

    if (!_isAvailable) {
        ERR("DeepSeekProvider is not available");
        return "";
    }

    double temperature = 0.7;
    long max_tokens = 2048;

    auto it = params.find("temperature");
    if (it != params.end()) {
        temperature = std::stod(it->second);
    }
    it = params.find("max_tokens");
    if (it != params.end()) {
        max_tokens = std::stoi(it->second);
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
    request["messages"] = Message_Array;
    request["temperature"] = temperature;
    request["max_tokens"] = max_tokens;
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
    httplib::Headers headers = {{"Authorization", "Bearer " + _api_key},
                                {"Content-Type", "application/json"}};

    // 发送POST请求
    auto resp = client.Post("/chat/completions", headers, request_str,
                            "application/json");
    if (!resp) {
        ERR("httplib::Post failed");
        return "DeepSeek response error";
    }

    INFO("DeepSeekProvider sendMessage response body: {}", resp->body);

    if (resp->status != 200) {
        ERR("httplib::Post failed, status: {}", resp->status);
        return "DeepSeek response status error";
    }
    INFO("DeepSeekProvider sendMessage response status: {}", resp->status);

    // 解析响应体
    Json::CharReaderBuilder reader;
    Json::Value response;
    std::string parseError;
    std::istringstream iss(resp->body);
    // 如果解析失败，返回错误
    if (!Json::parseFromStream(reader, iss, &response, &parseError)) {
        ERR("Json::parseFromStream failed, error: {}", parseError);
        return "DeepSeek response content parse error";
    }
    // 如果choices字段不存在或者不是数组类型，返回错误
    if (!response.isMember("choices") || !response["choices"].isArray()) {
        ERR("Json::parseFromStream failed, choices is not array type");
        return "DeepSeek response content parse error";
    }
    // 如果choices的数组没有message字段或者message字段不是对象类型，返回错误
    if (!response["choices"][0].isMember("message") ||
        !response["choices"][0]["message"].isObject()) {
        ERR("Json::parseFromStream failed, choices[0].message is not object "
            "type");
        return "DeepSeek response content parse error";
    }
    // 如果choices的数组没有message.content字段或者message.content字段不是字符串类型，返回错误
    if (!response["choices"][0]["message"].isMember("content") ||
        !response["choices"][0]["message"]["content"].isString()) {
        ERR("Json::parseFromStream failed, choices[0].message.content is not "
            "exist or not string type");
        return "DeepSeek response content parse error";
    }
    // 提取回复内容
    std::string replyContent =
        response["choices"][0]["message"]["content"].asString();
    INFO("DeepSeekProvider sendMessage response content: {}", replyContent);
    return replyContent;
}
// 发送消息 - 流式返回
std::string DeepSeekProvider::sendMessageStream(
    const std::vector<Message> &messages,
    const std::map<std::string, std::string> &params,
    std::function<void(const std::string &, bool)> callback) {
    if (!_isAvailable) {
        ERR("DeepSeekProvider is not available");
        return "DeepSeekProvider ERROR";
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
    request["max_tokens"] = max_tokens;
    request["stream"] = true;

    // 构造请求头
    httplib::Headers headers = {{"Authorization", "Bearer " + _api_key},
                                {"Content-Type", "application/json"},
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
    std::string replyContent = "";    // 接受流式内容的缓冲区
    bool gotError = false;            // 是否收到错误信息
    std::string errorContent = "";    // 错误内容
    std::string responseContent = ""; // 完整响应内容
    int statusCode = 0;               // 响应状态码
    bool streamFinished = false;      // 流式响应是否完成

    // 创建请求对象
    httplib::Request req;
    req.path = "/chat/completions";
    req.body = json_string;
    req.headers = headers;
    req.method = "POST";

    // 设置响应头处理
    req.response_handler = [&](const httplib::Response &resp) {
        statusCode = resp.status;
        if (statusCode != 200) {
            gotError = true;
            errorContent =
                "DeepSeekProvider sendMessageStream response status error: " +
                std::to_string(statusCode);

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
        replyContent.append(data, size);
        // DBG("DeepSeekProvider sendMessageStream response body: {}",
        //     replyContent);
        int pos = 0;
        while ((pos = replyContent.find("\n\n")) != std::string::npos) {

            // 接受一个chunk内容
            std::string chunk = replyContent.substr(0, pos);
            DBG("DeepSeekProvider sendMessageStream response chunk: {}", chunk);
            replyContent.erase(0, pos + 2);
            pos = 0;
            // 处理空⾏和注释, 以:开头的是注释⾏
            if (chunk.empty() || chunk[0] == ':') {
                continue;
            }

            // 检查事件类型
            if (chunk.compare(0, 6, "data: ") == 0) {
                std::string jsonStr = chunk.substr(6);
                // 处理结束标记
                if (jsonStr == "[DONE]") {
                    INFO("DeepSeekProvider sendMessageStream stream finished");
                    streamFinished = true;
                    return true;
                }

                // 解析JSON字符串
                Json::Value response;
                Json::CharReaderBuilder reader;
                std::string errorMsg;
                std::istringstream iss(jsonStr);
                // 解析失败，打印错误信息
                if (!Json::parseFromStream(reader, iss, &response, &errorMsg)) {
                    ERR("DeepSeekProvider sendMessageStream parse JSON error: "
                        "{}",
                        errorMsg);
                    return false;
                }

                // 如果choices字段不存在或者不是数组类型，返回错误
                if (!response.isMember("choices") ||
                    !response["choices"].isArray()) {
                    ERR("ChatGPTProvider sendMessageStream parse JSON error, "
                        "choices is not array "
                        "type");
                    return false;
                }
                // 如果choices的数组没有message字段或者message字段不是对象类型，返回错误
                if (!response["choices"][0].isMember("delta") ||
                    !response["choices"][0]["delta"].isObject()) {
                    ERR("Json::parseFromStream failed, choices[0].delta is "
                        "not object "
                        "type");
                    return false;
                }
                // 如果choices的数组没有delta.content字段，返回错误
                if (!response["choices"][0]["delta"].isMember("content")) {
                    ERR("Json::parseFromStream failed, "
                        "choices[0].delta.content is not "
                        "exist or not string type");
                    return false;
                }
                // 提取回复内容
                std::string Content =
                    response["choices"][0]["delta"]["content"].asString();
                INFO("DeepSeekProvider sendMessageStream response content: {}",
                     Content);
                responseContent += Content;

                // 调用回调函数, 将模型返回的数据给用户
                callback(Content, streamFinished);
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
        ERR("DeepSeekProvider sendMessageStream send request failed, please "
            "check network connection");
        return "";
    }

    // 确保流式操作正常处理
    if (!streamFinished) {
        WARN("DeepSeekProvider sendMessageStream stream not finished");
        callback("", true);
    }
    DBG("DeepSeekProvider sendMessageStream response content: {}",
        responseContent);
    return responseContent;
};

} // namespace chat_sdk