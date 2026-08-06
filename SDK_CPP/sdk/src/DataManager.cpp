#include "../include/DataManager.hpp"
#include "../include/util/logger.hpp"

namespace chat_sdk {
DataManager::DataManager(const std::string &dbName) : _dbName(dbName) {
    if (sqlite3_open(_dbName.c_str(), &_db) != SQLITE_OK) {
        ERR("连接数据库失败: {}", _dbName);
    } else {
        INFO("连接数据库成功: {}", _dbName);
    }
    if (_db == nullptr) {
        ERR("数据库未初始化");
        return;
    }
    if (!InitDB()) {
        ERR("初始化数据库失败: {}", _dbName);
    } else {
        INFO("初始化数据库成功: {}", _dbName);
    }
}
DataManager::~DataManager() { CloseDB(); }

// Session相关操作
// 插入Session到数据库
bool DataManager::InsertSession(const SessionInfo &sessionInfo) {
    if (_db == nullptr) {
        ERR("数据库未初始化");
        return false;
    }
    std::lock_guard<std::mutex> lock(_mutex);
    // 准备SQL语句
    std::string insertSessionSQL = R"(
            INSERT INTO sessions (sessionId, modelName, createTimeTimestamp, lastUpdateTimeTimestamp)
            VALUES (?, ?, ?, ?);
        )";

