// Copyright 2026 兰雨阳. All rights reserved.

#include <iostream>
#include <string>

#include "campus/app.h"

int main(int argc, char** argv) {
  std::string data_dir = "data";
  bool seed_demo = true;
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--data" && i + 1 < argc) {
      data_dir = argv[++i];
    } else if (arg == "--no-seed") {
      seed_demo = false;
    } else if (arg == "--help") {
      std::cout << "用法: campus_market [--data <目录>] [--no-seed]\n";
      return 0;
    }
  }
  return campus::RunApplication(data_dir, seed_demo);
}
