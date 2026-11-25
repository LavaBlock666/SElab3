// Copyright 2026 兰雨阳. All rights reserved.
// 内置 HTTP 服务与 JSON 接口实现。

#include "web/web_server.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <cctype>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "campus/services.h"
#include "campus/store.h"

namespace campus {
namespace {

// ---------------------------------------------------------------- 基础工具

std::string EscapeJson(const std::string& text) {
  std::string out;
  out.reserve(text.size() + 8);
  for (unsigned char c : text) {
    switch (c) {
      case '"':
        out += "\\\"";
        break;
      case '\\':
        out += "\\\\";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        if (c < 0x20) {
          char buffer[8];
          std::snprintf(buffer, sizeof(buffer), "\\u%04x", c);
          out += buffer;
        } else {
          out.push_back(static_cast<char>(c));
        }
    }
  }
  return out;
}

std::string Quote(const std::string& text) { return "\"" + EscapeJson(text) + "\""; }

std::string UrlDecode(const std::string& text) {
  std::string out;
  for (std::size_t i = 0; i < text.size(); ++i) {
    if (text[i] == '+') {
      out.push_back(' ');
    } else if (text[i] == '%' && i + 2 < text.size()) {
      auto hex = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
      };
      int high = hex(text[i + 1]);
      int low = hex(text[i + 2]);
      if (high >= 0 && low >= 0) {
        out.push_back(static_cast<char>(high * 16 + low));
        i += 2;
      } else {
        out.push_back(text[i]);
      }
    } else {
      out.push_back(text[i]);
    }
  }
  return out;
}

std::map<std::string, std::string> ParseParams(const std::string& text) {
  std::map<std::string, std::string> result;
  std::size_t start = 0;
  while (start <= text.size()) {
    std::size_t end = text.find('&', start);
    std::string pair =
        text.substr(start, end == std::string::npos ? std::string::npos : end - start);
    if (!pair.empty()) {
      std::size_t eq = pair.find('=');
      std::string key = UrlDecode(pair.substr(0, eq));
      std::string value =
          eq == std::string::npos ? "" : UrlDecode(pair.substr(eq + 1));
      result[key] = value;
    }
    if (end == std::string::npos) {
      break;
    }
    start = end + 1;
  }
  return result;
}

std::string Lower(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return text;
}

// ---------------------------------------------------------------- HTTP 模型

struct Request {
  std::string method;
  std::string path;
  std::string body;
  std::map<std::string, std::string> query;
  std::map<std::string, std::string> form;
  std::map<std::string, std::string> cookies;

  std::string Param(const std::string& key) const {
    auto it = form.find(key);
    if (it != form.end()) {
      return it->second;
    }
    it = query.find(key);
    return it != query.end() ? it->second : std::string();
  }

  bool Has(const std::string& key) const { return !Param(key).empty(); }
};

struct Response {
  int status = 200;
  std::string body;
  std::string content_type = "application/json; charset=utf-8";
  std::string extra_headers;
};

Response JsonOk(const std::string& data) {
  return {200, "{\"ok\":true,\"data\":" + data + "}", "application/json; charset=utf-8",
          ""};
}

Response JsonError(int status, const std::string& message) {
  return {status, "{\"ok\":false,\"error\":" + Quote(message) + "}",
          "application/json; charset=utf-8", ""};
}

std::string ItemJson(const Item& item, const std::string& category,
                     const std::string& seller) {
  std::ostringstream out;
  out << "{"
      << "\"id\":" << Quote(item.id) << ","
      << "\"title\":" << Quote(item.title) << ","
      << "\"price\":" << Quote(MoneyToString(item.price)) << ","
      << "\"price_cents\":" << item.price << ","
      << "\"status\":" << Quote(ToString(item.status)) << ","
      << "\"status_label\":" << Quote(ItemStatusLabel(item.status)) << ","
      << "\"category\":" << Quote(category) << ","
      << "\"category_id\":" << Quote(item.category_id) << ","
      << "\"seller\":" << Quote(seller) << ","
      << "\"seller_id\":" << Quote(item.seller_id) << ","
      << "\"description\":" << Quote(item.description) << ","
      << "\"views\":" << item.view_count << ","
      << "\"created_at\":" << Quote(FormatTime(item.created_at)) << ","
      << "\"reject_reason\":" << Quote(item.reject_reason)
      << "}";
  return out.str();
}

// ---------------------------------------------------------------- 服务器

class WebApp {
 public:
  WebApp(std::string data_dir, std::string web_root)
      : paths_(MakeDatabasePaths(data_dir)),
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
        web_root_(std::move(web_root)) {
    categories_.Load();
    users_.Load();
    items_.Load();
    reservations_.Load();
    reports_.Load();
    logs_.Load();
    notification_repo_.Load();
    SeedIfEmpty();
  }

