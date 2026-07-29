#include "../include/SessionManager.hpp"
#include "../include/util/logger.hpp"
#include <sstream>
namespace chat_sdk {
// 生成新的会话ID
std::string SessionManager::generateSessionId() {
    // 会话ID格式: session_时间戳_计数器
    // 获取时间戳
    std::time_t now = std::time(nullptr);

    // 获取计数器,并自增
    _sessionIdCounter.fetch_add(1);

    // 使用ostringstream格式化会话ID
    std::ostringstream oss;
    oss << "session_" << now << "_" << _sessionIdCounter;
    return oss.str();
}
// 生成新的消息ID
std::string SessionManager::generateMessageId(const int64_t messageCount) {
    // 消息ID格式: message_时间戳_计数器
    // 获取时间戳
    std::time_t now = std::time(nullptr);

    // 使用ostringstream格式化消息ID
    std::ostringstream oss;
    oss << "message_" << now << "_" << messageCount;
    return oss.str();
}
// 创建会话
std::string SessionManager::createSession(const std::string &modelName) {
    // 加锁
    std::lock_guard<std::mutex> lock(_mutex);
    // 生成新的会话ID
    std::string sessionId = generateSessionId();
    // 初始化会话信息
    auto sessionIfo = std::make_shared<SessionInfo>(modelName);
    sessionIfo->_sessionId = sessionId;
    sessionIfo->_createTimeTimestamp = std::time(nullptr);
    sessionIfo->_lastUpdateTimeTimestamp = std::time(nullptr);
    // 存储会话信息
    _sessions[sessionId] = sessionIfo;
    INFO("create session: {}, modelName: {}", sessionId, modelName);
    return sessionId;
}
// 获取会话信息
std::shared_ptr<SessionInfo>
SessionManager::getSessionInfo(const std::string &sessionId) const {
    std::lock_guard<std::mutex> lock(_mutex);
    // 检查会话是否存在
    auto it = _sessions.find(sessionId);
    if (it == _sessions.end()) {
        ERR("session not found: {}", sessionId);
        return nullptr;
    }
    return it->second;
}
// 向指定会话添加消息
bool SessionManager::addMessage(const std::string &sessionId,
                                const Message &message) {
    std::lock_guard<std::mutex> lock(_mutex);
    // 检查会话是否存在
    auto sessionIfo = getSessionInfo(sessionId);
    if (sessionIfo == nullptr) {
        ERR("session not found: {}", sessionId);
        return false;
    }
    Message msg(message._role, message._content);
    msg._messageId = generateMessageId(sessionIfo->_messages.size());

    // 更新最后更新时间戳
    updateTimestamp(sessionId);

    // 添加消息
    sessionIfo->_messages.push_back(msg);
    // // 更新会话信息
    // _sessions[sessionId] = sessionIfo;
    INFO("add message: {}", msg._messageId);
    return true;
}
// 获取指定会话的历史消息
std::vector<Message>
SessionManager::getMessages(const std::string &sessionId) const {
    std::lock_guard<std::mutex> lock(_mutex);
    // 检查会话是否存在
    auto sessionIfo = getSessionInfo(sessionId);
    if (sessionIfo == nullptr) {
        ERR("session not found: {}", sessionId);
        return {};
    }
    INFO("get messages count: {}", sessionIfo->_messages.size());
    return sessionIfo->_messages;
}
// 更新时间戳
void SessionManager::updateTimestamp(const std::string &sessionId) {
    std::lock_guard<std::mutex> lock(_mutex);
    // 检查会话是否存在
    auto sessionIfo = getSessionInfo(sessionId);
    if (sessionIfo == nullptr) {
        return;
    }
    // 更新最后更新时间戳
    sessionIfo->_lastUpdateTimeTimestamp = std::time(nullptr);
}
// 获取会话列表
std::vector<std::string> SessionManager::getSessionIds() const {
    std::lock_guard<std::mutex> lock(_mutex);
    // 将所有会话DI按更新时间戳降序排序

    // 获取所有会话ID
    std::vector<std::pair<std::string, std::shared_ptr<SessionInfo>>>
        sessionIds;
    sessionIds.reserve(_sessions.size());
    for (const auto &it : _sessions) {
        sessionIds.push_back(it);
    }
    // 按更新时间戳降序排序
    std::sort(
        sessionIds.begin(), sessionIds.end(),
        [](const std::pair<std::string, std::shared_ptr<SessionInfo>> &a,
           const std::pair<std::string, std::shared_ptr<SessionInfo>> &b) {
            return a.second->_lastUpdateTimeTimestamp >
                   b.second->_lastUpdateTimeTimestamp;
        });
    // 提取会话ID
    std::vector<std::string> sessionIdList;
    for (const auto &it : sessionIds) {
        sessionIdList.push_back(it.first);
    }
    return sessionIdList;
}
// 删除会话
bool SessionManager::deleteSession(const std::string &sessionId) {
    std::lock_guard<std::mutex> lock(_mutex);
    // 检查会话是否存在
    auto it = _sessions.find(sessionId);
    if (it == _sessions.end()) {
        WARN("session not found: {}", sessionId);
        return false;
    }
    // 删除会话
    _sessions.erase(it);
    INFO("delete session: {}", sessionId);
    return true;
}
// 清空所有会话
bool SessionManager::clearSessions() {
    std::lock_guard<std::mutex> lock(_mutex);
    // 清空所有会话
    _sessions.clear();
    _sessionIdCounter.store(0);
    INFO("clear all sessions");
    return true;
}
// 获取会话总数
int64_t SessionManager::getSessionCount() const {
    std::lock_guard<std::mutex> lock(_mutex);
    return _sessionIdCounter.load();
}
} // namespace chat_sdk