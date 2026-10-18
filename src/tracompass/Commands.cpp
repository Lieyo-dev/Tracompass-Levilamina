#include "tracompass/Commands.h"

#include "tracompass/Data.h"
#include "tracompass/Menus.h"
#include "tracompass/Tracking.h"

#include "ll/api/command/CommandHandle.h"
#include "ll/api/command/CommandRegistrar.h"
#include "ll/api/command/runtime/RuntimeCommand.h"
#include "ll/api/command/runtime/RuntimeOverload.h"

#include "mc/server/commands/CommandOrigin.h"
#include "mc/server/commands/CommandOutput.h"
#include "mc/server/commands/CommandPermissionLevel.h"
#include "mc/server/commands/CommandSelector.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/player/Player.h"

#include <vector>

namespace tracompass::commands {

namespace {

using ll::command::ParamKind;
using ll::command::RuntimeCommand;

Player* originPlayer(CommandOrigin const& origin, CommandOutput& output) {
    auto* entity = origin.getEntity();
    if (!entity || !entity->isType(ActorType::Player)) {
        output.error("该命令只能由玩家执行");
        return nullptr;
    }
    return static_cast<Player*>(entity);
}

bool requireOp(CommandOrigin const& origin, CommandOutput& output) {
    if (origin.getPermissionsLevel() >= CommandPermissionLevel::GameDirectors) return true;
    output.error("该命令需要 OP 权限");
    return false;
}

// 解析玩家选择器参数
std::vector<Player*> selectPlayers(RuntimeCommand const& self, char const* name, CommandOrigin const& origin) {
    std::vector<Player*> list;
    auto                 results = self[name].get<ParamKind::Player>().results(origin);
    for (auto* p : results) list.push_back(p);
    return list;
}

// 要求选择器恰好选中一名玩家
Player* selectOne(RuntimeCommand const& self, char const* name, CommandOrigin const& origin, CommandOutput& output) {
    auto list = selectPlayers(self, name, origin);
    if (list.empty()) {
        output.error("找不到指定的在线玩家");
        return nullptr;
    }
    if (list.size() > 1) {
        output.error("一次只能指定一名玩家");
        return nullptr;
    }
    return list.front();
}

} // namespace

void registerCommands() {
    static bool registered = false;
    if (registered) return;
    registered = true;

    auto& cmd = ll::command::CommandRegistrar::getInstance(false)
                    .getOrCreateCommand("tracompass", "Tracompass 追踪指南针", CommandPermissionLevel::Any);

    // /tracompass  打开菜单（OP：根菜单；非 OP：追踪玩家菜单）
    cmd.runtimeOverload().execute([](CommandOrigin const& origin, CommandOutput& output, RuntimeCommand const&) {
        if (auto* player = originPlayer(origin, output)) menu::openMain(*player);
    });

    // /tracompass allow <tracker> <target>  （需要 OP）
    cmd.runtimeOverload()
        .text("allow")
        .required("tracker", ParamKind::Player)
        .required("target", ParamKind::Player)
        .execute([](CommandOrigin const& origin, CommandOutput& output, RuntimeCommand const& self) {
            if (!requireOp(origin, output)) return;
            auto* tracker = selectOne(self, "tracker", origin, output);
            auto* target  = selectOne(self, "target", origin, output);
            if (!tracker || !target) return;
            if (tracker->getUuid() == target->getUuid()) {
                output.error("追踪者和被追踪者不能是同一个人");
                return;
            }
            if (data::allow({tracker->getUuid(), tracker->getRealName()}, {target->getUuid(), target->getRealName()})) {
                data::save();
                output.success("已允许 " + tracker->getRealName() + " 追踪 " + target->getRealName());
            } else {
                output.error("已经允许过了");
            }
        });

    // /tracompass deny <tracker> <target>  （需要 OP）
    cmd.runtimeOverload()
        .text("deny")
        .required("tracker", ParamKind::Player)
        .required("target", ParamKind::Player)
        .execute([](CommandOrigin const& origin, CommandOutput& output, RuntimeCommand const& self) {
            if (!requireOp(origin, output)) return;
            auto* tracker = selectOne(self, "tracker", origin, output);
            auto* target  = selectOne(self, "target", origin, output);
            if (!tracker || !target) return;
            if (data::deny(tracker->getUuid(), target->getUuid())) {
                tracking::revoke(tracker->getUuid(), target->getUuid());
                data::save();
                output.success("已取消 " + tracker->getRealName() + " 对 " + target->getRealName() + " 的追踪权限");
            } else {
                output.error(tracker->getRealName() + " 本来就不能追踪 " + target->getRealName());
            }
        });

    // /tracompass start <target>  可选中多名玩家，自己会被排除
    cmd.runtimeOverload()
        .text("start")
        .required("target", ParamKind::Player)
        .execute([](CommandOrigin const& origin, CommandOutput& output, RuntimeCommand const& self) {
            auto* hunter = originPlayer(origin, output);
            if (!hunter) return;
            auto targets = selectPlayers(self, "target", origin);
            if (targets.empty()) {
                output.error("找不到指定的在线玩家");
                return;
            }
            int ok = 0;
            for (auto* target : targets) {
                if (target->getUuid() == hunter->getUuid()) continue; // 排除自己
                auto result = tracking::start(*hunter, *target);
                if (result == tracking::StartResult::Ok) {
                    ++ok;
                    output.success(tracking::startMessage(result, target->getRealName()));
                } else {
                    output.error(tracking::startMessage(result, target->getRealName()));
                }
            }
            if (ok == 0 && targets.size() == 1 && targets.front()->getUuid() == hunter->getUuid()) {
                output.error("不能追踪自己");
            }
        });

    // /tracompass stop <target>
    cmd.runtimeOverload()
        .text("stop")
        .required("target", ParamKind::Player)
        .execute([](CommandOrigin const& origin, CommandOutput& output, RuntimeCommand const& self) {
            auto* hunter = originPlayer(origin, output);
            if (!hunter) return;
            for (auto* target : selectPlayers(self, "target", origin)) {
                if (tracking::stop(*hunter, target->getUuid())) {
                    output.success("已停止追踪 " + target->getRealName());
                } else {
                    output.error("你没有在追踪 " + target->getRealName());
                }
            }
        });

    // /tracompass stop  停止所有追踪（包括已离线的目标）
    cmd.runtimeOverload().text("stop").execute(
        [](CommandOrigin const& origin, CommandOutput& output, RuntimeCommand const&) {
            auto* hunter = originPlayer(origin, output);
            if (!hunter) return;
            int n = tracking::stopAll(*hunter);
            if (n > 0) output.success("已停止全部追踪（" + std::to_string(n) + " 个）");
            else output.error("你当前没有在追踪任何玩家");
        }
    );
}

} // namespace tracompass::commands
