#ifndef PLAYERBOT_IDLE_RULE_ERRANDS_H
#define PLAYERBOT_IDLE_RULE_ERRANDS_H

#include <string_view>

namespace Playerbot
{
// Rules a companion in its owner's group must not run: each sends the bot about the world on its own business. Upkeep
// (food, buffs, heals, rez, supplies, loot, rolls, equipping), survival, owner orders and assisting the group stay, and
// so do accepting the group's LFG proposal or BG port and playing the dungeon/BG once inside (bg_*, dungeon_dispatch,
// lfg_proposal_accept): the owner queued the group. Repairs and new trainer spells come free (BackfillOne).
inline bool IsWorldErrand(std::string_view name)
{
    // Taking the quest from an item already in the bags goes nowhere, so a companion does it too.
    if (name == "idle:quest_start_item")
        return false;
    // Accepting a guild invite goes nowhere either, and it is the player's own invite: "idle:guild_" below caught it, so a
    // companion following its player never answered a /ginvite (Karrz 2026-11-16: Kathleen, then Chaelanna, never joined).
    if (name == "idle:guild_invite_accept")
        return false;
    static constexpr std::string_view kPrefixes[] = {
        "idle:ah_", "idle:autoact_dispatch", "idle:bag_hearth", "idle:bags_full_recover", "idle:bank_deposit",
        "idle:calendar_", "idle:capital_bag_run", "idle:craft_order_", "idle:critical_repair", "idle:dual_spec_switch",
        "idle:dungeon_loop_requeue", "idle:dungeon_quest_approach", "idle:far_same_map_travel", "idle:gather",
        "idle:guild_", "idle:homebind", "idle:invite_nearby_player", "idle:legacy_vendor_dispatch",
        "idle:look_at_npc", "idle:loot_chest", "idle:mail_drain", "idle:mat_share_to_owner",
        "idle:pet_swap_from_stable", "idle:pursue_quest_", "idle:pvp_help_callout", "idle:assist_friend_pvp",
        "idle:quest_", "idle:react_to_passerby", "idle:resolve_junk_quests", "idle:swim_stuck_hearth",
        "idle:swim_stuck_walk_to_hub", "idle:tame_beast", "idle:taxi_discover", "idle:transport_ride", "idle:vendor_visit",
        "idle:bag_hearth", "idle:fidget", "idle:inn_rest", "idle:mailbox", "idle:town_duel", "idle:town_errand", "idle:town_linger", "idle:wander",
        "idle:walk_",
        "idle:wq_", "idle:yell_lfg", "idle:ambient_sit", "idle:rpg_",
    };
    for (std::string_view prefix : kPrefixes)
        if (name.starts_with(prefix))
            return true;
    return false;
}
}

#endif
