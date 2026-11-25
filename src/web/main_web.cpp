// Copyright 2026 兰雨阳. All rights reserved.

#include <cstdlib>
#include <string>

#include "web/web_server.h"

int main(int argc, char** argv) {
  std::string data_dir = "data";
  std::string web_root = "web";
  int port = 8765;
  bool open_browser = true;
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--data" && i + 1 < argc) {
      data_dir = argv[++i];
    } else if (arg == "--web" && i + 1 < argc) {
      web_root = argv[++i];
    } else if (arg == "--port" && i + 1 < argc) {
      port = std::atoi(argv[++i]);
    } else if (arg == "--no-open") {
      open_browser = false;
    } else if (arg == "--help") {
      std::printf(
          "用法: campus_market_web [--data 目录] [--web 前端目录] [--port 端口] "
          "[--no-open]\n");
      return 0;
    }
  }
  return campus::RunWebServer(data_dir, port, open_browser, web_root);
}
