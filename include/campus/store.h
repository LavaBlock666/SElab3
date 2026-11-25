// Copyright 2026 兰雨阳. All rights reserved.
// 仓储接口、文件持久化与基础设施（时钟、密码哈希、编号生成）。
//
// 仓储接口与领域层同构：服务只依赖接口，文件实现可以在测试中替换为
// 纯内存实现（构造时传入空路径即可）。

#ifndef CAMPUS_STORE_H_
#define CAMPUS_STORE_H_

#include <atomic>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "campus/model.h"

namespace campus {

// ---------- 序列化 ----------

std::vector<std::string> ToFields(const User& user);
bool FromFields(const std::vector<std::string>& fields, User* user);
std::vector<std::string> ToFields(const Category& category);
bool FromFields(const std::vector<std::string>& fields, Category* category);
std::vector<std::string> ToFields(const Item& item);
bool FromFields(const std::vector<std::string>& fields, Item* item);
std::vector<std::string> ToFields(const Reservation& reservation);
bool FromFields(const std::vector<std::string>& fields, Reservation* reservation);
std::vector<std::string> ToFields(const Report& report);
bool FromFields(const std::vector<std::string>& fields, Report* report);
std::vector<std::string> ToFields(const OperationLog& log);
bool FromFields(const std::vector<std::string>& fields, OperationLog* log);
std::vector<std::string> ToFields(const Notification& notification);
bool FromFields(const std::vector<std::string>& fields, Notification* notification);

// ---------- 持久化表 ----------

// 以 0x1f 分隔的文本表；路径为空时退化为纯内存表。
template <typename T>
class Table {
 public:
  explicit Table(std::string path) : path_(std::move(path)) {}

  void Load();
  void Save() const;

  const std::vector<T>& rows() const { return rows_; }
  std::vector<T>& rows() { return rows_; }
  bool persistent() const { return !path_.empty(); }
  const std::string& path() const { return path_; }

 private:
  std::string path_;
  std::vector<T> rows_;
};

// ---------- 仓储接口 ----------

class IUserRepository {
 public:
  virtual ~IUserRepository() = default;
  virtual std::optional<User> FindById(const std::string& id) const = 0;
  virtual std::optional<User> FindByAccount(const std::string& account) const = 0;
  virtual std::vector<User> All() const = 0;
  virtual void Add(const User& user) = 0;
  virtual void Update(const User& user) = 0;
};

class IItemRepository {
 public:
  virtual ~IItemRepository() = default;
  virtual std::optional<Item> FindById(const std::string& id) const = 0;
  virtual std::vector<Item> All() const = 0;
  virtual std::vector<Item> FindBySeller(const std::string& seller_id) const = 0;
  virtual void Add(const Item& item) = 0;
  virtual void Update(const Item& item) = 0;
};

class IReservationRepository {
 public:
  virtual ~IReservationRepository() = default;
  virtual std::optional<Reservation> FindById(const std::string& id) const = 0;
  virtual std::optional<Reservation> FindActiveByItem(
      const std::string& item_id) const = 0;
  virtual std::optional<Reservation> FindByRequestId(
      const std::string& request_id) const = 0;
  virtual std::vector<Reservation> FindByItem(const std::string& item_id) const = 0;
  virtual std::vector<Reservation> FindByBuyer(const std::string& buyer_id) const = 0;
  virtual std::vector<Reservation> All() const = 0;
  virtual void Add(const Reservation& reservation) = 0;
  virtual void Update(const Reservation& reservation) = 0;
};

class IReportRepository {
 public:
  virtual ~IReportRepository() = default;
  virtual std::optional<Report> FindById(const std::string& id) const = 0;
  virtual std::vector<Report> FindByItem(const std::string& item_id) const = 0;
  virtual std::vector<Report> All() const = 0;
  virtual void Add(const Report& report) = 0;
  virtual void Update(const Report& report) = 0;
};

class ILogRepository {
 public:
  virtual ~ILogRepository() = default;
  virtual std::vector<OperationLog> All() const = 0;
  virtual void Add(const OperationLog& log) = 0;
};

class ICategoryRepository {
 public:
  virtual ~ICategoryRepository() = default;
  virtual std::optional<Category> FindById(const std::string& id) const = 0;
  virtual std::vector<Category> All() const = 0;
  virtual void Add(const Category& category) = 0;
};

class INotificationRepository {
 public:
  virtual ~INotificationRepository() = default;
  virtual std::vector<Notification> FindByUser(const std::string& user_id) const = 0;
  virtual void Add(const Notification& notification) = 0;
  virtual void MarkRead(const std::string& id) = 0;
};

// ---------- 文件实现 ----------

class FileUserRepository : public IUserRepository {
 public:
  explicit FileUserRepository(const std::string& path);
  void Load();
  void Save() const;

