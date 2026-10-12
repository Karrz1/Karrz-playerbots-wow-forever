#include "CreatureStats.h"

#include "CreatureStatsFormat.h"

#include "Creature.h"
#include "CreatureData.h"
#include "Log.h"
#include "Map.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "TemporarySummon.h"
#include "fmt/format.h"

namespace Playerbot::Diagnostics::CreatureStats
{

// Map::SummonCreature returns null without saying why; these are its failure paths (Creature::Create / CreateFromProto / InitEntry),
// checked the same way so the report names the one that applies.
static std::string SummonFailureReason(CreatureTemplate const* tmpl, Position const& where)
{
    if (!where.IsPositionValid())
        return fmt::format("the summon position ({}, {}, {}) is not valid", where.GetPositionX(), where.GetPositionY(), where.GetPositionZ());
    if (!tmpl->GetFirstValidModel())
        return "creature_template_model has no valid model for the entry";
    if (CreatureModel const* model = ObjectMgr::ChooseDisplayId(tmpl, nullptr))
    {
        if (!sObjectMgr->GetCreatureModelRandomGender(const_cast<CreatureModel*>(model), tmpl))
            return fmt::format("display id {} has no creature_model_info row", model->CreatureDisplayID);
    }
    return "Creature::Create refused it with a valid position and model (zone script hook or Create returned false; check Server.log)";
}

std::uint32_t Run(Player* near, std::vector<std::uint32_t> const& entries, std::vector<std::string>& report)
{
    std::uint32_t logged = 0;
    for (std::uint32_t const entry : entries)
    {
        CreatureTemplate const* tmpl = sObjectMgr->GetCreatureTemplate(entry);
        if (!tmpl)
        {
            report.push_back(fmt::format("entry {}: not in creature_template", entry));
            continue;
        }
        Position const where = near->GetNearPosition(5.0f, 0.f);
        TempSummon* c = near->GetMap()->SummonCreature(entry, where, nullptr, 0ms, near);
        if (!c)
        {
            std::string const why = SummonFailureReason(tmpl, where);
            TC_LOG_ERROR("playerbot.v2", "[creature_stats] entry={} name=\"{}\" summon failed: {}", entry, tmpl->Name, why);
            report.push_back(fmt::format("entry {}: could not be summoned: {}", entry, why));
            continue;
        }
        // The creature is fully made by the time SummonCreature returns (SelectLevel and the stat update run in Create / AddToWorld).
        Row r;
        r.entry = entry;
        r.name = c->GetName();
        r.level = c->GetLevel();
        r.rank = uint32(tmpl->Classification);
        r.health = c->GetMaxHealth();
        r.mana = c->GetMaxPower(POWER_MANA);
        r.armor = c->GetArmor();
        c->CalculateMinMaxDamage(BASE_ATTACK, false, true, r.dmg_min, r.dmg_max);
        r.atk_ms = c->GetBaseAttackTime(BASE_ATTACK);
        for (std::uint32_t school = SPELL_SCHOOL_HOLY; school <= SPELL_SCHOOL_ARCANE; ++school)
            r.res[school - SPELL_SCHOOL_HOLY] = c->GetResistance(SpellSchools(school));
        std::string const line = FormatLine(r);
        TC_LOG_INFO("playerbot.v2", "{}", line);
        report.push_back(line);
        ++logged;
        c->DespawnOrUnsummon();
    }
    return logged;
}

} // namespace Playerbot::Diagnostics::CreatureStats
