// Copyright 2026 兰雨阳. All rights reserved.
// 单元测试：覆盖状态机、权限、唯一预约、幂等、锁定、举报联动与持久化。

#include <atomic>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "campus/services.h"
#include "campus/store.h"

namespace {

int g_failures = 0;
int g_checks = 0;

void Check(bool condition, const char* expression, const char* file, int line) {
  ++g_checks;
  if (!condition) {
    ++g_failures;
    std::cout << "  [FAIL] " << file << ":" << line << "  " << expression << "\n";
  }
}

#define CHECK(condition) Check((condition), #condition, __FILE__, __LINE__)

#define CHECK_THROWS(expression, expected_code)                         \
  do {                                                                  \
    bool caught = false;                                                \
    try {                                                               \
      (void)(expression);                                               \
    } catch (const campus::DomainError& error) {                        \
      caught = (error.code() == (expected_code));                       \
    }                                                                   \
    Check(caught, #expression " throws " #expected_code, __FILE__,      \
          __LINE__);                                                    \
  } while (0)

using namespace campus;

struct Fixture {
  FakeClock clock;
  FnvPasswordHasher hasher;
  SequenceIdGenerator ids{clock};
  FileUserRepository users{""};
  FileCategoryRepository categories{""};
  FileItemRepository items{""};
  FileReservationRepository reservations{""};
  FileReportRepository reports{""};
  FileLogRepository logs{""};
  FileNotificationRepository notifications{""};

  UserService user_service{users, hasher, clock, ids};
  ItemService item_service{users, items, categories, reservations, notifications,
                           logs, clock, ids};
  SearchService search_service{items, categories};
  ReservationService reservation_service{users, items, reservations,
                                         notifications, logs, clock, ids};
  ReportService report_service{users, items, reservations, reports,
                               notifications, logs, clock, ids};
  LogService log_service{logs, clock, ids};
  StatsService stats_service{users, items, categories, reservations, reports};

  User admin;
  User seller;
  User buyer;
  User buyer2;
  std::string category_id;

  Fixture() {
    Category category;
    category.id = "CT-1";
    category.name = "教材书籍";
    categories.Add(category);
    category_id = category.id;

    admin = user_service.CreateAdmin("admin@campus.edu", "13800000001",
                                     "Admin@123");
    seller = user_service.Register("seller@campus.edu", "13800000002",
                                   "Seller@123", true);
    buyer = user_service.Register("buyer@campus.edu", "13800000003", "Buyer@123",
                                  false);
    buyer2 = user_service.Register("buyer2@campus.edu", "13800000004",
                                   "Buyer@123", false);
  }