  ~WebApp() {
    categories_.Save();
    users_.Save();
    items_.Save();
    reservations_.Save();
    reports_.Save();
    logs_.Save();
    notification_repo_.Save();
  }

  Response Handle(const Request& request) {
    // 多个工作线程并发处理请求；业务状态（仓储、会话）在锁内访问。
    std::lock_guard<std::mutex> guard(mutex_);
    Response response;
    try {
      response = Route(request);
    } catch (const DomainError& error) {
      int status = 400;
      switch (error.code()) {
        case ErrorCode::kNotFound:
          status = 404;
          break;
        case ErrorCode::kUnauthorized:
          status = 401;
          break;
        case ErrorCode::kPermissionDenied:
          status = 403;
          break;
        case ErrorCode::kConflict:
        case ErrorCode::kDuplicate:
        case ErrorCode::kStateError:
          status = 409;
          break;
        default:
          status = 400;
      }
      response = JsonError(status, error.message());
    } catch (const std::exception& error) {
      response = JsonError(500, error.what());
    }
    // 写操作立即落盘，避免进程异常退出丢数据。
    if (request.method == "POST" && response.status == 200) {
      SaveAll();
    }
    return response;
  }

 private:
  void SaveAll() {
    categories_.Save();
    users_.Save();
    items_.Save();
    reservations_.Save();
    reports_.Save();
    logs_.Save();
    notification_repo_.Save();
  }

  // ---------- 会话 ----------

  std::optional<User> CurrentUser(const Request& request) const {
    auto it = request.cookies.find("sid");
    if (it == request.cookies.end()) {
      return std::nullopt;
    }
    auto session = sessions_.find(it->second);
    if (session == sessions_.end()) {
      return std::nullopt;
    }
    return users_.FindById(session->second);
  }

  int RequireRole(const Request& request, Role role) {
    auto user = CurrentUser(request);
    if (!user) {
      throw DomainError(ErrorCode::kUnauthorized, "请先登录");
    }
    if (!user->HasRole(role)) {
      throw DomainError(ErrorCode::kPermissionDenied, "当前账号没有该权限");
    }
    return 0;
  }

  std::string UserJson(const User& user) const {
    std::ostringstream out;
    out << "{" << "\"id\":" << Quote(user.id) << ",\"email\":" << Quote(user.email)
        << ",\"roles\":[";
    bool first = true;
    for (Role role : user.roles) {
      if (!first) {
        out << ",";
      }
      out << Quote(ToString(role));
      first = false;
    }
    out << "],\"is_admin\":" << (user.HasRole(Role::kAdmin) ? "true" : "false")
        << ",\"is_seller\":" << (user.HasRole(Role::kSeller) ? "true" : "false")
        << ",\"is_buyer\":" << (user.HasRole(Role::kBuyer) ? "true" : "false")
        << "}";
    return out.str();
  }

  // ---------- 路由 ----------

  Response Route(const Request& request) {
    const std::string& path = request.path;
    const std::string& method = request.method;

    if (method == "GET" && path == "/api/categories") {
      return CategoriesJson();
    }
    if (method == "GET" && path == "/api/items") {
      return ItemsJson(request);
    }
    if (method == "GET" && path == "/api/item") {
      return ItemDetailJson(request);
    }
    if (method == "POST" && path == "/api/login") {
      return Login(request);
    }
    if (method == "POST" && path == "/api/register") {
      return Register(request);
    }
    if (method == "POST" && path == "/api/logout") {
      return Logout(request);
    }
    if (method == "GET" && path == "/api/me") {
      auto user = CurrentUser(request);
      return JsonOk(user ? UserJson(*user) : "null");
    }
    if (method == "POST" && path == "/api/reserve") {
      return Reserve(request);
    }
    if (method == "POST" && path == "/api/report") {
      return SubmitReport(request);
    }
    if (method == "GET" && path == "/api/reservations") {
      return MyReservations(request);
    }
    if (method == "POST" && path == "/api/reservation/action") {
      return ReservationAction(request);
    }
    if (method == "GET" && path == "/api/seller/summary") {
      return SellerSummary(request);
    }
    if (method == "POST" && path == "/api/seller/item") {
      return SellerCreateItem(request);
    }
    if (method == "POST" && path == "/api/seller/action") {
      return SellerAction(request);
    }
    if (method == "GET" && path == "/api/audit") {
      return AuditList(request);
    }
    if (method == "POST" && path == "/api/audit") {
      return AuditAction(request);
    }
    if (method == "GET" && path == "/api/reports") {
      return ReportList(request);
    }
    if (method == "POST" && path == "/api/reports") {
      return ReportHandle(request);
    }
    if (method == "GET" && path == "/api/logs") {
      return Logs(request);
    }
    if (method == "GET" && path == "/api/stats") {
      return Stats(request);
    }
    if (method == "GET" && path == "/api/notifications") {
      return Notifications(request);
    }
    if (method == "POST" && path == "/api/notifications/read") {
      return ReadNotifications(request);
    }
    if (method == "GET") {
      return StaticFile(path);
    }
    return JsonError(404, "接口不存在");
  }

