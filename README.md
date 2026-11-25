# 校园二手交易系统（C++ 课程实现）

实验三：软件实现与构建。依据实验二的设计，用 C++17 实现一个命令行版校园二手交易系统。

## 构建与运行

依赖：MinGW-w64 g++ 14（或任意支持 C++17 的编译器）。

```bat
build.bat
build\campus_market.exe --data data
```

也可以使用 Make：

```bash
make          # 生成 build/campus_market.exe 与 build/campus_tests.exe
make test     # 只编译测试
build/campus_tests.exe
```

## 演示账号

| 角色 | 账号 | 密码 |
|---|---|---|
| 管理员 | admin@campus.edu | Admin@123 |
| 卖家 | seller@campus.edu | Seller@123 |
| 买家 | buyer@campus.edu | Buyer@123 |
| 买家 | buyer2@campus.edu | Buyer@123 |

首次运行且数据目录为空时会写入 5 个分类、4 个账号和 4 件已发布商品。

## 目录结构

```
include/campus/   领域模型、仓储接口、服务声明
src/              服务实现、文件仓储、控制台界面
tests/            单元测试（96 个断言）
UML/              类图、分层架构图与行为图（PlantUML）
data/             运行时数据（自动生成，不纳入版本库）
```

## 与实验二设计的差异

实验二按 Web 应用设计（Vue + Spring Boot + MySQL）。实验三改用 C++ 命令行实现，
属于任务书允许的“设计与实现不一致”，主要差异：

1. 界面由 Web 页面改为控制台菜单，保留游客/买家/卖家/管理员四类工作台；
2. 存储由 MySQL 改为以 0x1f 分隔的文本表，仓储接口保持不变，后续可替换；
3. 去掉对象存储与 Redis，图片字段改为描述文本；
4. 并发控制由数据库唯一索引改为服务内互斥量 + 仓储二次校验，语义一致。

## 关键实现

- 商品状态机集中在 `CanTransition()`，非法转换抛 `DomainError(kStateError)`；
- 预约支持幂等号：同一请求重发返回首次结果；
- 同一商品最多一条有效预约，见 `ReservationService::Reserve`；
- 密码加盐迭代哈希，连续 5 次失败锁定 15 分钟；
- 举报 24 小时内去重，判定成立时下架商品并作废有效预约；
- 操作日志只追加，统计口径排除已取消预约。
