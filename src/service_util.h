// Copyright 2026 兰雨阳. All rights reserved.
// 服务层内部共用的小工具：写日志、发通知、参数校验。

#ifndef CAMPUS_SERVICE_UTIL_H_
#define CAMPUS_SERVICE_UTIL_H_

#include <string>

#include "campus/services.h"

namespace campus {
namespace internal {

inline void WriteLog(ILogRepository& logs, IClock& clock, IIdGenerator& ids,
                     const std::string& operator_id, const std::string& action,
                     const std::string& target_type,
                     const std::string& target_id,
                     const std::string& detail) {
  OperationLog log;
  log.id = ids.Next("LG");
  log.operator_id = operator_id;
  log.action = action;
  log.target_type = target_type;
  log.target_id = target_id;
  log.detail = detail;
  log.created_at = clock.Now();
  logs.Add(log);
}

inline void Notify(INotificationRepository& notifications, IClock& clock,
                   IIdGenerator& ids, const std::string& user_id,
                   const std::string& type, const std::string& content) {
  Notification notification;
  notification.id = ids.Next("NT");
  notification.user_id = user_id;
  notification.type = type;
  notification.content = content;
  notification.read = false;
  notification.created_at = clock.Now();
  notifications.Add(notification);
}

inline void RequireText(const std::string& text, std::size_t min_length,
                        std::size_t max_length, const std::string& message) {
  std::size_t length = Trim(text).size();
  if (length < min_length || length > max_length) {
    throw DomainError(ErrorCode::kInvalidArgument, message);
  }
}

}  // namespace internal
}  // namespace campus

#endif  // CAMPUS_SERVICE_UTIL_H_