  // ---------- 接口实现 ----------

  Response CategoriesJson() const {
    std::ostringstream out;
    out << "[";
    bool first = true;
    for (const auto& category : categories_.All()) {
      if (!first) out << ",";
      out << "{\"id\":" << Quote(category.id) << ",\"name\":"
          << Quote(category.name) << "}";
      first = false;
    }
    out << "]";
    return JsonOk(out.str());
  }

  Response ItemsJson(const Request& request) {
    SearchQuery query;
    query.keyword = request.Param("keyword");
    query.category_id = request.Param("category");
    if (request.Has("min")) query.min_price = ParseMoney(request.Param("min"));
    if (request.Has("max")) query.max_price = ParseMoney(request.Param("max"));
    std::string sort = request.Param("sort");
    query.sort = sort == "price_asc"    ? SearchQuery::Sort::kPriceAsc
                 : sort == "price_desc" ? SearchQuery::Sort::kPriceDesc
                                        : SearchQuery::Sort::kLatest;
    try {
      query.page = std::max(1, std::stoi(request.Param("page").empty()
                                             ? "1"
                                             : request.Param("page")));
    } catch (...) {
      query.page = 1;
    }
    query.page_size = 6;
    SearchResult result = search_service_.Search(query);
    std::ostringstream out;
    out << "{\"total\":" << result.total << ",\"page\":" << query.page
        << ",\"total_pages\":" << std::max(1, result.total_pages) << ",\"items\":[";
    bool first = true;
    for (const auto& item : result.items) {
      if (!first) out << ",";
      auto category = categories_.FindById(item.category_id);
      auto seller = users_.FindById(item.seller_id);
      out << ItemJson(item, category ? category->name : "未分类",
                      seller ? seller->email : item.seller_id);
      first = false;
    }
    out << "]}";
    return JsonOk(out.str());
  }

  Response ItemDetailJson(const Request& request) {
    std::string id = request.Param("id");
    Item item = search_service_.ViewDetail(id);
    auto category = categories_.FindById(item.category_id);
    auto seller = users_.FindById(item.seller_id);
    std::ostringstream out;
    out << ItemJson(item, category ? category->name : "未分类",
                    seller ? seller->email : item.seller_id);
    return JsonOk(out.str());
  }

  Response Login(const Request& request) {
    User user = user_service_.Login(request.Param("account"),
                                    request.Param("password"));
    std::string sid = ids_.Next("SID");
    sessions_[sid] = user.id;
    Response response = JsonOk(UserJson(user));
    response.extra_headers = "Set-Cookie: sid=" + sid + "; Path=/; HttpOnly\r\n";
    return response;
  }

  Response Register(const Request& request) {
    bool seller = request.Param("seller") == "1" ||
                  request.Param("seller") == "true";
    User user = user_service_.Register(request.Param("email"),
                                       request.Param("phone"),
                                       request.Param("password"), seller);
    return JsonOk(UserJson(user));
  }

  Response Logout(const Request& request) {
    auto it = request.cookies.find("sid");
    if (it != request.cookies.end()) {
      sessions_.erase(it->second);
    }
    Response response = JsonOk("{\"message\":\"已退出\"}");
    response.extra_headers = "Set-Cookie: sid=; Path=/; Max-Age=0\r\n";
    return response;
  }

  Response Reserve(const Request& request) {
    auto user = CurrentUser(request);
    if (!user) {
      throw DomainError(ErrorCode::kUnauthorized, "请先登录");
    }
    Reservation reservation = reservation_service_.Reserve(
        user->id, request.Param("item_id"), ids_.Next("RQ"));
    std::ostringstream out;
    out << "{\"id\":" << Quote(reservation.id) << ",\"message\":\"预约成功，等待卖家确认\"}";
    return JsonOk(out.str());
  }

