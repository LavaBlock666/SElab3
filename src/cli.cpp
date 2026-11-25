// Copyright 2026 兰雨阳. All rights reserved.
// 控制台交互界面：游客、买家、卖家、管理员四类工作台。
//
// 界面层只负责输入输出与流程组织，业务规则全部在应用服务中；
// 每个操作都捕获 DomainError 并转成可读提示，避免程序因一次误操作退出。

#include "campus/app.h"

#include <cstdlib>
#include <functional>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

#include "campus/services.h"
#include "campus/store.h"

namespace campus {
namespace {

const char* kLine = "------------------------------------------------------------";

std::string ReadLine(const std::string& prompt) {
  std::cout << prompt;
  std::string line;
  if (!std::getline(std::cin, line)) {
    return "";
  }
  return Trim(line);
}

int ReadInt(const std::string& prompt, int min_value, int max_value) {
  std::string text = ReadLine(prompt);
  if (text.empty()) {
    return -1;
  }
  try {
    int value = std::stoi(text);
    if (value < min_value || value > max_value) {
      std::cout << "输入超出范围。\n";
      return -1;
    }
    return value;
  } catch (...) {
    std::cout << "请输入数字。\n";
    return -1;
  }
}

bool Confirm(const std::string& prompt) {
  std::string text = ToLower(ReadLine(prompt + " (y/n): "));
  return text == "y" || text == "yes" || text == "是";
}

class ConsoleUI {
 public:
  ConsoleUI(const std::string& data_dir, bool seed_demo)
      : paths_(MakeDatabasePaths(data_dir)),
        categories_(paths_.categories),
        users_(paths_.users),
        items_(paths_.items),
        reservations_(paths_.reservations),
        reports_(paths_.reports),
        logs_(paths_.logs),
        notifications_(paths_.notifications),
        ids_(clock_),
        user_service_(users_, hasher_, clock_, ids_),
        item_service_(users_, items_, categories_, reservations_, notifications_,
                      logs_, clock_, ids_),
        search_service_(items_, categories_),
        reservation_service_(users_, items_, reservations_, notifications_, logs_,
                             clock_, ids_),
        report_service_(users_, items_, reservations_, reports_, notifications_,
                        logs_, clock_, ids_),
        log_service_(logs_, clock_, ids_),
        stats_service_(users_, items_, categories_, reservations_, reports_),
        seed_demo_(seed_demo) {}

  int Run() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    LoadAll();
    if (seed_demo_) {
      SeedIfEmpty();
    }
    PrintWelcome();
    while (running_) {
      if (!current_) {
        GuestMenu();
      } else {
        UserMenu();
      }
    }
    SaveAll();
    std::cout << "数据已保存，再见。\n";
    return 0;
  }

 private:
  // ---------- 生命周期 ----------

  void LoadAll() {
    categories_.Load();
    users_.Load();
    items_.Load();
    reservations_.Load();
    reports_.Load();
    logs_.Load();
    notifications_.Load();
  }

  void SaveAll() {
    categories_.Save();
    users_.Save();
    items_.Save();
    reservations_.Save();
    reports_.Save();
    logs_.Save();
    notifications_.Save();
  }

  void SeedIfEmpty() {
    if (!categories_.All().empty()) {
      return;
    }
    std::vector<std::string> names = {"教材书籍", "数码电子", "生活用品",
                                      "运动户外", "其他"};
    for (std::size_t i = 0; i < names.size(); ++i) {
      Category category;
      category.id = "CT-" + std::to_string(i + 1);
      category.name = names[i];
      categories_.Add(category);
    }
    categories_.Save();

    if (!users_.All().empty()) {
      return;
    }
    User admin;
    User seller;
    User buyer;
    try {
      admin = user_service_.CreateAdmin("admin@campus.edu", "13800000001",
                                        "Admin@123");
      seller = user_service_.Register("seller@campus.edu", "13800000002",
                                      "Seller@123", true);
      buyer = user_service_.Register("buyer@campus.edu", "13800000003",
                                     "Buyer@123", false);
      user_service_.Register("buyer2@campus.edu", "13800000004", "Buyer@123",
                             false);
    } catch (const DomainError& error) {
      std::cout << "初始化演示账号失败：" << error.what() << "\n";
      return;
    }

    auto all_categories = categories_.All();
    struct DemoItem {
      std::string category_id;
      std::string title;
      std::string description;
      Money price;
    };
    std::vector<DemoItem> demo = {
        {all_categories[0].id, "高等数学 第七版 上下册", "考研用过，笔记齐全。", 2500},
        {all_categories[1].id, "27 英寸 2K 显示器", "自用两年，无坏点。", 62000},
        {all_categories[3].id, "捷安特山地自行车", "骑行约 300 公里，保养良好。", 48000},
        {all_categories[2].id, "宿舍收纳箱 三件套", "搬宿舍剩下的，全新。", 3500}};
    for (const auto& entry : demo) {
      try {
        Item item = item_service_.CreateDraft(seller.id, entry.category_id,
                                              entry.title, entry.description,
                                              entry.price);
        item_service_.SubmitForReview(seller.id, item.id);
        item_service_.Approve(admin.id, item.id);
      } catch (const DomainError& error) {
        std::cout << "初始化演示商品失败：" << error.what() << "\n";
      }
    }
    (void)buyer;
    SaveAll();
  }