    // 编译SQL语句
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(_db, insertSessionSQL.c_str(), -1, &stmt, nullptr) !=
        SQLITE_OK) {
        ERR("编译InsertSession失败: {}", sqlite3_errmsg(_db));
        return false;
    }
    // 绑定参数
    sqlite3_bind_text(stmt, 1, sessionInfo._sessionId.c_str(), -1,
                      SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, sessionInfo._modelName.c_str(), -1,
                      SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 3,
                       static_cast<int64_t>(sessionInfo._createTimeTimestamp));
    sqlite3_bind_int64(
        stmt, 4, static_cast<int64_t>(sessionInfo._lastUpdateTimeTimestamp));

    // 执行SQL语句
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        ERR("执行InsertSession失败: {}", sqlite3_errmsg(_db));
        sqlite3_finalize(stmt);
        return false;
    }
    INFO("插入Session成功: {}", sessionInfo._sessionId);
    // 释放SQL语句
    sqlite3_finalize(stmt);
    return true;
}
// 获取指定Session
std::shared_ptr<SessionInfo>
DataManager::GetSession(const std::string &sessionId) {
    if (_db == nullptr) {
        ERR("数据库未初始化");
        return nullptr;
    }

    std::lock_guard<std::mutex> lock(_mutex);
    // 准备SQL语句
    std::string getSessionSQL = R"(
            SELECT sessionId, modelName, createTimeTimestamp, lastUpdateTimeTimestamp
            FROM sessions
            WHERE sessionId = ?
        )";
    // 编译SQL语句
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(_db, getSessionSQL.c_str(), -1, &stmt, nullptr) !=
        SQLITE_OK) {
        ERR("编译GetSession失败: {}", sqlite3_errmsg(_db));
        return nullptr;
    }
    // 绑定参数
    sqlite3_bind_text(stmt, 1, sessionId.c_str(), -1, SQLITE_TRANSIENT);
    // 执行SQL语句
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        ERR("执行GetSession失败: {}", sqlite3_errmsg(_db));
        sqlite3_finalize(stmt);
        return nullptr;
    }
    // 解析结果
    SessionInfo sessionInfo;
    sessionInfo._sessionId =
        reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
    sessionInfo._modelName =
        reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
    sessionInfo._createTimeTimestamp =
        static_cast<std::time_t>(sqlite3_column_int64(stmt, 2));
    sessionInfo._lastUpdateTimeTimestamp =
        static_cast<std::time_t>(sqlite3_column_int64(stmt, 3));
    // 解析消息列表
    // 从数据库中获取所有指定会话的所有消息
    std::vector<Message> messages = GetAllMessages(sessionId);
    sessionInfo._messages.insert(sessionInfo._messages.end(), messages.begin(),
                                 messages.end());
    INFO("获取SessionInfo成功: {}", sessionId);
    // 释放SQL语句
    sqlite3_finalize(stmt);
    return std::make_shared<SessionInfo>(sessionInfo);
}
// 更新指定Session的最后更新时间戳
bool DataManager::UpdateSessionTimestamp(const std::string &sessionId,
                                         std::time_t lastUpdateTimeTimestamp) {
    if (_db == nullptr) {
        ERR("数据库未初始化");
        return false;
    }
    std::lock_guard<std::mutex> lock(_mutex);
    // 准备更新语句
    std::string updateSessionSQL = R"(
            UPDATE sessions
            SET lastUpdateTimeTimestamp = ?
            WHERE sessionId = ?
        )";
    // 编译更新语句
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(_db, updateSessionSQL.c_str(), -1, &stmt, nullptr) !=
        SQLITE_OK) {
        ERR("编译UpdateSession失败: {}", sqlite3_errmsg(_db));
        return false;
    }
    // 绑定参数
    sqlite3_bind_int64(stmt, 1, static_cast<int64_t>(lastUpdateTimeTimestamp));
    sqlite3_bind_text(stmt, 2, sessionId.c_str(), -1, SQLITE_TRANSIENT);
    // 执行更新语句
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        ERR("执行UpdateSession失败: {}", sqlite3_errmsg(_db));
        sqlite3_finalize(stmt);
        return false;
    }
    INFO("更新Session成功: {}", sessionId);
    // 释放更新语句
    sqlite3_finalize(stmt);
    return true;
}
// 删除指定Session
bool DataManager::DeleteSession(const std::string &sessionId) {
    if (_db == nullptr) {
        ERR("数据库未初始化");
        return false;
    }
    std::lock_guard<std::mutex> lock(_mutex);
    // 准备删除语句
    std::string deleteSessionSQL = R"(
            DELETE FROM sessions
            WHERE sessionId = ?
        )";
    // 编译删除语句
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(_db, deleteSessionSQL.c_str(), -1, &stmt, nullptr) !=
        SQLITE_OK) {
        ERR("编译DeleteSession失败: {}", sqlite3_errmsg(_db));
        return false;
    }
    // 绑定参数
    sqlite3_bind_text(stmt, 1, sessionId.c_str(), -1, SQLITE_TRANSIENT);
    // 执行删除语句
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        ERR("执行DeleteSession失败: {}", sqlite3_errmsg(_db));
        sqlite3_finalize(stmt);
        return false;
    }
    INFO("删除Session成功: {}", sessionId);
    // 释放删除语句
    sqlite3_finalize(stmt);
    return true;
}
// 获取所有SessionId
std::vector<std::string> DataManager::GetAllSessionsIds() {
    if (_db == nullptr) {
        ERR("数据库未初始化");
        return {};
    }
    std::lock_guard<std::mutex> lock(_mutex);
    // 准备查询语句
    // 将查询结果以最后更新事件戳降序排序
    std::string getAllSessionsIdsSQL = R"(
            SELECT sessionId FROM sessions
            ORDER BY lastUpdateTimeTimestamp DESC
        )";
    // 编译查询语句
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(_db, getAllSessionsIdsSQL.c_str(), -1, &stmt,
                           nullptr) != SQLITE_OK) {
        ERR("编译GetAllSessionsIds失败: {}", sqlite3_errmsg(_db));
        return {};
    }
    // 执行查询语句
    std::vector<std::string> sessionIds;
    int ret = sqlite3_step(stmt);
    while (ret == SQLITE_ROW) {
        sessionIds.push_back(
            reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0)));
        ret = sqlite3_step(stmt);
    }
    if (ret != SQLITE_DONE) {
        ERR("执行GetAllSessionsIds失败: {}", sqlite3_errmsg(_db));
        sqlite3_finalize(stmt);
        return {};
    }

    INFO("获取所有SessionId成功: {}条SessionId", sessionIds.size());
    // 释放查询语句
    sqlite3_finalize(stmt);
    return sessionIds;
}
// 获取所有SessionInfo
std::vector<std::shared_ptr<SessionInfo>> DataManager::GetAllSessions() {
    if (_db == nullptr) {
        ERR("数据库未初始化");
        return {};
    }
    std::lock_guard<std::mutex> lock(_mutex);
    // 准备查询语句
    // 将查询结果以最后更新事件戳降序排序
    std::string getAllSessionsSQL = R"(
            SELECT sessionId, modelName, createTimeTimestamp, lastUpdateTimeTimestamp FROM sessions
            ORDER BY lastUpdateTimeTimestamp DESC
        )";

    // 编译查询语句
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(_db, getAllSessionsSQL.c_str(), -1, &stmt,
                           nullptr) != SQLITE_OK) {
        ERR("编译GetAllSessions失败: {}", sqlite3_errmsg(_db));
        return {};
    }
    // 执行查询语句
    std::vector<std::shared_ptr<SessionInfo>> sessions;
    int ret = sqlite3_step(stmt);
    while (ret == SQLITE_ROW) {
        // 暂不获取messages，后续再根据需要添加
        auto session = std::make_shared<SessionInfo>();
        session->_sessionId =
            reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
        session->_modelName =
            reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
        session->_createTimeTimestamp =
            static_cast<std::time_t>(sqlite3_column_int64(stmt, 2));
        session->_lastUpdateTimeTimestamp =
            static_cast<std::time_t>(sqlite3_column_int64(stmt, 3));
        sessions.push_back(session);
        ret = sqlite3_step(stmt);
    }
    if (ret != SQLITE_DONE) {
        ERR("执行GetAllSessions失败: {}", sqlite3_errmsg(_db));
        sqlite3_finalize(stmt);
        return {};
    }
    INFO("获取所有SessionInfo成功: {}条SessionInfo", sessions.size());
    // 释放查询语句
    sqlite3_finalize(stmt);
    return sessions;
}

