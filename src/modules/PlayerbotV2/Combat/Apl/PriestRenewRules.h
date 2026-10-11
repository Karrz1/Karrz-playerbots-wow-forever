// Pure decision for the priest's early Renew on the tank (classic_heal:renew_tank in Apl_Classic_Priest.cpp), kept free of
// snapshot types so the thresholds are tested.

#pragma once

namespace Playerbot::Combat::ClassicHeal {

constexpr int kRenewTankMinManaPct = 35;     // below it the mana goes to the emergency heals
constexpr int kRenewTankEmergencyPct = 40;   // somebody at or under this belongs to Flash Heal / the big heal
constexpr int kRenewTankToppedPct = 90;

// Renew the tank when it is taking (or about to take) damage, carries none of this priest's Renews, and nobody is in the
// emergency band. Renew is instant and the cheapest healing per point, so it goes before the direct heals.
inline bool RenewTankDue(int tank_hp_pct, int incoming, bool tank_has_my_renew, int mana_pct, int lowest_member_pct)
{
    if (tank_has_my_renew || mana_pct < kRenewTankMinManaPct || lowest_member_pct <= kRenewTankEmergencyPct)
        return false;
    return incoming > 0 || tank_hp_pct <= kRenewTankToppedPct;
}

// Inner Fire has 20 charges and a raid's melee strips them, so the buff dropped and was recast again and again (MC Garr
// 2026-10-10: 37 casts at about 375 mana each). In a fight inside an instance it waits for comfortable mana; between pulls it
// is always recast.
constexpr int kInnerFireFightMinManaPct = 60;

inline bool InnerFireWorthRecast(bool in_combat, bool in_instance, int mana_pct)
{
    return !(in_combat && in_instance) || mana_pct >= kInnerFireFightMinManaPct;
}

} // namespace Playerbot::Combat::ClassicHeal
