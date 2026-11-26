// Copyright 2026 兰雨阳. All rights reserved.
// 文件仓储与基础设施实现。

#include "campus/store.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <random>
#include <sstream>

namespace campus {
namespace {

std::int64_t ToInt(const std::string& text, std::int64_t fallback = 0) {
  try {
    return std::stoll(text);
  } catch (...) {
    return fallback;
  }
}

std::string FromInt(std::int64_t value) { return std::to_string(value); }

void EnsureParentDirectory(const std::string& path) {
  std::filesystem::path file_path(path);
  if (file_path.has_parent_path()) {
    std::error_code error;
    std::filesystem::create_directories(file_path.parent_path(), error);
  }
}

}  // namespace

// ---------- 序列化 ----------

std::vector<std::string> ToFields(const User& user) {
  return {user.id,
          user.email,
          user.phone,
          user.password_hash,
          user.salt,
          JoinSet(user.roles),
          ToString(user.status),
          FromInt(user.failed_attempts),
          FromInt(user.lock_until),
          FromInt(user.created_at)};
}

bool FromFields(const std::vector<std::string>& fields, User* user) {
  if (fields.size() < 10) {
    return false;
  }
  user->id = fields[0];
  user->email = fields[1];
  user->phone = fields[2];
  user->password_hash = fields[3];
  user->salt = fields[4];
  user->roles = ParseRoleSet(fields[5]);
  user->status = ParseUserStatus(fields[6]).value_or(UserStatus::kActive);
  user->failed_attempts = static_cast<int>(ToInt(fields[7]));
  user->lock_until = ToInt(fields[8]);
  user->created_at = ToInt(fields[9]);
  return true;
}

std::vector<std::string> ToFields(const Category& category) {
  return {category.id, category.name};
}

bool FromFields(const std::vector<std::string>& fields, Category* category) {
  if (fields.size() < 2) {
    return false;
  }
  category->id = fields[0];
  category->name = fields[1];
  return true;
}

std::vector<std::string> ToFields(const Item& item) {
  return {item.id,
          item.seller_id,
          item.category_id,
          item.title,
          item.description,
          FromInt(item.price),
          ToString(item.status),
          FromInt(item.view_count),
          item.reject_reason,
          item.image,
          FromInt(item.created_at),
          FromInt(item.updated_at)};
}

bool FromFields(const std::vector<std::string>& fields, Item* item) {
  // 兼容旧数据：11 个字段时没有 image，时间字段前移。
  if (fields.size() < 11) {
    return false;
  }
  item->id = fields[0];
  item->seller_id = fields[1];
  item->category_id = fields[2];
  item->title = fields[3];
  item->description = fields[4];
  item->price = ToInt(fields[5]);
  item->status = ParseItemStatus(fields[6]).value_or(ItemStatus::kDraft);
  item->view_count = static_cast<int>(ToInt(fields[7]));
  item->reject_reason = fields[8];
  if (fields.size() >= 12) {
    item->image = fields[9];
    item->created_at = ToInt(fields[10]);
    item->updated_at = ToInt(fields[11]);
  } else {
    item->image.clear();
    item->created_at = ToInt(fields[9]);
    item->updated_at = ToInt(fields[10]);
  }
  return true;
}

std::vector<std::string> ToFields(const Reservation& reservation) {
  return {reservation.id,
          reservation.item_id,
          reservation.buyer_id,
          ToString(reservation.status),
          reservation.request_id,
          FromInt(reservation.created_at),
          FromInt(reservation.confirmed_at),
          FromInt(reservation.finished_at)};
}

bool FromFields(const std::vector<std::string>& fields, Reservation* reservation) {
  if (fields.size() < 8) {
    return false;
  }
  reservation->id = fields[0];
  reservation->item_id = fields[1];
  reservation->buyer_id = fields[2];
  reservation->status =
      ParseReservationStatus(fields[3]).value_or(ReservationStatus::kPending);
  reservation->request_id = fields[4];
  reservation->created_at = ToInt(fields[5]);
  reservation->confirmed_at = ToInt(fields[6]);
  reservation->finished_at = ToInt(fields[7]);
  return true;
}

std::vector<std::string> ToFields(const Report& report) {
  return {report.id,
          report.item_id,
          report.reporter_id,
          ToString(report.reason),
          report.description,
          ToString(report.status),
          report.result,
          report.handler_id,
          FromInt(report.created_at),
          FromInt(report.handled_at)};
}

bool FromFields(const std::vector<std::string>& fields, Report* report) {
  if (fields.size() < 10) {
    return false;
  }
  report->id = fields[0];
  report->item_id = fields[1];
  report->reporter_id = fields[2];
  report->reason = ParseReportReason(fields[3]).value_or(ReportReason::kOther);
  report->description = fields[4];
  report->status = ParseReportStatus(fields[5]).value_or(ReportStatus::kPending);
  report->result = fields[6];
  report->handler_id = fields[7];
  report->created_at = ToInt(fields[8]);
  report->handled_at = ToInt(fields[9]);
  return true;
}

std::vector<std::string> ToFields(const OperationLog& log) {
  return {log.id,
          log.operator_id,
          log.action,
          log.target_type,
          log.target_id,
          log.detail,
          FromInt(log.created_at)};
}

bool FromFields(const std::vector<std::string>& fields, OperationLog* log) {
  if (fields.size() < 7) {
    return false;
  }
  log->id = fields[0];
  log->operator_id = fields[1];
  log->action = fields[2];
  log->target_type = fields[3];
  log->target_id = fields[4];
  log->detail = fields[5];
  log->created_at = ToInt(fields[6]);
  return true;
}

std::vector<std::string> ToFields(const Notification& notification) {
  return {notification.id,
          notification.user_id,
          notification.type,
          notification.content,
          notification.read ? "1" : "0",
          FromInt(notification.created_at)};
}

bool FromFields(const std::vector<std::string>& fields,
                Notification* notification) {
  if (fields.size() < 6) {
    return false;
  }
  notification->id = fields[0];
  notification->user_id = fields[1];
  notification->type = fields[2];
  notification->content = fields[3];
  notification->read = fields[4] == "1";
  notification->created_at = ToInt(fields[5]);
  return true;
}

// ---------- 表 ----------

template <typename T>
void Table<T>::Load() {
  rows_.clear();
  if (path_.empty()) {
    return;
  }
  std::ifstream in(path_);
  if (!in) {
    return;
  }
  std::string line;
  while (std::getline(in, line)) {
    if (line.empty()) {
      continue;
    }
    T value{};
    if (FromFields(SplitFields(line), &value)) {
      rows_.push_back(std::move(value));
    }
  }
}

template <typename T>
void Table<T>::Save() const {
  if (path_.empty()) {
    return;
  }
  EnsureParentDirectory(path_);
  std::ofstream out(path_, std::ios::trunc);
  for (const auto& row : rows_) {
    out << JoinFields(ToFields(row)) << '\n';
  }
}

template class Table<User>;
template class Table<Category>;
template class Table<Item>;
template class Table<Reservation>;
template class Table<Report>;
template class Table<OperationLog>;
template class Table<Notification>;

// ---------- 用户仓储 ----------

FileUserRepository::FileUserRepository(const std::string& path) : table_(path) {}

void FileUserRepository::Load() { table_.Load(); }
void FileUserRepository::Save() const { table_.Save(); }

std::optional<User> FileUserRepository::FindById(const std::string& id) const {
  for (const auto& user : table_.rows()) {
    if (user.id == id) {
      return user;
    }
  }
  return std::nullopt;
}

std::optional<User> FileUserRepository::FindByAccount(
    const std::string& account) const {
  std::string needle = ToLower(account);
  for (const auto& user : table_.rows()) {
    if (ToLower(user.email) == needle || user.phone == account) {
      return user;
    }
  }
  return std::nullopt;
}

std::vector<User> FileUserRepository::All() const { return table_.rows(); }

void FileUserRepository::Add(const User& user) { table_.rows().push_back(user); }

void FileUserRepository::Update(const User& user) {
  for (auto& row : table_.rows()) {
    if (row.id == user.id) {
      row = user;
      return;
    }
  }
  table_.rows().push_back(user);
}

// ---------- 分类仓储 ----------

FileCategoryRepository::FileCategoryRepository(const std::string& path)
    : table_(path) {}

void FileCategoryRepository::Load() { table_.Load(); }
void FileCategoryRepository::Save() const { table_.Save(); }

std::optional<Category> FileCategoryRepository::FindById(
    const std::string& id) const {
  for (const auto& row : table_.rows()) {
    if (row.id == id) {
      return row;
    }
  }
  return std::nullopt;
}

std::vector<Category> FileCategoryRepository::All() const {
  return table_.rows();
}

void FileCategoryRepository::Add(const Category& category) {
  table_.rows().push_back(category);
}

// ---------- 商品仓储 ----------

FileItemRepository::FileItemRepository(const std::string& path) : table_(path) {}

void FileItemRepository::Load() { table_.Load(); }
void FileItemRepository::Save() const { table_.Save(); }

std::optional<Item> FileItemRepository::FindById(const std::string& id) const {
  for (const auto& row : table_.rows()) {
    if (row.id == id) {
      return row;
    }
  }
  return std::nullopt;
}

std::vector<Item> FileItemRepository::All() const { return table_.rows(); }

std::vector<Item> FileItemRepository::FindBySeller(
    const std::string& seller_id) const {
  std::vector<Item> result;
  for (const auto& row : table_.rows()) {
    if (row.seller_id == seller_id) {
      result.push_back(row);
    }
  }
  return result;
}

void FileItemRepository::Add(const Item& item) {
  table_.rows().push_back(item);
}

void FileItemRepository::Update(const Item& item) {
  for (auto& row : table_.rows()) {
    if (row.id == item.id) {
      row = item;
      return;
    }
  }
  table_.rows().push_back(item);
}

// ---------- 预约仓储 ----------

FileReservationRepository::FileReservationRepository(const std::string& path)
    : table_(path) {}

void FileReservationRepository::Load() { table_.Load(); }
void FileReservationRepository::Save() const { table_.Save(); }

std::optional<Reservation> FileReservationRepository::FindById(
    const std::string& id) const {
  for (const auto& row : table_.rows()) {
    if (row.id == id) {
      return row;
    }
  }
  return std::nullopt;
}

std::optional<Reservation> FileReservationRepository::FindActiveByItem(
    const std::string& item_id) const {
  for (const auto& row : table_.rows()) {
    if (row.item_id == item_id && IsActiveReservation(row.status)) {
      return row;
    }
  }
  return std::nullopt;
}

std::optional<Reservation> FileReservationRepository::FindByRequestId(
    const std::string& request_id) const {
  if (request_id.empty()) {
    return std::nullopt;
  }
  for (const auto& row : table_.rows()) {
    if (row.request_id == request_id) {
      return row;
    }
  }
  return std::nullopt;
}

std::vector<Reservation> FileReservationRepository::FindByItem(
    const std::string& item_id) const {
  std::vector<Reservation> result;
  for (const auto& row : table_.rows()) {
    if (row.item_id == item_id) {
      result.push_back(row);
    }
  }
  return result;
}

std::vector<Reservation> FileReservationRepository::FindByBuyer(
    const std::string& buyer_id) const {
  std::vector<Reservation> result;
  for (const auto& row : table_.rows()) {
    if (row.buyer_id == buyer_id) {
      result.push_back(row);
    }
  }
  return result;
}

std::vector<Reservation> FileReservationRepository::All() const {
  return table_.rows();
}

void FileReservationRepository::Add(const Reservation& reservation) {
  table_.rows().push_back(reservation);
}

void FileReservationRepository::Update(const Reservation& reservation) {
  for (auto& row : table_.rows()) {
    if (row.id == reservation.id) {
      row = reservation;
      return;
    }
  }
  table_.rows().push_back(reservation);
}

// ---------- 举报仓储 ----------

FileReportRepository::FileReportRepository(const std::string& path)
    : table_(path) {}

void FileReportRepository::Load() { table_.Load(); }
void FileReportRepository::Save() const { table_.Save(); }

std::optional<Report> FileReportRepository::FindById(
    const std::string& id) const {
  for (const auto& row : table_.rows()) {
    if (row.id == id) {
      return row;
    }
  }
  return std::nullopt;
}

std::vector<Report> FileReportRepository::FindByItem(
    const std::string& item_id) const {
  std::vector<Report> result;
  for (const auto& row : table_.rows()) {
    if (row.item_id == item_id) {
      result.push_back(row);
    }
  }
  return result;
}

std::vector<Report> FileReportRepository::All() const { return table_.rows(); }

void FileReportRepository::Add(const Report& report) {
  table_.rows().push_back(report);
}

void FileReportRepository::Update(const Report& report) {
  for (auto& row : table_.rows()) {
    if (row.id == report.id) {
      row = report;
      return;
    }
  }
  table_.rows().push_back(report);
}

// ---------- 日志仓储 ----------

FileLogRepository::FileLogRepository(const std::string& path) : table_(path) {}

void FileLogRepository::Load() { table_.Load(); }
void FileLogRepository::Save() const { table_.Save(); }

std::vector<OperationLog> FileLogRepository::All() const { return table_.rows(); }

void FileLogRepository::Add(const OperationLog& log) {
  table_.rows().push_back(log);
}

// ---------- 通知仓储 ----------

FileNotificationRepository::FileNotificationRepository(const std::string& path)
    : table_(path) {}

void FileNotificationRepository::Load() { table_.Load(); }
void FileNotificationRepository::Save() const { table_.Save(); }

std::vector<Notification> FileNotificationRepository::FindByUser(
    const std::string& user_id) const {
  std::vector<Notification> result;
  for (const auto& row : table_.rows()) {
    if (row.user_id == user_id) {
      result.push_back(row);
    }
  }
  return result;
}

void FileNotificationRepository::Add(const Notification& notification) {
  table_.rows().push_back(notification);
}

void FileNotificationRepository::MarkRead(const std::string& id) {
  for (auto& row : table_.rows()) {
    if (row.id == id) {
      row.read = true;
      return;
    }
  }
}

// ---------- 路径与基础设施 ----------

DatabasePaths MakeDatabasePaths(const std::string& data_dir) {
  DatabasePaths paths;
  paths.users = data_dir + "/users.db";
  paths.categories = data_dir + "/categories.db";
  paths.items = data_dir + "/items.db";
  paths.reservations = data_dir + "/reservations.db";
  paths.reports = data_dir + "/reports.db";
  paths.logs = data_dir + "/logs.db";
  paths.notifications = data_dir + "/notifications.db";
  return paths;
}

std::string FnvPasswordHasher::GenerateSalt() const {
  std::random_device device;
  std::mt19937_64 engine(device());
  std::uniform_int_distribution<int> dist(0, 255);
  std::ostringstream out;
  out << std::hex << std::setfill('0');
  for (int i = 0; i < 16; ++i) {
    out << std::setw(2) << dist(engine);
  }
  return out.str();
}

std::string FnvPasswordHasher::Hash(const std::string& password,
                                    const std::string& salt) const {
  // 迭代 FNV-1a，每轮把上一轮结果拼进输入，拉长暴力破解成本。
  std::uint64_t state = 1469598103934665603ULL;
  std::string material = salt + ":" + password;
  for (int round = 0; round < kIterations; ++round) {
    for (char c : material) {
      state ^= static_cast<unsigned char>(c);
      state *= 1099511628211ULL;
    }
    state ^= static_cast<std::uint64_t>(round);
    state *= 1099511628211ULL;
  }
  std::ostringstream out;
  out << std::hex << std::setw(16) << std::setfill('0') << state;
  return out.str();
}

bool FnvPasswordHasher::Verify(const std::string& password,
                               const std::string& salt,
                               const std::string& hash) const {
  return Hash(password, salt) == hash;
}

std::string SequenceIdGenerator::Next(const std::string& prefix) {
  std::uint64_t index = counter_.fetch_add(1) + 1;
  std::ostringstream out;
  out << prefix << '-' << clock_.Now() << '-' << std::setw(4)
      << std::setfill('0') << index;
  return out.str();
}

}  // namespace campus
