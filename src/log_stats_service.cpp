// Copyright 2026 兰雨阳. All rights reserved.
// 操作日志与统计服务。

#include <algorithm>
#include <unordered_map>
#include <set>

#include "campus/services.h"

namespace campus {

LogService::LogService(ILogRepository& logs, IClock& clock, IIdGenerator& ids)
    : logs_(logs), clock_(clock), ids_(ids) {}

void LogService::Record(const std::string& operator_id,
                        const std::string& action,
                        const std::string& target_type,
                        const std::string& target_id,
                        const std::string& detail) {
  OperationLog log;
  log.id = ids_.Next("LG");
  log.operator_id = operator_id;
  log.action = action;
  log.target_type = target_type;
  log.target_id = target_id;
  log.detail = detail;
  log.created_at = clock_.Now();
  logs_.Add(log);
}

std::vector<OperationLog> LogService::Query(const LogFilter& filter) const {
  std::vector<OperationLog> result;
  for (const auto& log : logs_.All()) {
    if (filter.operator_id && log.operator_id != *filter.operator_id) {
      continue;
    }
    if (filter.target_type && log.target_type != *filter.target_type) {
      continue;
    }
    if (filter.from && log.created_at < *filter.from) {
      continue;
    }
    if (filter.to && log.created_at > *filter.to) {
      continue;
    }
    result.push_back(log);
  }
  std::sort(result.begin(), result.end(),
            [](const OperationLog& left, const OperationLog& right) {
              if (left.created_at != right.created_at) {
                return left.created_at > right.created_at;
              }
              return left.id > right.id;
            });
  if (filter.limit > 0 && result.size() > filter.limit) {
    result.resize(filter.limit);
  }
  return result;
}

StatsService::StatsService(IUserRepository& users, IItemRepository& items,
                           ICategoryRepository& categories,
                           IReservationRepository& reservations,
                           IReportRepository& reports)
    : users_(users),
      items_(items),
      categories_(categories),
      reservations_(reservations),
      reports_(reports) {}

StatsReport StatsService::Overview(Timestamp from, Timestamp to) const {
  if (from > to) {
    throw DomainError(ErrorCode::kInvalidArgument, "起始时间不能晚于结束时间");
  }
  StatsReport report;
  report.from = from;
  report.to = to;

  std::unordered_map<std::string, std::string> category_names;
  for (const auto& category : categories_.All()) {
    category_names[category.id] = category.name;
  }

  for (const auto& item : items_.All()) {
    std::string name = category_names.count(item.category_id) > 0
                           ? category_names[item.category_id]
                           : "未分类";
    report.items_by_category[name] += 1;
    report.items_by_status[item.status] += 1;
    if (item.created_at >= from && item.created_at <= to) {
      report.new_items += 1;
    }
  }

  for (const auto& reservation : reservations_.All()) {
    if (reservation.created_at < from || reservation.created_at > to) {
      continue;
    }
    // 统计口径：已取消、已作废的预约不计入预约成功。
    if (reservation.status != ReservationStatus::kCanceled &&
        reservation.status != ReservationStatus::kVoided) {
      report.successful_reservations += 1;
    }
  }

  for (const auto& item : reports_.All()) {
    if (item.status == ReportStatus::kPending) {
      report.pending_reports += 1;
    }
  }

  for (const auto& user : users_.All()) {
    if (user.status == UserStatus::kActive && user.created_at <= to) {
      report.active_users += 1;
    }
  }
  return report;
}

SellerStats StatsService::ForSeller(const std::string& seller_id) const {
  SellerStats stats;
  std::set<std::string> item_ids;
  for (const auto& item : items_.FindBySeller(seller_id)) {
    item_ids.insert(item.id);
    stats.total_views += item.view_count;
    switch (item.status) {
      case ItemStatus::kPending:
        stats.pending += 1;
        break;
      case ItemStatus::kReserved:
      case ItemStatus::kConfirmed:
        stats.reserved += 1;
        break;
      case ItemStatus::kCompleted:
        stats.completed += 1;
        break;
      case ItemStatus::kPublished:
        stats.published += 1;
        break;
      default:
        break;
    }
  }
  for (const auto& reservation : reservations_.All()) {
    if (item_ids.count(reservation.item_id) > 0) {
      stats.total_reservations += 1;
    }
  }
  return stats;
}

}  // namespace campus
