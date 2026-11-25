// Copyright 2026 兰雨阳. All rights reserved.
// 领域模型实现：枚举映射、状态机、时间与金额格式化、文件字段转义。

#include "campus/model.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace campus {
namespace {

constexpr char kFieldSeparator = '\x1f';

std::string TwoDigits(int value) {
  std::ostringstream out;
  out << std::setw(2) << std::setfill('0') << value;
  return out.str();
}

}  // namespace

Timestamp Now() {
  using namespace std::chrono;
  return duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
}

std::string FormatTime(Timestamp time) {
  std::time_t raw = static_cast<std::time_t>(time);
  std::tm parts{};
#if defined(_WIN32)
  localtime_s(&parts, &raw);
#else
  localtime_r(&raw, &parts);
#endif
  std::ostringstream out;
  out << (parts.tm_year + 1900) << '-' << TwoDigits(parts.tm_mon + 1) << '-'
      << TwoDigits(parts.tm_mday) << ' ' << TwoDigits(parts.tm_hour) << ':'
      << TwoDigits(parts.tm_min) << ':' << TwoDigits(parts.tm_sec);
  return out.str();
}

std::string FormatDate(Timestamp time) {
  std::time_t raw = static_cast<std::time_t>(time);
  std::tm parts{};
#if defined(_WIN32)
  localtime_s(&parts, &raw);
#else
  localtime_r(&raw, &parts);
#endif
  std::ostringstream out;
  out << (parts.tm_year + 1900) << '-' << TwoDigits(parts.tm_mon + 1) << '-'
      << TwoDigits(parts.tm_mday);
  return out.str();
}

std::optional<Timestamp> ParseTime(const std::string& text) {
  std::tm parts{};
  std::istringstream in(text);
  in >> std::get_time(&parts, "%Y-%m-%d %H:%M:%S");
  if (in.fail()) {
    return std::nullopt;
  }
  parts.tm_isdst = -1;
  std::time_t raw = std::mktime(&parts);
  if (raw == static_cast<std::time_t>(-1)) {
    return std::nullopt;
  }
  return static_cast<Timestamp>(raw);
}

std::string ToString(Role role) {
  switch (role) {
    case Role::kBuyer:
      return "buyer";
    case Role::kSeller:
      return "seller";
    case Role::kAdmin:
      return "admin";
  }
  return "buyer";
}

std::string ToString(UserStatus status) {
  return status == UserStatus::kActive ? "active" : "locked";
}

std::string ToString(ItemStatus status) {
  switch (status) {
    case ItemStatus::kDraft:
      return "draft";
    case ItemStatus::kPending:
      return "pending";
    case ItemStatus::kPublished:
      return "published";
    case ItemStatus::kReserved:
      return "reserved";
    case ItemStatus::kConfirmed:
      return "confirmed";
    case ItemStatus::kCompleted:
      return "completed";
    case ItemStatus::kOffline:
      return "offline";
  }
  return "draft";
}

std::string ToString(ReservationStatus status) {
  switch (status) {
    case ReservationStatus::kPending:
      return "pending";
    case ReservationStatus::kConfirmed:
      return "confirmed";
    case ReservationStatus::kCompleted:
      return "completed";
    case ReservationStatus::kCanceled:
      return "canceled";
    case ReservationStatus::kVoided:
      return "voided";
  }
  return "pending";
}

std::string ToString(ReportStatus status) {
  switch (status) {
    case ReportStatus::kPending:
      return "pending";
    case ReportStatus::kUpheld:
      return "upheld";
    case ReportStatus::kRejected:
      return "rejected";
  }
  return "pending";
}

std::string ToString(ReportReason reason) {
  switch (reason) {
    case ReportReason::kMisleading:
      return "misleading";
    case ReportReason::kDuplicate:
      return "duplicate";
    case ReportReason::kProhibited:
      return "prohibited";
    case ReportReason::kContact:
      return "contact";
    case ReportReason::kOther:
      return "other";
  }
  return "other";
}

std::optional<Role> ParseRole(const std::string& text) {
  if (text == "buyer") return Role::kBuyer;
  if (text == "seller") return Role::kSeller;
  if (text == "admin") return Role::kAdmin;
  return std::nullopt;
}

std::optional<UserStatus> ParseUserStatus(const std::string& text) {
  if (text == "active") return UserStatus::kActive;
  if (text == "locked") return UserStatus::kLocked;
  return std::nullopt;
}

