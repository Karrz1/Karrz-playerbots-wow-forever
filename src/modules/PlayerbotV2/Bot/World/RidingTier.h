#pragma once
// Vanilla riding for a bot, in one place: no riding below level 40, Apprentice (skill 75 and the race's normal mount) from 40, Journeyman (150 and the
// swift mount) from 60. The realm's own data agrees (trainer spell 33388 needs level 40, 33391 level 60). Pure: tested in tests/game/ClassicRidingTier.cpp.

#include <cstdint>

namespace Playerbot::V2::World {

constexpr std::uint8_t kApprenticeRidingLevel = 40;
constexpr std::uint8_t kJourneymanRidingLevel = 60;

constexpr std::uint16_t RidingSkillForLevel(std::uint8_t level)
{
    return level >= kJourneymanRidingLevel ? 150 : level >= kApprenticeRidingLevel ? 75 : 0;
}

// What a bot of `level` that holds riding `skill` needs: the skill target (0 = no riding at all), and whether its mounts and riding spells go.
struct RidingFix
{
    std::uint16_t skill = 0;       // the skill the bot should hold
    bool set_skill = false;        // its skill differs from `skill` (raise it, lower it, or remove it at 0)
    bool strip_mounts = false;     // below 40: every mount spell and riding tier spell goes
};

constexpr RidingFix PlanRiding(std::uint8_t level, std::uint16_t skill)
{
    RidingFix fix;
    fix.skill = RidingSkillForLevel(level);
    fix.set_skill = skill != fix.skill;
    fix.strip_mounts = fix.skill == 0;
    return fix;
}

} // namespace Playerbot::V2::World
