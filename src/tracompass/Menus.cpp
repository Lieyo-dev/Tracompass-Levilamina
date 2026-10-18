#include "tracompass/Menus.h"

#include "tracompass/Data.h"
#include "tracompass/Tracking.h"

#include "ll/api/form/CustomForm.h"
#include "ll/api/form/ModalForm.h"
#include "ll/api/form/SimpleForm.h"
#include "ll/api/service/Bedrock.h"

#include "mc/world/level/Level.h"

#include <algorithm>
#include <variant>

namespace tracompass::menu {

namespace {

using ll::form::CustomForm;
using ll::form::CustomFormResult;
using ll::form::FormCancelReason;
using ll::form::ModalForm;
using ll::form::ModalFormResult;
using ll::form::ModalFormSelectedButton;
using ll::form::SimpleForm;

void openRoot(Player& player);
void openAddTracker(Player& player);
void openTrackerList(Player& player);
void openTrackerMenu(Player& player, mce::UUID const& tracker);
void openAddTarget(Player& player, mce::UUID const& tracker);
void openTargetList(Player& player, mce::UUID const& tracker);
void openTrackMenu(Player& player);

std::vector<data::PlayerRef> onlinePlayers() {
    std::vector<data::PlayerRef> list;
    if (auto level = ll::service::getLevel(); level.has_value()) {
        level->forEachPlayer([&](Player& p) {
            list.push_back({p.getUuid(), p.getRealName()});
            return true;
        });
    }
    return list;
}

// 从自定义表单结果里取出下拉栏选中的下标
std::optional<size_t> dropdownIndex(CustomFormResult const& result, std::string const& name) {
    if (!result) return std::nullopt;
    auto it = result->find(name);
    if (it == result->end()) return std::nullopt;
    if (auto* idx = std::get_if<uint64>(&it->second)) return static_cast<size_t>(*idx);
    return std::nullopt;
}

bool requireOp(Player& player) {
    if (player.isOperator()) return true;
    player.sendMessage("§c需要 OP 权限");
    return false;
}

// ---------- 根菜单（OP） ----------
void openRoot(Player& player) {
    SimpleForm form("Tracompass");
    form.appendButton("添加追踪者", [](Player& p) {
        if (requireOp(p)) openAddTracker(p);
    });
    form.appendButton("追踪者列表", [](Player& p) {
        if (requireOp(p)) openTrackerList(p);
    });
    form.appendButton("追踪玩家", [](Player& p) { openTrackMenu(p); });
    form.sendTo(player);
}

// ---------- 添加追踪者 ----------
void openAddTracker(Player& player) {
    auto                     players = onlinePlayers();
    std::vector<std::string> names;
    for (auto& p : players) names.push_back(p.name);

    CustomForm form("添加追踪者");
    form.appendDropdown("player", "在线玩家", names);
    form.setSubmitButton("保存");
    form.sendTo(player, [players](Player& p, CustomFormResult const& result, FormCancelReason) {
        auto idx = dropdownIndex(result, "player");
        if (!idx || *idx >= players.size()) {
            openRoot(p);
            return;
        }
        if (!requireOp(p)) return;
        auto& chosen = players[*idx];
        if (data::addTracker(chosen)) {
            data::save();
            p.sendMessage("§a已添加追踪者 " + chosen.name);
        } else {
            p.sendMessage("§e追踪者 " + chosen.name + " 已存在");
        }
        openRoot(p);
    });
}

// ---------- 追踪者列表 ----------
void openTrackerList(Player& player) {
    SimpleForm form("追踪者列表");
    auto&      trackers = data::trackers();
    if (trackers.empty()) form.setContent("暂无追踪者");
    for (auto& t : trackers) {
        mce::UUID uuid = t.self.uuid;
        form.appendButton(
            t.self.name + "\n§7可追踪 " + std::to_string(t.targets.size()) + " 人",
            [uuid](Player& p) {
                if (requireOp(p)) openTrackerMenu(p, uuid);
            }
        );
    }
    form.appendButton("返回", [](Player& p) { openRoot(p); });
    form.sendTo(player);
}

void openTrackerMenu(Player& player, mce::UUID const& tracker) {
    auto* entry = data::findTracker(tracker);
    if (!entry) {
        openTrackerList(player);
        return;
    }
    SimpleForm form("追踪者: " + entry->self.name);
    form.appendButton("添加被追踪者", [tracker](Player& p) {
        if (requireOp(p)) openAddTarget(p, tracker);
    });
    form.appendButton("查看/删除被追踪者", [tracker](Player& p) {
        if (requireOp(p)) openTargetList(p, tracker);
    });
    form.appendButton("删除当前追踪者", [tracker](Player& p) {
        if (!requireOp(p)) return;
        auto* e = data::findTracker(tracker);
        if (!e) {
            openTrackerList(p);
            return;
        }
        std::string name = e->self.name;
        ModalForm   confirm("删除追踪者", "确定要删除追踪者 " + name + " 吗？\n其正在进行的追踪也会被结束。", "删除", "取消");
        confirm.sendTo(p, [tracker, name](Player& pl, ModalFormResult result, FormCancelReason) {
            if (result && *result == ModalFormSelectedButton::Upper && requireOp(pl)) {
                if (auto* e2 = data::findTracker(tracker)) {
                    auto targets = e2->targets; // 拷贝后再删除
                    for (auto& t : targets) tracking::revoke(tracker, t.uuid);
                }
                data::removeTracker(tracker);
                data::save();
                pl.sendMessage("§a已删除追踪者 " + name);
                openTrackerList(pl);
            } else {
                openTrackerMenu(pl, tracker);
            }
        });
    });
    form.appendButton("返回", [](Player& p) { openTrackerList(p); });
    form.sendTo(player);
}

// ---------- 添加被追踪者 ----------
void openAddTarget(Player& player, mce::UUID const& tracker) {
    auto* entry = data::findTracker(tracker);
    if (!entry) {
        openTrackerList(player);
        return;
    }
    // 在线玩家中，排除追踪者本人以及已被允许的
    std::vector<data::PlayerRef> candidates;
    for (auto& p : onlinePlayers()) {
        if (p.uuid == tracker || data::isAllowed(tracker, p.uuid)) continue;
        candidates.push_back(p);
    }
    if (candidates.empty()) {
        player.sendMessage("§e没有可添加的在线玩家");
        openTrackerMenu(player, tracker);
        return;
    }
    std::vector<std::string> names;
    for (auto& p : candidates) names.push_back(p.name);

    data::PlayerRef trackerRef = entry->self;
    CustomForm      form("添加被追踪者 - " + trackerRef.name);
    form.appendDropdown("player", "在线玩家", names);
    form.setSubmitButton("保存");
    form.sendTo(player, [trackerRef, candidates](Player& p, CustomFormResult const& result, FormCancelReason) {
        auto idx = dropdownIndex(result, "player");
        if (!idx || *idx >= candidates.size()) {
            openTrackerMenu(p, trackerRef.uuid);
            return;
        }
        if (!requireOp(p)) return;
        auto& chosen = candidates[*idx];
        if (data::allow(trackerRef, chosen)) {
            data::save();
            p.sendMessage("§a已允许 " + trackerRef.name + " 追踪 " + chosen.name);
        } else {
            p.sendMessage("§e已经允许过了");
        }
        openTrackerMenu(p, trackerRef.uuid);
    });
}

// ---------- 查看/删除被追踪者 ----------
void openTargetList(Player& player, mce::UUID const& tracker) {
    auto* entry = data::findTracker(tracker);
    if (!entry) {
        openTrackerList(player);
        return;
    }
    SimpleForm form("被追踪者 - " + entry->self.name, "点击被追踪者可将其删除");
    if (entry->targets.empty()) form.setContent("暂无被追踪者");
    for (auto& target : entry->targets) {
        data::PlayerRef ref = target;
        form.appendButton(ref.name, [tracker, ref](Player& p) {
            if (!requireOp(p)) return;
            ModalForm confirm("删除被追踪者", "确定要删除被追踪者 " + ref.name + " 吗？", "删除", "取消");
            confirm.sendTo(p, [tracker, ref](Player& pl, ModalFormResult result, FormCancelReason) {
                if (result && *result == ModalFormSelectedButton::Upper && requireOp(pl)) {
                    if (data::deny(tracker, ref.uuid)) {
                        tracking::revoke(tracker, ref.uuid);
                        data::save();
                        pl.sendMessage("§a已删除被追踪者 " + ref.name);
                    }
                }
                openTargetList(pl, tracker);
            });
        });
    }
    form.appendButton("返回", [tracker](Player& p) { openTrackerMenu(p, tracker); });
    form.sendTo(player);
}

// ---------- 追踪玩家 ----------
void openTrackMenu(Player& player) {
    struct Candidate {
        data::PlayerRef ref;
        bool            online;
    };
    std::vector<Candidate> candidates;
    auto                   contains = [&](mce::UUID const& u) {
        return std::any_of(candidates.begin(), candidates.end(), [&](auto& c) { return c.ref.uuid == u; });
    };
    // OP：所有在线玩家；非 OP：自己被允许追踪的在线玩家（均排除自己）
    for (auto& p : onlinePlayers()) {
        if (p.uuid == player.getUuid()) continue;
        if (player.isOperator() || data::isAllowed(player.getUuid(), p.uuid)) candidates.push_back({p, true});
    }
    // 正在追踪但已离线的目标，也列出来以便停止追踪
    if (auto* sessions = tracking::sessionsOf(player.getUuid())) {
        for (auto& s : *sessions)
            if (!contains(s.target)) candidates.push_back({{s.target, s.targetName}, false});
    }

    SimpleForm form("追踪玩家", "点击玩家开始追踪，再次点击已在追踪的玩家则停止追踪");
    if (candidates.empty()) form.setContent("当前没有可追踪的玩家");
    for (auto& c : candidates) {
        bool        isOn     = tracking::isTracking(player.getUuid(), c.ref.uuid);
        std::string text     = (isOn ? "§a[追踪中] §r" : "") + c.ref.name + (c.online ? "" : " §7(离线)");
        data::PlayerRef ref  = c.ref;
        form.appendButton(text, [ref](Player& p) {
            if (tracking::isTracking(p.getUuid(), ref.uuid)) {
                tracking::stop(p, ref.uuid);
                p.sendMessage("§e已停止追踪 " + ref.name);
            } else if (auto level = ll::service::getLevel(); level.has_value()) {
                if (auto* target = level->getPlayer(ref.uuid)) {
                    p.sendMessage(tracking::startMessage(tracking::start(p, *target), ref.name));
                } else {
                    p.sendMessage("§c玩家 " + ref.name + " 不在线");
                }
            }
            openTrackMenu(p);
        });
    }
    if (player.isOperator()) form.appendButton("返回", [](Player& p) { openRoot(p); });
    form.sendTo(player);
}

} // namespace

void openMain(Player& player) {
    if (player.isOperator()) openRoot(player);
    else openTrackMenu(player);
}

} // namespace tracompass::menu
