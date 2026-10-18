#pragma once

#include "mc/platform/UUID.h"
#include "mc/world/actor/player/Player.h"

#include <filesystem>
#include <string>
#include <vector>

// 追踪会话：一个猎人（追踪者）可同时追踪多个目标，每个目标对应一个专属磁石指针
namespace tracompass::tracking {

struct Session {
    mce::UUID   target;
    std::string targetName;
    uint        handle; // 该磁石指针专属的 trackingHandle
};

void load(std::filesystem::path const& dataDir);
void save();

[[nodiscard]] std::vector<Session> const* sessionsOf(mce::UUID const& hunter);
[[nodiscard]] bool                        isTracking(mce::UUID const& hunter, mce::UUID const& target);

// OP 可追踪任何人；非 OP 需要被允许。不能追踪自己
[[nodiscard]] bool canTrack(Player const& hunter, Player const& target);

enum class StartResult { Ok, Self, NotAllowed, Already, InventoryFull };
StartResult start(Player& hunter, Player& target);
// 把 start 的结果转成给玩家看的提示
[[nodiscard]] std::string startMessage(StartResult result, std::string const& targetName);

// 停止追踪某个目标并销毁对应指针；未在追踪返回 false
bool stop(Player& hunter, mce::UUID const& target);
// 停止所有追踪，返回停止数量
int stopAll(Player& hunter);

// 撤销 hunter 对 target 的追踪（hunter 离线时仅删除记录，指针在其下次进服时清理）
void revoke(mce::UUID const& hunter, mce::UUID const& target);
void revokeAll(mce::UUID const& hunter);

// 玩家进服时：清理不属于任何有效会话的追踪指针
void purgeStale(Player& player);

// 每 5 tick 调用：向在线猎人刷新指针位置
void tick();

} // namespace tracompass::tracking
