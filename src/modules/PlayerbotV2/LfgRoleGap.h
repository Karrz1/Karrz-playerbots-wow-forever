// LfgRoleGap.h: which places of a 5-man or bigger group are still free (5-man: 1 tank, 1 healer, 3 damage; 10-man: 1, 3, 6).
// Header only, no game types.
#pragma once

#include "Define.h"

#include <span>

namespace Playerbot::V2
{

// Role bits as the group stores them (lfg::PLAYER_ROLE_*).
constexpr uint8 kLfgRoleTank   = 0x02;
constexpr uint8 kLfgRoleHealer = 0x04;
constexpr uint8 kLfgRoleDamage = 0x08;

struct LfgRoleGap
{
    uint8 tank = 0, healer = 0, dps = 0;
    uint8 Total() const { return uint8(tank + healer + dps); }
    bool operator==(LfgRoleGap const&) const = default;
};

constexpr uint32 kLfgMinPartySize = 5;
constexpr uint32 kLfgMaxPartySize = 40;

// The role mix of an empty group of `size` places: about 1 tank per 10 players and 1 healer per 3, the rest damage.
// 5 -> 1 tank, 1 healer, 3 damage; 10 -> 1, 3, 6; 20 -> 2, 6, 12; 40 -> 4, 13, 23.
inline LfgRoleGap LfgRoleMix(uint32 size)
{
    size = size < kLfgMinPartySize ? kLfgMinPartySize : (size > kLfgMaxPartySize ? kLfgMaxPartySize : size);
    uint32 const tank = size / 10 ? size / 10 : 1;
    uint32 const healer = size / 3 ? size / 3 : 1;
    return LfgRoleGap{ uint8(tank), uint8(healer), uint8(size - tank - healer) };
}

// The places a group of `size` (default a 5-man party) still lacks after the real members have taken theirs. `memberRoles`
// holds one role mask per real member (0 = unknown: counts as damage). A member takes the first of tank, healer, damage
// from their mask whose place is still free, else any free place; members past the group size take none.
inline LfgRoleGap ComputeLfgRoleGap(std::span<uint8 const> memberRoles, uint32 size = kLfgMinPartySize)
{
    LfgRoleGap gap = LfgRoleMix(size);
    uint32 const places = uint32(gap.Total());
    uint32 seated = 0;
    for (uint8 mask : memberRoles)
    {
        if (++seated > places)
            break;
        if (!mask)
            mask = kLfgRoleDamage;
        if ((mask & kLfgRoleTank) && gap.tank)          --gap.tank;
        else if ((mask & kLfgRoleHealer) && gap.healer) --gap.healer;
        else if ((mask & kLfgRoleDamage) && gap.dps)    --gap.dps;
        else if (gap.dps)                               --gap.dps;
        else if (gap.healer)                            --gap.healer;
        else if (gap.tank)                              --gap.tank;
    }
    return gap;
}

// What a pending Group Finder listing does at a check. `msLeft` is the time left of the wait (<= 0 when it is over),
// `realInRange` the real (non-bot) players online in the leader's level range who could still join. With
// Lfg.InstantFillIfNobody nobody to wait for means the bots come at once; otherwise they come when the wait is over.
enum class LfgFillAction : uint8 { Wait, FillNow, FillAfterWait };

inline LfgFillAction DecideLfgFill(int32 msLeft, uint32 realInRange, bool instantFillIfNobody)
{
    if (instantFillIfNobody && realInRange == 0)
        return LfgFillAction::FillNow;
    return msLeft <= 0 ? LfgFillAction::FillAfterWait : LfgFillAction::Wait;
}
} // namespace Playerbot::V2