  std::string PublishItem(const std::string& title = "高等数学 第七版",
                          Money price = 2500) {
    Item item = item_service.CreateDraft(seller.id, category_id, title,
                                         "九成新，笔记齐全。", price);
    item_service.SubmitForReview(seller.id, item.id);
    item_service.Approve(admin.id, item.id);
    return item.id;
  }
};

void TestStateMachine() {
  CHECK(CanTransition(ItemStatus::kDraft, ItemStatus::kPending));
  CHECK(CanTransition(ItemStatus::kPending, ItemStatus::kPublished));
  CHECK(CanTransition(ItemStatus::kPending, ItemStatus::kDraft));
  CHECK(CanTransition(ItemStatus::kPublished, ItemStatus::kReserved));
  CHECK(CanTransition(ItemStatus::kReserved, ItemStatus::kPublished));
  CHECK(CanTransition(ItemStatus::kReserved, ItemStatus::kConfirmed));
  CHECK(CanTransition(ItemStatus::kConfirmed, ItemStatus::kCompleted));
  CHECK(!CanTransition(ItemStatus::kCompleted, ItemStatus::kReserved));
  CHECK(!CanTransition(ItemStatus::kOffline, ItemStatus::kPublished));
  CHECK(!CanTransition(ItemStatus::kPublished, ItemStatus::kCompleted));
  CHECK(CanTransition(ReservationStatus::kPending,
                      ReservationStatus::kConfirmed));
  CHECK(!CanTransition(ReservationStatus::kCanceled,
                       ReservationStatus::kPending));
}

void TestFormatting() {
  CHECK(MoneyToString(2500) == "25.00");
  CHECK(MoneyToString(5) == "0.05");
  CHECK(ParseMoney("12.3").value_or(-1) == 1230);
  CHECK(ParseMoney("0").value_or(-1) == 0);
  CHECK(!ParseMoney("1.234"));
  CHECK(!ParseMoney("abc"));
  std::string raw = "a\nb\\c";
  CHECK(EscapeField(raw) == "a\\nb\\\\c");
  CHECK(SplitFields(JoinFields({raw, "x"}))[0] == raw);
  auto time = ParseTime("2026-11-09 10:00:00");
  CHECK(time.has_value());
  CHECK(FormatTime(*time) == "2026-11-09 10:00:00");
}

void TestRegister() {
  Fixture fixture;
  auto user = fixture.user_service.Register("new@campus.edu", "13900000001",
                                            "Passw0rd", false);
  CHECK(user.HasRole(Role::kBuyer));
  CHECK(!user.HasRole(Role::kSeller));
  CHECK_THROWS(fixture.user_service.Register("new@campus.edu", "13900000002",
                                             "Passw0rd", false),
               ErrorCode::kDuplicate);
  CHECK_THROWS(fixture.user_service.Register("bad", "13900000003", "Passw0rd",
                                             false),
               ErrorCode::kInvalidArgument);
  CHECK_THROWS(fixture.user_service.Register("x@campus.edu", "123",
                                             "Passw0rd", false),
               ErrorCode::kInvalidArgument);
  CHECK_THROWS(fixture.user_service.Register("y@campus.edu", "13900000004",
                                             "short1", false),
               ErrorCode::kInvalidArgument);
}

void TestLoginAndLockout() {
  Fixture fixture;
  User user = fixture.user_service.Login("buyer@campus.edu", "Buyer@123");
  CHECK(user.email == "buyer@campus.edu");

  for (int i = 0; i < 4; ++i) {
    CHECK_THROWS(fixture.user_service.Login("buyer@campus.edu", "wrong"),
                 ErrorCode::kUnauthorized);
  }
  CHECK_THROWS(fixture.user_service.Login("buyer@campus.edu", "wrong"),
               ErrorCode::kUnauthorized);
  // 锁定后即使密码正确也要拒绝。
  CHECK_THROWS(fixture.user_service.Login("buyer@campus.edu", "Buyer@123"),
               ErrorCode::kUnauthorized);
  fixture.clock.Advance(kLockSeconds + 1);
  User unlocked = fixture.user_service.Login("buyer@campus.edu", "Buyer@123");
  CHECK(unlocked.status == UserStatus::kActive);
}

void TestItemLifecycle() {
  Fixture fixture;
  Item draft = fixture.item_service.CreateDraft(fixture.seller.id,
                                               fixture.category_id, "显示器",
                                               "无坏点", 62000);
  CHECK(draft.status == ItemStatus::kDraft);
  Item pending = fixture.item_service.SubmitForReview(fixture.seller.id, draft.id);
  CHECK(pending.status == ItemStatus::kPending);
  // 重复提交返回原结果，不产生第二条记录。
  Item again = fixture.item_service.SubmitForReview(fixture.seller.id, draft.id);
  CHECK(again.id == draft.id);
  CHECK(again.status == ItemStatus::kPending);

  CHECK_THROWS(fixture.item_service.Reject(fixture.admin.id, draft.id, ""),
               ErrorCode::kInvalidArgument);
  Item rejected = fixture.item_service.Reject(fixture.admin.id, draft.id, "图片不清晰");
  CHECK(rejected.status == ItemStatus::kDraft);
  CHECK(rejected.reject_reason == "图片不清晰");

  fixture.item_service.SubmitForReview(fixture.seller.id, draft.id);
  Item published = fixture.item_service.Approve(fixture.admin.id, draft.id);
  CHECK(published.status == ItemStatus::kPublished);

  // 改价后重新进入待审核。
  Item repriced = fixture.item_service.ChangePrice(fixture.seller.id, draft.id, 58000);
  CHECK(repriced.status == ItemStatus::kPending);
  CHECK(repriced.price == 58000);
}

void TestPermissions() {
  Fixture fixture;
  Item item = fixture.item_service.CreateDraft(fixture.seller.id,
                                               fixture.category_id, "台灯",
                                               "护眼", 3000);
  // 买家没有卖家角色，不能发布商品。
  CHECK_THROWS(fixture.item_service.CreateDraft(fixture.buyer.id,
                                                fixture.category_id, "台灯",
                                                "护眼", 3000),
               ErrorCode::kPermissionDenied);
  // 非本人不能修改。
  CHECK_THROWS(fixture.item_service.SubmitForReview(fixture.buyer.id, item.id),
               ErrorCode::kPermissionDenied);
  // 非管理员不能审核。
  CHECK_THROWS(fixture.item_service.Approve(fixture.seller.id, item.id),
               ErrorCode::kPermissionDenied);
}

void TestOfflineVoidsReservation() {
  Fixture fixture;
  std::string item_id = fixture.PublishItem();
  Reservation reservation =
      fixture.reservation_service.Reserve(fixture.buyer.id, item_id, "REQ-1");
  CHECK(reservation.status == ReservationStatus::kPending);
  fixture.item_service.TakeOffline(fixture.seller.id, item_id, "不卖了");
  auto item = fixture.items.FindById(item_id);
  CHECK(item && item->status == ItemStatus::kOffline);
  auto updated = fixture.reservations.FindById(reservation.id);
  CHECK(updated && updated->status == ReservationStatus::kVoided);
  auto notes = fixture.notifications.FindByUser(fixture.buyer.id);
  CHECK(!notes.empty());
}

void TestReservationFlow() {
  Fixture fixture;
  std::string item_id = fixture.PublishItem();
  Reservation reservation =
      fixture.reservation_service.Reserve(fixture.buyer.id, item_id, "REQ-A");
  auto item = fixture.items.FindById(item_id);
  CHECK(item && item->status == ItemStatus::kReserved);

  // 重复请求号返回原预约。
  Reservation duplicate =
      fixture.reservation_service.Reserve(fixture.buyer.id, item_id, "REQ-A");
  CHECK(duplicate.id == reservation.id);
  // 其他买家再次预约冲突。
  CHECK_THROWS(
      fixture.reservation_service.Reserve(fixture.buyer2.id, item_id, "REQ-B"),
      ErrorCode::kConflict);
  // 卖家不能预约自己的商品。
  CHECK_THROWS(
      fixture.reservation_service.Reserve(fixture.seller.id, item_id, "REQ-C"),
      ErrorCode::kConflict);

  // 会话层：确认后买家不能取消。
  CHECK_THROWS(fixture.reservation_service.Cancel(fixture.seller.id,
                                                  reservation.id),
               ErrorCode::kPermissionDenied);
  Reservation confirmed =
      fixture.reservation_service.Confirm(fixture.seller.id, reservation.id);
  CHECK(confirmed.status == ReservationStatus::kConfirmed);
  CHECK_THROWS(
      fixture.reservation_service.Cancel(fixture.buyer.id, reservation.id),
      ErrorCode::kStateError);
  Reservation completed =
      fixture.reservation_service.Complete(fixture.buyer.id, reservation.id);
  CHECK(completed.status == ReservationStatus::kCompleted);
  auto final_item = fixture.items.FindById(item_id);
  CHECK(final_item && final_item->status == ItemStatus::kCompleted);
}

void TestCancelReservation() {
  Fixture fixture;
  std::string item_id = fixture.PublishItem();
  Reservation reservation =
      fixture.reservation_service.Reserve(fixture.buyer.id, item_id, "REQ-D");
  fixture.reservation_service.Cancel(fixture.buyer.id, reservation.id);
  auto item = fixture.items.FindById(item_id);
  CHECK(item && item->status == ItemStatus::kPublished);
  auto canceled = fixture.reservations.FindById(reservation.id);
  CHECK(canceled && canceled->status == ReservationStatus::kCanceled);
  // 取消后可以再次被别人预约。
  Reservation next =
      fixture.reservation_service.Reserve(fixture.buyer2.id, item_id, "REQ-E");
  CHECK(next.status == ReservationStatus::kPending);
}

void TestConcurrentReservation() {
  Fixture fixture;
  std::string item_id = fixture.PublishItem();
  std::atomic<int> success{0};
  std::atomic<int> conflict{0};
  std::vector<std::thread> threads;
  for (int i = 0; i < 8; ++i) {
    threads.emplace_back([&, i]() {
      try {
        fixture.reservation_service.Reserve(fixture.buyer.id, item_id,
                                            "REQ-" + std::to_string(i));
        success.fetch_add(1);
      } catch (const DomainError&) {
        conflict.fetch_add(1);
      }
    });
  }
  for (auto& thread : threads) {
    thread.join();
  }
  CHECK(success.load() == 1);
  CHECK(conflict.load() == 7);
  int active = 0;
  for (const auto& reservation : fixture.reservations.All()) {
    if (IsActiveReservation(reservation.status)) {
      ++active;
    }
  }
  CHECK(active == 1);
}

void TestSearch() {
  Fixture fixture;
  fixture.PublishItem("高等数学 第七版", 2500);
  fixture.PublishItem("线性代数", 1800);
  fixture.PublishItem("显示器", 62000);

  SearchQuery query;
  query.keyword = "数学";
  SearchResult result = fixture.search_service.Search(query);
  CHECK(result.total == 1);
  CHECK(result.items.size() == 1);

  SearchQuery by_price;
  by_price.min_price = 2000;
  by_price.max_price = 70000;
  by_price.sort = SearchQuery::Sort::kPriceAsc;
  result = fixture.search_service.Search(by_price);
  CHECK(result.total == 2);
  CHECK(result.items[0].price <= result.items[1].price);

  SearchQuery paged;
  paged.page = 2;
  paged.page_size = 2;
  result = fixture.search_service.Search(paged);
  CHECK(result.total == 3);
  CHECK(result.total_pages == 2);
  CHECK(result.items.size() == 1);

  SearchQuery invalid;
  invalid.page_size = 0;
  CHECK_THROWS(fixture.search_service.Search(invalid),
               ErrorCode::kInvalidArgument);
}

void TestReportFlow() {
  Fixture fixture;
  std::string item_id = fixture.PublishItem();
  Reservation reservation =
      fixture.reservation_service.Reserve(fixture.buyer.id, item_id, "REQ-R");
  Report report = fixture.report_service.Submit(fixture.buyer2.id, item_id,
                                                ReportReason::kMisleading,
                                                "描述与实物不符");
  CHECK(report.status == ReportStatus::kPending);
  // 24 小时内重复举报返回原记录。
  Report duplicate = fixture.report_service.Submit(
      fixture.buyer2.id, item_id, ReportReason::kMisleading, "再举报一次");
  CHECK(duplicate.id == report.id);

  Report handled =
      fixture.report_service.Handle(fixture.admin.id, report.id, true, "属实");
  CHECK(handled.status == ReportStatus::kUpheld);
  auto item = fixture.items.FindById(item_id);
  CHECK(item && item->status == ItemStatus::kOffline);
  auto reservation_after = fixture.reservations.FindById(reservation.id);
  CHECK(reservation_after &&
        reservation_after->status == ReservationStatus::kVoided);
  // 同一举报不能重复处理。
  CHECK_THROWS(fixture.report_service.Handle(fixture.admin.id, report.id, true,
                                             "again"),
               ErrorCode::kStateError);
}

void TestReportRejected() {
  Fixture fixture;
  std::string item_id = fixture.PublishItem();
  Report report = fixture.report_service.Submit(
      fixture.buyer.id, item_id, ReportReason::kOther, "看着不顺眼");
  fixture.report_service.Handle(fixture.admin.id, report.id, false, "证据不足");
  auto item = fixture.items.FindById(item_id);
  CHECK(item && item->status == ItemStatus::kPublished);
}

void TestLogAndStats() {
  Fixture fixture;
  std::string item_id = fixture.PublishItem();
  fixture.reservation_service.Reserve(fixture.buyer.id, item_id, "REQ-S");
  fixture.reservation_service.Cancel(fixture.buyer.id,
                                     fixture.reservations.All().back().id);

  LogFilter filter;
  filter.target_type = "商品";
  auto logs = fixture.log_service.Query(filter);
  CHECK(!logs.empty());

  StatsReport stats =
      fixture.stats_service.Overview(fixture.clock.Now() - 3600,
                                     fixture.clock.Now() + 3600);
  CHECK(stats.new_items == 1);
  // 取消的预约不计入预约成功。
  CHECK(stats.successful_reservations == 0);
  CHECK(stats.items_by_status[ItemStatus::kPublished] == 1);

  SellerStats seller_stats = fixture.stats_service.ForSeller(fixture.seller.id);
  CHECK(seller_stats.total_views == 0);
  CHECK(seller_stats.total_reservations == 1);
}

void TestPersistence() {
  namespace fs = std::filesystem;
  fs::path dir = fs::temp_directory_path() / "campus_persistence_test";
  fs::remove_all(dir);
  fs::create_directories(dir);
  std::string base = dir.string();

  {
    FileUserRepository users(base + "/users.db");
    FileCategoryRepository categories(base + "/categories.db");
    FileItemRepository items(base + "/items.db");
    FileReservationRepository reservations(base + "/reservations.db");
    FileReportRepository reports(base + "/reports.db");
    FileLogRepository logs(base + "/logs.db");
    FileNotificationRepository notifications(base + "/notifications.db");
    FakeClock clock;
    FnvPasswordHasher hasher;
    SequenceIdGenerator ids(clock);
    UserService user_service(users, hasher, clock, ids);
    ItemService item_service(users, items, categories, reservations,
                             notifications, logs, clock, ids);
    Category category;
    category.id = "CT-1";
    category.name = "教材书籍";
    categories.Add(category);
    User admin = user_service.CreateAdmin("a@campus.edu", "13800000001",
                                          "Admin@123");
    User seller = user_service.Register("s@campus.edu", "13800000002",
                                        "Seller@123", true);
    Item item = item_service.CreateDraft(seller.id, category.id, "教材", "描述",
                                         1000);
    item_service.SubmitForReview(seller.id, item.id);
    item_service.Approve(admin.id, item.id);
    users.Save();
    categories.Save();
    items.Save();
    reservations.Save();
    reports.Save();
    logs.Save();
    notifications.Save();
  }

  {
    FileUserRepository users(base + "/users.db");
    FileItemRepository items(base + "/items.db");
    users.Load();
    items.Load();
    CHECK(users.All().size() == 2);
    CHECK(items.All().size() == 1);
    CHECK(items.All()[0].status == ItemStatus::kPublished);
  }
  fs::remove_all(dir);
}

void TestValidation() {
  Fixture fixture;
  CHECK_THROWS(fixture.item_service.CreateDraft(fixture.seller.id, "CT-X",
                                                "标题", "描述", 100),
               ErrorCode::kInvalidArgument);
  CHECK_THROWS(fixture.item_service.CreateDraft(fixture.seller.id,
                                                fixture.category_id, "标", "描述",
                                                100),
               ErrorCode::kInvalidArgument);
  CHECK_THROWS(fixture.item_service.CreateDraft(fixture.seller.id,
                                                fixture.category_id, "标题", "描述",
                                                0),
               ErrorCode::kInvalidArgument);
  CHECK_THROWS(fixture.item_service.CreateDraft(fixture.seller.id,
                                                fixture.category_id, "标题", "描述",
                                                kMaxPrice + 1),
               ErrorCode::kInvalidArgument);
}

struct TestCase {
  const char* name;
  void (*function)();
};

}  // namespace

int main() {
  std::vector<TestCase> tests = {
      {"state machine", TestStateMachine},
      {"formatting and escaping", TestFormatting},
      {"registration validation", TestRegister},
      {"login and lockout", TestLoginAndLockout},
      {"item lifecycle", TestItemLifecycle},
      {"permission control", TestPermissions},
      {"offline voids reservation", TestOfflineVoidsReservation},
      {"reservation flow", TestReservationFlow},
      {"cancel reservation", TestCancelReservation},
      {"concurrent reservation uniqueness", TestConcurrentReservation},
      {"combined search", TestSearch},
      {"report handling", TestReportFlow},
      {"report rejected", TestReportRejected},
      {"logs and statistics", TestLogAndStats},
      {"persistence", TestPersistence},
      {"input validation", TestValidation},
  };

  for (const auto& test : tests) {
    std::cout << "[ RUN      ] " << test.name << "\n";
    int before = g_failures;
    test.function();
    std::cout << (g_failures == before ? "[       OK ] " : "[  FAILED  ] ")
              << test.name << "\n";
  }
  std::cout << "\n" << tests.size() << " test groups, " << g_checks
            << " assertions, " << g_failures << " failure(s)\n";
  return g_failures == 0 ? 0 : 1;
}
