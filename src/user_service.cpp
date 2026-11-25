// Copyright 2026 兰雨阳. All rights reserved.
// 用户与权限服务：注册、登录、锁定策略与权限断言。

#include "campus/services.h"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace campus {
namespace {

bool IsValidEmail(const std::string& email) {
  if (email.size() < 5 || email.size() > 64) {
    return false;
  }
  std::size_t at = email.find('@');
  if (at == std::string::npos || at == 0 || at + 1 >= email.size()) {
    return false;
  }
  std::string domain = email.substr(at + 1);
  return domain.find('.') != std::string::npos &&
         domain.find(' ') == std::string::npos;
}

bool IsValidPhone(const std::string& phone) {
  if (phone.size() != 11 || phone[0] != '1') {
    return false;
  }
  return std::all_of(phone.begin(), phone.end(),
                     [](unsigned char c) { return std::isdigit(c) != 0; });
}

bool IsStrongPassword(const std::string& password) {
  if (password.size() < static_cast<std::size_t>(kPasswordMinLength)) {
    return false;
  }
  bool has_alpha = false;
  bool has_digit = false;
  for (char c : password) {
    has_alpha = has_alpha || std::isalpha(static_cast<unsigned char>(c)) != 0;
    has_digit = has_digit || std::isdigit(static_cast<unsigned char>(c)) != 0;
  }
  return has_alpha && has_digit;
}

}  // namespace

User MustFindUser(const IUserRepository& users, const std::string& id) {
  auto user = users.FindById(id);
  if (!user) {
    throw DomainError(ErrorCode::kNotFound, "用户不存在");
  }
  return *user;
}

void MustHaveRole(const User& user, Role role) {
  if (!user.HasRole(role)) {
    throw DomainError(ErrorCode::kPermissionDenied, "当前账号没有该操作权限");
  }
}

Item MustFindItem(const IItemRepository& items, const std::string& id) {
  auto item = items.FindById(id);
  if (!item) {
    throw DomainError(ErrorCode::kNotFound, "商品不存在");
  }
  return *item;
}

std::string RoleLabel(Role role) {
  switch (role) {
    case Role::kBuyer:
      return "买家";
    case Role::kSeller:
      return "卖家";
    case Role::kAdmin:
      return "管理员";
  }
  return "未知";
}

std::string ItemStatusLabel(ItemStatus status) {
  switch (status) {
    case ItemStatus::kDraft:
      return "草稿";
    case ItemStatus::kPending:
      return "待审核";
    case ItemStatus::kPublished:
      return "已发布";
    case ItemStatus::kReserved:
      return "已预约";
    case ItemStatus::kConfirmed:
      return "已确认";
    case ItemStatus::kCompleted:
      return "已完成";
    case ItemStatus::kOffline:
      return "已下架";
  }
  return "未知";
}

std::string ReservationStatusLabel(ReservationStatus status) {
  switch (status) {
    case ReservationStatus::kPending:
      return "待确认";
    case ReservationStatus::kConfirmed:
      return "已确认";
    case ReservationStatus::kCompleted:
      return "已完成";
    case ReservationStatus::kCanceled:
      return "已取消";
    case ReservationStatus::kVoided:
      return "已作废";
  }
  return "未知";
}

std::string ReportStatusLabel(ReportStatus status) {
  switch (status) {
    case ReportStatus::kPending:
      return "待处理";
    case ReportStatus::kUpheld:
      return "已成立";
    case ReportStatus::kRejected:
      return "已驳回";
  }
  return "未知";
}

std::string ReportReasonLabel(ReportReason reason) {
  switch (reason) {
    case ReportReason::kMisleading:
      return "虚假描述";
    case ReportReason::kDuplicate:
      return "重复发布";
    case ReportReason::kProhibited:
      return "违禁物品";
    case ReportReason::kContact:
      return "联系方式";
    case ReportReason::kOther:
      return "其他";
  }
  return "其他";
}

UserService::UserService(IUserRepository& users, IPasswordHasher& hasher,
                         IClock& clock, IIdGenerator& ids)
    : users_(users), hasher_(hasher), clock_(clock), ids_(ids) {}

void UserService::EnsureUnique(const std::string& email,
                               const std::string& phone) const {
  for (const auto& user : users_.All()) {
    if (ToLower(user.email) == ToLower(email)) {
      throw DomainError(ErrorCode::kDuplicate, "该邮箱已被注册");
    }
    if (user.phone == phone) {
      throw DomainError(ErrorCode::kDuplicate, "该手机号已被注册");
    }
  }
}

User UserService::Register(const std::string& email, const std::string& phone,
                           const std::string& password, bool as_seller) {
  std::string clean_email = ToLower(Trim(email));
  std::string clean_phone = Trim(phone);
  if (!IsValidEmail(clean_email)) {
    throw DomainError(ErrorCode::kInvalidArgument, "邮箱格式不正确");
  }
  if (!IsValidPhone(clean_phone)) {
    throw DomainError(ErrorCode::kInvalidArgument, "手机号必须是 11 位数字");
  }
  if (!IsStrongPassword(password)) {
    throw DomainError(ErrorCode::kInvalidArgument,
                      "密码至少 8 位，且同时包含字母和数字");
  }
  EnsureUnique(clean_email, clean_phone);

  User user;
  user.id = ids_.Next("US");
  user.email = clean_email;
  user.phone = clean_phone;
  user.salt = hasher_.GenerateSalt();
  user.password_hash = hasher_.Hash(password, user.salt);
  user.roles.insert(Role::kBuyer);
  if (as_seller) {
    user.roles.insert(Role::kSeller);
  }
  user.status = UserStatus::kActive;
  user.created_at = clock_.Now();
  users_.Add(user);
  return user;
}

User UserService::CreateAdmin(const std::string& email, const std::string& phone,
                              const std::string& password) {
  User user = Register(email, phone, password, false);
  user.roles.clear();
  user.roles.insert(Role::kAdmin);
  users_.Update(user);
  return user;
}

User UserService::Login(const std::string& account, const std::string& password) {
  std::string key = Trim(account);
  auto found = users_.FindByAccount(key);
  if (!found) {
    throw DomainError(ErrorCode::kUnauthorized, "账号或密码错误");
  }
  User user = *found;
  Timestamp now = clock_.Now();

  if (user.IsLocked(now)) {
    Timestamp remain = (user.lock_until - now + 59) / 60;
    throw DomainError(ErrorCode::kUnauthorized,
                      "账号已锁定，请 " + std::to_string(remain) + " 分钟后再试");
  }
  if (user.status == UserStatus::kLocked) {
    // 锁定期已过，自动解锁并重新计数。
    user.status = UserStatus::kActive;
    user.failed_attempts = 0;
    user.lock_until = 0;
  }

  if (!hasher_.Verify(password, user.salt, user.password_hash)) {
    user.failed_attempts += 1;
    if (user.failed_attempts >= kMaxLoginFailures) {
      user.status = UserStatus::kLocked;
      user.lock_until = now + kLockSeconds;
      users_.Update(user);
      throw DomainError(ErrorCode::kUnauthorized,
                        "密码连续错误 5 次，账号锁定 15 分钟");
    }
    int remain = kMaxLoginFailures - user.failed_attempts;
    users_.Update(user);
    throw DomainError(ErrorCode::kUnauthorized,
                      "密码错误，还可尝试 " + std::to_string(remain) + " 次");
  }

  user.failed_attempts = 0;
  user.status = UserStatus::kActive;
  user.lock_until = 0;
  users_.Update(user);
  return user;
}

}  // namespace campus
