// Pure rules for "is it safe to sit down and eat / drink" (Classic::SafeToRecover), kept free of snapshot types so the
// thresholds can be tested.

#pragma once

namespace Playerbot::Classic {

// A hostile blocks sitting down only when it is engaged with the bot's side (its victim is the bot or a group member) and
// close enough to join in. The old rule counted every enemy with any victim within 45 y, so a dungeon healer never sat:
// some pack member or patrol is always within 45 y (Deadmines, live 2026-10-10: priest at 87 % "not_safe_to_sit", started
// the next fights at 64 % and was dry two pulls later).
constexpr float kRecoverEnemyRange = 30.0f;      // aggro reach (20 y at most for same-level mobs) plus a margin
constexpr float kRecoverMemberFightRange = 45.0f;

inline bool EnemyBlocksRecovery(bool victim_is_bot_or_group, float dist2)
{
    return victim_is_bot_or_group && dist2 < kRecoverEnemyRange * kRecoverEnemyRange;
}

inline bool MemberFightBlocksRecovery(float dist2)
{
    return dist2 < kRecoverMemberFightRange * kRecoverMemberFightRange;
}

} // namespace Playerbot::Classic