std::optional<ItemStatus> ParseItemStatus(const std::string& text) {
  if (text == "draft") return ItemStatus::kDraft;
  if (text == "pending") return ItemStatus::kPending;
  if (text == "published") return ItemStatus::kPublished;
  if (text == "reserved") return ItemStatus::kReserved;
  if (text == "confirmed") return ItemStatus::kConfirmed;
  if (text == "completed") return ItemStatus::kCompleted;
  if (text == "offline") return ItemStatus::kOffline;
  return std::nullopt;
}

std::optional<ReservationStatus> ParseReservationStatus(
    const std::string& text) {
  if (text == "pending") return ReservationStatus::kPending;
  if (text == "confirmed") return ReservationStatus::kConfirmed;
  if (text == "completed") return ReservationStatus::kCompleted;
  if (text == "canceled") return ReservationStatus::kCanceled;
  if (text == "voided") return ReservationStatus::kVoided;
  return std::nullopt;
}

std::optional<ReportStatus> ParseReportStatus(const std::string& text) {
  if (text == "pending") return ReportStatus::kPending;
  if (text == "upheld") return ReportStatus::kUpheld;
  if (text == "rejected") return ReportStatus::kRejected;
  return std::nullopt;
}

std::optional<ReportReason> ParseReportReason(const std::string& text) {
  if (text == "misleading") return ReportReason::kMisleading;
  if (text == "duplicate") return ReportReason::kDuplicate;
  if (text == "prohibited") return ReportReason::kProhibited;
  if (text == "contact") return ReportReason::kContact;
  if (text == "other") return ReportReason::kOther;
  return std::nullopt;
}

bool CanTransition(ItemStatus from, ItemStatus to) {
  // 商品状态机：草稿→待审核→已发布→已预约→已确认→已完成；
  // 驳回回草稿；已发布/已预约可下架；已完成与已下架是终态。
  switch (from) {
    case ItemStatus::kDraft:
      return to == ItemStatus::kPending;
    case ItemStatus::kPending:
      return to == ItemStatus::kPublished || to == ItemStatus::kDraft;
    case ItemStatus::kPublished:
      return to == ItemStatus::kReserved || to == ItemStatus::kPending ||
             to == ItemStatus::kOffline;
    case ItemStatus::kReserved:
      return to == ItemStatus::kPublished || to == ItemStatus::kConfirmed ||
             to == ItemStatus::kOffline;
    case ItemStatus::kConfirmed:
      return to == ItemStatus::kCompleted;
    case ItemStatus::kCompleted:
    case ItemStatus::kOffline:
      return false;
  }
  return false;
}

bool CanTransition(ReservationStatus from, ReservationStatus to) {
  switch (from) {
    case ReservationStatus::kPending:
      return to == ReservationStatus::kConfirmed ||
             to == ReservationStatus::kCanceled ||
             to == ReservationStatus::kVoided;
    case ReservationStatus::kConfirmed:
      return to == ReservationStatus::kCompleted ||
             to == ReservationStatus::kVoided;
    case ReservationStatus::kCompleted:
    case ReservationStatus::kCanceled:
    case ReservationStatus::kVoided:
      return false;
  }
  return false;
}

bool IsActiveReservation(ReservationStatus status) {
  return status == ReservationStatus::kPending ||
         status == ReservationStatus::kConfirmed;
}

bool IsTerminal(ItemStatus status) {
  return status == ItemStatus::kCompleted || status == ItemStatus::kOffline;
}

std::string ToLower(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return text;
}

std::string Trim(const std::string& text) {
  std::size_t begin = 0;
  while (begin < text.size() &&
         std::isspace(static_cast<unsigned char>(text[begin])) != 0) {
    ++begin;
  }
  std::size_t end = text.size();
  while (end > begin &&
         std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) {
    --end;
  }
  return text.substr(begin, end - begin);
}

std::size_t Utf8Length(const std::string& text) {
  std::size_t count = 0;
  for (unsigned char c : text) {
    if ((c & 0xC0) != 0x80) {
      ++count;
    }
  }
  return count;
}

bool StartsWith(const std::string& text, const std::string& prefix) {
  return text.size() >= prefix.size() &&
         text.compare(0, prefix.size(), prefix) == 0;
}

bool Contains(const std::string& text, const std::string& needle) {
  return text.find(needle) != std::string::npos;
}

