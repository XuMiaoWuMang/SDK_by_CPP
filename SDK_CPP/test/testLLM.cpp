// #include "../sdk/include/ChatSDK.hpp"
// #include "../sdk/include/DeepSeekProvider.hpp"
// #include "../sdk/include/OllamaLLMProvider.hpp"
// #include "../sdk/include/util/logger.hpp"
#include <ai_cpp_sdk/ChatSDK.hpp>
#include <ai_cpp_sdk/util/logger.hpp>
#include <gtest/gtest.h>

// TEST(DeepSeekProvider, initModel) {
//     // 初始化测试参数
//     std::map<std::string, std::string> modelConfig = {
//         {"base_url", "https://api.deepseek.com"},
//         {"api_key", std::getenv("DEEPSEEK_API_KEY")},
//         {"model_name", "deepseek-v4-flash"}};

//     // 实例化DeepSeekProvider
//     auto provider = std::make_shared<chat_sdk::DeepSeekProvider>();
//     provider->initModel(modelConfig);
//     // 检查模型是否可用
//     ASSERT_TRUE(provider->isAvailable());
//     // 数据包的参数
//     std::map<std::string, std::string> requestParams = {
//         {"temperature", "0.7"},
//         {"max_tokens", "2048"},
//     }; // 测试消息
//     std::vector<chat_sdk::Message> messages = {
//         chat_sdk::Message("user", "你好,你是谁?"),
//     };
//     // 发送消息
//     // 全量返回
//     // std::string response = provider->sendMessage(messages, requestParams);

//     // 流式返回
//     auto writeFunc = [&](const std::string &content, bool streamFinished) {
//         INFO(content);
//         if (streamFinished) {
//             INFO("[DONE]");
//         }
//     };
//     std::string response =
//         provider->sendMessageStream(messages, requestParams, writeFunc);
//     EXPECT_FALSE(response.empty());

//     INFO("response: {}", response);
//     // 检查响应是否为空
// }
// TEST(OllamaLLMProviderTest, sendMessage) {
//     auto ollamaLLMProvider = std::make_shared<chat_sdk::OllamaLLMProvider>();
//     ASSERT_TRUE(ollamaLLMProvider != nullptr);
//     std::map<std::string, std::string> param_map;
//     param_map["model_name"] = "deepseek-r1:1.5b";
//     param_map["endpoint"] = "http://127.0.0.1:11434";
//     param_map["model_desc"] =
//         "本地部署deepseek-r1:1.5b模型, 采⽤专家混合架构，专注于深度理解与推理
//         ";
//     ollamaLLMProvider->initModel(param_map);
//     ASSERT_TRUE(ollamaLLMProvider->isAvailable());
//     std::vector<chat_sdk::Message> messages;
//     messages.push_back({"user", "你好"});
//     // std::string response = ollamaLLMProvider->sendMessage(messages,
//     // param_map);

//     // 流式返回
//     auto writeFunc = [&](const std::string &content, bool streamFinished) {
//         INFO(content);
//         if (streamFinished) {
//             INFO("[DONE]");
//         }
//     };
//     std::string response =
//         ollamaLLMProvider->sendMessageStream(messages, param_map, writeFunc);

//     EXPECT_FALSE(response.empty());
//     INFO("response {}", response);
// }
TEST(ChatSDKTest, sendMessage) {
    auto sdk = std::make_shared<chat_sdk::ChatSDK>();
    ASSERT_TRUE(sdk);
    // 模型的配置数组
    // deepseek远端模型
    auto deepseekConfig = std::make_shared<chat_sdk::RemoteConfig>();
    deepseekConfig->_apiKey = std::getenv("DEEPSEEK_API_KEY");
    deepseekConfig->_modelName = "deepseek-v4-flash";

    // ollama本地模型
    auto ollamaConfig = std::make_shared<chat_sdk::OllamaConfig>();
    ollamaConfig->_modelName = "deepseek-r1:1.5b";
    ollamaConfig->_endpoint = "http://127.0.0.1:11434";
    ollamaConfig->_modelDesc =
        "本地部署deepseek-r1:1.5b模型, 采⽤专家混合架构，专注于深度理解与推理";

    std::vector<std::shared_ptr<chat_sdk::Config>> configLists = {
        deepseekConfig, ollamaConfig};

    sdk->initLLMManager(configLists); // 初始化LLMManager
    // 发送消息
    std::string sessionId = sdk->createSession("deepseek-v4-flash");
    // std::string response = sdk->sendMessage(sessionId, "你好,你是谁?");
    // 流式返回
    auto writeFunc = [&](const std::string &content, bool streamFinished) {
        INFO(content);
        if (streamFinished) {
            INFO("[DONE]");
        }
    };
    std::string response =
        sdk->sendMessageStream(sessionId, "你好,你是谁?", writeFunc);
    EXPECT_FALSE(response.empty());
    INFO("response: {}", response);
}

int main(int argc, char **argv) {
    // 初始化日志
    sdk_logger::Logger::initLogger("testLLM", "stdout", spdlog::level::debug);
    ::testing::InitGoogleTest(&argc, argv);

    // 运行测试
    return RUN_ALL_TESTS();
}
