// Copyright 2026 兰雨阳. All rights reserved.
// Console front-end. All user-facing text is English so the output is safe in
// terminals with non-UTF8 code pages. Business rules stay in the services layer.

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

#include <filesystem>

#include "campus/demo_seed.h"
#include "campus/instance_lock.h"
#include "campus/services.h"
#include "campus/store.h"

namespace campus {
namespace {

const char* kLine = "------------------------------------------------------------";

const char* RoleEn(Role role) {
  switch (role) {
    case Role::kBuyer:
      return "Buyer";
    case Role::kSeller:
      return "Seller";
    case Role::kAdmin:
      return "Admin";
  }
  return "Unknown";
}

const char* ItemStatusEn(ItemStatus status) {
  switch (status) {
    case ItemStatus::kDraft:
      return "Draft";
    case ItemStatus::kPending:
      return "Pending review";
    case ItemStatus::kPublished:
      return "Published";
    case ItemStatus::kReserved:
      return "Reserved";
    case ItemStatus::kConfirmed:
      return "Confirmed";
    case ItemStatus::kCompleted:
      return "Completed";
    case ItemStatus::kOffline:
      return "Offline";
  }
  return "Unknown";
}

const char* ReservationStatusEn(ReservationStatus status) {
  switch (status) {
    case ReservationStatus::kPending:
      return "Pending";
    case ReservationStatus::kConfirmed:
      return "Confirmed";
    case ReservationStatus::kCompleted:
      return "Completed";
    case ReservationStatus::kCanceled:
      return "Canceled";
    case ReservationStatus::kVoided:
      return "Voided";
  }
  return "Unknown";
}

const char* ReportReasonEn(ReportReason reason) {
  switch (reason) {
    case ReportReason::kMisleading:
      return "Misleading description";
    case ReportReason::kDuplicate:
      return "Duplicate listing";
    case ReportReason::kProhibited:
      return "Prohibited item";
    case ReportReason::kContact:
      return "Contact info in description";
    case ReportReason::kOther:
      return "Other";
  }
  return "Other";
}

const char* ErrorText(ErrorCode code) {
  switch (code) {
    case ErrorCode::kNotFound:
      return "not found";
    case ErrorCode::kInvalidArgument:
      return "invalid input";
    case ErrorCode::kConflict:
      return "conflict with current state";
    case ErrorCode::kPermissionDenied:
      return "permission denied";
    case ErrorCode::kStateError:
      return "operation not allowed in current state";
    case ErrorCode::kUnauthorized:
      return "wrong credentials or session expired";
    case ErrorCode::kDuplicate:
      return "duplicate record";
  }
  return "request failed";
}

// Notification types are produced by the services in Chinese; map the known
// ones to English and fall back to a generic label.
std::string NotificationTypeEn(const std::string& type) {
  if (type == u8"新的预约") return "New reservation";
  if (type == u8"预约取消") return "Reservation canceled";
  if (type == u8"预约确认") return "Reservation confirmed";
  if (type == u8"预约作废") return "Reservation voided";
  if (type == u8"审核结果") return "Review result";
  if (type == u8"商品下架") return "Item offline";
  if (type == u8"交易完成") return "Trade completed";
  if (type == u8"举报结果") return "Report result";
  return "Notification";
}

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
      std::cout << "Value out of range.\n";
      return -1;
    }
    return value;
  } catch (...) {
    std::cout << "Please enter a number.\n";
    return -1;
  }
}

bool Confirm(const std::string& prompt) {
  std::string text = ToLower(ReadLine(prompt + " (y/n): "));
  return text == "y" || text == "yes";
}

class ConsoleUI {
 public:
  ConsoleUI(const std::string& data_dir, bool seed_demo)
      : data_dir_(data_dir),
        paths_(MakeDatabasePaths(data_dir)),
        categories_(paths_.categories),
        users_(paths_.users),
        items_(paths_.items),
        reservations_(paths_.reservations),
        reports_(paths_.reports),
        logs_(paths_.logs),
        notification_repo_(paths_.notifications),
        ids_(clock_),
        user_service_(users_, hasher_, clock_, ids_),
        item_service_(users_, items_, categories_, reservations_,
                      notification_repo_, logs_, clock_, ids_),
        search_service_(items_, categories_),
        reservation_service_(users_, items_, reservations_, notification_repo_,
                             logs_, clock_, ids_),
        report_service_(users_, items_, reservations_, reports_,
                        notification_repo_, logs_, clock_, ids_),
        log_service_(logs_, clock_, ids_),
        stats_service_(users_, items_, categories_, reservations_, reports_),
        seed_demo_(seed_demo) {}

