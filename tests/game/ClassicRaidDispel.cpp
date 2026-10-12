/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "tc_catch2.h"
#include <algorithm>

#include "../../src/modules/PlayerbotV2/Bot/Raid/RaidEncounters.h"

using namespace Playerbot::Raid;

// MC Garr 2026-10-10: "aura 19496 on 40 members" drove 330 Cleanse / Dispel Magic casts in one fight and emptied every healer.
TEST_CASE("Garr's Magma Shackles is never dispelled", "[ClassicRaidDispel]")
{
    RaidRules const* mc = RulesForMap(409);
    REQUIRE(mc != nullptr);
    CHECK(OwnsAura(409, 19496));    // the generic dispel plan leaves it alone
    for (Dispel const& rule : mc->dispel)
        CHECK(rule.aura != 19496);  // and the raid brain hands out no order for it
    CHECK(std::find(mc->noDispel.begin(), mc->noDispel.end(), 19496u) != mc->noDispel.end());
}

TEST_CASE("Lucifron's curse and Impending Doom are still dispelled", "[ClassicRaidDispel]")
{
    RaidRules const* mc = RulesForMap(409);
    REQUIRE(mc != nullptr);
    auto has = [&](uint32_t aura) { return std::any_of(mc->dispel.begin(), mc->dispel.end(), [&](Dispel const& d) { return d.aura == aura; }); };
    CHECK(has(19703));
    CHECK(has(19702));
}