  Response SubmitReport(const Request& request) {
    auto user = CurrentUser(request);
    if (!user) {
      throw DomainError(ErrorCode::kUnauthorized, "请先登录");
    }
    static const ReportReason kReasons[] = {
        ReportReason::kMisleading, ReportReason::kDuplicate,
        ReportReason::kProhibited, ReportReason::kContact, ReportReason::kOther};
    int reason = 0;
    try {
      reason = std::stoi(request.Param("reason"));
    } catch (...) {
      reason = 0;
    }
    if (reason < 0 || reason > 4) {
      reason = 4;
    }
    Report report = report_service_.Submit(user->id, request.Param("item_id"),
                                           kReasons[reason],
                                           request.Param("description"));
    std::ostringstream out;
    out << "{\"id\":" << Quote(report.id) << ",\"message\":\"举报已提交\"}";
    return JsonOk(out.str());
  }

  std::string ReservationJson(const Reservation& reservation) const {
    auto item = items_.FindById(reservation.item_id);
    auto buyer = users_.FindById(reservation.buyer_id);
    std::ostringstream out;
    out << "{\"id\":" << Quote(reservation.id) << ",\"item_id\":"
        << Quote(reservation.item_id) << ",\"item_title\":"
        << Quote(item ? item->title : reservation.item_id) << ",\"buyer\":"
        << Quote(buyer ? buyer->email : reservation.buyer_id) << ",\"status\":"
        << Quote(ToString(reservation.status)) << ",\"status_label\":"
        << Quote(ReservationStatusLabel(reservation.status))
        << ",\"created_at\":" << Quote(FormatTime(reservation.created_at)) << "}";
    return out.str();
  }

  Response MyReservations(const Request& request) {
    auto user = CurrentUser(request);
    if (!user) {
      throw DomainError(ErrorCode::kUnauthorized, "请先登录");
    }
    std::ostringstream out;
    out << "[";
    bool first = true;
    for (const auto& reservation : reservation_service_.ListByBuyer(user->id)) {
      if (!first) out << ",";
      out << ReservationJson(reservation);
      first = false;
    }
    out << "]";
    return JsonOk(out.str());
  }

  Response ReservationAction(const Request& request) {
    auto user = CurrentUser(request);
    if (!user) {
      throw DomainError(ErrorCode::kUnauthorized, "请先登录");
    }
    std::string action = request.Param("action");
    std::string id = request.Param("id");
    if (action == "cancel") {
      reservation_service_.Cancel(user->id, id);
      return JsonOk("{\"message\":\"预约已取消\"}");
    }
    if (action == "complete") {
      reservation_service_.Complete(user->id, id);
      return JsonOk("{\"message\":\"已确认完成\"}");
    }
    if (action == "confirm") {
      reservation_service_.Confirm(user->id, id);
      return JsonOk("{\"message\":\"已确认交易\"}");
    }
    return JsonError(400, "未知操作");
  }

  Response SellerSummary(const Request& request) {
    auto user = CurrentUser(request);
    if (!user || !user->HasRole(Role::kSeller)) {
      throw DomainError(ErrorCode::kPermissionDenied, "需要卖家身份");
    }
    SellerStats stats = stats_service_.ForSeller(user->id);
    std::ostringstream out;
    out << "{\"stats\":{\"published\":" << stats.published
        << ",\"pending\":" << stats.pending << ",\"reserved\":"
        << stats.reserved << ",\"completed\":" << stats.completed
        << ",\"views\":" << stats.total_views << ",\"reservations\":"
        << stats.total_reservations << "},\"items\":[";
    bool first = true;
    for (const auto& item : item_service_.ListBySeller(user->id)) {
      if (!first) out << ",";
      auto category = categories_.FindById(item.category_id);
      out << ItemJson(item, category ? category->name : "未分类", user->email);
      first = false;
    }
    out << "],\"reservations\":[";
    first = true;
    for (const auto& item : item_service_.ListBySeller(user->id)) {
      for (const auto& reservation : reservation_service_.ListByItem(item.id)) {
        if (reservation.status != ReservationStatus::kPending) {
          continue;
        }
        if (!first) out << ",";
        out << ReservationJson(reservation);
        first = false;
      }
    }
    out << "]}";
    return JsonOk(out.str());
  }

