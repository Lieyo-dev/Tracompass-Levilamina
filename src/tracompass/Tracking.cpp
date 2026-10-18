#include "tracompass/Tracking.h"

#include "tracompass/Data.h"

#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/ecs/gamerefs_entity/GameRefsEntity.h" // 补全 GameRefs<EntityId>，TrackingRecord 的 OwnerPtr<EntityId> 成员需要
#include "mc/deps/ecs/gamerefs_entity/OwnerStorageEntity.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/deps/nbt/IntTag.h"
#include "mc/network/MinecraftPackets.h"
#include "mc/network/packet/PositionTrackingDBServerBroadcastPacket.h"
#include "mc/safety/RedactableString.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/PositionTrackingId.h"
#include "mc/world/level/position_trackingdb/OperationBase.h"
#include "mc/world/level/position_trackingdb/TrackingRecord.h"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <unordered_map>

namespace tracompass::tracking {

namespace {

using Action = PositionTrackingDBServerBroadcastPacketPayload::Action;

std::unordered_map<mce::UUID, std::vector<Session>> gSessions; // 猎人 -> 会话（猎人离线/重进时保留）
uint                                                gNextHandle = 0x40000000; // 自定义 handle，避开原版记录
std::filesystem::path                               gFile;
constexpr char const*                               kMarkerKey = "tracompass";

// ---------- 数据包 ----------
// 磁石指针在客户端通过物品 NBT 的 trackingHandle 查本地 PositionTrackingDB。
// 只向猎人发送广播包，写入/更新/删除他专属 handle 的记录，不改动服务端真实数据库，也不影响其他玩家。
void sendTrackingPacket(Player& hunter, uint handle, Action action, BlockPos const& pos, int dim) {
    PositionTrackingId id;
    id.mRawId = handle;

    auto  base = MinecraftPackets::createPacket(MinecraftPacketIds::PositionTrackingDBServerBroadcast);
    auto& pkt  = static_cast<PositionTrackingDBServerBroadcastPacket&>(*base);
    pkt.mAction = action;
    *pkt.mId    = id;
    if (action == Action::Update) {
        // 用游戏自带的序列化生成记录数据，保证客户端能正确解析
        PositionTrackingDB::TrackingRecord record;
        *record.mId            = id;
        *record.mPosition      = pos;
        *record.mDimensionType = dim;
        record.mStatus         = PositionTrackingDB::TrackingRecord::RecordStatus::Ok;
        *pkt.mData             = record.serialize();
    }
    pkt.sendTo(hunter);
}

void sendUpdate(Player& hunter, Session const& session, Player& target) {
    sendTrackingPacket(hunter, session.handle, Action::Update, target.getFeetBlockPos(), (int)target.getDimensionId());
}

Player* findPlayer(mce::UUID const& uuid) {
    auto level = ll::service::getLevel();
    return level.has_value() ? level->getPlayer(uuid) : nullptr;
}

// ---------- 物品 ----------
bool isTracompassItem(ItemStack const& item) {
    return !item.isNull() && item.mUserData && item.mUserData->contains(kMarkerKey);
}

int handleOf(ItemStack const& item) {
    if (item.mUserData && item.mUserData->contains("trackingHandle", Tag::Type::Int))
        return item.mUserData->at("trackingHandle").get<IntTag>();
    return 0;
}

// 删除背包中满足条件的追踪指针（普通磁石指针/追溯指针不受影响）
template <class Pred>
int removeCompasses(Player& player, Pred&& pred) {
    auto& inv = player.getInventory();
    int   n   = 0;
    for (int i = 0; i < inv.getContainerSize(); ++i) {
        auto const& item = inv.getItem(i);
        if (isTracompassItem(item) && pred((uint)handleOf(item))) {
            inv.setItem(i, ItemStack::EMPTY_ITEM());
            ++n;
        }
    }
    if (n) player.sendInventory(false);
    return n;
}

// 追踪指针：死亡不掉落(keep_on_death)、锁定在物品栏内不可丢弃/放入容器(item_lock=2)
bool giveCompass(Player& player, uint handle, std::string const& targetName) {
    auto tag = CompoundTag::fromSnbt(
        "{Name:\"minecraft:lodestone_compass\",Count:1b,Damage:0s,"
        "tag:{trackingHandle:" + std::to_string((int)handle) + ",tracompass:1b,"
        "\"minecraft:keep_on_death\":1b,\"minecraft:item_lock\":2b}}"
    );
    if (!tag) return false;
    auto item = ItemStack::fromTag(*tag);
    item.setCustomName(Bedrock::Safety::RedactableString("§c追踪指南针 §7-> " + targetName));
    bool ok = player.add(item);
    player.sendInventory(false);
    return ok;
}

} // namespace

// ---------- 持久化 ----------
void load(std::filesystem::path const& dataDir) {
    gFile = dataDir / "sessions.txt";
    gSessions.clear();
    std::ifstream in(gFile);
    if (!in) return;
    std::string line;
    if (std::getline(in, line)) gNextHandle = (uint)std::strtoul(line.c_str(), nullptr, 10);
    if (gNextHandle < 0x40000000) gNextHandle = 0x40000000;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::vector<std::string> p;
        std::stringstream        ss(line);
        std::string              item;
        while (std::getline(ss, item, '\t')) p.push_back(item);
        // <hunter> <target> <handle> <targetName>
        if (p.size() == 4) {
            gSessions[mce::UUID::fromString(p[0])].push_back(
                {mce::UUID::fromString(p[1]), p[3], (uint)std::strtoul(p[2].c_str(), nullptr, 10)}
            );
        }
    }
}

