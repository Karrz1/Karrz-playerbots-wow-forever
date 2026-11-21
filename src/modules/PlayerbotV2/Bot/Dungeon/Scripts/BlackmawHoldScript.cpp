// BlackmawHoldScript - per-dungeon advice for Blackmaw Hold (map 2974, Classic 1.60 Forever, levels 55-60).
// The creatures and the boss fights are in classic_instance_blackmaw_hold.cpp and 2026_11_19_70/71_world_forever_blackmaw_hold;
// the route (playerbot_dungeon_routes, 0044_blackmaw_hold_route.sql) and this file follow the same boss order: Withermaw,
// Bonesnout the Unbroken, Impaan the Imprisoner, the Unbound Spirit, the Blackmaw Behemoth, Elder Shaman Bloodmane, Varrox
// Bleakhoof (the shortest walk from the landing that ends at Varrox; every leg was path-checked on the 2974 navmesh).
// The Unbound Spirit cannot be hit until Elder Bramblesnap's ritual has run; it starts when the group comes near him, so
// walking to the Spirit's spot is enough.

#include "../DungeonScript.h"

namespace Playerbot {

namespace {

class BlackmawHoldScript final : public DungeonScript
{
public:
    uint32_t  map_id() const override { return 2974; }   // Blackmaw Hold
    char const* name() const override { return "blackmaw_hold"; }

    DungeonAdvice get_advice(BotSnapshotView const& /*s*/) const override
    {
        DungeonAdvice a;
        // Every living Ritualist takes a quarter off the damage Bonesnout takes (all four: none), and Clawbert heals the
        // Behemoth to full: they die first. Then the healers of the packs.
        a.high_priority_kill_entries = {
            258668,   // Legashi Ritualist (Bonesnout's shield)
            247996,   // Clawbert (Restore on the Behemoth)
            277474,   // Blackmaw Devotee (Healing Wave; they join Bloodmane)
            258508,   // Blackmaw Shaman (Healing Wave)
            258516,   // Blackmaw Druid (Healing Wave)
        };
        a.cc_priority_entries = {
            258508,   // Blackmaw Shaman
            258516,   // Blackmaw Druid
            268864,   // Legashi Plaguebringer (Shadow Bolt, Curse of Weakness)
        };
        a.mandatory_interrupt_spells = {
            1244951,   // Restore (Clawbert: heals the Behemoth to full)
            1283422,   // Healing Wave (Shamans, Druids, Devotees)
        };
        // Elder Bramblesnap is friendly: he runs the ritual that frees the Unbound Spirit.
        a.ignore_entries = {
            248765,   // Elder Bramblesnap
        };
        a.bosses = {
            247195,   // Withermaw
            247198,   // Bonesnout the Unbroken
            247206,   // Impaan the Imprisoner
            247231,   // Unbound Spirit
            247207,   // Blackmaw Behemoth
            247209,   // Elder Shaman Bloodmane
            247234,   // Varrox Bleakhoof
        };
        // 1:1 with `bosses`: their spawn spots (2026_11_19_70_world_forever_blackmaw_hold.sql).
        a.progression_waypoints = {
            { 3694.7f, 3428.0f, 302.4f },      // Withermaw
            { 3386.7f, 3370.7f, 249.98f },     // Bonesnout the Unbroken
            { 3320.0f, 3578.7f, 343.74f },     // Impaan the Imprisoner
            { 3265.0f, 2920.0f, 205.09f },     // Unbound Spirit (Elder Bramblesnap stands 14 y away)
            { 3665.0f, 2905.0f, 172.26f },     // Blackmaw Behemoth
            { 3484.98f, 2968.62f, 138.75f },   // Elder Shaman Bloodmane
            { 3556.27f, 2819.47f, 175.98f },   // Varrox Bleakhoof
        };
        return a;
    }
};

} // anonymous

std::unique_ptr<DungeonScript> MakeBlackmawHoldScript()
{
    return std::make_unique<BlackmawHoldScript>();
}

} // namespace Playerbot
