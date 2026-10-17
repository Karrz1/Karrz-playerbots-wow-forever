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

#include "../../src/modules/PlayerbotV2/Combat/LazyLock.h"

using namespace Playerbot::Lazy;

namespace
{
// A level 60 affliction / destruction warlock with everything known and ready except the racial, a fresh target in range, armor and
// pet up.
Input Fresh()
{
    Input in;
    for (uint32_t s = DemonArmor; s < Count; ++s)
        in.known |= Bit(Cast(s)), in.ready |= Bit(Cast(s));
    in.has_target = true;
    in.in_combat = true;
    in.has_pet = true;
    in.armor_up = true;
    in.ready &= ~Bit(Racial);
    return in;
}
}

TEST_CASE("Nothing happens while a cast is going", "[ClassicLazyLock]")
{
    Input in = Fresh();
    in.casting = true;
    CHECK(Pick(in) == None);
}

TEST_CASE("A fresh melee mob gets Curse of Agony, then the dots, then Shadow Bolt", "[ClassicLazyLock]")
{
    Input in = Fresh();
    CHECK(Pick(in) == AmplifyCurse);
    in.ready &= ~Bit(AmplifyCurse);
    CHECK(Pick(in) == CurseOfAgony);
    in.on_target |= Bit(CurseOfAgony);
    CHECK(Pick(in) == Corruption);
    in.on_target |= Bit(Corruption);
    CHECK(Pick(in) == Immolate);
    in.on_target |= Bit(Immolate);
    CHECK(Pick(in) == SiphonLife);
    in.on_target |= Bit(SiphonLife);
    CHECK(Pick(in) == Conflagrate);
    in.ready &= ~Bit(Conflagrate);
    CHECK(Pick(in) == ShadowBolt);
}

TEST_CASE("A caster gets Curse of Tongues instead of Agony", "[ClassicLazyLock]")
{
    Input in = Fresh();
    in.target_caster = true;
    CHECK(Pick(in) == CurseOfTongues);
    in.on_target |= Bit(CurseOfTongues);
    CHECK(Pick(in) == Corruption);
}

TEST_CASE("A caster whose Tongues is already another warlock's gets Agony", "[ClassicLazyLock]")
{
    Input in = Fresh();
    in.target_caster = true;
    in.another_tongues = true;
    in.ready &= ~Bit(AmplifyCurse);
    CHECK(Pick(in) == CurseOfAgony);
}

TEST_CASE("A caster without Tongues trained gets Agony", "[ClassicLazyLock]")
{
    Input in = Fresh();
    in.target_caster = true;
    in.known &= ~Bit(CurseOfTongues);
    in.ready &= ~Bit(AmplifyCurse);
    CHECK(Pick(in) == CurseOfAgony);
}

TEST_CASE("One curse only: a curse of ours on the target is not replaced", "[ClassicLazyLock]")
{
    Input in = Fresh();
    in.target_caster = true;
    in.on_target |= Bit(CurseOfAgony);
    CHECK(Pick(in) == Corruption);
}

TEST_CASE("The global cooldown leaves nothing to cast", "[ClassicLazyLock]")
{
    Input in = Fresh();
    in.ready = 0;
    CHECK(Pick(in) == None);
}

TEST_CASE("A mob about to die gets no dots, an ordinary one a finisher, a boss still the dots", "[ClassicLazyLock]")
{
    Input in = Fresh();
    in.target_hp_pct = 30;
    CHECK(Pick(in) == ShadowBolt);
    in.target_boss = true;
    in.ready &= ~Bit(AmplifyCurse);
    CHECK(Pick(in) == CurseOfAgony);
}

TEST_CASE("Shadowburn needs three shards and the last 20 percent", "[ClassicLazyLock]")
{
    Input in = Fresh();
    in.target_hp_pct = 15;
    in.soul_shards = 2;
    in.ready &= ~Bit(DrainSoul);
    CHECK(Pick(in) == ShadowBolt);
    in.soul_shards = 3;
    CHECK(Pick(in) == Shadowburn);
}

TEST_CASE("Drain Soul tops up the shards on a dying ordinary mob", "[ClassicLazyLock]")
{
    Input in = Fresh();
    in.target_hp_pct = 20;
    in.soul_shards = 1;
    CHECK(Pick(in) == DrainSoul);
    in.target_boss = true;
    CHECK(Pick(in) != DrainSoul);
}

TEST_CASE("Shadow Trance puts the instant bolt before the dots", "[ClassicLazyLock]")
{
    Input in = Fresh();
    in.trance = true;
    CHECK(Pick(in) == ShadowBoltTrance);
}

TEST_CASE("The racial goes on a fight that will last, not on a nearly dead mob", "[ClassicLazyLock]")
{
    Input in = Fresh();
    in.ready |= Bit(Racial);
    CHECK(Pick(in) == Racial);
    in.target_hp_pct = 40;
    CHECK(Pick(in) != Racial);
}

TEST_CASE("Low health: Death Coil, then Drain Life", "[ClassicLazyLock]")
{
    Input in = Fresh();
    in.hp_pct = 25;
    CHECK(Pick(in) == DeathCoil);
    in.ready &= ~Bit(DeathCoil);
    CHECK(Pick(in) == DrainLife);
}

TEST_CASE("Low mana and healthy: Life Tap; low mana and hurt: no tap", "[ClassicLazyLock]")
{
    Input in = Fresh();
    in.mana_pct = 15;
    CHECK(Pick(in) == LifeTap);
    in.hp_pct = 45;
    CHECK(Pick(in) != LifeTap);
}

TEST_CASE("Armor and pet come first when missing; the pet only out of combat", "[ClassicLazyLock]")
{
    Input in = Fresh();
    in.armor_up = false;
    CHECK(Pick(in) == DemonArmor);
    in.armor_up = true;
    in.has_pet = false;
    CHECK(Pick(in) != SummonPet);
    in.in_combat = false;
    CHECK(Pick(in) == SummonPet);
}

TEST_CASE("No target: upkeep only", "[ClassicLazyLock]")
{
    Input in = Fresh();
    in.has_target = false;
    CHECK(Pick(in) == None);
    in.armor_up = false;
    CHECK(Pick(in) == DemonArmor);
}

TEST_CASE("A target out of range gets nothing", "[ClassicLazyLock]")
{
    Input in = Fresh();
    in.target_in_range = false;
    CHECK(Pick(in) == None);
}

TEST_CASE("With a wand: Shadow Bolt down to 35 percent mana, then the wand once", "[ClassicLazyLock]")
{
    Input in = Fresh();
    in.wand_equipped = true;
    in.on_target = Bit(CurseOfAgony) | Bit(Corruption) | Bit(Immolate) | Bit(SiphonLife);
    in.ready &= ~Bit(Conflagrate);
    in.mana_pct = 40;
    CHECK(Pick(in) == ShadowBolt);
    in.mana_pct = 30;
    CHECK(Pick(in) == Wand);
    in.wand_shooting = true;
    CHECK(Pick(in) == None);
}

TEST_CASE("Without a wand, or on a boss, Shadow Bolt goes on to 8 percent mana", "[ClassicLazyLock]")
{
    Input in = Fresh();
    in.on_target = Bit(CurseOfAgony) | Bit(Corruption) | Bit(Immolate) | Bit(SiphonLife);
    in.ready &= ~Bit(Conflagrate);
    in.hp_pct = 45;   // too hurt to Life Tap
    in.mana_pct = 12;
    CHECK(Pick(in) == ShadowBolt);
    in.mana_pct = 5;
    CHECK(Pick(in) == None);
}
