// Copyright 2026 兰雨阳. All rights reserved.
// 商品管理服务：草稿、提交审核、审核、改价、下架。
//
// 商品状态的每次变化都经过 CanTransition() 校验，并落一条操作日志；
// 下架时联动作废有效预约，避免出现“商品已下架但预约仍有效”的悬挂数据。

#include <algorithm>
#include <vector>

#include "campus/services.h"
#include "service_util.h"

namespace campus {
namespace {

void ValidateContent(const std::string& title, const std::string& description,
                     Money price) {
  internal::RequireText(title, 2, 60, "标题长度需在 2 到 60 个字符之间");
  if (Utf8Length(Trim(description)) > 500) {
    throw DomainError(ErrorCode::kInvalidArgument, "描述不能超过 500 个字符");
  }
  if (price <= 0) {
    throw DomainError(ErrorCode::kInvalidArgument, "价格必须大于 0");
  }
  if (price > kMaxPrice) {
    throw DomainError(ErrorCode::kInvalidArgument, "价格超出上限");
  }
}

}  // namespace

ItemService::ItemService(IUserRepository& users, IItemRepository& items,
                         ICategoryRepository& categories,
                         IReservationRepository& reservations,
                         INotificationRepository& notifications,
                         ILogRepository& logs, IClock& clock, IIdGenerator& ids)
    : users_(users),
      items_(items),
      categories_(categories),
      reservations_(reservations),
      notifications_(notifications),
      logs_(logs),
      clock_(clock),
      ids_(ids) {}

Item ItemService::CreateDraft(const std::string& seller_id,
                              const std::string& category_id,
                              const std::string& title,
                              const std::string& description, Money price) {
  User seller = MustFindUser(users_, seller_id);
  MustHaveRole(seller, Role::kSeller);
  if (!categories_.FindById(category_id)) {
    throw DomainError(ErrorCode::kInvalidArgument, "分类不存在");
  }
  ValidateContent(title, description, price);

  Item item;
  item.id = ids_.Next("IT");
  item.seller_id = seller_id;
  item.category_id = category_id;
  item.title = Trim(title);
  item.description = Trim(description);
  item.price = price;
  item.status = ItemStatus::kDraft;
  item.created_at = clock_.Now();
  item.updated_at = item.created_at;
  items_.Add(item);
  internal::WriteLog(logs_, clock_, ids_, seller_id, "创建草稿", "商品", item.id,
                     item.title);
  return item;
}

Item ItemService::UpdateDraft(const std::string& seller_id,
                              const std::string& item_id,
                              const std::string& title,
                              const std::string& description,
                              const std::string& category_id, Money price) {
  Item item = MustFindItem(items_, item_id);
  if (item.seller_id != seller_id) {
    throw DomainError(ErrorCode::kPermissionDenied, "只能修改自己的商品");
  }
  if (item.status != ItemStatus::kDraft) {
    throw DomainError(ErrorCode::kStateError, "只有草稿状态的商品可以编辑");
  }
  if (!categories_.FindById(category_id)) {
    throw DomainError(ErrorCode::kInvalidArgument, "分类不存在");
  }
  ValidateContent(title, description, price);
  item.title = Trim(title);
  item.description = Trim(description);
  item.category_id = category_id;
  item.price = price;
  item.updated_at = clock_.Now();
  items_.Update(item);
  return item;
}

Item ItemService::SubmitForReview(const std::string& seller_id,
                                  const std::string& item_id) {
  Item item = MustFindItem(items_, item_id);
  if (item.seller_id != seller_id) {
    throw DomainError(ErrorCode::kPermissionDenied, "只能提交自己的商品");
  }
  // 重复提交：已经是待审核时直接返回原结果，不产生第二条待审记录。
  if (item.status == ItemStatus::kPending) {
    return item;
  }
  return Transition(item_id, ItemStatus::kPending, seller_id, "提交审核");
}

Item ItemService::Approve(const std::string& admin_id,
                          const std::string& item_id) {
  User admin = MustFindUser(users_, admin_id);
  MustHaveRole(admin, Role::kAdmin);
  Item item = MustFindItem(items_, item_id);
  if (item.status != ItemStatus::kPending) {
    throw DomainError(ErrorCode::kStateError, "只有待审核商品可以审核");
  }
  item.reject_reason.clear();
  items_.Update(item);
  Item result = Transition(item_id, ItemStatus::kPublished, admin_id, "审核通过");
  internal::Notify(notifications_, clock_, ids_, result.seller_id, "审核结果",
                   "商品《" + result.title + "》审核通过，已发布");
  return result;
}

Item ItemService::Reject(const std::string& admin_id,
                         const std::string& item_id,
                         const std::string& reason) {
  if (Trim(reason).empty()) {
    throw DomainError(ErrorCode::kInvalidArgument, "驳回必须填写理由");
  }
  User admin = MustFindUser(users_, admin_id);
  MustHaveRole(admin, Role::kAdmin);
  Item item = MustFindItem(items_, item_id);
  if (item.status != ItemStatus::kPending) {
    throw DomainError(ErrorCode::kStateError, "只有待审核商品可以驳回");
  }
  item.reject_reason = Trim(reason);
  items_.Update(item);
  Item result = Transition(item_id, ItemStatus::kDraft, admin_id,
                           "审核驳回：" + item.reject_reason);
  internal::Notify(notifications_, clock_, ids_, result.seller_id, "审核结果",
                   "商品《" + result.title + "》被驳回：" + item.reject_reason);
  return result;
}

Item ItemService::UpdatePublished(const std::string& seller_id,
                                  const std::string& item_id,
                                  const std::string& title,
                                  const std::string& description) {
  Item item = MustFindItem(items_, item_id);
  if (item.seller_id != seller_id) {
    throw DomainError(ErrorCode::kPermissionDenied, "只能修改自己的商品");
  }
  if (item.status != ItemStatus::kPublished) {
    throw DomainError(ErrorCode::kStateError, "只有已发布商品可以修改描述");
  }
  internal::RequireText(title, 2, 60, "标题长度需在 2 到 60 个字符之间");
  if (Utf8Length(Trim(description)) > 500) {
    throw DomainError(ErrorCode::kInvalidArgument, "描述不能超过 500 个字符");
  }
  item.title = Trim(title);
  item.description = Trim(description);
  item.updated_at = clock_.Now();
  items_.Update(item);
  internal::WriteLog(logs_, clock_, ids_, seller_id, "修改商品", "商品", item.id,
                     item.title);
  return item;
}

Item ItemService::ChangePrice(const std::string& seller_id,
                              const std::string& item_id, Money new_price) {
  Item item = MustFindItem(items_, item_id);
  if (item.seller_id != seller_id) {
    throw DomainError(ErrorCode::kPermissionDenied, "只能修改自己的商品");
  }
  if (new_price <= 0 || new_price > kMaxPrice) {
    throw DomainError(ErrorCode::kInvalidArgument, "价格不合法");
  }
  if (item.status == ItemStatus::kReserved) {
    throw DomainError(ErrorCode::kStateError, "已预约商品不能改价，请先取消预约");
  }
  if (item.status == ItemStatus::kCompleted ||
      item.status == ItemStatus::kOffline) {
    throw DomainError(ErrorCode::kStateError, "该状态的商品不能改价");
  }
  item.price = new_price;
  item.updated_at = clock_.Now();
  items_.Update(item);
  if (item.status == ItemStatus::kPublished) {
    // 改价需要重新审核。
    return Transition(item_id, ItemStatus::kPending, seller_id,
                      "改价后重新提交审核");
  }
  internal::WriteLog(logs_, clock_, ids_, seller_id, "修改价格", "商品", item.id,
                     MoneyToString(new_price));
  return item;
}

Item ItemService::TakeOffline(const std::string& seller_id,
                              const std::string& item_id,
                              const std::string& reason) {
  Item item = MustFindItem(items_, item_id);
  if (item.seller_id != seller_id) {
    throw DomainError(ErrorCode::kPermissionDenied, "只能下架自己的商品");
  }
  if (item.status != ItemStatus::kPublished &&
      item.status != ItemStatus::kReserved) {
    throw DomainError(ErrorCode::kStateError, "当前状态不能下架");
  }
  VoidActiveReservation(item_id, "商品被卖家下架");
  Item result = Transition(item_id, ItemStatus::kOffline, seller_id,
                           "下架：" + (reason.empty() ? "未填写原因" : reason));
  return result;
}

Item ItemService::SetImage(const std::string& seller_id,
                           const std::string& item_id,
                           const std::string& url) {
  Item item = MustFindItem(items_, item_id);
  if (item.seller_id != seller_id) {
    throw DomainError(ErrorCode::kPermissionDenied, "只能修改自己的商品");
  }
  if (IsTerminal(item.status)) {
    throw DomainError(ErrorCode::kStateError, "已结束的商品不能修改图片");
  }
  item.image = url;
  item.updated_at = clock_.Now();
  items_.Update(item);
  internal::WriteLog(logs_, clock_, ids_, seller_id, "修改图片", "商品", item.id,
                     url.empty() ? "清除图片" : "更新图片");
  return item;
}
std::vector<Item> ItemService::ListBySeller(const std::string& seller_id) const {
  return items_.FindBySeller(seller_id);
}

std::vector<Item> ItemService::ListByStatus(ItemStatus status) const {
  std::vector<Item> result;
  for (const auto& item : items_.All()) {
    if (item.status == status) {
      result.push_back(item);
    }
  }
  return result;
}

std::optional<Item> ItemService::Find(const std::string& item_id) const {
  return items_.FindById(item_id);
}

Item ItemService::Transition(const std::string& item_id, ItemStatus target,
                             const std::string& operator_id,
                             const std::string& detail) {
  Item item = MustFindItem(items_, item_id);
  if (!CanTransition(item.status, target)) {
    throw DomainError(ErrorCode::kStateError,
                      "非法状态转换：" + ItemStatusLabel(item.status) + " → " +
                          ItemStatusLabel(target));
  }
  item.status = target;
  item.updated_at = clock_.Now();
  items_.Update(item);
  internal::WriteLog(logs_, clock_, ids_, operator_id, detail, "商品", item.id,
                     ItemStatusLabel(target));
  return item;
}

void ItemService::VoidActiveReservation(const std::string& item_id,
                                        const std::string& reason) {
  auto active = reservations_.FindActiveByItem(item_id);
  if (!active) {
    return;
  }
  Reservation reservation = *active;
  reservation.status = ReservationStatus::kVoided;
  reservation.finished_at = clock_.Now();
  reservations_.Update(reservation);
  internal::Notify(notifications_, clock_, ids_, reservation.buyer_id, "预约作废",
                   "您在商品 " + item_id + " 上的预约已作废：" + reason);
  internal::WriteLog(logs_, clock_, ids_, reservation.buyer_id, "预约作废",
                     "预约", reservation.id, reason);
}

}  // namespace campus