  // ---------- 展示 ----------

  void PrintWelcome() const {
    std::cout << "\n"
              << kLine << "\n"
              << "  校园二手交易系统（C++ 课程实现）\n"
              << "  数据目录：" << paths_.users.substr(0, paths_.users.rfind('/'))
              << "\n"
              << kLine << "\n";
    std::cout << "演示账号：admin@campus.edu / Admin@123（管理员）\n"
              << "          seller@campus.edu / Seller@123（卖家）\n"
              << "          buyer@campus.edu / Buyer@123（买家）\n"
              << kLine << "\n";
  }

  void PrintCategories() const {
    std::cout << "分类：";
    for (const auto& category : categories_.All()) {
      std::cout << "[" << category.id << "] " << category.name << "  ";
    }
    std::cout << "\n";
  }

  std::string UserLabel(const std::string& user_id) const {
    auto user = users_.FindById(user_id);
    return user ? user->email : user_id;
  }

  void PrintItemRow(int index, const Item& item) const {
    auto category = categories_.FindById(item.category_id);
    std::cout << "  " << index << ") " << item.title << "  ¥"
              << MoneyToString(item.price) << "  "
              << ItemStatusLabel(item.status) << "  "
              << (category ? category->name : "未分类") << "  浏览 "
              << item.view_count << "\n";
    std::cout << "     编号 " << item.id << "  卖家 " << UserLabel(item.seller_id)
              << "\n";
  }

  void PrintItemTable(const std::vector<Item>& list) const {
    if (list.empty()) {
      std::cout << "  （暂无商品）\n";
      return;
    }
    for (std::size_t i = 0; i < list.size(); ++i) {
      PrintItemRow(static_cast<int>(i + 1), list[i]);
    }
  }

  void PrintItemDetail(const Item& item) const {
    auto category = categories_.FindById(item.category_id);
    std::cout << "\n" << kLine << "\n"
              << "标题：" << item.title << "\n"
              << "价格：¥" << MoneyToString(item.price) << "\n"
              << "分类：" << (category ? category->name : "未分类") << "\n"
              << "状态：" << ItemStatusLabel(item.status) << "\n"
              << "卖家：" << UserLabel(item.seller_id) << "\n"
              << "浏览：" << item.view_count << " 次\n"
              << "发布：" << FormatTime(item.created_at) << "\n"
              << "描述：" << item.description << "\n";
    if (!item.reject_reason.empty()) {
      std::cout << "驳回理由：" << item.reject_reason << "\n";
    }
    std::cout << kLine << "\n";
  }

  void Pause() const { ReadLine("\n按回车继续..."); }

  // ---------- 菜单 ----------

  void GuestMenu() {
    std::cout << "\n=== 游客 ===\n"
              << "1) 浏览 / 检索商品\n"
              << "2) 登录\n"
              << "3) 注册\n"
              << "0) 退出\n";
    int choice = ReadInt("请选择：", 0, 3);
    if (choice <= 0) {
      running_ = false;
      return;
    }
    Guard([&] {
      switch (choice) {
        case 1:
          SearchFlow();
          break;
        case 2:
          LoginFlow();
          break;
        case 3:
          RegisterFlow();
          break;
        default:
          break;
      }
    });
  }

