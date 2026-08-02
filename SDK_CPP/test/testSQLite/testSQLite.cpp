#include <iostream>
#include <sqlite3.h>
#include <string>
struct StudentInfo {
    std::string name;
    int age;
    std::string gender;
    double score;
    StudentInfo(const std::string &name, int age, const std::string &gender,
                double score)
        : name(name), age(age), gender(gender), score(score) {}
    StudentInfo() = default;
};

class StudentDB {
  public:
    StudentDB(const std::string &dbNamePath) {
        // 创建数据库
        int ret = sqlite3_open(dbNamePath.c_str(), &_db);
        if (ret != SQLITE_OK) {
            std::cerr << "open db failed: " << sqlite3_errmsg(_db) << std::endl;
            sqlite3_close(_db);
            return;
        }
        // 初始化数据库
        if (!InitDB()) {
            sqlite3_close(_db);
            return;
        }
    }
    ~StudentDB() {
        if (_db) {
            sqlite3_close(_db);
            _db = nullptr;
        }
    }
    // 插入学生信息
    bool insertStudent(const StudentInfo &student) {
        // 检查数据库是否已初始化
        if (!_db) {
            std::cerr << "db not initialized" << std::endl;
            return false;
        }
        // 检查SQL语句
        std::string insertSQL = R"(
            INSERT INTO student (name, age, gender, score)
            VALUES (?, ?, ?, ?);
        )";
        sqlite3_stmt *stmt = nullptr;
        int ret =
            sqlite3_prepare_v2(_db, insertSQL.c_str(), -1, &stmt, nullptr);
        if (ret != SQLITE_OK) {
            std::cerr << "prepare insertSQL failed: " << sqlite3_errmsg(_db)
                      << std::endl;
            return false;
        }
        // 绑定参数
        sqlite3_bind_text(stmt, 1, student.name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 2, student.age);
        sqlite3_bind_text(stmt, 3, student.gender.c_str(), -1,
                          SQLITE_TRANSIENT);
        sqlite3_bind_double(stmt, 4, student.score);
        // 执行SQL语句
        ret = sqlite3_step(stmt);
        if (ret != SQLITE_DONE) {
            std::cerr << "execute insertSQL failed: " << sqlite3_errmsg(_db)
                      << std::endl;
            sqlite3_finalize(stmt);
            return false;
        }
        // 关闭语句
        sqlite3_finalize(stmt);
        return true;
    }
    // 查询学生信息
    bool queryStudyinfo(const std::string &name) {
        // 检查数据库是否已初始化
        if (!_db) {
            std::cerr << "db not initialized" << std::endl;
            return false;
        }
        // 准备SQL语句
        std::string querySQL = R"(
            SELECT * FROM student WHERE name = ?
        )";
        // 检查SQL语句
        sqlite3_stmt *stmt = nullptr;
        int ret = sqlite3_prepare_v2(_db, querySQL.c_str(), -1, &stmt, nullptr);
        if (ret != SQLITE_OK) {
            std::cerr << "prepare querySQL failed: " << sqlite3_errmsg(_db)
                      << std::endl;
            return false;
        }
        // 绑定参数
        sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);
        // 执行SQL语句
        ret = sqlite3_step(stmt);
        if (ret != SQLITE_ROW) {
            std::cerr << "execute querySQL failed: " << sqlite3_errmsg(_db)
                      << std::endl;
            sqlite3_finalize(stmt);
            return false;
        }
        // 读取结果
        StudentInfo student;
        student.name =
            reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
        student.age = sqlite3_column_int(stmt, 2);
        student.gender =
            reinterpret_cast<const char *>(sqlite3_column_text(stmt, 3));
        student.score = sqlite3_column_double(stmt, 4);

        // 打印查询结果
        std::cout << "name: " << student.name << ", ";
        std::cout << "age: " << student.age << ", ";
        std::cout << "gender: " << student.gender << ", ";
        std::cout << "score: " << student.score << std::endl;
        // 关闭语句
        sqlite3_finalize(stmt);
        return true;
    }

    bool queryAllStudyinfo() {
        // 检查数据库是否已初始化
        if (!_db) {
            std::cerr << "db not initialized" << std::endl;
            return false;
        }
        // 准备SQL语句
        std::string querySQL = R"(
            SELECT * FROM student
        )";
        // 检查SQL语句
        sqlite3_stmt *stmt = nullptr;
        int ret = sqlite3_prepare_v2(_db, querySQL.c_str(), -1, &stmt, nullptr);
        if (ret != SQLITE_OK) {
            std::cerr << "prepare querySQL failed: " << sqlite3_errmsg(_db)
                      << std::endl;
            return false;
        }

        // 执行SQL语句
        ret = sqlite3_step(stmt);
        std::cout << "-----------------所有学生信息------------------"
                  << std::endl;
        while (ret == SQLITE_ROW) {
            // 读取结果
            StudentInfo student;
            student.name =
                reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
            student.age = sqlite3_column_int(stmt, 2);
            student.gender =
                reinterpret_cast<const char *>(sqlite3_column_text(stmt, 3));
            student.score = sqlite3_column_double(stmt, 4);

            // 打印查询结果
            std::cout << "name: " << student.name << ", ";
            std::cout << "age: " << student.age << ", ";
            std::cout << "gender: " << student.gender << ", ";
            std::cout << "score: " << student.score << std::endl;

            ret = sqlite3_step(stmt);
        }
        if (ret != SQLITE_DONE) {
            std::cerr << "execute querySQL failed: " << sqlite3_errmsg(_db)
                      << std::endl;
            sqlite3_finalize(stmt);
            return false;
        }
        // 关闭语句
        sqlite3_finalize(stmt);
        return true;
    }

    bool updateStudyinfo(const std::string &name, const StudentInfo &student) {
        // 检查数据库是否已初始化
        if (!_db) {
            std::cerr << "db not initialized" << std::endl;
            return false;
        }
        // 准备SQL语句
        std::string updateSQL = R"(
            UPDATE student SET age = ?, gender = ?, score = ? WHERE name = ?
        )";
        // 检查SQL语句
        sqlite3_stmt *stmt = nullptr;
        int ret =
            sqlite3_prepare_v2(_db, updateSQL.c_str(), -1, &stmt, nullptr);
        if (ret != SQLITE_OK) {
            std::cerr << "prepare updateSQL failed: " << sqlite3_errmsg(_db)
                      << std::endl;
            return false;
        }
        // 绑定参数
        sqlite3_bind_int(stmt, 1, student.age);
        sqlite3_bind_text(stmt, 2, student.gender.c_str(), -1,
                          SQLITE_TRANSIENT);
        sqlite3_bind_double(stmt, 3, student.score);
        sqlite3_bind_text(stmt, 4, student.name.c_str(), -1, SQLITE_TRANSIENT);
        // 执行SQL语句
        ret = sqlite3_step(stmt);
        if (ret != SQLITE_DONE && ret != SQLITE_ROW) {
            std::cerr << "execute updateSQL failed: " << sqlite3_errmsg(_db)
                      << std::endl;
            sqlite3_finalize(stmt);
            return false;
        }
        // 关闭语句
        sqlite3_finalize(stmt);
        return true;
    }
    // 删除学生信息
    bool deleteStudentInfo(const std::string &name) {
        if (!_db) {
            std::cerr << "db not initialized" << std::endl;
            return false;
        }
        // 准备SQL语句
        std::string deleteSQL = R"(
            DELETE FROM student WHERE name = ?
        )";
        // 检查SQL语句
        sqlite3_stmt *stmt = nullptr;
        int ret =
            sqlite3_prepare_v2(_db, deleteSQL.c_str(), -1, &stmt, nullptr);
        if (ret != SQLITE_OK) {
            std::cerr << "prepare deleteSQL failed: " << sqlite3_errmsg(_db)
                      << std::endl;
            return false;
        }
        // 绑定参数
        sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);
        // 执行SQL语句
        ret = sqlite3_step(stmt);
        if (ret != SQLITE_DONE) {
            std::cerr << "execute deleteSQL failed: " << sqlite3_errmsg(_db)
                      << std::endl;
            sqlite3_finalize(stmt);
            return false;
        }
        // 关闭语句
        sqlite3_finalize(stmt);
        return true;
    }

  private:
    bool InitDB() {
        // 初始化数据表
        std::string dbSQL = R"(
            CREATE TABLE IF NOT EXISTS student (
                stu_id INTEGER PRIMARY KEY AUTOINCREMENT,
                name TEXT NOT NULL,
                age INTEGER,
                gender TEXT,
                score REAL
            );
        )";
        // 检查SQL语句
        sqlite3_stmt *stmt = nullptr;
        int ret = sqlite3_prepare_v2(_db, dbSQL.c_str(), -1, &stmt, nullptr);
        if (ret != SQLITE_OK) {
            std::cerr << "prepare dbSQL failed: " << sqlite3_errmsg(_db)
                      << std::endl;
            return false;
        }

        // 执行SQL语句
        ret = sqlite3_step(stmt);
        if (ret != SQLITE_DONE) {
            std::cerr << "execute dbSQL failed: " << sqlite3_errmsg(_db)
                      << std::endl;
            sqlite3_finalize(stmt);
            return false;
        }
        // 关闭语句
        sqlite3_finalize(stmt);
        return true;
    }

  private:
    sqlite3 *_db = nullptr;
};

