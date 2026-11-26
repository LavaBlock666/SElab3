// Copyright 2026 兰雨阳. All rights reserved.
// 单实例锁：同一个数据目录只允许一个进程写入，避免两个实例互相覆盖数据。

#ifndef CAMPUS_INSTANCE_LOCK_H_
#define CAMPUS_INSTANCE_LOCK_H_

#include <windows.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace campus {

class InstanceLock {
 public:
  InstanceLock() = default;
  InstanceLock(const InstanceLock&) = delete;
  InstanceLock& operator=(const InstanceLock&) = delete;

  ~InstanceLock() {
    if (held_) {
      std::error_code error;
      std::filesystem::remove(path_, error);
    }
  }

  // 成功获取返回 true；已有存活实例占用时返回 false。
  bool Acquire(const std::string& data_dir) {
    std::filesystem::path dir(data_dir);
    std::error_code error;
    std::filesystem::create_directories(dir, error);

    path_ = (dir / "instance.lock").string();
    if (std::filesystem::exists(path_, error)) {
      std::ifstream in(path_);
      long pid = 0;
      in >> pid;
      in.close();
      if (pid > 0 && ProcessAlive(static_cast<unsigned long>(pid))) {
        return false;
      }
    }
    std::ofstream out(path_, std::ios::trunc);
    out << GetCurrentProcessId();
    held_ = true;
    return true;
  }

 private:
  static bool ProcessAlive(unsigned long pid) {
    HANDLE handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (handle == nullptr) {
      return false;
    }
    DWORD code = 0;
    bool alive = GetExitCodeProcess(handle, &code) && code == STILL_ACTIVE;
    CloseHandle(handle);
    return alive;
  }

  std::string path_;
  bool held_ = false;
};

}  // namespace campus

#endif  // CAMPUS_INSTANCE_LOCK_H_
