// Copyright 2026 兰雨阳. All rights reserved.
// 举报处理服务：24 小时去重、管理员判定与下架联动。

#include <vector>

#include "campus/services.h"
#include "service_util.h"

namespace campus {

ReportService::ReportService(IUserRepository& users, IItemRepository& items,
                             IReservationRepository& reservations,
                             IReportRepository& reports,
                             INotificationRepository& notifications,
                             ILogRepository& logs, IClock& clock,
                             IIdGenerator& ids)
    : users_(users),
      items_(items),
      reservations_(reservations),
      reports_(reports),
      notifications_(notifications),
      logs_(logs),
      clock_(clock),
      ids_(ids) {}

Report ReportService::Submit(const std::string& reporter_id,
                             const std::string& item_id, ReportReason reason,
                             const std::string& description) {
  User reporter = MustFindUser(users_, reporter_id);
  Item item = MustFindItem(items_, item_id);
  if (item.seller_id == reporter_id) {
    throw DomainError(ErrorCode::kConflict, "不能举报自己发布的商品");
  }
  if (item.status == ItemStatus::kOffline) {
    throw DomainError(ErrorCode::kStateError, "商品已下架，无需举报");
  }
  if (Trim(description).empty()) {
    throw DomainError(ErrorCode::kInvalidArgument, "请填写举报说明");
  }
  if (Trim(description).size() > 300) {
    throw DomainError(ErrorCode::kInvalidArgument, "举报说明不能超过 300 个字符");
  }

  Timestamp now = clock_.Now();
  for (const auto& existing : reports_.All()) {
    if (existing.reporter_id == reporter_id && existing.item_id == item_id &&
        now - existing.created_at < kReportWindowSeconds) {
      // 24 小时内重复举报：返回第一次的结果，不新增记录。
      return existing;
    }
  }

  Report report;
  report.id = ids_.Next("RP");
  report.item_id = item_id;
  report.reporter_id = reporter_id;
  report.reason = reason;
  report.description = Trim(description);
  report.status = ReportStatus::kPending;
  report.created_at = now;
  reports_.Add(report);

  internal::WriteLog(logs_, clock_, ids_, reporter_id, "提交举报", "举报",
                     report.id, ReportReasonLabel(reason));
  return report;
}

Report ReportService::Handle(const std::string& admin_id,
                             const std::string& report_id, bool uphold,
                             const std::string& note) {
  User admin = MustFindUser(users_, admin_id);
  MustHaveRole(admin, Role::kAdmin);
  auto found = reports_.FindById(report_id);
  if (!found) {
    throw DomainError(ErrorCode::kNotFound, "举报不存在");
  }
  Report report = *found;
  if (report.status != ReportStatus::kPending) {
    throw DomainError(ErrorCode::kStateError, "该举报已处理，不能重复处理");
  }

  Timestamp now = clock_.Now();
  report.handler_id = admin_id;
  report.handled_at = now;
  report.result = Trim(note);

  if (!uphold) {
    report.status = ReportStatus::kRejected;
    reports_.Update(report);
    internal::WriteLog(logs_, clock_, ids_, admin_id, "举报驳回", "举报",
                       report.id, report.result);
    internal::Notify(notifications_, clock_, ids_, report.reporter_id, "举报结果",
                     "您对商品 " + report.item_id + " 的举报未成立");
    return report;
  }

  report.status = ReportStatus::kUpheld;
  auto item = items_.FindById(report.item_id);
  if (item && item->status != ItemStatus::kOffline) {
    if (CanTransition(item->status, ItemStatus::kOffline)) {
      auto active = reservations_.FindActiveByItem(item->id);
      if (active) {
        Reservation reservation = *active;
        reservation.status = ReservationStatus::kVoided;
        reservation.finished_at = now;
        reservations_.Update(reservation);
        internal::Notify(notifications_, clock_, ids_, reservation.buyer_id,
                         "预约作废", "商品《" + item->title + "》因举报被下架，预约作废");
      }
      Item updated = *item;
      updated.status = ItemStatus::kOffline;
      updated.updated_at = now;
      items_.Update(updated);
      internal::Notify(notifications_, clock_, ids_, updated.seller_id,
                       "商品下架", "商品《" + updated.title + "》因举报成立被下架");
    } else {
      report.result += "（商品已进入交易流程，仅记录处理结论）";
    }
  }
  reports_.Update(report);
  internal::WriteLog(logs_, clock_, ids_, admin_id, "举报成立", "举报", report.id,
                     report.result);
  internal::Notify(notifications_, clock_, ids_, report.reporter_id, "举报结果",
                   "您对商品 " + report.item_id + " 的举报已成立");
  return report;
}

std::vector<Report> ReportService::ListPending() const {
  std::vector<Report> result;
  for (const auto& report : reports_.All()) {
    if (report.status == ReportStatus::kPending) {
      result.push_back(report);
    }
  }
  return result;
}

std::vector<Report> ReportService::ListByItem(const std::string& item_id) const {
  return reports_.FindByItem(item_id);
}

std::optional<Report> ReportService::Find(const std::string& id) const {
  return reports_.FindById(id);
}

}  // namespace campus
