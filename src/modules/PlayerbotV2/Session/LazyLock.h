// One-button warlock: `.playerbot lazy` (the add-on's "Lazy lock" key) casts the next spell of the rotation on the player's target.
#pragma once

#include "Define.h"
#include <string>

class Player;

namespace Playerbot {

// One press. Returns the text to show the player when the press failed in a way worth telling (empty when it cast, or when nothing
// was ready and the next press will try again).
std::string LazyLockPress(Player* player);

} // namespace Playerbot
