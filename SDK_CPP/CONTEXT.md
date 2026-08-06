# AI_SDK_CPP

基于 C++17 的智能聊天 SDK（`sdk/`），封装云端大模型（DeepSeek / Gemini / ChatGPT）与本地 Ollama 模型的调用、会话持久化与消息收发；`ChatServer/` 是基于该 SDK 的 HTTP 聊天服务。

## Language

**模型 (model)**:
一个可对话的大语言模型实例，如 deepseek-v4-flash、deepseek-r1:1.5b。
_Avoid_: 大模型、LLM

**Provider**:
模型的提供方，也是配置中唯一区分云端/本地的字段：`ollama` 为本地，其余（deepseek/gemini/chatgpt）为云端。
_Avoid_: 厂商、供应商

**云端模型 (remote model)**:
需要 API Key 的在线模型；配置条目带 `provider` 非 ollama。
_Avoid_: 远端模型、线上模型

**本地模型 (local model)**:
通过 Ollama 部署的本机模型；配置条目 `provider` 为 ollama，必须提供 `endpoint`。
_Avoid_: 离线模型

**会话 (session)**:
一次连续对话的容器，拥有唯一会话 ID、所属模型、消息列表与时间戳。
_Avoid_: 对话组、聊天记录

**消息 (message)**:
会话中的单条记录，由角色（user/assistant）与内容组成。
_Avoid_: 聊天内容、对话条目

**全量消息 (full message)**:
一次请求返回完整回复的发送方式。
_Avoid_: 同步消息

**流式消息 (stream message)**:
以 SSE 增量逐帧返回回复的发送方式，回调携带增量数据与结束标志。
_Avoid_: 异步消息、增量返回

**配置 (config)**:
ChatServer 的 JSON 配置文件（env.conf），承载服务器参数、全局默认模型参数与模型列表。
_Avoid_: 参数文件、设置

**API Key**:
云端模型的访问密钥；通过环境变量提供，配置文件中可选的 `api_key` 字段可覆盖。
_Avoid_: 密钥、token、口令
