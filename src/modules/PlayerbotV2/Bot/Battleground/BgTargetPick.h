// BG victim validity: a battleground bot never keeps a target it cannot hurt, and re-picks when an enemy player is close.
//
// the owner, WSG 2026-10-09: a Horde priest stood next to him swinging at nothing and ignored him. The idle engage rule only ran with an empty
// victim, and the in-combat BG switches only react to a carrier, a peel or a healer, so a victim that was gone, dead, untargetable or
// not fighting back held the bot for good. Pure function over the snapshot's unit lists (templated, so tests/game/BgTargetPick.cpp can
// run it on plain structs). Used by State_Idle and State_InCombat; both log the result as [bg_target].
#pragma once

#include <cstdint>
#include <vector>

namespace Playerbot::BgTargetPick
{
    enum class Why : uint8_t
    {
        None,           // keep the victim
        Missing,        // the victim is not among the units around the bot (gone, out of range, not visible)
        Unfightable,    // dead, untargetable, pacified, or unreachable
        IdleMob,        // a creature that does not fight us while an enemy player is close
    };

    inline char const* WhyName(Why w)
    {
        switch (w)
        {
            case Why::Missing:     return "missing";
            case Why::Unfightable: return "unfightable";
            case Why::IdleMob:     return "idle_mob";
            default:               return "keep";
        }
    }

    // How far a battleground bot looks for an enemy to fight (playtest, WSG 2026-10-09: "make the radius where they attack people bigger").
    // The snapshot scans this far in a battleground (BotSnapshotBuilder), the engage and re-pick rules read it.
    constexpr float kBgEngageRange = 55.0f;
    constexpr float kIdleMobPlayerRange = 15.0f;   // an enemy player this close beats a creature that is not attacking us

    // `me` is this bot's guid. `enemies` / `attackers` are the snapshot's lists (victim lookup uses both, as victim_info() does).
    // Returns the guid to switch to (default-constructed guid when none) and why the victim was dropped.
    template <class Unit, class Guid>
    Guid Pick(Guid const& me, Guid const& victim, float bx, float by, bool ranged, float range,
              std::vector<Unit> const& enemies, std::vector<Unit> const& attackers, Why& why)
    {
        why = Why::None;
        Guid const none{};
        if (victim == none)
            return none;

        Unit const* v = nullptr;
        for (Unit const& u : attackers)
            if (u.guid == victim) { v = &u; break; }
        if (!v)
            for (Unit const& u : enemies)
                if (u.guid == victim) { v = &u; break; }

        auto dist2 = [&](Unit const& u) { float const dx = u.x - bx, dy = u.y - by; return dx * dx + dy * dy; };
        auto fightable = [](Unit const& u) { return u.hp > 0 && !u.untargetable && !u.is_pacified && !u.cannot_reach; };

        if (!v) why = Why::Missing;
        else if (!fightable(*v)) why = Why::Unfightable;

        // The nearest live enemy player, in line of sight for ranged bots and not locked down by a friendly CC.
        Unit const* best = nullptr;
        float best_d = range * range;
        for (Unit const& u : enemies)
        {
            if (!u.is_player || !fightable(u) || u.is_cc_locked || u.guid == victim) continue;
            if (ranged && !u.in_los) continue;
            float const d = dist2(u);
            if (d < best_d) { best_d = d; best = &u; }
        }

        if (why == Why::None && v && !v->is_player && !(v->victim == me) && best &&
            best_d <= kIdleMobPlayerRange * kIdleMobPlayerRange)
            why = Why::IdleMob;

        if (why == Why::None || !best)
            return none;
        return best->guid;
    }

    // Whether the open-engage rule may start a fight with this creature: only one that already fights this bot, its pet or a teammate
    // (`friends`: guids of the friendly units around the bot), or the creature the bot's order names (`order_entry`, 0 = none).
    // Bot-only AV 253: bots on the march opened on every creature within the 40 yd AV aggro range (ike3's range is for enemy PLAYERS):
    // 822 openings on creatures that were not in combat, 68 of them on Korrak the Bloodrager (113k HP, 1.8-2.3k a hit), who killed 79
    // bots one at a time, and the rams, wolves, harpies and garrison guards beside the roads split every squad on its way.
    template <class Unit, class Guid, class Friends>
    bool CreatureWorthOpening(Unit const& u, Guid const& me, Guid const& pet, Friends const& friends, uint32_t order_entry)
    {
        if (order_entry != 0 && u.entry == order_entry)
            return true;
        Guid const none{};
        if (u.victim == none)
            return false;
        if (u.victim == me || (!(pet == none) && u.victim == pet))
            return true;
        for (auto const& f : friends)
            if (f.guid == u.victim)
                return true;
        return false;
    }

    // The creature that is hitting this bot in melee (an attacker whose victim is `me`, alive and fightable, within kBesetRange), nearest
    // first; nullptr when none. Walking on does not shake it: the creature runs as fast as the bot and hits it in the back.
    // Bot-only AV 255: 298 of the 884 deaths had the in-combat march to the ordered boss (combat:bg_push_endgame_advance) as their last
    // rule, with the killer 2-5 yd away: 97 in Coldtooth Mine to the Whitewhisker kobolds (one bot died at the same spot 20 times on the
    // way to the mine boss), 23 each to the Frostwolf Warriors and Mine Layers at Iceblood, 15 to Commander Randolph.
    constexpr float kBesetRange = 10.0f;
    template <class Unit, class Guid>
    Unit const* BesetBy(Guid const& me, float bx, float by, std::vector<Unit> const& attackers)
    {
        Unit const* best = nullptr;
        float best_d = kBesetRange * kBesetRange;
        for (Unit const& u : attackers)
        {
            if (u.is_player || !(u.victim == me) || u.hp <= 0 || u.untargetable || u.is_pacified || u.cannot_reach) continue;
            float const dx = u.x - bx, dy = u.y - by, d = dx * dx + dy * dy;
            if (d <= best_d) { best_d = d; best = &u; }
        }
        return best;
    }

    // The enemy player attacking this bot (an attacker that is a player, its victim `me`, alive and fightable, not held by a friendly
    // CC, within kShotByRange), nearest first; nullptr when none. A pusher fights back before the march goes on, as BesetBy does for
    // creatures. Bot-only AV 264: after both general pushes went, 152 of the 290 deaths to players had the march
    // (combat:bg_push_endgame_advance) as their last rule: the two squads met on the river road under Iceblood Tower and walked past each
    // other, shot from 0-40 yd (136 of the 152), and neither got past Iceblood in 28 minutes.
    constexpr float kShotByRange = 40.0f;
    template <class Unit, class Guid>
    Unit const* ShotBy(Guid const& me, float bx, float by, std::vector<Unit> const& attackers)
    {
        Unit const* best = nullptr;
        float best_d = kShotByRange * kShotByRange;
        for (Unit const& u : attackers)
        {
            if (!u.is_player || !(u.victim == me) || u.hp <= 0 || u.untargetable || u.is_pacified || u.cannot_reach || u.is_cc_locked)
                continue;
            float const dx = u.x - bx, dy = u.y - by, d = dx * dx + dy * dy;
            if (d <= best_d) { best_d = d; best = &u; }
        }
        return best;
    }
}
