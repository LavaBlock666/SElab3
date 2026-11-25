// Copyright 2026 兰雨阳. All rights reserved.
// 控制台入口：初始化数据目录并启动交互界面。

#ifndef CAMPUS_APP_H_
#define CAMPUS_APP_H_

#include <string>

namespace campus {

// data_dir：数据文件目录；seed_demo：数据为空时是否写入演示数据。
int RunApplication(const std::string& data_dir, bool seed_demo);

}  // namespace campus

#endif  // CAMPUS_APP_H_
