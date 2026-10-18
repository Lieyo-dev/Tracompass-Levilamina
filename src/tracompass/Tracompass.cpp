#include "tracompass/Tracompass.h"

#include "tracompass/Commands.h"
#include "tracompass/Data.h"
#include "tracompass/Tracking.h"

#include "ll/api/event/EventBus.h"
#include "ll/api/event/player/PlayerJoinEvent.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"
#include "ll/api/mod/RegisterHelper.h"

namespace tracompass {

Tracompass& Tracompass::getInstance() {
    static Tracompass instance;
    return instance;
}

bool Tracompass::load() { return true; }

bool Tracompass::enable() {
    auto dataDir = mSelf.getDataDir();
    data::load(dataDir);
    tracking::load(dataDir);

    // 直接注册命令（不监听 ServerCommandRegisterEvent）
    commands::registerCommands();

    auto& bus = ll::event::EventBus::getInstance();
    // 每 5 tick 刷新一次指针位置
    mTickListener = bus.emplaceListener<ll::event::ServerLevelTickEvent>([](ll::event::ServerLevelTickEvent&) {
        static int counter = 0;
        if (++counter % 5 == 0) tracking::tick();
    });
    // 玩家进服时清理已失效的追踪指针（例如离线期间被撤销了权限）
    mJoinListener = bus.emplaceListener<ll::event::PlayerJoinEvent>([](ll::event::PlayerJoinEvent& event) {
        tracking::purgeStale(event.self());
    });
    return true;
}

bool Tracompass::disable() {
    auto& bus = ll::event::EventBus::getInstance();
    if (mTickListener) bus.removeListener(mTickListener);
    if (mJoinListener) bus.removeListener(mJoinListener);
    mTickListener.reset();
    mJoinListener.reset();
    return true;
}

} // namespace tracompass

LL_REGISTER_MOD(tracompass::Tracompass, tracompass::Tracompass::getInstance());
