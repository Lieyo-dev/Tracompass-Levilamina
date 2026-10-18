#pragma once

#include "mc/platform/UUID.h"

#include <filesystem>
#include <string>
#include <vector>

// 追踪者（Tracker）与其可追踪的被追踪者（Target）的授权数据，持久化到数据目录的 trackers.txt
namespace tracompass::data {

struct PlayerRef {
    mce::UUID   uuid;
    std::string name;
};

struct TrackerEntry {
    PlayerRef              self;
    std::vector<PlayerRef> targets;
};

void load(std::filesystem::path const& dataDir);
void save();

[[nodiscard]] std::vector<TrackerEntry> const& trackers();
[[nodiscard]] TrackerEntry*                    findTracker(mce::UUID const& uuid);

// 添加追踪者；已存在返回 false
bool addTracker(PlayerRef const& tracker);
// 删除追踪者；不存在返回 false
bool removeTracker(mce::UUID const& tracker);
// 允许 tracker 追踪 target（tracker 不存在时自动创建）；已允许返回 false
bool allow(PlayerRef const& tracker, PlayerRef const& target);
// 取消允许；不存在返回 false
bool deny(mce::UUID const& tracker, mce::UUID const& target);
[[nodiscard]] bool isAllowed(mce::UUID const& tracker, mce::UUID const& target);

} // namespace tracompass::data
