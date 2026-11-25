// Copyright 2026 兰雨阳. All rights reserved.
// 应用服务层：用例编排与事务边界。
//
// 对应实验二包图中的 7 个应用服务；服务只依赖仓储接口与基础设施接口，
// 便于单元测试替换为内存实现与假时钟。

#ifndef CAMPUS_SERVICES_H_
#define CAMPUS_SERVICES_H_

#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "campus/model.h"
#include "campus/store.h"

namespace campus {

// ---------- 公共辅助 ----------

User MustFindUser(const IUserRepository& users, const std::string& id);
void MustHaveRole(const User& user, Role role);
Item MustFindItem(const IItemRepository& items, const std::string& id);

std::string RoleLabel(Role role);
std::string ItemStatusLabel(ItemStatus status);
std::string ReservationStatusLabel(ReservationStatus status);
std::string ReportStatusLabel(ReportStatus status);
std::string ReportReasonLabel(ReportReason reason);

// ---------- 用户与权限 ----------

class UserService {
 public:
  UserService(IUserRepository& users, IPasswordHasher& hasher, IClock& clock,
              IIdGenerator& ids);

  // 注册：邮箱/手机号唯一，密码至少 8 位且同时含字母与数字。
  User Register(const std::string& email, const std::string& phone,
                const std::string& password, bool as_seller);
  // 登录：连续 5 次失败锁定 15 分钟；锁定期内即使密码正确也拒绝。
  User Login(const std::string& account, const std::string& password);
  // 管理员创建账号（初始化演示数据用）。
  User CreateAdmin(const std::string& email, const std::string& phone,
                   const std::string& password);

 private:
  void EnsureUnique(const std::string& email, const std::string& phone) const;
  IUserRepository& users_;
  IPasswordHasher& hasher_;
  IClock& clock_;
  IIdGenerator& ids_;
};

// ---------- 商品管理 ----------

class ItemService {
 public:
  ItemService(IUserRepository& users, IItemRepository& items,
              ICategoryRepository& categories, IReservationRepository& reservations,
              INotificationRepository& notifications, ILogRepository& logs,
              IClock& clock, IIdGenerator& ids);

  Item CreateDraft(const std::string& seller_id, const std::string& category_id,
                   const std::string& title, const std::string& description,
                   Money price);
  Item UpdateDraft(const std::string& seller_id, const std::string& item_id,
                   const std::string& title, const std::string& description,
                   const std::string& category_id, Money price);
  Item SubmitForReview(const std::string& seller_id, const std::string& item_id);
  Item Approve(const std::string& admin_id, const std::string& item_id);
  Item Reject(const std::string& admin_id, const std::string& item_id,
              const std::string& reason);
  Item UpdatePublished(const std::string& seller_id, const std::string& item_id,
                       const std::string& title, const std::string& description);
  Item ChangePrice(const std::string& seller_id, const std::string& item_id,
                   Money new_price);
  Item TakeOffline(const std::string& seller_id, const std::string& item_id,
                   const std::string& reason);
  std::vector<Item> ListBySeller(const std::string& seller_id) const;
  std::vector<Item> ListByStatus(ItemStatus status) const;
  std::optional<Item> Find(const std::string& item_id) const;

 private:
  Item Transition(const std::string& item_id, ItemStatus target,
                  const std::string& operator_id, const std::string& detail);
  void VoidActiveReservation(const std::string& item_id,
                             const std::string& reason);
  IUserRepository& users_;
  IItemRepository& items_;
  ICategoryRepository& categories_;
  IReservationRepository& reservations_;
  INotificationRepository& notifications_;
  ILogRepository& logs_;
  IClock& clock_;
  IIdGenerator& ids_;
};

// ---------- 检索 ----------

struct SearchQuery {
  enum class Sort { kLatest, kPriceAsc, kPriceDesc };
  std::string keyword;
  std::string category_id;
  std::optional<Money> min_price;
  std::optional<Money> max_price;
  std::optional<ItemStatus> status;
  Sort sort = Sort::kLatest;
  int page = 1;
  int page_size = 10;
};

struct SearchResult {
  std::vector<Item> items;
  int total = 0;
  int page = 1;
  int page_size = 10;
  int total_pages = 0;
};

class SearchService {
 public:
  SearchService(IItemRepository& items, ICategoryRepository& categories);

  SearchResult Search(const SearchQuery& query);
  // 查看详情：增加浏览次数。
  Item ViewDetail(const std::string& item_id);