  void UserMenu() {
    std::cout << "\n=== " << current_->email << "（";
    bool first = true;
    for (Role role : current_->roles) {
      if (!first) {
        std::cout << " / ";
      }
      std::cout << RoleLabel(role);
      first = false;
    }
    std::cout << "）===\n";

    std::vector<std::pair<std::string, std::function<void()>>> actions;
    if (current_->HasRole(Role::kBuyer)) {
      actions.push_back({"检索商品", [this] { SearchFlow(); }});
      actions.push_back({"我的预约", [this] { MyReservations(); }});
    }
    if (current_->HasRole(Role::kSeller)) {
      actions.push_back({"发布商品", [this] { CreateItemFlow(); }});
      actions.push_back({"我的商品", [this] { SellerProducts(); }});
      actions.push_back({"处理预约", [this] { SellerReservations(); }});
      actions.push_back({"我的数据", [this] { SellerStats(); }});
    }
    if (current_->HasRole(Role::kAdmin)) {
      actions.push_back({"商品审核", [this] { AdminReview(); }});
      actions.push_back({"举报处理", [this] { AdminReports(); }});
      actions.push_back({"操作日志", [this] { AdminLogs(); }});
      actions.push_back({"统计报表", [this] { AdminStats(); }});
    }
    actions.push_back({"我的通知", [this] { Notifications(); }});
    actions.push_back({"退出登录", [this] { LogoutFlow(); }});

    for (std::size_t i = 0; i < actions.size(); ++i) {
      std::cout << (i + 1) << ") " << actions[i].first << "\n";
    }
    std::cout << "0) 退出程序\n";
    int choice = ReadInt("请选择：", 0, static_cast<int>(actions.size()));
    if (choice == 0) {
      running_ = false;
      return;
    }
    Guard([&] {
      actions[choice - 1].second();
      SaveAll();
    });
  }

  template <typename Function>
  void Guard(Function&& function) {
    try {
      function();
    } catch (const DomainError& error) {
      std::cout << "错误：" << error.what() << "\n";
    } catch (const std::exception& error) {
      std::cout << "异常：" << error.what() << "\n";
    }
  }

  // ---------- 账号 ----------

  void LoginFlow() {
    std::string account = ReadLine("邮箱或手机号：");
    std::string password = ReadLine("密码：");
    User user = user_service_.Login(account, password);
    current_ = user;
    std::cout << "登录成功，欢迎 " << user.email << "\n";
  }

  void RegisterFlow() {
    std::string email = ReadLine("邮箱：");
    std::string phone = ReadLine("手机号：");
    std::string password = ReadLine("密码（至少 8 位，含字母和数字）：");
    bool as_seller = Confirm("是否同时开通卖家身份");
    User user = user_service_.Register(email, phone, password, as_seller);
    std::cout << "注册成功，账号编号 " << user.id << "，请登录。\n";
  }

  void LogoutFlow() {
    current_.reset();
    std::cout << "已退出登录。\n";
  }

  // ---------- 检索与详情 ----------

  void SearchFlow() {
    SearchQuery query;
    query.keyword = ReadLine("关键字（可空）：");
    PrintCategories();
    query.category_id = ReadLine("分类编号（可空）：");
    std::string min_text = ReadLine("最低价（可空）：");
    std::string max_text = ReadLine("最高价（可空）：");
    if (!min_text.empty()) {
      query.min_price = ParseMoney(min_text);
    }
    if (!max_text.empty()) {
      query.max_price = ParseMoney(max_text);
    }
    std::cout << "排序：1) 最新 2) 价格升序 3) 价格降序\n";
    int sort = ReadInt("请选择（默认 1）：", 0, 3);
    query.sort = sort == 2   ? SearchQuery::Sort::kPriceAsc
                 : sort == 3 ? SearchQuery::Sort::kPriceDesc
                             : SearchQuery::Sort::kLatest;
    query.page = 1;
    query.page_size = 5;

    while (true) {
      SearchResult result = search_service_.Search(query);
      std::cout << "\n共 " << result.total << " 件商品，第 " << result.page << "/"
                << (result.total_pages == 0 ? 1 : result.total_pages) << " 页\n";
      PrintItemTable(result.items);
      std::string action =
          ReadLine("[编号]查看详情  n)下一页  p)上一页  0)返回：");
      if (action == "0" || action.empty()) {
        return;
      }
      if (action == "n") {
        if (query.page < result.total_pages) {
          ++query.page;
        }
        continue;
      }
      if (action == "p") {
        if (query.page > 1) {
          --query.page;
        }
        continue;
      }
      int index = -1;
      try {
        index = std::stoi(action);
      } catch (...) {
        std::cout << "无效输入。\n";
        continue;
      }
      if (index >= 1 && index <= static_cast<int>(result.items.size())) {
        DetailFlow(result.items[index - 1].id);
      }
    }
  }