  int Run() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
    InstanceLock lock;
    if (!lock.Acquire(data_dir_)) {
      std::cout << "Another instance is already using this data directory.\n";
      return 1;
    }
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
    std::cout << "Data saved. Goodbye.\n";
    return 0;
  }

 private:
  // ---------------------------------------------------------------- setup

  void LoadAll() {
    categories_.Load();
    users_.Load();
    items_.Load();
    reservations_.Load();
    reports_.Load();
    logs_.Load();
    notification_repo_.Load();
  }

  void SaveAll() {
    categories_.Save();
    users_.Save();
    items_.Save();
    reservations_.Save();
    reports_.Save();
    logs_.Save();
    notification_repo_.Save();
  }

  void SeedIfEmpty() {
    SeedDemoData(categories_, users_, user_service_, item_service_);
    SaveAll();
  }

  // ---------------------------------------------------------------- display

  void PrintWelcome() const {
    std::cout << "\n"
              << kLine << "\n"
              << "  Campus Market - second-hand trading (console build)\n"
              << "  Data directory: "
              << std::filesystem::absolute(data_dir_).string()
              << "\n"
              << kLine << "\n";
    std::cout << "Demo accounts (password in brackets):\n"
              << "  admin@campus.edu  (Admin@123)\n"
              << "  seller@campus.edu (Seller@123)\n"
              << "  buyer@campus.edu  (Buyer@123)\n"
              << kLine << "\n";
  }

  void PrintCategories() const {
    std::cout << "Categories: ";
    for (const auto& category : categories_.All()) {
      std::cout << "[" << category.id << "] " << category.name
                << "  ";
    }
    std::cout << "\n";
  }

  std::string UserLabel(const std::string& user_id) const {
    auto user = users_.FindById(user_id);
    return user ? user->email : user_id;
  }

  void PrintItemRow(int index, const Item& item) const {
    auto category = categories_.FindById(item.category_id);
    std::cout << "  " << index << ") " << item.title << "  $"
              << MoneyToString(item.price) << "  "
              << ItemStatusEn(item.status) << "  "
              << (category ? category->name : "Uncategorized")
              << "  views " << item.view_count << "\n";
    std::cout << "     id " << item.id << "  seller "
              << UserLabel(item.seller_id) << "\n";
  }

  void PrintItemTable(const std::vector<Item>& list) const {
    if (list.empty()) {
      std::cout << "  (no items)\n";
      return;
    }
    for (std::size_t i = 0; i < list.size(); ++i) {
      PrintItemRow(static_cast<int>(i + 1), list[i]);
    }
  }

  void PrintItemDetail(const Item& item) const {
    auto category = categories_.FindById(item.category_id);
    std::cout << "\n" << kLine << "\n"
              << "Title: " << item.title << "\n"
              << "Price: $" << MoneyToString(item.price) << "\n"
              << "Category: "
              << (category ? category->name : "Uncategorized")
              << "\n"
              << "Status: " << ItemStatusEn(item.status) << "\n"
              << "Seller: " << UserLabel(item.seller_id) << "\n"
              << "Views: " << item.view_count << "\n"
              << "Created: " << FormatTime(item.created_at) << "\n"
              << "Description: " << item.description << "\n";
    if (!item.reject_reason.empty()) {
      std::cout << "Reject reason: " << item.reject_reason << "\n";
    }
    std::cout << kLine << "\n";
  }

  void Pause() const { ReadLine("\nPress Enter to continue..."); }

  // ---------------------------------------------------------------- menus

  void GuestMenu() {
    std::cout << "\n=== Guest ===\n"
              << "1) Browse / search items\n"
              << "2) Login\n"
              << "3) Register\n"
              << "0) Exit\n";
    int choice = ReadInt("Select: ", 0, 3);
    if (choice <= 0) {
      running_ = false;
      return;
    }
    Guard("guest menu", [&] {
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
    std::cout << "\n=== " << current_->email << " (";
    bool first = true;
    for (Role role : current_->roles) {
      if (!first) {
        std::cout << " / ";
      }
      std::cout << RoleEn(role);
      first = false;
    }
    std::cout << ") ===\n";

    std::vector<std::pair<std::string, std::function<void()>>> actions;
    if (current_->HasRole(Role::kBuyer)) {
      actions.push_back({"Browse items", [this] { SearchFlow(); }});
      actions.push_back({"My reservations", [this] { MyReservations(); }});
    }
    if (current_->HasRole(Role::kSeller)) {
      actions.push_back({"Publish item", [this] { CreateItemFlow(); }});
      actions.push_back({"My items", [this] { SellerProducts(); }});
      actions.push_back({"Reservations to confirm", [this] { SellerReservations(); }});
      actions.push_back({"My stats", [this] { SellerStats(); }});
    }
    if (current_->HasRole(Role::kAdmin)) {
      actions.push_back({"Review items", [this] { AdminReview(); }});
      actions.push_back({"Handle reports", [this] { AdminReports(); }});
      actions.push_back({"Operation logs", [this] { AdminLogs(); }});
      actions.push_back({"Statistics", [this] { AdminStats(); }});
    }
    actions.push_back({"Notifications", [this] { Notifications(); }});
    actions.push_back({"Logout", [this] { LogoutFlow(); }});

    for (std::size_t i = 0; i < actions.size(); ++i) {
      std::cout << (i + 1) << ") " << actions[i].first << "\n";
    }
    std::cout << "0) Exit program\n";
    int choice = ReadInt("Select: ", 0, static_cast<int>(actions.size()));
    if (choice == 0) {
      running_ = false;
      return;
    }
    Guard("menu action", [&] {
      actions[choice - 1].second();
      SaveAll();
    });
  }

  template <typename Function>
  void Guard(const std::string& context, Function&& function) {
    try {
      function();
    } catch (const DomainError& error) {
      std::cout << "Error (" << context << "): " << ErrorText(error.code())
                << " [" << static_cast<int>(error.code()) << "]\n";
    } catch (const std::exception& error) {
      std::cout << "Unexpected error: " << error.what() << "\n";
    }
  }

  // ---------------------------------------------------------------- account

  void LoginFlow() {
    std::string account = ReadLine("Email or phone: ");
    std::string password = ReadLine("Password: ");
    User user = user_service_.Login(account, password);
    current_ = user;
    std::cout << "Login successful. Welcome " << user.email << "\n";
  }

  void RegisterFlow() {
    std::string email = ReadLine("Email: ");
    std::string phone = ReadLine("Phone: ");
    std::string password = ReadLine("Password (min 8 chars, letters + digits): ");
    bool as_seller = Confirm("Enable seller role as well");
    User user = user_service_.Register(email, phone, password, as_seller);
    std::cout << "Registered. Account id " << user.id << ". Please login.\n";
  }

  void LogoutFlow() {
    current_.reset();
    std::cout << "Logged out.\n";
  }

  // ---------------------------------------------------------------- search

  void SearchFlow() {
    SearchQuery query;
    query.keyword = ReadLine("Keyword (optional): ");
    PrintCategories();
    query.category_id = ReadLine("Category id (optional): ");
    std::string min_text = ReadLine("Min price (optional): ");
    std::string max_text = ReadLine("Max price (optional): ");
    if (!min_text.empty()) {
      query.min_price = ParseMoney(min_text);
    }
    if (!max_text.empty()) {
      query.max_price = ParseMoney(max_text);
    }
    std::cout << "Sort: 1) latest 2) price ascending 3) price descending\n";
    int sort = ReadInt("Select (default 1): ", 0, 3);
    query.sort = sort == 2   ? SearchQuery::Sort::kPriceAsc
                 : sort == 3 ? SearchQuery::Sort::kPriceDesc
                             : SearchQuery::Sort::kLatest;
    query.page = 1;
    query.page_size = 5;

    while (true) {
      SearchResult result = search_service_.Search(query);
      std::cout << "\n" << result.total << " item(s), page " << result.page << "/"
                << (result.total_pages == 0 ? 1 : result.total_pages) << "\n";
      PrintItemTable(result.items);
      std::string action =
          ReadLine("[number] view details  n) next  p) previous  0) back: ");
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
        std::cout << "Invalid input.\n";
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
    if (!current_) {
      return;
    }
    bool is_buyer = current_->HasRole(Role::kBuyer);
    bool is_owner = item.seller_id == current_->id;
    if (is_owner) {
      return;
    }
    bool can_reserve = is_buyer && item.status == ItemStatus::kPublished;
    std::cout << (can_reserve ? "1) Reserve item  " : "")
              << "2) Report item  0) back\n";
    int action = ReadInt("Select: ", 0, 2);
    if (action == 1 && can_reserve) {
      Reservation reservation = reservation_service_.Reserve(
          current_->id, item.id, ids_.Next("RQ"));
      std::cout << "Reserved. Reservation id " << reservation.id
                << ", waiting for the seller to confirm.\n";
      return;
    }
    if (action == 2) {
      std::cout << "Report reason: 1) misleading 2) duplicate 3) prohibited "
                   "4) contact info 5) other\n";
      int reason = ReadInt("Select reason: ", 1, 5);
      if (reason < 1) {
        return;
      }
      static const ReportReason kReasons[] = {
          ReportReason::kMisleading, ReportReason::kDuplicate,
          ReportReason::kProhibited, ReportReason::kContact,
          ReportReason::kOther};
      std::string description = ReadLine("Description: ");
      Report report = report_service_.Submit(current_->id, item.id,
                                             kReasons[reason - 1], description);
      std::cout << "Report submitted. Id " << report.id
                << ", waiting for the admin.\n";
    }
  }

  // ---------------------------------------------------------------- buyer

  void MyReservations() {
    std::vector<Reservation> list =
        reservation_service_.ListByBuyer(current_->id);
    if (list.empty()) {
      std::cout << "No reservations yet.\n";
      return;
    }
    for (std::size_t i = 0; i < list.size(); ++i) {
      auto item = items_.FindById(list[i].item_id);
      std::cout << "  " << (i + 1) << ") " << list[i].id << "  "
                << (item ? item->title : list[i].item_id) << "  "
                << ReservationStatusEn(list[i].status) << "  "
                << FormatTime(list[i].created_at) << "\n";
    }
    int index = ReadInt("Select reservation (0 back): ", 0,
                        static_cast<int>(list.size()));
    if (index <= 0) {
      return;
    }
    const Reservation& reservation = list[index - 1];
    std::cout << "1) Cancel reservation  2) Confirm completion  0) back\n";
    int action = ReadInt("Select: ", 0, 2);
    if (action == 1) {
      reservation_service_.Cancel(current_->id, reservation.id);
      std::cout << "Reservation canceled.\n";
    } else if (action == 2) {
      reservation_service_.Complete(current_->id, reservation.id);
      std::cout << "Trade completed.\n";
    }
  }

  // ---------------------------------------------------------------- seller

  void CreateItemFlow() {
    PrintCategories();
    std::string category_id = ReadLine("Category id: ");
    std::string title = ReadLine("Title: ");
    std::string description = ReadLine("Description: ");
    std::string price_text = ReadLine("Price: ");
    auto price = ParseMoney(price_text);
    if (!price) {
      std::cout << "Invalid price.\n";
      return;
    }
    Item item = item_service_.CreateDraft(current_->id, category_id, title,
                                          description, *price);
    std::cout << "Draft saved. Id " << item.id << "\n";
    if (Confirm("Submit for review now")) {
      item = item_service_.SubmitForReview(current_->id, item.id);
      std::cout << "Submitted. Current status: " << ItemStatusEn(item.status)
                << "\n";
    }
  }

  void SellerProducts() {
    std::vector<Item> list = item_service_.ListBySeller(current_->id);
    if (list.empty()) {
      std::cout << "You have no items yet.\n";
      return;
    }
    PrintItemTable(list);
    int index = ReadInt("Select item (0 back): ", 0,
                        static_cast<int>(list.size()));
    if (index <= 0) {
      return;
    }
    Item item = list[index - 1];
    PrintItemDetail(item);
    std::cout << "1) Edit description  2) Change price  3) Take offline  "
                 "4) Submit for review  0) back\n";
    int action = ReadInt("Select: ", 0, 4);
    switch (action) {
      case 1: {
        std::string title = ReadLine("New title (Enter to keep): ");
        std::string description = ReadLine("New description (Enter to keep): ");
        Item updated = item_service_.UpdatePublished(
            current_->id, item.id, title.empty() ? item.title : title,
            description.empty() ? item.description : description);
        std::cout << "Updated: " << updated.title << "\n";
        break;
      }
      case 2: {
        std::string price_text = ReadLine("New price: ");
        auto price = ParseMoney(price_text);
        if (!price) {
          std::cout << "Invalid price.\n";
          return;
        }
        Item updated = item_service_.ChangePrice(current_->id, item.id, *price);
        std::cout << "Price changed. Current status: "
                  << ItemStatusEn(updated.status) << "\n";
        break;
      }
      case 3: {
        std::string reason = ReadLine("Reason (optional): ");
        item_service_.TakeOffline(current_->id, item.id, reason);
        std::cout << "Item is offline.\n";
        break;
      }
      case 4: {
        Item submitted = item_service_.SubmitForReview(current_->id, item.id);
        std::cout << "Submitted. Current status: "
                  << ItemStatusEn(submitted.status) << "\n";
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
      std::cout << "No reservations waiting for confirmation.\n";
      return;
    }
    for (std::size_t i = 0; i < pending.size(); ++i) {
      auto item = items_.FindById(pending[i].item_id);
      std::cout << "  " << (i + 1) << ") " << pending[i].id << "  "
                << (item ? item->title : "") << "  buyer "
                << UserLabel(pending[i].buyer_id) << "\n";
    }
    int index = ReadInt("Select reservation (0 back): ", 0,
                        static_cast<int>(pending.size()));
    if (index <= 0) {
      return;
    }
    reservation_service_.Confirm(current_->id, pending[index - 1].id);
    std::cout << "Trade confirmed. Waiting for offline delivery.\n";
  }

  void SellerStats() {
    campus::SellerStats stats = stats_service_.ForSeller(current_->id);
    std::cout << "\nPublished " << stats.published << ", pending "
              << stats.pending << ", reserved " << stats.reserved
              << ", completed " << stats.completed << "\n"
              << "Total views " << stats.total_views << ", total reservations "
              << stats.total_reservations << "\n";
  }

  // ---------------------------------------------------------------- admin

  void AdminReview() {
    std::vector<Item> pending = item_service_.ListByStatus(ItemStatus::kPending);
    if (pending.empty()) {
      std::cout << "No items waiting for review.\n";
      return;
    }
    PrintItemTable(pending);
    int index =
        ReadInt("Select item (0 back): ", 0, static_cast<int>(pending.size()));
    if (index <= 0) {
      return;
    }
    Item item = pending[index - 1];
    PrintItemDetail(item);
    std::cout << "1) Approve  2) Reject  0) back\n";
    int action = ReadInt("Select: ", 0, 2);
    if (action == 1) {
      item_service_.Approve(current_->id, item.id);
      std::cout << "Approved and published.\n";
    } else if (action == 2) {
      std::string reason = ReadLine("Reject reason (required): ");
      item_service_.Reject(current_->id, item.id, reason);
      std::cout << "Rejected and returned to draft.\n";
    }
  }

  void AdminReports() {
    std::vector<Report> pending = report_service_.ListPending();
    if (pending.empty()) {
      std::cout << "No pending reports.\n";
      return;
    }
    for (std::size_t i = 0; i < pending.size(); ++i) {
      auto item = items_.FindById(pending[i].item_id);
      std::cout << "  " << (i + 1) << ") " << pending[i].id << "  "
                << ReportReasonEn(pending[i].reason) << "  "
                << (item ? item->title : "") << "\n";
    }
    int index =
        ReadInt("Select report (0 back): ", 0, static_cast<int>(pending.size()));
    if (index <= 0) {
      return;
    }
    const Report& report = pending[index - 1];
    std::cout << "Reporter: " << UserLabel(report.reporter_id) << "\n"
              << "Description: " << report.description << "\n"
              << "Reports on this item: "
              << report_service_.ListByItem(report.item_id).size() << "\n";
    std::cout << "1) Uphold (take item offline)  2) Reject  0) back\n";
    int action = ReadInt("Select: ", 0, 2);
    if (action == 1 || action == 2) {
      std::string note = ReadLine("Handling note (optional): ");
      report_service_.Handle(current_->id, report.id, action == 1, note);
      std::cout << "Report handled.\n";
    }
  }

  void AdminLogs() {
    LogFilter filter;
    std::string target =
        ToLower(ReadLine("Target type (item/reservation/report, optional): "));
    if (target == "item") {
      filter.target_type = u8"商品";
    } else if (target == "reservation" || target == "res") {
      filter.target_type = u8"预约";
    } else if (target == "report") {
      filter.target_type = u8"举报";
    }
    filter.limit = 30;
    std::vector<OperationLog> list = log_service_.Query(filter);
    if (list.empty()) {
      std::cout << "No matching logs.\n";
      return;
    }
    for (const auto& log : list) {
      std::cout << "  " << FormatTime(log.created_at) << "  "
                << UserLabel(log.operator_id) << "  " << log.action
                << "  " << log.target_type << "/" << log.target_id;
      if (!log.detail.empty()) {
        std::cout << "  " << log.detail;
      }
      std::cout << "\n";
    }
    std::cout << list.size() << " log(s) shown (max 30)\n";
  }

  void AdminStats() {
    std::string from_text = ReadLine("Start date (YYYY-MM-DD, optional): ");
    std::string to_text = ReadLine("End date (YYYY-MM-DD, optional): ");
    Timestamp now = clock_.Now();
    Timestamp from = now - 7 * 24 * 3600;
    Timestamp to = now;
    if (!from_text.empty()) {
      auto parsed = ParseTime(from_text + " 00:00:00");
      if (!parsed) {
        std::cout << "Invalid date format.\n";
        return;
      }
      from = *parsed;
    }
    if (!to_text.empty()) {
      auto parsed = ParseTime(to_text + " 23:59:59");
      if (!parsed) {
        std::cout << "Invalid date format.\n";
        return;
      }
      to = *parsed;
    }
    StatsReport report = stats_service_.Overview(from, to);
    std::cout << "\nRange: " << FormatTime(from) << " ~ " << FormatTime(to)
              << "\nNew items " << report.new_items << ", successful reservations "
              << report.successful_reservations << ", pending reports "
              << report.pending_reports << ", active users "
              << report.active_users << "\nItems by category: ";
    for (const auto& entry : report.items_by_category) {
      std::cout << entry.first << "=" << entry.second << "  ";
    }
    std::cout << "\nItems by status: ";
    for (const auto& entry : report.items_by_status) {
      std::cout << ItemStatusEn(entry.first) << "=" << entry.second << "  ";
    }
    std::cout << "\n";
  }

  // ---------------------------------------------------------------- notifications

  void Notifications() {
    std::vector<Notification> list =
        notification_repo_.FindByUser(current_->id);
    if (list.empty()) {
      std::cout << "No notifications.\n";
      return;
    }
    for (const auto& notification : list) {
      std::cout << "  [" << (notification.read ? "read" : "unread") << "] "
                << FormatTime(notification.created_at) << "  "
                << NotificationTypeEn(notification.type) << "  "
                << notification.content << "\n";
    }
    if (Confirm("Mark all as read")) {
      for (const auto& notification : list) {
        notification_repo_.MarkRead(notification.id);
      }
      notification_repo_.Save();
      std::cout << "All notifications marked as read.\n";
    }
  }

  // ---------------------------------------------------------------- members

  std::string data_dir_;
  DatabasePaths paths_;
  FileCategoryRepository categories_;
  FileUserRepository users_;
  FileItemRepository items_;
  FileReservationRepository reservations_;
  FileReportRepository reports_;
  FileLogRepository logs_;
  FileNotificationRepository notification_repo_;
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
