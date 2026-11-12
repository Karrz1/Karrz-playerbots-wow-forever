// ClassicTalentBuilds - vanilla talent builds for bots on Classic 1.60 (Forever).
//
// Forever stores the 1.12 talent trees as a trait config, and its client data
// has no retail loadouts, so bots would otherwise level with no talents. Each
// build is an ordered list of (talent name, ranks): ranks are bought in list
// order, one at a time, as far as the bot's level allows, so the order is also
// the levelling order. Talents are matched by the name of their trait
// definition's spell, which survives the id differences between 1.12 and the
// 1.60 client data. Builds are keyed by the retail spec ids PlayerbotV2 uses
// as "virtual" Classic specs (see ClassicSpec.h).

#pragma once

#include "Define.h"
#include <span>

namespace Playerbot::Classic {

struct TalentPick
{
    char const* name;
    uint8 ranks;
    bool filler = false;   // bought only with points left after every other pick has been tried
};

struct TalentBuild
{
    uint8 cls;
    uint32 spec;
    std::span<TalentPick const> picks;
};

// Every build of every class, in the order specs are preferred when a bot's
// talents fit several builds equally well (the class default comes first).
TC_GAME_API std::span<TalentBuild const> GetTalentBuilds();

// The build for (cls, spec); empty when there is none.
TC_GAME_API std::span<TalentPick const> GetTalentBuild(uint8 cls, uint32 spec);

} // namespace Playerbot::Classic
