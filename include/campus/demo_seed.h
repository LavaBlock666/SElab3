// Copyright 2026 兰雨阳. All rights reserved.
// 演示数据：命令行与网页共用同一份英文演示数据，避免两边数据语言不一致。

#ifndef CAMPUS_DEMO_SEED_H_
#define CAMPUS_DEMO_SEED_H_

#include "campus/services.h"
#include "campus/store.h"

namespace campus {

// 分类或用户已存在时不重复写入；调用方负责保存各表。
void SeedDemoData(ICategoryRepository& categories, IUserRepository& users,
                  UserService& user_service, ItemService& item_service);

}  // namespace campus

#endif  // CAMPUS_DEMO_SEED_H_