  void DetailFlow(const std::string& item_id) {
    Item item = search_service_.ViewDetail(item_id);
    PrintItemDetail(item);
    if (!current_ || item.seller_id == current_->id) {
      return;
    }
    bool can_reserve =
        current_->HasRole(Role::kBuyer) && item.status == ItemStatus::kPublished;
    std::cout << (can_reserve ? "1) 预约该商品  " : "")
              << "2) 举报该商品  0) 返回\n";
    int action = ReadInt("请选择：", 0, 2);
    if (action == 1 && can_reserve) {
      std::string request_id = ids_.Next("RQ");
      Reservation reservation =
          reservation_service_.Reserve(current_->id, item.id, request_id);
      std::cout << "预约成功，预约编号 " << reservation.id
                << "，等待卖家确认。\n";
      return;
    }
    if (action == 2) {
      std::cout << "举报原因：1) 虚假描述 2) 重复发布 3) 违禁物品 "
                   "4) 联系方式 5) 其他\n";
      int reason = ReadInt("请选择原因：", 1, 5);
      if (reason < 1) {
        return;
      }
      static const ReportReason kReasons[] = {
          ReportReason::kMisleading, ReportReason::kDuplicate,
          ReportReason::kProhibited, ReportReason::kContact,
          ReportReason::kOther};
      std::string description = ReadLine("举报说明：");
      Report report = report_service_.Submit(current_->id, item.id,
                                             kReasons[reason - 1], description);
      std::cout << "举报已提交，编号 " << report.id << "，等待管理员处理。\n";
    }
  }

  // ---------- 买家 ----------

  void MyReservations() {
    std::vector<Reservation> list =
        reservation_service_.ListByBuyer(current_->id);
    if (list.empty()) {
      std::cout << "你还没有预约记录。\n";
      return;
    }
    for (std::size_t i = 0; i < list.size(); ++i) {
      auto item = items_.FindById(list[i].item_id);
      std::cout << "  " << (i + 1) << ") " << list[i].id << "  "
                << (item ? item->title : list[i].item_id) << "  "
                << ReservationStatusLabel(list[i].status) << "  "
                << FormatTime(list[i].created_at) << "\n";
    }
    int index = ReadInt("选择预约（0 返回）：", 0, static_cast<int>(list.size()));
    if (index <= 0) {
      return;
    }
    const Reservation& reservation = list[index - 1];
    std::cout << "1) 取消预约  2) 确认完成  0) 返回\n";
    int action = ReadInt("请选择：", 0, 2);
    if (action == 1) {
      reservation_service_.Cancel(current_->id, reservation.id);
      std::cout << "预约已取消。\n";
    } else if (action == 2) {
      reservation_service_.Complete(current_->id, reservation.id);
      std::cout << "已确认完成，交易结束。\n";
    }
  }

  // ---------- 卖家 ----------

  void CreateItemFlow() {
    PrintCategories();
    std::string category_id = ReadLine("分类编号：");
    std::string title = ReadLine("标题：");
    std::string description = ReadLine("描述：");
    std::string price_text = ReadLine("价格（元）：");
    auto price = ParseMoney(price_text);
    if (!price) {
      std::cout << "价格格式不正确。\n";
      return;
    }
    Item item = item_service_.CreateDraft(current_->id, category_id, title,
                                          description, *price);
    std::cout << "草稿已保存，编号 " << item.id << "\n";
    if (Confirm("是否立即提交审核")) {
      item = item_service_.SubmitForReview(current_->id, item.id);
      std::cout << "已提交审核，当前状态：" << ItemStatusLabel(item.status)
                << "\n";
    }
  }

