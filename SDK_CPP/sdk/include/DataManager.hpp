#pragma once
#include "common.hpp"
#include <memory>
#include <mutex>
#include <sqlite3.h>
#include <string>
namespace chat_sdk {
class DataManager {
  public:
    DataManager(const std::string &dbName);
    ~DataManager();

    // Session相关操作
    // 插入Session到数据库
    bool InsertSession(const SessionInfo &sessionInfo);
    // 获取指定Session
    std::shared_ptr<SessionInfo> GetSession(const std::string &sessionId);
    // 更新时间戳
    bool UpdateSessionTimestamp(const std::string &sessionId,
                                std::time_t lastUpdateTimeTimestamp);
    // 删除指定Session
    bool DeleteSession(const std::string &sessionId);
    // 获取所有SessionId
    std::vector<std::string> GetAllSessionsIds();
    // 获取所有Session
    std::vector<std::shared_ptr<SessionInfo>> GetAllSessions();
    // 获取会话总数
    size_t GetSessionsCount() const;
    // 删除所有会话
    bool DeleteAllSessions();
    // Message相关操作
    // 插入Message到数据库
    bool InsertMessage(const std::string &sessionId, const Message &message);
    // 获取指定Session的所有Message
    std::vector<Message> GetAllMessages(const std::string &sessionId);
    // 删除指定会话的历史消息
    bool DeleteMessages(const std::string &sessionId);

  private:
    // 初始化数据库
    bool InitDB();
    // 关闭数据库连接
    void CloseDB();
    // 执行SQL语句
    bool ExecuteSQL(const std::string &sql);

  private:
    sqlite3 *_db = nullptr;
    std::string _dbName;
    mutable std::mutex _mutex;
};
} // namespace chat_sdk