std::string MoneyToString(Money money) {
  bool negative = money < 0;
  Money value = negative ? -money : money;
  std::ostringstream out;
  if (negative) {
    out << '-';
  }
  out << (value / 100) << '.' << TwoDigits(static_cast<int>(value % 100));
  return out.str();
}

std::optional<Money> ParseMoney(const std::string& text) {
  std::string cleaned = Trim(text);
  if (cleaned.empty()) {
    return std::nullopt;
  }
  bool negative = false;
  if (cleaned[0] == '-') {
    negative = true;
    cleaned.erase(cleaned.begin());
  }
  std::size_t dot = cleaned.find('.');
  std::string integer_part = dot == std::string::npos ? cleaned
                                                      : cleaned.substr(0, dot);
  std::string fraction_part =
      dot == std::string::npos ? "" : cleaned.substr(dot + 1);
  if (integer_part.empty() && fraction_part.empty()) {
    return std::nullopt;
  }
  if (fraction_part.size() > 2) {
    return std::nullopt;
  }
  while (fraction_part.size() < 2) {
    fraction_part.push_back('0');
  }
  Money value = 0;
  for (char c : integer_part) {
    if (std::isdigit(static_cast<unsigned char>(c)) == 0) {
      return std::nullopt;
    }
    value = value * 10 + (c - '0');
    if (value > kMaxPrice) {
      return std::nullopt;
    }
  }
  value *= 100;
  if (!fraction_part.empty()) {
    if (std::isdigit(static_cast<unsigned char>(fraction_part[0])) == 0 ||
        std::isdigit(static_cast<unsigned char>(fraction_part[1])) == 0) {
      return std::nullopt;
    }
    value += (fraction_part[0] - '0') * 10 + (fraction_part[1] - '0');
  }
  if (negative) {
    value = -value;
  }
  if (value > kMaxPrice || value < 0) {
    return std::nullopt;
  }
  return value;
}

std::string EscapeField(const std::string& text) {
  std::string out;
  out.reserve(text.size());
  for (char c : text) {
    switch (c) {
      case '\\':
        out += "\\\\";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case kFieldSeparator:
        out += "\\x1f";
        break;
      default:
        out.push_back(c);
    }
  }
  return out;
}

std::string UnescapeField(const std::string& text) {
  std::string out;
  out.reserve(text.size());
  for (std::size_t i = 0; i < text.size(); ++i) {
    if (text[i] != '\\' || i + 1 >= text.size()) {
      out.push_back(text[i]);
      continue;
    }
    char next = text[i + 1];
    if (next == '\\') {
      out.push_back('\\');
      ++i;
    } else if (next == 'n') {
      out.push_back('\n');
      ++i;
    } else if (next == 'r') {
      out.push_back('\r');
      ++i;
    } else if (next == 'x' && i + 3 < text.size() && text[i + 2] == '1' &&
               text[i + 3] == 'f') {
      out.push_back(kFieldSeparator);
      i += 3;
    } else {
      out.push_back(text[i]);
    }
  }
  return out;
}

std::string JoinFields(const std::vector<std::string>& fields) {
  std::string out;
  for (std::size_t i = 0; i < fields.size(); ++i) {
    if (i > 0) {
      out.push_back(kFieldSeparator);
    }
    out += EscapeField(fields[i]);
  }
  return out;
}

std::vector<std::string> SplitFields(const std::string& line) {
  std::vector<std::string> fields;
  std::string current;
  for (char c : line) {
    if (c == kFieldSeparator) {
      fields.push_back(UnescapeField(current));
      current.clear();
    } else {
      current.push_back(c);
    }
  }
  fields.push_back(UnescapeField(current));
  return fields;
}

std::string JoinSet(const std::set<Role>& roles) {
  std::string out;
  for (Role role : roles) {
    if (!out.empty()) {
      out.push_back(',');
    }
    out += ToString(role);
  }
  return out;
}

std::set<Role> ParseRoleSet(const std::string& text) {
  std::set<Role> roles;
  std::string token;
  for (char c : text) {
    if (c == ',') {
      if (auto role = ParseRole(token)) {
        roles.insert(*role);
      }
      token.clear();
    } else {
      token.push_back(c);
    }
  }
  if (auto role = ParseRole(token)) {
    roles.insert(*role);
  }
  return roles;
}

}  // namespace campus