int main() {
    StudentDB studentDB("studentDB.db");
    StudentInfo stu1 = {"张三", 18, "male", 90.0};
    StudentInfo stu2 = {"李四", 18, "male", 75.8};
    StudentInfo stu3 = {"王五", 18, "male", 85.5};
    StudentInfo stu4 = {"赵六", 18, "male", 95.0};
    studentDB.insertStudent(stu1);
    studentDB.insertStudent(stu2);
    studentDB.insertStudent(stu3);
    studentDB.insertStudent(stu4);
    studentDB.queryStudyinfo("张三");
    // 输出: name: 张三, age: 18, gender: male, score: 90.0
    studentDB.queryStudyinfo("李四");
    // 输出: name: 李四, age: 18, gender: male, score: 75.8
    studentDB.queryStudyinfo("王五");
    // 输出: name: 王五, age: 18, gender: male, score: 85.5
    studentDB.queryStudyinfo("赵六");
    // 输出: name: 赵六, age: 18, gender: male, score: 95.0
    std::cout << "***************分割线*****************" << std::endl;
    studentDB.queryAllStudyinfo();
    // 输出:
    // -----------------所有学生信息------------------
    // name: 张三, age: 18, gender: male, score: 90.0
    // name: 李四, age: 18, gender: male, score: 75.8
    // name: 王五, age: 18, gender: male, score: 85.5
    // name: 赵六, age: 18, gender: male, score: 95.0
    std::cout << "***************分割线*****************" << std::endl;
    studentDB.deleteStudentInfo("李四");
    studentDB.queryAllStudyinfo();
    // 输出:
    // -----------------所有学生信息------------------
    // name: 张三, age: 18, gender: male, score: 90.0
    // name: 王五, age: 18, gender: male, score: 85.5
    // name: 赵六, age: 18, gender: male, score: 95.0
    std::cout << "***************分割线*****************" << std::endl;
    StudentInfo stu = {"张三", 19, "female", 85.5};
    studentDB.updateStudyinfo("张三", stu);
    studentDB.queryStudyinfo("张三");
    // 输出: name: 张三, age: 19, gender: female, score: 85.5
    std::cout << "***************分割线*****************" << std::endl;
    studentDB.queryAllStudyinfo();
    // 输出:
    // -----------------所有学生信息------------------
    // name: 张三, age: 19, gender: female, score: 85.5
    // name: 王五, age: 18, gender: male, score: 85.5
    // name: 赵六, age: 18, gender: male, score: 95.0

    std::cout << "***************分割线*****************" << std::endl;
    return 0;
}