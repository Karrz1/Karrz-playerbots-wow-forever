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

namespace
{
constexpr uint32_t kRagnaros = 11502;
RaidRules const& Mc()
{
    RaidRules const* r = RulesForMap(409);
    REQUIRE(r != nullptr);
    return *r;
}
}

// MC 2026-10-12 (c8456ad0): Wrath of Ragnaros (20566) runs ResetThreatList; with nobody in melee reach he casts Magma Blast (6000) on the top threat.
TEST_CASE("Ragnaros is taken back by a tank after his Wrath resets threat", "[ClassicRaidRagnaros]")
{
    RaidRules const& r = Mc();
    CHECK(std::find(r.retaunt.begin(), r.retaunt.end(), kRagnaros) != r.retaunt.end());
    // The bosses that had the rule before it became data keep it.
    for (uint32_t boss : { 12118u, 11982u, 12259u, 12264u })
        CHECK(std::find(r.retaunt.begin(), r.retaunt.end(), boss) != r.retaunt.end());
}

// Five tank deaths in three minutes to Elemental Fire (20564, 2400 a tick, reapplied by every swing).
TEST_CASE("Ragnaros' tanks swap on Elemental Fire and drink Greater Fire Protection", "[ClassicRaidRagnaros]")
{
    RaidRules const& r = Mc();
    bool swap = false;
    for (TankSwap const& t : r.tankSwap)
        if (t.enemy == kRagnaros)
        {
            swap = true;
            CHECK(t.debuffAura == 20564u);
            CHECK(t.stacks == 1u);
        }
    CHECK(swap);
    bool potion = false;
    for (ResistPotion const& p : r.resistPotion)
        if (p.enemy == kRagnaros)
        {
            potion = true;
            CHECK(p.item == 13457u);
            CHECK(p.who == Who::Tank);
        }
    CHECK(potion);
}

// Lava Burst (21908) summons game object 178088 for 3 s; its trap (spell 21158) triggers within 3.5 y.
TEST_CASE("Ragnaros' Lava Burst traps are avoided by everybody but the tanks", "[ClassicRaidRagnaros]")
{
    RaidRules const& r = Mc();
    bool found = false;
    for (AvoidGameObject const& g : r.avoidGameObject)
        if (g.entry == 178088u)
        {
            found = true;
            CHECK(g.radius > 3.5f);          // outside the trigger radius
            CHECK(g.who == Who::NonTank);    // the tanks stay in his melee reach
        }
    CHECK(found);
}

TEST_CASE("Ragnaros' casters and healers stand outside his knockback", "[ClassicRaidRagnaros]")
{
    RaidRules const& r = Mc();
    bool ranged = false, healer = false;
    for (AvoidEnemy const& a : r.avoidEnemy)
        if (a.enemy == kRagnaros)
        {
            CHECK(a.radius >= 20.0f);
            ranged = ranged || a.who == Who::Ranged;
            healer = healer || a.who == Who::Healer;
        }
    CHECK(ranged);
    CHECK(healer);
}

// MC Geddon 2026-10-12 (b0a1c616): the ranged / healer slot rows start at the circle's radius + the hazard margin + 1 (State_Idle.cpp,
// RaidRangedApproachTank), measured from Geddon; the Inferno circle and the standing circle must be the same number or the rows move
// while it opens, and the rows must stay inside the 33 y spell reach (30 y spells plus the mob's reach).
TEST_CASE("Geddon's Inferno circle and his ranged / healer circle agree and leave the rows in spell reach", "[ClassicRaidGeddon]")
{
    RaidRules const& r = Mc();
    float aura = 0.f, ranged = 0.f, healer = 0.f;
    for (AvoidEnemyAura const& a : r.avoidEnemyAura)
        if (a.enemy == 12056u)
            aura = a.radius;
    for (AvoidEnemy const& a : r.avoidEnemy)
        if (a.enemy == 12056u)
        {
            if (a.who == Who::Ranged) ranged = a.radius;
            if (a.who == Who::Healer) healer = a.radius;
        }
    CHECK(aura > 21.5f);            // Inferno's radius 20 plus the victim's combat reach
    CHECK(ranged == aura);
    CHECK(healer == aura);
    CHECK(aura + 2.0f + 1.0f + 3.0f <= 33.0f);   // the second row (one spacing further out) is still in reach
}
