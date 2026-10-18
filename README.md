# Tracompass

猎人游戏追踪指南针（LeviLamina，无 addon，磁石指针方案）。

## 菜单 `/tracompass`（权限 Any）
- OP：打开根菜单，含“添加追踪者”“追踪者列表”“追踪玩家”。
  - 添加追踪者：下拉选择在线玩家，点“保存”。
  - 追踪者列表：点击某个追踪者 → 添加被追踪者 / 查看、删除被追踪者 / 删除当前追踪者。
  - 追踪玩家：可追踪所有在线玩家。
- 非 OP：直接打开“追踪玩家”，列出自己被允许追踪的在线玩家。点击玩家开始追踪，再次点击停止追踪。

## 命令
- `/tracompass allow <tracker> <target>`（需 OP）：允许 tracker 追踪 target
- `/tracompass deny <tracker> <target>`（需 OP）：取消允许（同时结束 tracker 对 target 的追踪）
- `/tracompass start <target>`：开始追踪；可同时追踪多个目标，自己会被排除
- `/tracompass stop <target>`：停止追踪某个目标
- `/tracompass stop`：停止全部追踪

## 说明
- 每个被追踪目标对应一个独立的磁石指针：死亡不掉落、锁定在物品栏内不可丢弃，只能通过停止追踪销毁。普通磁石指针/追溯指针不受影响。
- 追踪关系保存在数据目录的 `sessions.txt`，追踪者授权保存在 `trackers.txt`，下线重进/服务器重启后继续追踪。
- 目标与猎人不在同一维度时，指针乱转。

## 原理
磁石指针在客户端读取物品 NBT 的 `trackingHandle`，再到本地 `PositionTrackingDB` 查记录。
每个追踪分配一个专属 handle（从 0x40000000 起），插件每 5 tick 只向猎人发送
`PositionTrackingDBServerBroadcastPacket(Update)`，记录数据由 `TrackingRecord::serialize()` 生成。
服务端真实数据库不被改动，也不影响其他玩家。

## 构建
```
xmake f -m release -y
xmake
```
产物在 `bin/Tracompass/`，整个目录放到服务器 `plugins/` 下。
