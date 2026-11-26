// Copyright 2026 兰雨阳. All rights reserved.
// 统一的演示数据（英文，CLI 与 Web 共用）。

#include "campus/demo_seed.h"

#include <string>
#include <vector>

namespace campus {

void SeedDemoData(ICategoryRepository& categories, IUserRepository& users,
                  UserService& user_service, ItemService& item_service) {
  if (!categories.All().empty()) {
    return;
  }
  const char* names[] = {"Books", "Electronics", "Daily goods", "Sports",
                         "Others"};
  for (int i = 0; i < 5; ++i) {
    Category category;
    category.id = "CT-" + std::to_string(i + 1);
    category.name = names[i];
    categories.Add(category);
  }
  if (!users.All().empty()) {
    return;
  }

  User admin;
  User seller;
  try {
    admin = user_service.CreateAdmin("admin@campus.edu", "13800000001",
                                     "Admin@123");
    seller = user_service.Register("seller@campus.edu", "13800000002",
                                   "Seller@123", true);
    user_service.Register("buyer@campus.edu", "13800000003", "Buyer@123", false);
    user_service.Register("buyer2@campus.edu", "13800000004", "Buyer@123",
                          false);
  } catch (const DomainError&) {
    return;
  }

  auto all_categories = categories.All();
  struct DemoItem {
    int category_index;
    const char* title;
    const char* description;
    Money price;
  };
  const DemoItem demo[] = {
      {0, "Calculus Textbook 7th Edition",
       "Used for one semester, notes included, no missing pages.", 2500},
      {1, "27-inch 2K Monitor",
       "Two years old, no dead pixels, original stand included.", 62000},
      {3, "Mountain Bike", "About 300 km ridden, well maintained.", 48000},
      {2, "Storage Box Set", "Brand new, left over from moving out.", 3500}};
  for (const auto& entry : demo) {
    try {
      Item item = item_service.CreateDraft(
          seller.id, all_categories[entry.category_index].id, entry.title,
          entry.description, entry.price);
      item_service.SubmitForReview(seller.id, item.id);
      item_service.Approve(admin.id, item.id);
    } catch (const DomainError&) {
      // 单条失败不影响其余演示数据。
    }
  }
}

}  // namespace campus