  Response SellerCreateItem(const Request& request) {
    auto user = CurrentUser(request);
    if (!user || !user->HasRole(Role::kSeller)) {
      throw DomainError(ErrorCode::kPermissionDenied, "需要卖家身份");
    }
    auto price = ParseMoney(request.Param("price"));
    if (!price) {
      throw DomainError(ErrorCode::kInvalidArgument, "价格格式不正确");
    }
    Item item = item_service_.CreateDraft(
        user->id, request.Param("category"), request.Param("title"),
        request.Param("description"), *price);
    if (request.Param("submit") == "1") {
      item = item_service_.SubmitForReview(user->id, item.id);
    }
    auto category = categories_.FindById(item.category_id);
    return JsonOk(ItemJson(item, category ? category->name : "未分类", user->email));
  }

  Response SellerAction(const Request& request) {
    auto user = CurrentUser(request);
    if (!user || !user->HasRole(Role::kSeller)) {
      throw DomainError(ErrorCode::kPermissionDenied, "需要卖家身份");
    }
    std::string action = request.Param("action");
    std::string id = request.Param("id");
    if (action == "submit") {
      item_service_.SubmitForReview(user->id, id);
      return JsonOk("{\"message\":\"已提交审核\"}");
    }
    if (action == "offline") {
      item_service_.TakeOffline(user->id, id, "卖家主动下架");
      return JsonOk("{\"message\":\"商品已下架\"}");
    }
    if (action == "price") {
      auto price = ParseMoney(request.Param("price"));
      if (!price) {
        throw DomainError(ErrorCode::kInvalidArgument, "价格格式不正确");
      }
      Item item = item_service_.ChangePrice(user->id, id, *price);
      return JsonOk("{\"message\":\"已改价，当前状态：" +
                    ItemStatusLabel(item.status) + "\"}");
    }
    return JsonError(400, "未知操作");
  }

  Response AuditList(const Request& request) {
    RequireRole(request, Role::kAdmin);
    std::vector<Item> pending = item_service_.ListByStatus(ItemStatus::kPending);
    std::ostringstream out;
    out << "[";
    bool first = true;
    for (const auto& item : pending) {
      if (!first) out << ",";
      auto category = categories_.FindById(item.category_id);
      auto seller = users_.FindById(item.seller_id);
      out << ItemJson(item, category ? category->name : "未分类",
                      seller ? seller->email : item.seller_id);
      first = false;
    }
    out << "]";
    return JsonOk(out.str());
  }

  Response AuditAction(const Request& request) {
    auto user = CurrentUser(request);
    if (!user || !user->HasRole(Role::kAdmin)) {
      throw DomainError(ErrorCode::kPermissionDenied, "需要管理员身份");
    }
    std::string action = request.Param("action");
    std::string id = request.Param("id");
    if (action == "approve") {
      item_service_.Approve(user->id, id);
      return JsonOk("{\"message\":\"已通过并发布\"}");
    }
    if (action == "reject") {
      item_service_.Reject(user->id, id, request.Param("reason"));
      return JsonOk("{\"message\":\"已驳回并退回草稿\"}");
    }
    return JsonError(400, "未知操作");
  }

  Response ReportList(const Request& request) {
    RequireRole(request, Role::kAdmin);
    std::ostringstream out;
    out << "[";
    bool first = true;
    for (const auto& report : report_service_.ListPending()) {
      if (!first) out << ",";
      auto item = items_.FindById(report.item_id);
      auto reporter = users_.FindById(report.reporter_id);
      out << "{\"id\":" << Quote(report.id) << ",\"item_id\":"
          << Quote(report.item_id) << ",\"item_title\":"
          << Quote(item ? item->title : report.item_id) << ",\"reporter\":"
          << Quote(reporter ? reporter->email : report.reporter_id)
          << ",\"reason\":" << Quote(ToString(report.reason)) << ",\"reason_label\":"
          << Quote(ReportReasonLabel(report.reason)) << ",\"description\":"
          << Quote(report.description) << ",\"created_at\":"
          << Quote(FormatTime(report.created_at)) << ",\"history\":"
          << report_service_.ListByItem(report.item_id).size() << "}";
      first = false;
    }
    out << "]";
    return JsonOk(out.str());
  }

