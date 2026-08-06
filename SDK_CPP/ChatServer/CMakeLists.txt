# 设置最小版本
cmake_minimum_required(VERSION 3.10)
# 项目名称
project(AIChatServer)
# 设置C++标准
set(CMAKE_CXX_STANDARD 17)
# 设置C++标准警告
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# 设置构建类型
set(CMAKE_BUILD_TYPE Debug)
# 添加可执行文件 (SDK 已安装到系统, 通过静态库链接, 无需再编译 SDK 源码)
add_executable(AIChatServer main.cpp ChatServer.cpp)
# 设置输出目录
set(EXECUTABLE_OUTPUT_PATH ${CMAKE_BINARY_DIR})
# 查找已安装的 AI_SDK_CPP 静态库 (/usr/local/lib/libAI_SDK_CPP.a)
find_library(AI_SDK_CPP_LIB NAMES AI_SDK_CPP PATHS /usr/local/lib)
# # 添加头文件的搜索路径 (SDK 头文件已安装到 /usr/local/include/ai_cpp_sdk)
# include_directories(/usr/local/include)
# 添加CPPHTTPLIB_OPENSSL_SUPPORT宏
target_compile_definitions(AIChatServer PRIVATE CPPHTTPLIB_OPENSSL_SUPPORT)
# 查找OpenSSL库
find_package(OpenSSL REQUIRED)
include_directories(${OPENSSL_INCLUDE_DIR})
# 查找spdlog库
find_package(spdlog REQUIRED)
include_directories(${SPDLOG_INCLUDE_DIR})
# 查找gflags库 (main.cpp 用于解析命令行参数和配置文件)
find_package(gflags REQUIRED)
include_directories(${GFLAGS_INCLUDE_DIR})
# 添加链接库
target_link_libraries(AIChatServer ${AI_SDK_CPP_LIB} jsoncpp spdlog fmt OpenSSL::SSL OpenSSL::Crypto sqlite3 gflags)
