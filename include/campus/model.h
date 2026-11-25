// Copyright 2026 兰雨阳. All rights reserved.
// 校园二手交易系统 —— 领域模型定义。
//
// 本文件对应实验二类图中的领域实体：用户、分类、商品、预约、举报与操作日志，
// 以及商品状态、预约状态、举报状态三个枚举。状态转换的合法性集中在
// CanTransition() 中校验，避免散落在各个服务里。

#ifndef CAMPUS_MODEL_H_
#define CAMPUS_MODEL_H_

#include <cstdint>
#include <exception>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace campus {

// 金额统一用“分”存储，避免浮点误差。
using Money = std::int64_t;
// 时间统一用 Unix 秒，格式化后写日志与界面。
using Timestamp = std::int64_t;

constexpr Money kMaxPrice = 100000000;  // 一百万元
constexpr int kPasswordMinLength = 8;
constexpr int kMaxLoginFailures = 5;
constexpr Timestamp kLockSeconds = 15 * 60;
constexpr Timestamp kReportWindowSeconds = 24 * 60 * 60;

// ---------- 枚举 ----------

enum class Role { kBuyer, kSeller, kAdmin };
enum class UserStatus { kActive, kLocked };
enum class ItemStatus {
  kDraft,
  kPending,
  kPublished,
  kReserved,
  kConfirmed,
  kCompleted,
  kOffline
};
enum class ReservationStatus {
  kPending,
  kConfirmed,
  kCompleted,
  kCanceled,
  kVoided
};
enum class ReportStatus { kPending, kUpheld, kRejected };
enum class ReportReason {
  kMisleading,  // 虚假描述
  kDuplicate,   // 重复发布
  kProhibited,  // 违禁物品
  kContact,     // 描述中留联系方式
  kOther
};

std::string ToString(Role role);
std::string ToString(UserStatus status);
std::string ToString(ItemStatus status);
std::string ToString(ReservationStatus status);
std::string ToString(ReportStatus status);
std::string ToString(ReportReason reason);

std::optional<Role> ParseRole(const std::string& text);
std::optional<UserStatus> ParseUserStatus(const std::string& text);
std::optional<ItemStatus> ParseItemStatus(const std::string& text);
std::optional<ReservationStatus> ParseReservationStatus(const std::string& text);
std::optional<ReportStatus> ParseReportStatus(const std::string& text);
std::optional<ReportReason> ParseReportReason(const std::string& text);

// 商品与预约的状态转换规则。表中未列出的转换一律返回 false。
bool CanTransition(ItemStatus from, ItemStatus to);
bool CanTransition(ReservationStatus from, ReservationStatus to);

// 商品是否还占用“有效预约”名额。
bool IsActiveReservation(ReservationStatus status);
// 商品是否已终结（已完成或已下架）。
bool IsTerminal(ItemStatus status);

// ---------- 实体 ----------

struct User {
  std::string id;
  std::string email;
  std::string phone;
  std::string password_hash;
  std::string salt;
  std::set<Role> roles;
  UserStatus status = UserStatus::kActive;
  int failed_attempts = 0;
  Timestamp lock_until = 0;
  Timestamp created_at = 0;

  bool HasRole(Role role) const { return roles.count(role) > 0; }
  bool IsLocked(Timestamp now) const {
    return status == UserStatus::kLocked && now < lock_until;
  }
};

struct Category {
  std::string id;
  std::string name;
};

struct Item {
  std::string id;
  std::string seller_id;
  std::string category_id;
  std::string title;
  std::string description;
  Money price = 0;
  ItemStatus status = ItemStatus::kDraft;
  int view_count = 0;
  std::string reject_reason;
  Timestamp created_at = 0;
  Timestamp updated_at = 0;
};

struct Reservation {
  std::string id;
  std::string item_id;
  std::string buyer_id;
  ReservationStatus status = ReservationStatus::kPending;
  std::string request_id;  // 幂等号：同一请求重发返回原结果
  Timestamp created_at = 0;
  Timestamp confirmed_at = 0;
  Timestamp finished_at = 0;
};

struct Report {
  std::string id;
  std::string item_id;
  std::string reporter_id;
  ReportReason reason = ReportReason::kOther;
  std::string description;
  ReportStatus status = ReportStatus::kPending;
  std::string result;
  std::string handler_id;
  Timestamp created_at = 0;
  Timestamp handled_at = 0;
};

struct OperationLog {
  std::string id;
  std::string operator_id;
  std::string action;
  std::string target_type;
  std::string target_id;
  std::string detail;
  Timestamp created_at = 0;
};

struct Notification {
  std::string id;
  std::string user_id;
  std::string type;
  std::string content;
  bool read = false;
  Timestamp created_at = 0;
};

// ---------- 错误 ----------

enum class ErrorCode {
  kNotFound,
  kInvalidArgument,
  kConflict,
  kPermissionDenied,
  kStateError,
  kUnauthorized,
  kDuplicate
};

class DomainError : public std::exception {
 public:
  DomainError(ErrorCode code, std::string message)
      : code_(code), message_(std::move(message)) {}

  const char* what() const noexcept override { return message_.c_str(); }
  ErrorCode code() const { return code_; }
  const std::string& message() const { return message_; }

 private:
  ErrorCode code_;
  std::string message_;
};

// ---------- 工具 ----------

std::string ToLower(std::string text);
std::string Trim(const std::string& text);
// 按 UTF-8 码点计数，避免中文标题按字节数误判长度。
std::size_t Utf8Length(const std::string& text);
bool StartsWith(const std::string& text, const std::string& prefix);
bool Contains(const std::string& text, const std::string& needle);

Timestamp Now();
std::string FormatTime(Timestamp time);
std::optional<Timestamp> ParseTime(const std::string& text);
std::string FormatDate(Timestamp time);

std::string MoneyToString(Money money);
std::optional<Money> ParseMoney(const std::string& text);

// 文件存储使用 0x1f 作为字段分隔符，换行与分隔符需要转义。
std::string EscapeField(const std::string& text);
std::string UnescapeField(const std::string& text);
std::string JoinFields(const std::vector<std::string>& fields);
std::vector<std::string> SplitFields(const std::string& line);
std::string JoinSet(const std::set<Role>& roles);
std::set<Role> ParseRoleSet(const std::string& text);

}  // namespace campus

#endif  // CAMPUS_MODEL_H_
