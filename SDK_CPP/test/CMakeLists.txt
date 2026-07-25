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
add_executable(testLLM testLLM.cpp ../sdk/src/DeepSeekProvider.cpp ../sdk/src/util/logger.cpp)
# 设置输出目录
set(EXECUTABLE_OUTPUT_PATH ${CMAKE_BINARY_DIR})
# 添加头文件的搜索路径
include_directories(${CMAKE_PROJECT_SOURCE_DIR}/../sdk/include)
# 添加CPPHTTPLIB_OPENSSL_SUPPORT宏
target_compile_definitions(testLLM PRIVATE CPPHTTPLIB_OPENSSL_SUPPORT)
# 查找OpenSSL库
find_package(OpenSSL REQUIRED)
include_directories(${OPENSSL_INCLUDE_DIR})
# 查找spdlog库
find_package(spdlog REQUIRED)
include_directories(${SPDLOG_INCLUDE_DIR})
# 添加链接库
target_link_libraries(testLLM gtest jsoncpp spdlog fmt OpenSSL::SSL OpenSSL::Crypto)