  std::optional<User> FindById(const std::string& id) const override;
  std::optional<User> FindByAccount(const std::string& account) const override;
  std::vector<User> All() const override;
  void Add(const User& user) override;
  void Update(const User& user) override;

 private:
  Table<User> table_;
};

class FileItemRepository : public IItemRepository {
 public:
  explicit FileItemRepository(const std::string& path);
  void Load();
  void Save() const;

  std::optional<Item> FindById(const std::string& id) const override;
  std::vector<Item> All() const override;
  std::vector<Item> FindBySeller(const std::string& seller_id) const override;
  void Add(const Item& item) override;
  void Update(const Item& item) override;

 private:
  Table<Item> table_;
};

class FileReservationRepository : public IReservationRepository {
 public:
  explicit FileReservationRepository(const std::string& path);
  void Load();
  void Save() const;

  std::optional<Reservation> FindById(const std::string& id) const override;
  std::optional<Reservation> FindActiveByItem(
      const std::string& item_id) const override;
  std::optional<Reservation> FindByRequestId(
      const std::string& request_id) const override;
  std::vector<Reservation> FindByItem(const std::string& item_id) const override;
  std::vector<Reservation> FindByBuyer(const std::string& buyer_id) const override;
  std::vector<Reservation> All() const override;
  void Add(const Reservation& reservation) override;
  void Update(const Reservation& reservation) override;

 private:
  Table<Reservation> table_;
};

class FileReportRepository : public IReportRepository {
 public:
  explicit FileReportRepository(const std::string& path);
  void Load();
  void Save() const;

  std::optional<Report> FindById(const std::string& id) const override;
  std::vector<Report> FindByItem(const std::string& item_id) const override;
  std::vector<Report> All() const override;
  void Add(const Report& report) override;
  void Update(const Report& report) override;

 private:
  Table<Report> table_;
};

class FileLogRepository : public ILogRepository {
 public:
  explicit FileLogRepository(const std::string& path);
  void Load();
  void Save() const;

  std::vector<OperationLog> All() const override;
  void Add(const OperationLog& log) override;

 private:
  Table<OperationLog> table_;
};

class FileCategoryRepository : public ICategoryRepository {
 public:
  explicit FileCategoryRepository(const std::string& path);
  void Load();
  void Save() const;

  std::optional<Category> FindById(const std::string& id) const override;
  std::vector<Category> All() const override;
  void Add(const Category& category) override;

 private:
  Table<Category> table_;
};

class FileNotificationRepository : public INotificationRepository {
 public:
  explicit FileNotificationRepository(const std::string& path);
  void Load();
  void Save() const;

  std::vector<Notification> FindByUser(const std::string& user_id) const override;
  void Add(const Notification& notification) override;
  void MarkRead(const std::string& id) override;

 private:
  Table<Notification> table_;
};

// 统一加载/保存全部表，便于程序启动与退出。
struct DatabasePaths {
  std::string users;
  std::string categories;
  std::string items;
  std::string reservations;
  std::string reports;
  std::string logs;
  std::string notifications;
};

DatabasePaths MakeDatabasePaths(const std::string& data_dir);

// ---------- 基础设施 ----------

class IClock {
 public:
  virtual ~IClock() = default;
  virtual Timestamp Now() const = 0;
};

class SystemClock : public IClock {
 public:
  Timestamp Now() const override { return campus::Now(); }
};

class FakeClock : public IClock {
 public:
  explicit FakeClock(Timestamp start = 1700000000) : now_(start) {}
  Timestamp Now() const override { return now_; }
  void Advance(Timestamp seconds) { now_ += seconds; }

 private:
  Timestamp now_;
};

class IPasswordHasher {
 public:
  virtual ~IPasswordHasher() = default;
  virtual std::string GenerateSalt() const = 0;
  virtual std::string Hash(const std::string& password,
                           const std::string& salt) const = 0;
  virtual bool Verify(const std::string& password, const std::string& salt,
                      const std::string& hash) const = 0;
};

// 加盐 + 多次迭代的 FNV-1a，避免明文存储；课程场景下无需引入第三方库。
class FnvPasswordHasher : public IPasswordHasher {
 public:
  std::string GenerateSalt() const override;
  std::string Hash(const std::string& password,
                   const std::string& salt) const override;
  bool Verify(const std::string& password, const std::string& salt,
              const std::string& hash) const override;

 private:
  static constexpr int kIterations = 10000;
};

class IIdGenerator {
 public:
  virtual ~IIdGenerator() = default;
  virtual std::string Next(const std::string& prefix) = 0;
};

class SequenceIdGenerator : public IIdGenerator {
 public:
  explicit SequenceIdGenerator(const IClock& clock) : clock_(clock) {}
  std::string Next(const std::string& prefix) override;

 private:
  const IClock& clock_;
  std::atomic<std::uint64_t> counter_{0};
};

}  // namespace campus

#endif  // CAMPUS_STORE_H_
