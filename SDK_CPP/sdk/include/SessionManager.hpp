#include "common.hpp"
#include <atomic>
#include <memory>
#include <mutex>
#include <unordered_map>
namespace chat_sdk {
class SessionManager {
  public:
    // 创建会话
    std::string createSession(const std::string &modelName);
    // 获取会话信息
    std::shared_ptr<SessionInfo>
    getSessionInfo(const std::string &sessionId) const;
    // 向指定会话添加消息
    bool addMessage(const std::string &sessionId, const Message &message);
    // 获取指定会话的历史消息
    std::vector<Message> getMessages(const std::string &sessionId) const;
    // 更新时间戳
    void updateTimestamp(const std::string &sessionId);
    // 获取会话列表
    std::vector<std::string> getSessionIds() const;
    // 删除会话
    bool deleteSession(const std::string &sessionId);
    // 清空所有会话
    bool clearSessions();
    // 获取会话总数
    int64_t getSessionCount() const;

  private:
    // 生成新的会话ID
    std::string generateSessionId();
    // 生成新的消息ID
    std::string generateMessageId(const int64_t messageCount);

  private:
    std::unordered_map<std::string, std::shared_ptr<SessionInfo>> _sessions;
    mutable std::mutex
        _mutex; // mutable
                // 可以让锁即使在const成员函数中，也能被修改(加锁和释放锁)
    std::atomic<int64_t> _sessionIdCounter = {0};
};
} // namespace chat_sdk
