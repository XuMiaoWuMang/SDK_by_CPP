# Cmake版本的最低要求
cmake_minimum_required(VERSION 3.10)

# 项目名称
set(PROJECT_NAME AI_SDK_CPP)

# 项目类型
project(${PROJECT_NAME})

# 设置C++标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 设置构建release模式
set(CMAKE_BUILD_TYPE Release)

# 项目头文件目录
file(GLOB_RECURSE HEAD_FILES "include/*.hpp")

# 项目源文件
file(GLOB_RECURSE SOURCES_FILES "src/*.cpp")

# 生成静态库
add_library(${PROJECT_NAME} STATIC ${SOURCES_FILES} ${HEAD_FILES})

# 设置输出目录
set(EXECUTABLE_OUTPUT_PATH ${CMAKE_CURRENT_SOURCE_DIR}/build)

# 设置静态库目录
link_directories(/usr/local/lib)

# 查找OpenSSL库
find_package(OpenSSL REQUIRED)
include_directories(${OPENSSL_INCLUDE_DIR})
# 查找spdlog库
find_package(spdlog REQUIRED)
include_directories(${SPDLOG_INCLUDE_DIR})

# 设置宏定义
target_compile_definitions(${PROJECT_NAME} PUBLIC CPPHTTPLIB_OPENSSL_SUPPORT)  

# 包含头文件目录
target_include_directories(${PROJECT_NAME} PUBLIC CMAKE_CURRENT_SOURCE_DIR/include)

# 添加链接库
target_link_libraries(${PROJECT_NAME} gtest jsoncpp spdlog fmt OpenSSL::SSL OpenSSL::Crypto sqlite3)

# 安装规则 拷贝静态库到系统库目录
install(TARGETS ${PROJECT_NAME} 
ARCHIVE DESTINATION lib)

# 安装规则 复制头文件到系统头目录
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/ 
DESTINATION include/ai_cpp_sdk
FILES_MATCHING PATTERN "*.hpp"
)