void save() {
    std::error_code ec;
    std::filesystem::create_directories(gFile.parent_path(), ec);
    std::ofstream out(gFile, std::ios::trunc);
    out << gNextHandle << '\n';
    for (auto& [hunter, list] : gSessions)
        for (auto& s : list)
            out << hunter.asString() << '\t' << s.target.asString() << '\t' << s.handle << '\t' << s.targetName << '\n';
}

// ---------- 查询 ----------
std::vector<Session> const* sessionsOf(mce::UUID const& hunter) {
    auto it = gSessions.find(hunter);
    return it == gSessions.end() ? nullptr : &it->second;
}

bool isTracking(mce::UUID const& hunter, mce::UUID const& target) {
    auto* list = sessionsOf(hunter);
    if (!list) return false;
    return std::any_of(list->begin(), list->end(), [&](auto& s) { return s.target == target; });
}

bool canTrack(Player const& hunter, Player const& target) {
    if (hunter.getUuid() == target.getUuid()) return false;
    if (hunter.isOperator()) return true;
    return data::isAllowed(hunter.getUuid(), target.getUuid());
}

// ---------- 控制 ----------
StartResult start(Player& hunter, Player& target) {
    if (hunter.getUuid() == target.getUuid()) return StartResult::Self;
    if (!canTrack(hunter, target)) return StartResult::NotAllowed;
    if (isTracking(hunter.getUuid(), target.getUuid())) return StartResult::Already;

    uint handle = gNextHandle++;
    if (!giveCompass(hunter, handle, target.getRealName())) return StartResult::InventoryFull;
    Session session{target.getUuid(), target.getRealName(), handle};
    gSessions[hunter.getUuid()].push_back(session);
    save();
    sendUpdate(hunter, session, target);
    return StartResult::Ok;
}

std::string startMessage(StartResult result, std::string const& targetName) {
    switch (result) {
    case StartResult::Ok:
        return "§a已开始追踪 " + targetName;
    case StartResult::Self:
        return "§c不能追踪自己";
    case StartResult::NotAllowed:
        return "§c你没有权限追踪 " + targetName;
    case StartResult::Already:
        return "§e已经在追踪 " + targetName;
    case StartResult::InventoryFull:
        return "§c背包已满，无法获得追踪指南针";
    }
    return {};
}

bool stop(Player& hunter, mce::UUID const& target) {
    auto it = gSessions.find(hunter.getUuid());
    if (it == gSessions.end()) return false;
    auto& list = it->second;
    auto  s    = std::find_if(list.begin(), list.end(), [&](auto& x) { return x.target == target; });
    if (s == list.end()) return false;
    uint handle = s->handle;
    sendTrackingPacket(hunter, handle, Action::Destroy, BlockPos(0, 0, 0), 0);
    removeCompasses(hunter, [handle](uint h) { return h == handle; });
    list.erase(s);
    if (list.empty()) gSessions.erase(it);
    save();
    return true;
}

int stopAll(Player& hunter) {
    auto it = gSessions.find(hunter.getUuid());
    if (it == gSessions.end()) return 0;
    auto targets = it->second; // 拷贝，stop 会修改容器
    int  n       = 0;
    for (auto& s : targets)
        if (stop(hunter, s.target)) ++n;
    return n;
}

void revoke(mce::UUID const& hunter, mce::UUID const& target) {
    if (auto* player = findPlayer(hunter)) {
        stop(*player, target);
        return;
    }
    auto it = gSessions.find(hunter);
    if (it == gSessions.end()) return;
    std::erase_if(it->second, [&](auto& s) { return s.target == target; });
    if (it->second.empty()) gSessions.erase(it);
    save();
}

void revokeAll(mce::UUID const& hunter) {
    if (auto* player = findPlayer(hunter)) {
        stopAll(*player);
        return;
    }
    if (gSessions.erase(hunter)) save();
}

void purgeStale(Player& player) {
    auto* list = sessionsOf(player.getUuid());
    removeCompasses(player, [list](uint h) {
        if (!list) return true;
        return std::none_of(list->begin(), list->end(), [h](auto& s) { return s.handle == h; });
    });
}

void tick() {
    for (auto& [hunterUuid, list] : gSessions) {
        auto* hunter = findPlayer(hunterUuid);
        if (!hunter) continue; // 猎人离线：保留会话，重进后继续
        for (auto& s : list) {
            // 目标离线时保持客户端上一次收到的位置
            if (auto* target = findPlayer(s.target)) sendUpdate(*hunter, s, *target);
        }
    }
}

} // namespace tracompass::tracking
