#pragma once

#include "mc/world/actor/player/Player.h"

namespace tracompass::menu {

// OP：打开 Tracompass 根菜单；非 OP：直接打开“追踪玩家”菜单
void openMain(Player& player);

} // namespace tracompass::menu