  void SellerProducts() {
    std::vector<Item> list = item_service_.ListBySeller(current_->id);
    if (list.empty()) {
      std::cout << "你还没有发布商品。\n";
      return;
    }
    PrintItemTable(list);
    int index = ReadInt("选择商品（0 返回）：", 0, static_cast<int>(list.size()));
    if (index <= 0) {
      return;
    }
    Item item = list[index - 1];
    PrintItemDetail(item);
    std::cout << "1) 修改描述  2) 修改价格  3) 下架  4) 提交审核  0) 返回\n";
    int action = ReadInt("请选择：", 0, 4);
    switch (action) {
      case 1: {
        std::string title = ReadLine("新标题（回车保持不变）：");
        std::string description = ReadLine("新描述（回车保持不变）：");
        Item updated = item_service_.UpdatePublished(
            current_->id, item.id, title.empty() ? item.title : title,
            description.empty() ? item.description : description);
        std::cout << "已更新：" << updated.title << "\n";
        break;
      }
      case 2: {
        std::string price_text = ReadLine("新价格（元）：");
        auto price = ParseMoney(price_text);
        if (!price) {
          std::cout << "价格格式不正确。\n";
          return;
        }
        Item updated = item_service_.ChangePrice(current_->id, item.id, *price);
        std::cout << "已改价，当前状态：" << ItemStatusLabel(updated.status)
                  << "\n";
        break;
      }
      case 3: {
        std::string reason = ReadLine("下架原因（可空）：");
        item_service_.TakeOffline(current_->id, item.id, reason);
        std::cout << "商品已下架。\n";
        break;
      }
      case 4: {
        Item submitted = item_service_.SubmitForReview(current_->id, item.id);
        std::cout << "已提交，当前状态：" << ItemStatusLabel(submitted.status)
                  << "\n";
        break;
      }
      default:
        break;
    }
  }

  void SellerReservations() {
    std::vector<Item> my_items = item_service_.ListBySeller(current_->id);
    std::vector<Reservation> pending;
    for (const auto& item : my_items) {
      for (const auto& reservation : reservation_service_.ListByItem(item.id)) {
        if (reservation.status == ReservationStatus::kPending) {
          pending.push_back(reservation);
        }
      }
    }
    if (pending.empty()) {
      std::cout << "没有待确认的预约。\n";
      return;
    }
    for (std::size_t i = 0; i < pending.size(); ++i) {
      auto item = items_.FindById(pending[i].item_id);
      std::cout << "  " << (i + 1) << ") " << pending[i].id << "  "
                << (item ? item->title : "") << "  买家 "
                << UserLabel(pending[i].buyer_id) << "\n";
    }
    int index = ReadInt("选择预约（0 返回）：", 0,
                        static_cast<int>(pending.size()));
    if (index <= 0) {
      return;
    }
    reservation_service_.Confirm(current_->id, pending[index - 1].id);
    std::cout << "已确认交易，等待线下交付。\n";
  }

  void SellerStats() {
    campus::SellerStats stats = stats_service_.ForSeller(current_->id);
    std::cout << "\n在售 " << stats.published << " 件，待审核 " << stats.pending
              << " 件，预约中 " << stats.reserved << " 件，已完成 "
              << stats.completed << " 件\n"
              << "总浏览 " << stats.total_views << " 次，总预约 "
              << stats.total_reservations << " 次\n";
  }

  // ---------- 管理员 ----------

  void AdminReview() {
    std::vector<Item> pending = item_service_.ListByStatus(ItemStatus::kPending);
    if (pending.empty()) {
      std::cout << "没有待审核商品。\n";
      return;
    }
    PrintItemTable(pending);
    int index =
        ReadInt("选择商品（0 返回）：", 0, static_cast<int>(pending.size()));
    if (index <= 0) {
      return;
    }
    Item item = pending[index - 1];
    PrintItemDetail(item);
    std::cout << "1) 通过  2) 驳回  0) 返回\n";
    int action = ReadInt("请选择：", 0, 2);
    if (action == 1) {
      item_service_.Approve(current_->id, item.id);
      std::cout << "已通过并发布。\n";
    } else if (action == 2) {
      std::string reason = ReadLine("驳回理由（必填）：");
      item_service_.Reject(current_->id, item.id, reason);
      std::cout << "已驳回并退回草稿。\n";
    }
  }

  void AdminReports() {
    std::vector<Report> pending = report_service_.ListPending();
    if (pending.empty()) {
      std::cout << "没有待处理举报。\n";
      return;
    }
    for (std::size_t i = 0; i < pending.size(); ++i) {
      auto item = items_.FindById(pending[i].item_id);
      std::cout << "  " << (i + 1) << ") " << pending[i].id << "  "
                << ReportReasonLabel(pending[i].reason) << "  "
                << (item ? item->title : "") << "\n";
    }
    int index =
        ReadInt("选择举报（0 返回）：", 0, static_cast<int>(pending.size()));
    if (index <= 0) {
      return;
    }
    const Report& report = pending[index - 1];
    std::cout << "举报人：" << UserLabel(report.reporter_id)
              << "\n说明：" << report.description << "\n"
              << "该商品历史举报 " << report_service_.ListByItem(report.item_id).size()
              << " 条\n";
    std::cout << "1) 判定成立（下架商品）  2) 判定不成立  0) 返回\n";
    int action = ReadInt("请选择：", 0, 2);
    if (action == 1 || action == 2) {
      std::string note = ReadLine("处理备注（可空）：");
      report_service_.Handle(current_->id, report.id, action == 1, note);
      std::cout << "举报已处理。\n";
    }
  }