 private:
  IItemRepository& items_;
  ICategoryRepository& categories_;
};

// ---------- 预约交易 ----------

class ReservationService {
 public:
  ReservationService(IUserRepository& users, IItemRepository& items,
                     IReservationRepository& reservations,
                     INotificationRepository& notifications, ILogRepository& logs,
                     IClock& clock, IIdGenerator& ids);

  // 预约：唯一有效预约 + 幂等号 + 行锁，三者共同保证并发安全。
  Reservation Reserve(const std::string& buyer_id, const std::string& item_id,
                      const std::string& request_id);
  Reservation Cancel(const std::string& buyer_id, const std::string& reservation_id);
  Reservation Confirm(const std::string& seller_id,
                      const std::string& reservation_id);
  Reservation Complete(const std::string& buyer_id,
                       const std::string& reservation_id);
  std::vector<Reservation> ListByBuyer(const std::string& buyer_id) const;
  std::vector<Reservation> ListByItem(const std::string& item_id) const;
  std::optional<Reservation> Find(const std::string& id) const;

 private:
  Reservation Apply(const std::string& reservation_id, ReservationStatus target,
                    const std::string& operator_id);
  IUserRepository& users_;
  IItemRepository& items_;
  IReservationRepository& reservations_;
  INotificationRepository& notifications_;
  ILogRepository& logs_;
  IClock& clock_;
  IIdGenerator& ids_;
  std::mutex mutex_;  // 串行化“查状态 + 写预约”，数据库里对应行锁
};

// ---------- 举报处理 ----------

class ReportService {
 public:
  ReportService(IUserRepository& users, IItemRepository& items,
                IReservationRepository& reservations, IReportRepository& reports,
                INotificationRepository& notifications, ILogRepository& logs,
                IClock& clock, IIdGenerator& ids);

  Report Submit(const std::string& reporter_id, const std::string& item_id,
                ReportReason reason, const std::string& description);
  // 判定成立则下架商品并作废有效预约；不成立只关闭举报。
  Report Handle(const std::string& admin_id, const std::string& report_id,
                bool uphold, const std::string& note);
  std::vector<Report> ListPending() const;
  std::vector<Report> ListByItem(const std::string& item_id) const;
  std::optional<Report> Find(const std::string& id) const;

 private:
  IUserRepository& users_;
  IItemRepository& items_;
  IReservationRepository& reservations_;
  IReportRepository& reports_;
  INotificationRepository& notifications_;
  ILogRepository& logs_;
  IClock& clock_;
  IIdGenerator& ids_;
};

// ---------- 操作日志 ----------

struct LogFilter {
  std::optional<std::string> operator_id;
  std::optional<std::string> target_type;
  std::optional<Timestamp> from;
  std::optional<Timestamp> to;
  std::size_t limit = 200;
};

class LogService {
 public:
  LogService(ILogRepository& logs, IClock& clock, IIdGenerator& ids);

  void Record(const std::string& operator_id, const std::string& action,
              const std::string& target_type, const std::string& target_id,
              const std::string& detail);
  std::vector<OperationLog> Query(const LogFilter& filter) const;

 private:
  ILogRepository& logs_;
  IClock& clock_;
  IIdGenerator& ids_;
};

// ---------- 统计 ----------

struct StatsReport {
  std::map<std::string, int> items_by_category;   // 分类名 → 商品数
  std::map<ItemStatus, int> items_by_status;      // 状态 → 数量
  int new_items = 0;                              // 区间内新增商品
  int successful_reservations = 0;                // 区间内预约成功
  int pending_reports = 0;
  int active_users = 0;
  Timestamp from = 0;
  Timestamp to = 0;
};

struct SellerStats {
  int published = 0;
  int pending = 0;
  int reserved = 0;
  int completed = 0;
  int total_views = 0;
  int total_reservations = 0;
};

class StatsService {
 public:
  StatsService(IUserRepository& users, IItemRepository& items,
               ICategoryRepository& categories,
               IReservationRepository& reservations, IReportRepository& reports);

  StatsReport Overview(Timestamp from, Timestamp to) const;
  SellerStats ForSeller(const std::string& seller_id) const;

 private:
  IUserRepository& users_;
  IItemRepository& items_;
  ICategoryRepository& categories_;
  IReservationRepository& reservations_;
  IReportRepository& reports_;
};

}  // namespace campus

#endif  // CAMPUS_SERVICES_H_