  Response ReportHandle(const Request& request) {
    auto user = CurrentUser(request);
    if (!user || !user->HasRole(Role::kAdmin)) {
      throw DomainError(ErrorCode::kPermissionDenied, "需要管理员身份");
    }
    bool uphold = request.Param("uphold") == "1";
    report_service_.Handle(user->id, request.Param("id"), uphold,
                           request.Param("note"));
    return JsonOk("{\"message\":\"举报已处理\"}");
  }

  Response Logs(const Request& request) {
    RequireRole(request, Role::kAdmin);
    LogFilter filter;
    std::string target = request.Param("target");
    if (!target.empty()) {
      filter.target_type = target;
    }
    filter.limit = 20;
    std::ostringstream out;
    out << "[";
    bool first = true;
    for (const auto& log : log_service_.Query(filter)) {
      if (!first) out << ",";
      auto user = users_.FindById(log.operator_id);
      out << "{\"time\":" << Quote(FormatTime(log.created_at)) << ",\"operator\":"
          << Quote(user ? user->email : log.operator_id) << ",\"action\":"
          << Quote(log.action) << ",\"target_type\":" << Quote(log.target_type)
          << ",\"target_id\":" << Quote(log.target_id) << ",\"detail\":"
          << Quote(log.detail) << "}";
      first = false;
    }
    out << "]";
    return JsonOk(out.str());
  }

  Response Stats(const Request& request) {
    RequireRole(request, Role::kAdmin);
    Timestamp now = clock_.Now();
    StatsReport stats = stats_service_.Overview(now - 7 * 24 * 3600, now);
    std::ostringstream out;
    out << "{\"new_items\":" << stats.new_items << ",\"successful_reservations\":"
        << stats.successful_reservations << ",\"pending_reports\":"
        << stats.pending_reports << ",\"active_users\":" << stats.active_users
        << ",\"categories\":[";
    bool first = true;
    for (const auto& entry : stats.items_by_category) {
      if (!first) out << ",";
      out << "{\"name\":" << Quote(entry.first) << ",\"count\":"
          << entry.second << "}";
      first = false;
    }
    out << "],\"statuses\":[";
    first = true;
    for (const auto& entry : stats.items_by_status) {
      if (!first) out << ",";
      out << "{\"name\":" << Quote(ItemStatusLabel(entry.first))
          << ",\"count\":" << entry.second << "}";
      first = false;
    }
    out << "],\"daily\":[";
    std::vector<int> daily(7, 0);
    for (const auto& reservation : reservations_.All()) {
      Timestamp age = now - reservation.created_at;
      int day = static_cast<int>(age / 86400);
      if (day >= 0 && day < 7) {
        ++daily[6 - day];
      }
    }
    for (int i = 0; i < 7; ++i) {
      if (i > 0) out << ",";
      out << daily[i];
    }
    out << "]}";
    return JsonOk(out.str());
  }

  Response Notifications(const Request& request) {
    auto user = CurrentUser(request);
    if (!user) {
      throw DomainError(ErrorCode::kUnauthorized, "请先登录");
    }
    std::ostringstream out;
    out << "[";
    bool first = true;
    for (const auto& notification : notification_repo_.FindByUser(user->id)) {
      if (!first) out << ",";
      out << "{\"id\":" << Quote(notification.id) << ",\"type\":"
          << Quote(notification.type) << ",\"content\":"
          << Quote(notification.content) << ",\"read\":"
          << (notification.read ? "true" : "false") << ",\"time\":"
          << Quote(FormatTime(notification.created_at)) << "}";
      first = false;
    }
    out << "]";
    return JsonOk(out.str());
  }

  Response ReadNotifications(const Request& request) {
    auto user = CurrentUser(request);
    if (!user) {
      throw DomainError(ErrorCode::kUnauthorized, "请先登录");
    }
    for (const auto& notification : notification_repo_.FindByUser(user->id)) {
      notification_repo_.MarkRead(notification.id);
    }
    notification_repo_.Save();
    return JsonOk("{\"message\":\"已全部标记为已读\"}");
  }

  // ---------- 静态文件 ----------

