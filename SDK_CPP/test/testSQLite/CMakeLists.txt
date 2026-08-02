# 设置最小版本
cmake_minimum_required(VERSION 3.10)
# 项目名称
project(testLLM)
# 设置C++标准
set(CMAKE_CXX_STANDARD 17)
# 设置C++标准警告
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# 设置构建类型
set(CMAKE_BUILD_TYPE Debug)
# 添加可执行文件
add_executable(testSQLite testSQLite.cpp)
# 设置输出目录
set(EXECUTABLE_OUTPUT_PATH ${CMAKE_BINARY_DIR})
# 添加链接库
target_link_libraries(testSQLite sqlite3)