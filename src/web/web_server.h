// Copyright 2026 兰雨阳. All rights reserved.
// 内置 HTTP 服务：托管 web/ 下的前端页面，并向后端应用服务提供 JSON 接口。
//
// 只监听 127.0.0.1，按“一个连接一次请求”的简单模型处理，适合课程演示；
// 所有业务规则仍由 services 层负责，HTTP 层只做协议与鉴权。

#ifndef CAMPUS_WEB_SERVER_H_
#define CAMPUS_WEB_SERVER_H_

#include <string>

namespace campus {

// 启动服务；端口被占用时自动向后尝试。返回进程退出码。
int RunWebServer(const std::string& data_dir, int port, bool open_browser,
                 const std::string& web_root);

}  // namespace campus

#endif  // CAMPUS_WEB_SERVER_H_