  Response StaticFile(const std::string& path) const {
    std::string relative = path == "/" ? "index.html" : path.substr(1);
    if (relative.find("..") != std::string::npos) {
      return JsonError(400, "非法路径");
    }
    std::string full = web_root_ + "/" + relative;
    std::ifstream in(full, std::ios::binary);
    if (!in) {
      return JsonError(404, "页面不存在");
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    std::string extension;
    std::size_t dot = relative.rfind('.');
    if (dot != std::string::npos) {
      extension = Lower(relative.substr(dot + 1));
    }
    std::string content_type = "text/plain; charset=utf-8";
    if (extension == "html") content_type = "text/html; charset=utf-8";
    else if (extension == "css") content_type = "text/css; charset=utf-8";
    else if (extension == "js") content_type = "application/javascript; charset=utf-8";
    else if (extension == "svg") content_type = "image/svg+xml";
    else if (extension == "png") content_type = "image/png";
    else if (extension == "ico") content_type = "image/x-icon";
    return {200, buffer.str(), content_type, ""};
  }

  // ---------- 初始化演示数据 ----------

  void SeedIfEmpty() {
    if (!categories_.All().empty()) {
      return;
    }
    const char* names[] = {"教材书籍", "数码电子", "生活用品", "运动户外", "其他"};
    for (int i = 0; i < 5; ++i) {
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
    try {
      admin = user_service_.CreateAdmin("admin@campus.edu", "13800000001",
                                        "Admin@123");
      seller = user_service_.Register("seller@campus.edu", "13800000002",
                                      "Seller@123", true);
      user_service_.Register("buyer@campus.edu", "13800000003", "Buyer@123",
                             false);
      user_service_.Register("buyer2@campus.edu", "13800000004", "Buyer@123",
                             false);
    } catch (const DomainError&) {
      return;
    }
    auto list = categories_.All();
    struct Demo {
      int category;
      const char* title;
      const char* description;
      Money price;
    };
    Demo demo[] = {
        {0, "高等数学 第七版 上下册", "考研用过，笔记齐全，无缺页。", 2500},
        {1, "27 英寸 2K 显示器", "自用两年，无坏点，带原装支架。", 62000},
        {3, "捷安特山地自行车", "骑行约 300 公里，保养良好。", 48000},
        {2, "宿舍收纳箱 三件套", "搬宿舍剩下的，全新未使用。", 3500}};
    for (const auto& entry : demo) {
      try {
        Item item = item_service_.CreateDraft(
            seller.id, list[entry.category].id, entry.title, entry.description,
            entry.price);
        item_service_.SubmitForReview(seller.id, item.id);
        item_service_.Approve(admin.id, item.id);
      } catch (const DomainError&) {
      }
    }
    categories_.Save();
    users_.Save();
    items_.Save();
    logs_.Save();
  }

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

  std::mutex mutex_;
  std::map<std::string, std::string> sessions_;
  std::string web_root_;
};

// ---------------------------------------------------------------- 网络

std::string ReadRequest(SOCKET client) {
  std::string data;
  char buffer[4096];
  std::size_t header_end = std::string::npos;
  while (true) {
    int received = recv(client, buffer, sizeof(buffer), 0);
    if (received <= 0) {
      break;
    }
    data.append(buffer, received);
    header_end = data.find("\r\n\r\n");
    if (header_end != std::string::npos) {
      break;
    }
    if (data.size() > 1024 * 1024) {
      break;
    }
  }
  if (header_end == std::string::npos) {
    return data;
  }
  std::size_t content_length = 0;
  std::string headers = data.substr(0, header_end);
  std::size_t pos = Lower(headers).find("content-length:");
  if (pos != std::string::npos) {
    content_length = static_cast<std::size_t>(
        std::atol(headers.c_str() + pos + 15));
  }
  std::size_t body_start = header_end + 4;
  while (data.size() < body_start + content_length) {
    int received = recv(client, buffer, sizeof(buffer), 0);
    if (received <= 0) {
      break;
    }
    data.append(buffer, received);
  }
  return data;
}

Request ParseRequest(const std::string& raw) {
  Request request;
  std::size_t header_end = raw.find("\r\n\r\n");
  std::string head = raw.substr(0, header_end);
  std::string body =
      header_end == std::string::npos ? "" : raw.substr(header_end + 4);
  std::istringstream lines(head);
  std::string line;
  std::getline(lines, line);
  std::istringstream first(line);
  first >> request.method;
  std::string target;
  first >> target;
  std::size_t question = target.find('?');
  if (question != std::string::npos) {
    request.path = target.substr(0, question);
    request.query = ParseParams(target.substr(question + 1));
  } else {
    request.path = target;
  }
  while (std::getline(lines, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    std::size_t colon = line.find(':');
    if (colon == std::string::npos) {
      continue;
    }
    std::string key = Lower(line.substr(0, colon));
    std::string value = line.substr(colon + 1);
    while (!value.empty() && value.front() == ' ') {
      value.erase(value.begin());
    }
    if (key == "cookie") {
      request.cookies = ParseParams(value);
      std::size_t start = 0;
      while (start <= value.size()) {
        std::size_t end = value.find(';', start);
        std::string pair = value.substr(start, end == std::string::npos
                                                   ? std::string::npos
                                                   : end - start);
        std::size_t eq = pair.find('=');
        if (eq != std::string::npos) {
          std::string cookie_key = Trim(pair.substr(0, eq));
          std::string cookie_value = Trim(pair.substr(eq + 1));
          request.cookies[cookie_key] = cookie_value;
        }
        if (end == std::string::npos) {
          break;
        }
        start = end + 1;
      }
    }
  }
  request.body = body;
  std::string content_type;
  std::size_t ct = Lower(head).find("content-type:");
  if (ct != std::string::npos) {
    content_type = Lower(head.substr(ct, 80));
  }
  if (content_type.find("application/x-www-form-urlencoded") != std::string::npos) {
    request.form = ParseParams(body);
  } else if (!body.empty()) {
    request.form = ParseParams(body);
  }
  return request;
}

void SendResponse(SOCKET client, const Response& response) {
  std::ostringstream out;
  out << "HTTP/1.1 " << response.status << " "
      << (response.status == 200 ? "OK" : "ERROR") << "\r\n";
  out << "Content-Type: " << response.content_type << "\r\n";
  out << "Content-Length: " << response.body.size() << "\r\n";
  out << "Cache-Control: no-store\r\n";
  if (!response.extra_headers.empty()) {
    out << response.extra_headers;
  }
  out << "Connection: close\r\n\r\n";
  std::string head = out.str();
  send(client, head.c_str(), static_cast<int>(head.size()), 0);
  send(client, response.body.c_str(), static_cast<int>(response.body.size()), 0);
}

void OpenBrowser(const std::string& url) {
  ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

}  // namespace

int RunWebServer(const std::string& data_dir, int port, bool open_browser,
                 const std::string& web_root) {
  WSADATA wsa;
  if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
    std::printf("WSAStartup 失败\n");
    return 1;
  }
  WebApp app(data_dir, web_root);

  SOCKET server = INVALID_SOCKET;
  int actual_port = port;
  for (int attempt = 0; attempt < 8; ++attempt, ++actual_port) {
    server = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server == INVALID_SOCKET) {
      continue;
    }
    int reuse = 1;
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char*>(&reuse), sizeof(reuse));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = inet_addr("127.0.0.1");
    address.sin_port = htons(static_cast<unsigned short>(actual_port));
    if (bind(server, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0 &&
        listen(server, 16) == 0) {
      break;
    }
    closesocket(server);
    server = INVALID_SOCKET;
  }
  if (server == INVALID_SOCKET) {
    std::printf("无法监听端口 %d\n", port);
    WSACleanup();
    return 1;
  }

  std::string url = "http://127.0.0.1:" + std::to_string(actual_port) + "/";
  std::printf("校园二手交易系统已启动：%s\n", url.c_str());
  std::printf("按 Ctrl+C 结束。\n");
  if (open_browser) {
    OpenBrowser(url);
  }

  // 工作线程池：I/O 并发处理，业务状态由 WebApp::Handle 内的互斥量串行化。
  constexpr int kWorkers = 4;
  std::mutex queue_mutex;
  std::condition_variable queue_cv;
  std::deque<SOCKET> queue;
  bool stopping = false;

  auto worker = [&]() {
    for (;;) {
      SOCKET client = INVALID_SOCKET;
      {
        std::unique_lock<std::mutex> lock(queue_mutex);
        queue_cv.wait(lock, [&]() { return stopping || !queue.empty(); });
        if (queue.empty()) {
          if (stopping) {
            return;
          }
          continue;
        }
        client = queue.front();
        queue.pop_front();
      }
      std::string raw = ReadRequest(client);
      if (!raw.empty()) {
        Request request = ParseRequest(raw);
        Response response = app.Handle(request);
        SendResponse(client, response);
      }
      closesocket(client);
    }
  };
  std::vector<std::thread> workers;
  workers.reserve(kWorkers);
  for (int i = 0; i < kWorkers; ++i) {
    workers.emplace_back(worker);
  }

  while (true) {
    SOCKET client = accept(server, nullptr, nullptr);
    if (client == INVALID_SOCKET) {
      continue;
    }
    {
      std::lock_guard<std::mutex> lock(queue_mutex);
      queue.push_back(client);
    }
    queue_cv.notify_one();
  }

  closesocket(server);
  WSACleanup();
  return 0;
}

}  // namespace campus