  void AdminLogs() {
    LogFilter filter;
    std::string target = ReadLine("对象类型（商品/预约/举报，可空）：");
    if (!target.empty()) {
      filter.target_type = target;
    }
    filter.limit = 30;
    std::vector<OperationLog> list = log_service_.Query(filter);
    if (list.empty()) {
      std::cout << "没有匹配的日志。\n";
      return;
    }
    for (const auto& log : list) {
      std::cout << "  " << FormatTime(log.created_at) << "  "
                << UserLabel(log.operator_id) << "  " << log.action << "  "
                << log.target_type << "/" << log.target_id;
      if (!log.detail.empty()) {
        std::cout << "  " << log.detail;
      }
      std::cout << "\n";
    }
    std::cout << "共 " << list.size() << " 条（最多显示 30 条）\n";
  }

  void AdminStats() {
    std::string from_text = ReadLine("开始日期（YYYY-MM-DD，可空）：");
    std::string to_text = ReadLine("结束日期（YYYY-MM-DD，可空）：");
    Timestamp now = clock_.Now();
    Timestamp from = now - 7 * 24 * 3600;
    Timestamp to = now;
    if (!from_text.empty()) {
      auto parsed = ParseTime(from_text + " 00:00:00");
      if (!parsed) {
        std::cout << "日期格式不正确。\n";
        return;
      }
      from = *parsed;
    }
    if (!to_text.empty()) {
      auto parsed = ParseTime(to_text + " 23:59:59");
      if (!parsed) {
        std::cout << "日期格式不正确。\n";
        return;
      }
      to = *parsed;
    }
    StatsReport report = stats_service_.Overview(from, to);
    std::cout << "\n统计区间：" << FormatTime(from) << " ~ " << FormatTime(to)
              << "\n新增商品 " << report.new_items << "，预约成功 "
              << report.successful_reservations << "，待处理举报 "
              << report.pending_reports << "，活跃用户 " << report.active_users
              << "\n分类商品数：";
    for (const auto& entry : report.items_by_category) {
      std::cout << entry.first << "=" << entry.second << "  ";
    }
    std::cout << "\n各状态数量：";
    for (const auto& entry : report.items_by_status) {
      std::cout << ItemStatusLabel(entry.first) << "=" << entry.second << "  ";
    }
    std::cout << "\n";
  }

  // ---------- 通知 ----------

  void Notifications() {
    std::vector<Notification> list =
        notifications_.FindByUser(current_->id);
    if (list.empty()) {
      std::cout << "暂无通知。\n";
      return;
    }
    for (const auto& notification : list) {
      std::cout << "  [" << (notification.read ? "已读" : "未读") << "] "
                << FormatTime(notification.created_at) << "  "
                << notification.type << "：" << notification.content << "\n";
    }
    if (Confirm("是否全部标记为已读")) {
      for (const auto& notification : list) {
        notifications_.MarkRead(notification.id);
      }
      notifications_.Save();
      std::cout << "已全部标记为已读。\n";
    }
  }

  // ---------- 成员 ----------

  DatabasePaths paths_;
  FileCategoryRepository categories_;
  FileUserRepository users_;
  FileItemRepository items_;
  FileReservationRepository reservations_;
  FileReportRepository reports_;
  FileLogRepository logs_;
  FileNotificationRepository notifications_;
  SystemClock clock_;
  FnvPasswordHasher hasher_;
  SequenceIdGenerator ids_;

  UserService user_service_;
  ItemService item_service_;
  SearchService search_service_;
  ReservationService reservation_service_;
  ReportService report_service_;
  LogService log_service_;
  StatsService stats_service_;

  std::optional<User> current_;
  bool running_ = true;
  bool seed_demo_ = true;
};

}  // namespace

int RunApplication(const std::string& data_dir, bool seed_demo) {
  ConsoleUI ui(data_dir, seed_demo);
  return ui.Run();
}

}  // namespace campus
