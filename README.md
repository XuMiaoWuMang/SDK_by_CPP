# SDK_by_CPP

使用c++完成SDK接入远端或本地大模型

## 配置环境

本项目使用了以下第三方库和工具:

- `gflags`: 用于命令行参数解析
  - 安装: `apt install libgflags-dev`
- `spdlog`: 用于日志记录
  - 安装: `apt install libspdlog-dev`
- `fmt`: 用于字符串格式化
  - 安装: `apt install libfmt-dev`
- `jsoncpp`: 用于JSON解析和序列解析
  - 安装: `apt install libjsoncpp-dev`
- `gtest`: 用于单元测试
  - 安装: `apt install libgtest-dev`
- `ssl`: 用于SSL/TLS加密
  - 安装: `apt install libssl-dev`
- `cmake`: 用于构建构建系统
  - 安装: `apt install cmake`
- `httplib`: 用于HTTP请求
  - 安装:
    1. `cd ~ && git clone https://github.com/yhirose/cpp-httplib.git`
    2. `sudo cp cpp-httplib/cpp-httplib/httplib.h /usr/include/`
  - 注意:
    - github 使用梯子下载更好
    - 这是一个只需要头文件的库, 在项⽬中只需时只需要包含该头⽂件即可, 将httplib.h拷⻉到系统⽬录下，在程序中 `#include <httplib.h>` 时能直接找到
- `SQLite`: 用于数据库操作
  - 安装: `apt install libsqlite3-dev`