// 获取会话总数
size_t DataManager::GetSessionsCount() const {
    if (_db == nullptr) {
        ERR("数据库未初始化");
        return 0;
    }
    std::lock_guard<std::mutex> lock(_mutex);
    // 准备查询语句
    std::string getAllSessionsSQL = R"(
            SELECT COUNT(sessionId) FROM sessions
        )";
    // 编译查询语句
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(_db, getAllSessionsSQL.c_str(), -1, &stmt,
                           nullptr) != SQLITE_OK) {
        ERR("编译GetSessionsCount失败: {}", sqlite3_errmsg(_db));
        return 0;
    }
    // 执行查询语句
    size_t count = 0;
    int ret = sqlite3_step(stmt);
    if (ret == SQLITE_ROW) {
        count = sqlite3_column_int64(stmt, 0);
    }
    if (ret != SQLITE_DONE) {
        ERR("执行GetSessionsCount失败: {}", sqlite3_errmsg(_db));
        sqlite3_finalize(stmt);
        return 0;
    }
    INFO("获取会话总数成功: {}", count);
    // 释放查询语句
    sqlite3_finalize(stmt);
    return count;
}
bool DataManager::DeleteAllSessions() {
    if (_db == nullptr) {
        ERR("数据库未初始化");
        return false;
    }
    std::lock_guard<std::mutex> lock(_mutex);
    // 删除所有会话
    std::string deleteAllSessionsSQL = R"(
            DELETE FROM sessions
        )";
    // 编译删除语句
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(_db, deleteAllSessionsSQL.c_str(), -1, &stmt,
                           nullptr) != SQLITE_OK) {
        ERR("编译DeleteAllSessions失败: {}", sqlite3_errmsg(_db));
        return false;
    }
    // 执行删除语句
    int ret = sqlite3_step(stmt);
    if (ret != SQLITE_DONE) {
        ERR("执行DeleteAllSessions失败: {}", sqlite3_errmsg(_db));
        sqlite3_finalize(stmt);
        return false;
    }
    // 释放删除语句
    sqlite3_finalize(stmt);
    return true;
}
// Message相关操作
// 插入Message到数据库
bool DataManager::InsertMessage(const std::string &sessionId,
                                const Message &message) {
    if (_db == nullptr) {
        ERR("数据库未初始化");
        return false;
    }

    // 准备SQL语句插入Message
    std::string insertMessageSQL = R"(
            INSERT INTO messages (sessionId, messageId, role, sendTimeTimestamp, content)
            VALUES (?, ?, ?, ?, ?)
        )";

    // 编译插入语句
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(_db, insertMessageSQL.c_str(), -1, &stmt, nullptr) !=
        SQLITE_OK) {
        ERR("编译InsertMessage失败: {}", sqlite3_errmsg(_db));
        return false;
    }
    // 绑定参数
    sqlite3_bind_text(stmt, 1, sessionId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, message._messageId.c_str(), -1,
                      SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, message._role.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 4,
                       static_cast<int64_t>(message._sendTimeTimestamp));
    sqlite3_bind_text(stmt, 5, message._content.c_str(), -1, SQLITE_TRANSIENT);
    // 执行插入语句
    int ret = sqlite3_step(stmt);
    if (ret != SQLITE_DONE) {
        ERR("执行InsertMessage失败: {}", sqlite3_errmsg(_db));
        sqlite3_finalize(stmt);
        return false;
    }
    // 准备更新语句
    std::string updateSessionSQL = R"(
            UPDATE sessions
            SET lastUpdateTimeTimestamp = ?
            WHERE sessionId = ?
        )";
    // 编译更新语句
    sqlite3_stmt *updateStmt = nullptr;
    if (sqlite3_prepare_v2(_db, updateSessionSQL.c_str(), -1, &updateStmt,
                           nullptr) != SQLITE_OK) {
        ERR("编译UpdateSession失败: {}", sqlite3_errmsg(_db));
        return false;
    }
    // 绑定参数
    sqlite3_bind_int64(updateStmt, 1,
                       static_cast<int64_t>(message._sendTimeTimestamp));
    sqlite3_bind_text(updateStmt, 2, sessionId.c_str(), -1, SQLITE_TRANSIENT);
    // 执行更新语句
    if (sqlite3_step(updateStmt) != SQLITE_DONE) {
        ERR("执行UpdateSession失败: {}", sqlite3_errmsg(_db));
        sqlite3_finalize(updateStmt);
        return false;
    }
    INFO("更新Session成功: {}", sessionId);
    // 释放更新语句
    sqlite3_finalize(updateStmt);
    INFO("插入Message成功: {}", message._messageId);
    // 释放插入语句
    sqlite3_finalize(stmt);
    return true;
}
// 获取指定Session的所有Message
std::vector<Message> DataManager::GetAllMessages(const std::string &sessionId) {
    if (_db == nullptr) {
        ERR("数据库未初始化");
        return {};
    }
    // 准备SQL语句查询指定Session的所有Message
    std::string getAllMessagesSQL = R"(
            SELECT messageId, role, sendTimeTimestamp, content FROM messages
            WHERE sessionId = ? ORDER BY sendTimeTimestamp
        )";
    // 编译SQL语句
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(_db, getAllMessagesSQL.c_str(), -1, &stmt,
                           nullptr) != SQLITE_OK) {
        ERR("编译GetAllMessages失败: {}", sqlite3_errmsg(_db));
        return {};
    }
    // 绑定参数
    sqlite3_bind_text(stmt, 1, sessionId.c_str(), -1, SQLITE_TRANSIENT);

    // 执行SQL语句
    std::vector<Message> messages;
    int ret = sqlite3_step(stmt);
    while (ret == SQLITE_ROW) {
        Message msg(
            reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1)),
            reinterpret_cast<const char *>(sqlite3_column_text(stmt, 3)));
        msg._messageId =
            reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
        msg._sendTimeTimestamp =
            static_cast<std::time_t>(sqlite3_column_int64(stmt, 2));
        messages.push_back(msg);
        ret = sqlite3_step(stmt);
    }
    if (ret != SQLITE_DONE) {
        ERR("执行GetAllMessages失败: {}", sqlite3_errmsg(_db));
        sqlite3_finalize(stmt);
        return {};
    }
    INFO("获取指定Session的所有Message成功: {}条Message", messages.size());
    // 释放查询语句
    sqlite3_finalize(stmt);
    return messages;
}
// 删除指定会话的历史消息
bool DataManager::DeleteMessages(const std::string &sessionId) {
    if (_db == nullptr) {
        ERR("数据库未初始化");
        return false;
    }
    std::lock_guard<std::mutex> lock(_mutex);
    // 准备删除语句
    std::string deleteMessagesSQL = R"(
            DELETE FROM messages
            WHERE sessionId = ?
        )";
    // 编译删除语句
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(_db, deleteMessagesSQL.c_str(), -1, &stmt,
                           nullptr) != SQLITE_OK) {
        ERR("编译DeleteMessages失败: {}", sqlite3_errmsg(_db));
        return false;
    }

    // 绑定参数
    sqlite3_bind_text(stmt, 1, sessionId.c_str(), -1, SQLITE_TRANSIENT);
    // 执行删除语句
    int ret = sqlite3_step(stmt);
    if (ret != SQLITE_DONE) {
        ERR("执行DeleteMessages失败: {}", sqlite3_errmsg(_db));
        sqlite3_finalize(stmt);
        return false;
    }
    INFO("删除指定会话的历史消息成功: {}", sessionId);
    // 释放删除语句
    sqlite3_finalize(stmt);
    return true;
}
// 初始化数据库
bool DataManager::InitDB() {
    if (_db == nullptr) {
        ERR("数据库未初始化");
        return false;
    }

    // 创建Session表的SQL语句
    std::string createSessionTableSQL = R"(
            CREATE TABLE IF NOT EXISTS sessions (
                sessionId TEXT PRIMARY KEY,
                modelName TEXT,
                createTimeTimestamp INTEGER NOT NULL,
                lastUpdateTimeTimestamp INTEGER NOT NULL
            );
        )";
    // 执行创建Session表的SQL语句
    if (!ExecuteSQL(createSessionTableSQL)) {
        ERR("创建Session表失败...");
        return false;
    }

    // 创建Message表
    std::string createMessageTableSQL = R"(
            CREATE TABLE IF NOT EXISTS messages (
                messageId TEXT PRIMARY KEY,
                sessionId TEXT,
                role TEXT,
                content TEXT,
                sendTimeTimestamp INTEGER,
                FOREIGN KEY (sessionId) REFERENCES sessions (sessionId) ON DELETE CASCADE
            );
        )";
    // 执行创建Message表的SQL语句
    if (!ExecuteSQL(createMessageTableSQL)) {
        ERR("创建Message表失败...");
        return false;
    }

    return true;
}
// 关闭数据库连接
void DataManager::CloseDB() {
    if (_db != nullptr) {
        sqlite3_close(_db);
        _db = nullptr;
    }
}
// 执行SQL语句
bool DataManager::ExecuteSQL(const std::string &sql) {
    if (_db == nullptr) {
        ERR("数据库未初始化");
        return false;
    }
    INFO("执行SQL语句: {}", sql);
    char *errMsg = nullptr;
    int ret = sqlite3_exec(_db, sql.c_str(), nullptr, nullptr, &errMsg);
    if (ret != SQLITE_OK) {
        ERR("执行SQL语句失败: {}", errMsg);
        // sqlite3_close(_db);
        sqlite3_free(errMsg);
        return false;
    }
    return true;
}

} // namespace chat_sdk