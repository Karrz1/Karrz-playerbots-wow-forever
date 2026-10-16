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

#include "../../src/modules/PlayerbotV2/Bot/ThreatLimits.h"

using namespace Playerbot;

// The raid brain and the generic hold read one limit: a caster between 1.10 and 1.20 times the tank's threat used to be released by the
// brain and held for good by the generic gate.
TEST_CASE("Melee never releases above the tank's threat and casters keep a tenth of margin", "[ClassicThreatLimits]")
{
    CHECK(ThreatHardRatio(true)  == 1.0f);
    CHECK(ThreatHardRatio(false) == 1.10f);
    CHECK(ThreatHardRatio(false) > ThreatHardRatio(true));
}

// Raid pulls: the tank takes a while to run in, so the DPS open eight seconds after its first hit on a boss (the owner's Attack still releases at once).
// The generic hold, the raid brain's hold and every boss fight script read this one number.
TEST_CASE("A raid's DPS wait eight seconds after the tank's first hit on a boss", "[ClassicThreatLimits]")
{
    CHECK(kRaidBossOpenWaitMs == 8000u);
    // The "forgotten Attack" release (Playerbot.Raid.DpsWaitTimeoutMs) is the one that actually opened the live Lucifron fight at 6 s; it was
    // limited to six seconds in the config reader. A raid may now be set up to eight, a party stays at six.
    CHECK(kRaidDpsWaitTimeoutMaxMs == 8000u);
    CHECK(kPartyDpsWaitTimeoutMs == 6000u);
    CHECK(kRaidDpsWaitTimeoutMaxMs > kPartyDpsWaitTimeoutMs);
}
