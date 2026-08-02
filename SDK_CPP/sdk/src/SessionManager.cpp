#include "../include/SessionManager.hpp"
#include "../include/util/logger.hpp"
#include <sstream>
#include <type_traits>
namespace chat_sdk {
SessionManager::SessionManager(const std::string &dbName)
    : _dataManager(dbName) {
    // 获取所有会话
    auto sessions = _dataManager.GetAllSessions();
    for (auto &session : sessions) {
        _sessions[session->_sessionId] = session;
    }
    INFO("获取所有会话成功: {}", _sessions.size());
}
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
    _mutex.lock();
    // 生成新的会话ID
    std::string sessionId = generateSessionId();
    // 初始化会话信息
    auto sessionIfo = std::make_shared<SessionInfo>(modelName);
    sessionIfo->_sessionId = sessionId;
    sessionIfo->_createTimeTimestamp = std::time(nullptr);
    sessionIfo->_lastUpdateTimeTimestamp = std::time(nullptr);
    // 存储会话信息
    _sessions[sessionId] = sessionIfo;
    // 解锁
    _mutex.unlock();
    // 存储会话信息到数据库
    _dataManager.InsertSession(*sessionIfo);
    INFO("create session: {}, modelName: {}", sessionId, modelName);
    return sessionId;
}
// 获取会话信息
std::shared_ptr<SessionInfo>
SessionManager::getSessionInfo(const std::string &sessionId) {
    // 加锁
    _mutex.lock();

    // 检查会话是否在内存中存在
    auto it = _sessions.find(sessionId);
    if (it != _sessions.end()) {
        // 获取messages信息
        _mutex.unlock();
        it->second->_messages = _dataManager.GetAllMessages(sessionId);
        return it->second;
    }

    _mutex.unlock();
    // 检查会话是否在数据库中存在
    auto sessionIfo = _dataManager.GetSession(sessionId);
    if (sessionIfo != nullptr) {
        // 加锁
        _mutex.lock();
        auto it = _sessions.find(sessionId);
        if (it == _sessions.end()) {
            _sessions[sessionId] = sessionIfo;
        }
        // 解锁
        _mutex.unlock();
        sessionIfo->_messages = _dataManager.GetAllMessages(sessionId);
        return sessionIfo;
    }
    WARN("session not found: {}", sessionId);
    return nullptr;
}
// 向指定会话添加消息
bool SessionManager::addMessage(const std::string &sessionId,
                                const Message &message) {
    // 加锁
    _mutex.lock();
    // 检查会话是否存在
    auto sessionIfo = _sessions.find(sessionId);
    if (sessionIfo == _sessions.end()) {
        // 解锁
        _mutex.unlock();
        WARN("session not found: {}", sessionId);
        return false;
    }
    Message msg(message._role, message._content);
    msg._messageId = generateMessageId(sessionIfo->second->_messages.size());

    // 更新最后更新时间戳
    sessionIfo->second->_lastUpdateTimeTimestamp = std::time(nullptr);

    // 添加消息
    sessionIfo->second->_messages.push_back(msg);
    // 解锁
    _mutex.unlock();
    INFO("add message: {}", msg._messageId);
    // 存储消息到数据库
    _dataManager.InsertMessage(sessionId, msg);
    return true;
}
// 获取指定会话的历史消息
std::vector<Message> SessionManager::getMessages(const std::string &sessionId) {
    _mutex.lock();
    // 检查会话是否在内存中存在
    auto sessionIfo = _sessions.find(sessionId);
    if (sessionIfo != _sessions.end()) {
        // 解锁
        _mutex.unlock();
        INFO("get messages count: {}", sessionIfo->second->_messages.size());
        return sessionIfo->second->_messages;
    }

    _mutex.unlock();
    // 若不存在，则查找数据库中的消息
    auto messages = _dataManager.GetAllMessages(sessionId);
    if (!messages.empty()) {
        _mutex.lock();
        for (auto &msg : messages) {
            sessionIfo->second->_messages.push_back(msg);
            INFO("get messages count: {}",
                 sessionIfo->second->_messages.size());
        }
        _mutex.unlock();
    }

    ERR("not found messages in session: {}", sessionId);
    return {};
}
// 更新时间戳
void SessionManager::updateTimestamp(const std::string &sessionId) {
    _mutex.lock();
    // 检查会话是否存在
    auto sessionIfo = _sessions.find(sessionId);
    if (sessionIfo == _sessions.end()) {
        return;
    }
    // 更新最后更新时间戳
    sessionIfo->second->_lastUpdateTimeTimestamp = std::time(nullptr);
    _mutex.unlock();
    // 更新会话信息到数据库
    _dataManager.UpdateSessionTimestamp(
        sessionId, sessionIfo->second->_lastUpdateTimeTimestamp);
}
// 获取会话列表
std::vector<std::string> SessionManager::getSessionIds() {
    auto sessions = _dataManager.GetAllSessions();

    std::lock_guard<std::mutex> lock(_mutex);
    // 将所有会话DI按更新时间戳降序排序

    // 获取所有会话ID
    std::vector<std::pair<std::time_t, std::shared_ptr<SessionInfo>>>
        sessionIds;
    sessionIds.reserve(_sessions.size());
    for (const auto &it : _sessions) {
        sessionIds.push_back({it.second->_lastUpdateTimeTimestamp, it.second});
    }
    // 合并数据库中的会话
    for (const auto &it : sessions) {
        if (_sessions.find(it->_sessionId) == _sessions.end()) {
            sessionIds.push_back({it->_lastUpdateTimeTimestamp, it});
        }
    }
    // 按更新时间戳降序排序
    std::sort(sessionIds.begin(), sessionIds.end(),
              [](const auto &a, const auto &b) { return a.first > b.first; });
    // 提取会话ID
    std::vector<std::string> sessionIdList;
    for (const auto &it : sessionIds) {
        sessionIdList.push_back(it.second->_sessionId);
    }
    return sessionIdList;
}
// 删除会话
bool SessionManager::deleteSession(const std::string &sessionId) {
    _mutex.lock();
    // 检查会话是否存在
    auto it = _sessions.find(sessionId);
    if (it == _sessions.end()) {
        _mutex.unlock();
        WARN("session not found: {}", sessionId);
        return false;
    }
    // 删除会话
    _sessions.erase(it);
    _mutex.unlock();
    // 删除会话信息
    _dataManager.DeleteSession(sessionId);
    INFO("delete session: {}", sessionId);
    return true;
}
// 清空所有会话
bool SessionManager::clearSessions() {
    _mutex.lock();
    // 清空所有会话
    _sessions.clear();
    _mutex.unlock();
    // 清空所有会话信息
    _dataManager.DeleteAllSessions();
    INFO("clear all sessions");
    return true;
}
// 获取会话总数
int64_t SessionManager::getSessionCount() const {
    std::lock_guard<std::mutex> lock(_mutex);
    return _sessions.size();
}
} // namespace chat_sdk