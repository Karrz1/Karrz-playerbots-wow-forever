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
#include <cmath>

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
    // The bosses that had the rule before it became data keep it, and Geddon, Garr, Golemagg and Sulfuron take their boss back from a DPS too.
    for (uint32_t boss : { 12118u, 11982u, 12259u, 12264u, 12056u, 12057u, 11988u, 12098u })
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
            CHECK(p.who == Who::Anyone);   // Might of Ragnaros: 2000 fire to 20 y round a random mana user
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

// MC Geddon 2026-10-13 (09a8ff07): three casters died to one Living Bomb Explosion (20476: radius index 19 = 18 yards from the carrier plus the
// victim's reach) because the carrier only got 13-15 y from the nearest raider.
TEST_CASE("Living Bomb: the carrier runs, and the raid keeps, more than the Explosion's 18 yards", "[ClassicRaidGeddon]")
{
    RaidRules const& r = Mc();
    bool run = false, avoid = false;
    for (CarrierRun const& c : r.carrierRun)
        if (c.aura == 20475u)
        {
            run = true;
            CHECK(c.clearDistance >= 19.5f);
        }
    for (AvoidCarrier const& c : r.avoidCarrier)
        if (c.aura == 20475u)
        {
            avoid = true;
            CHECK(c.radius >= 19.5f);
        }
    CHECK(run);
    CHECK(avoid);
}

// MC 2026-10-13 (5b31e3df): Living Bomb's Explosion (3200) hits its carrier too; a caster with 3150 health died of its own bomb 29 y from the boss.
TEST_CASE("Baron Geddon's raid drinks Greater Fire Protection against the Living Bomb", "[ClassicRaidRagnaros]")
{
    RaidRules const& r = Mc();
    bool potion = false;
    for (ResistPotion const& p : r.resistPotion)
        if (p.enemy == 12056u)
        {
            potion = true;
            CHECK(p.item == 13457u);
            CHECK(p.who == Who::Anyone);   // any role can carry the bomb
        }
    CHECK(potion);
}

// n24 onyxia run 2: every DPS was ranked onto the whelps while Onyxia flew and she never left 57-61 percent.
TEST_CASE("Onyxia's whelps outrank her only for melee and the AoE share in phase 2", "[ClassicRaidRagnaros]")
{
    RaidRules const* r = RulesForMap(249);
    REQUIRE(r != nullptr);
    int phase2Ranged = 1, phase2Melee = 0, phase2Share = 0;
    bool gatedOnly = true;
    for (Priority const& p : r->priority)
    {
        if (p.enemy != 11262)
            continue;
        if (p.gateEnemy != 10184)
            gatedOnly = false;
        if (p.hpMax == 65.0f && p.hpMin == 40.0f)
        {
            if (p.who == Who::NonTank) phase2Ranged = p.value;
            if (p.who == Who::Melee) phase2Melee = p.value;
            if (p.who == Who::AoeShare) phase2Share = p.value;
        }
    }
    CHECK(phase2Ranged < 1);      // below Onyxia's default 1
    CHECK(phase2Melee >= 2);
    CHECK(phase2Share >= 2);
    CHECK(gatedOnly);
    CHECK(std::find(KillOrder(249).begin(), KillOrder(249).end(), 11262u) == KillOrder(249).end());   // no unconditional group focus on whelps
}

// n31 onyxia_full runs 5 and 6: the Deep Breath corridor must come from the cast spell's waypoints, not from her position and facing.
TEST_CASE("Onyxia's Deep Breath corridors are fixed paths between opposite waypoints", "[ClassicRaidRagnaros]")
{
    RaidRules const* r = RulesForMap(249);
    REQUIRE(r != nullptr);
    int count = 0;
    for (AvoidBreath const& b : r->avoidBreath)
    {
        if (b.enemy != 10184)
            continue;
        ++count;
        CHECK(b.fixedPath);
        CHECK(b.halfWidth >= 31.0f);   // every n31 victim sat within 31 y of its line
        float const len = std::sqrt((b.bx - b.ax) * (b.bx - b.ax) + (b.by - b.ay) * (b.by - b.ay));
        CHECK(len > 60.0f);
        CHECK(len < 100.0f);
    }
    CHECK(count == 8);
}

// n38 onyxia_full: ten ranged massed on one bearing died to one Flame Breath; ranged and healers take the flank wedges like the melee.
TEST_CASE("Onyxia's ranged and healers stand in the flank wedges in phases 1 and 3", "[ClassicRaidRagnaros]")
{
    RaidRules const* r = RulesForMap(249);
    REQUIRE(r != nullptr);
    for (Who who : { Who::Melee, Who::Ranged, Who::Healer })
    {
        int phases = 0;
        for (StandFlank const& f : r->standFlank)
            if (f.enemy == 10184 && f.who == who)
            {
                ++phases;
                CHECK(f.centerDeg - f.halfAngleDeg >= 50.0f);   // outside the 45 degree front cones
                CHECK(f.centerDeg + f.halfAngleDeg <= 90.0f);   // in front of the tail sweep
                if (who != Who::Melee)
                    CHECK(f.minDist >= 20.0f);                  // outside Wing Buffet
            }
        CHECK(phases == 2);
    }
}

// n58 live 2026-10-15: posts and dodge moves at the two cave mouths pulled out the static whelps; no raid move may end near the caves.
TEST_CASE("Onyxia's cave alcoves are out of bounds and no post stands in them", "[ClassicRaidRagnaros]")
{
    RaidRules const* r = RulesForMap(249);
    REQUIRE(r != nullptr);
    int caves = 0;
    for (KeepOut const& k : r->keepOut)
        if (k.enemy == 10184)
        {
            ++caves;
            CHECK(k.radius >= 20.0f);
            CHECK(std::fabs(k.x + 30.5f) < 1.0f);
            CHECK((std::fabs(k.y + 254.5f) < 1.0f || std::fabs(k.y + 177.1f) < 1.0f));
        }
    CHECK(caves == 2);
    for (Stand const& s : r->stand)
        if (s.enemy == 10184)
            for (KeepOut const& k : r->keepOut)
                CHECK(std::hypot(s.x - k.x, s.y - k.y) >= k.radius);
}

// n50 onyxia_full run 1: the ranged stood 54-78 y from the flying Onyxia and she did not lose health in phase 2.
TEST_CASE("Onyxia's phase 2 ranged follow her", "[ClassicRaidRagnaros]")
{
    RaidRules const* r = RulesForMap(249);
    REQUIRE(r != nullptr);
    bool follow = false;
    for (StayNear const& s : r->stayNear)
        if (s.enemy == 10184 && s.who == Who::Ranged && s.hpMax == 65.0f && s.hpMin == 40.0f)
        {
            follow = true;
            CHECK(s.radius <= 22.0f);   // 20 y of altitude leave about 30 y of range
        }
    CHECK(follow);
}

// n57 onyxia_full run 1: the main tank turned Onyxia away from the raid every 9 s; the ranged on the flank wedges re-positioned each time.
TEST_CASE("Onyxia's frontal cones do not make the tank turn her", "[ClassicRaidRagnaros]")
{
    RaidRules const* r = RulesForMap(249);
    REQUIRE(r != nullptr);
    int cones = 0;
    for (AvoidFrontal const& f : r->avoidFrontal)
        if (f.enemy == 10184)
        {
            ++cones;
            CHECK(!f.tankTurns);
        }
    CHECK(cones == 2);
}
