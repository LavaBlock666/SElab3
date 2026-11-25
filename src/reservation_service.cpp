// Copyright 2026 兰雨阳. All rights reserved.
// 预约交易服务：唯一有效预约、幂等提交、卖家确认与买家完成。
//
// 并发安全分两层：
//   1) 服务内互斥量串行化“检查状态 + 写入预约”，对应数据库中的行锁；
//   2) 仓储层 FindActiveByItem 再次确认，任何入口都不能产生第二条有效预约。

#include <vector>

#include "campus/services.h"
#include "service_util.h"

namespace campus {
namespace {

void RequireSameItemState(const Item& item, ItemStatus expected,
                          const std::string& message) {
  if (item.status != expected) {
    throw DomainError(ErrorCode::kStateError, message);
  }
}

}  // namespace

ReservationService::ReservationService(
    IUserRepository& users, IItemRepository& items,
    IReservationRepository& reservations,
    INotificationRepository& notifications, ILogRepository& logs, IClock& clock,
    IIdGenerator& ids)
    : users_(users),
      items_(items),
      reservations_(reservations),
      notifications_(notifications),
      logs_(logs),
      clock_(clock),
      ids_(ids) {}

Reservation ReservationService::Reserve(const std::string& buyer_id,
                                        const std::string& item_id,
                                        const std::string& request_id) {
  std::lock_guard<std::mutex> guard(mutex_);
  User buyer = MustFindUser(users_, buyer_id);
  MustHaveRole(buyer, Role::kBuyer);

  // 幂等：同一请求号直接返回第一次的结果。
  if (!request_id.empty()) {
    auto existing = reservations_.FindByRequestId(request_id);
    if (existing) {
      if (existing->buyer_id != buyer_id) {
        throw DomainError(ErrorCode::kConflict, "请求号已被其他用户使用");
      }
      return *existing;
    }
  }

  Item item = MustFindItem(items_, item_id);
  if (item.seller_id == buyer_id) {
    throw DomainError(ErrorCode::kConflict, "不能预约自己发布的商品");
  }
  if (item.status != ItemStatus::kPublished) {
    std::string reason = item.status == ItemStatus::kReserved
                             ? "该商品已被预约"
                             : "商品当前状态不可预约：" + ItemStatusLabel(item.status);
    throw DomainError(ErrorCode::kConflict, reason);
  }
  if (reservations_.FindActiveByItem(item_id)) {
    throw DomainError(ErrorCode::kConflict, "该商品已被预约");
  }

  Reservation reservation;
  reservation.id = ids_.Next("RS");
  reservation.item_id = item_id;
  reservation.buyer_id = buyer_id;
  reservation.status = ReservationStatus::kPending;
  reservation.request_id = request_id;
  reservation.created_at = clock_.Now();
  reservations_.Add(reservation);

  if (!CanTransition(item.status, ItemStatus::kReserved)) {
    throw DomainError(ErrorCode::kStateError, "商品状态不允许转为已预约");
  }
  item.status = ItemStatus::kReserved;
  item.updated_at = clock_.Now();
  items_.Update(item);

  internal::WriteLog(logs_, clock_, ids_, buyer_id, "预约商品", "预约",
                     reservation.id, item.title);
  internal::Notify(notifications_, clock_, ids_, item.seller_id, "新的预约",
                   "商品《" + item.title + "》收到新的预约请求，请及时确认");
  return reservation;
}

Reservation ReservationService::Cancel(const std::string& buyer_id,
                                       const std::string& reservation_id) {
  std::lock_guard<std::mutex> guard(mutex_);
  auto found = reservations_.FindById(reservation_id);
  if (!found) {
    throw DomainError(ErrorCode::kNotFound, "预约不存在");
  }
  Reservation reservation = *found;
  if (reservation.buyer_id != buyer_id) {
    throw DomainError(ErrorCode::kPermissionDenied, "只能取消自己的预约");
  }
  if (reservation.status == ReservationStatus::kConfirmed) {
    throw DomainError(ErrorCode::kStateError, "卖家已确认交易，不能取消");
  }
  if (reservation.status != ReservationStatus::kPending) {
    throw DomainError(ErrorCode::kStateError,
                      "当前预约状态不可取消：" +
                          ReservationStatusLabel(reservation.status));
  }
  reservation.status = ReservationStatus::kCanceled;
  reservation.finished_at = clock_.Now();
  reservations_.Update(reservation);

  Item item = MustFindItem(items_, reservation.item_id);
  if (CanTransition(item.status, ItemStatus::kPublished)) {
    item.status = ItemStatus::kPublished;
    item.updated_at = clock_.Now();
    items_.Update(item);
  }
  internal::WriteLog(logs_, clock_, ids_, buyer_id, "取消预约", "预约",
                     reservation.id, item.title);
  internal::Notify(notifications_, clock_, ids_, item.seller_id, "预约取消",
                   "商品《" + item.title + "》的预约已被买家取消");
  return reservation;
}

Reservation ReservationService::Confirm(const std::string& seller_id,
                                        const std::string& reservation_id) {
  std::lock_guard<std::mutex> guard(mutex_);
  auto found = reservations_.FindById(reservation_id);
  if (!found) {
    throw DomainError(ErrorCode::kNotFound, "预约不存在");
  }
  Reservation reservation = *found;
  Item item = MustFindItem(items_, reservation.item_id);
  if (item.seller_id != seller_id) {
    throw DomainError(ErrorCode::kPermissionDenied, "只能确认自己商品的预约");
  }
  RequireSameItemState(item, ItemStatus::kReserved, "商品不处于已预约状态");
  if (reservation.status != ReservationStatus::kPending) {
    throw DomainError(ErrorCode::kStateError, "该预约不是待确认状态");
  }

  reservation.status = ReservationStatus::kConfirmed;
  reservation.confirmed_at = clock_.Now();
  reservations_.Update(reservation);

  item.status = ItemStatus::kConfirmed;
  item.updated_at = clock_.Now();
  items_.Update(item);

  internal::WriteLog(logs_, clock_, ids_, seller_id, "确认交易", "预约",
                     reservation.id, item.title);
  internal::Notify(notifications_, clock_, ids_, reservation.buyer_id, "预约确认",
                   "卖家已确认商品《" + item.title + "》，请线下交付");
  return reservation;
}

Reservation ReservationService::Complete(const std::string& buyer_id,
                                         const std::string& reservation_id) {
  std::lock_guard<std::mutex> guard(mutex_);
  auto found = reservations_.FindById(reservation_id);
  if (!found) {
    throw DomainError(ErrorCode::kNotFound, "预约不存在");
  }
  Reservation reservation = *found;
  if (reservation.buyer_id != buyer_id) {
    throw DomainError(ErrorCode::kPermissionDenied, "只能确认自己的预约");
  }
  Item item = MustFindItem(items_, reservation.item_id);
  RequireSameItemState(item, ItemStatus::kConfirmed, "商品不处于已确认状态");
  if (reservation.status != ReservationStatus::kConfirmed) {
    throw DomainError(ErrorCode::kStateError, "该预约不是已确认状态");
  }

  reservation.status = ReservationStatus::kCompleted;
  reservation.finished_at = clock_.Now();
  reservations_.Update(reservation);

  item.status = ItemStatus::kCompleted;
  item.updated_at = clock_.Now();
  items_.Update(item);

  internal::WriteLog(logs_, clock_, ids_, buyer_id, "确认完成", "预约",
                     reservation.id, item.title);
  internal::Notify(notifications_, clock_, ids_, item.seller_id, "交易完成",
                   "商品《" + item.title + "》买家已确认完成");
  return reservation;
}

std::vector<Reservation> ReservationService::ListByBuyer(
    const std::string& buyer_id) const {
  return reservations_.FindByBuyer(buyer_id);
}

std::vector<Reservation> ReservationService::ListByItem(
    const std::string& item_id) const {
  return reservations_.FindByItem(item_id);
}

std::optional<Reservation> ReservationService::Find(
    const std::string& id) const {
  return reservations_.FindById(id);
}

}  // namespace campus
