// Copyright 2026 兰雨阳. All rights reserved.
// 商品检索服务：多条件组合、排序、分页与详情浏览。

#include <algorithm>

#include "campus/services.h"

namespace campus {
namespace {

bool MatchesKeyword(const Item& item, const std::string& keyword) {
  if (keyword.empty()) {
    return true;
  }
  std::string needle = ToLower(keyword);
  return Contains(ToLower(item.title), needle) ||
         Contains(ToLower(item.description), needle);
}

}  // namespace

SearchService::SearchService(IItemRepository& items,
                             ICategoryRepository& categories)
    : items_(items), categories_(categories) {}

SearchResult SearchService::Search(const SearchQuery& query) {
  if (query.page < 1) {
    throw DomainError(ErrorCode::kInvalidArgument, "页码必须大于等于 1");
  }
  if (query.page_size < 1 || query.page_size > 50) {
    throw DomainError(ErrorCode::kInvalidArgument, "每页数量需在 1 到 50 之间");
  }
  if (query.min_price && query.max_price &&
      *query.min_price > *query.max_price) {
    throw DomainError(ErrorCode::kInvalidArgument, "价格下限不能大于上限");
  }
  if (!query.category_id.empty() && !categories_.FindById(query.category_id)) {
    throw DomainError(ErrorCode::kInvalidArgument, "分类不存在");
  }

  SearchResult result;
  result.page = query.page;
  result.page_size = query.page_size;

  std::vector<Item> matched;
  for (const auto& item : items_.All()) {
    // 默认只查已发布商品；指定状态时按指定状态过滤。
    ItemStatus expected = query.status.value_or(ItemStatus::kPublished);
    if (item.status != expected) {
      continue;
    }
    if (!query.category_id.empty() && item.category_id != query.category_id) {
      continue;
    }
    if (query.min_price && item.price < *query.min_price) {
      continue;
    }
    if (query.max_price && item.price > *query.max_price) {
      continue;
    }
    if (!MatchesKeyword(item, query.keyword)) {
      continue;
    }
    matched.push_back(item);
  }

  switch (query.sort) {
    case SearchQuery::Sort::kLatest:
      std::sort(matched.begin(), matched.end(),
                [](const Item& left, const Item& right) {
                  if (left.created_at != right.created_at) {
                    return left.created_at > right.created_at;
                  }
                  return left.id > right.id;
                });
      break;
    case SearchQuery::Sort::kPriceAsc:
      std::sort(matched.begin(), matched.end(),
                [](const Item& left, const Item& right) {
                  return left.price < right.price;
                });
      break;
    case SearchQuery::Sort::kPriceDesc:
      std::sort(matched.begin(), matched.end(),
                [](const Item& left, const Item& right) {
                  return left.price > right.price;
                });
      break;
  }

  result.total = static_cast<int>(matched.size());
  result.total_pages =
      result.total == 0 ? 0 : (result.total + query.page_size - 1) / query.page_size;
  int begin = (query.page - 1) * query.page_size;
  if (begin < result.total) {
    int end = std::min(begin + query.page_size, result.total);
    result.items.assign(matched.begin() + begin, matched.begin() + end);
  }
  return result;
}

Item SearchService::ViewDetail(const std::string& item_id) {
  Item item = MustFindItem(items_, item_id);
  item.view_count += 1;
  items_.Update(item);
  return item;
}

}  // namespace campus
