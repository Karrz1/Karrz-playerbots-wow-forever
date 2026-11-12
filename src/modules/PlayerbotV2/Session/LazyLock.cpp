// One-button warlock: gathers the facts for Lazy::Pick (Combat/LazyLock.h) from the live Player and casts the answer through the
// bot API (highest known rank, range / facing / stun / silence gates). World thread only (the command and add-on handlers run there).
//
// A spell the server refuses for want of a resource (no soul shard for the Voidwalker, no mana, a missing reagent) is taken out of the
// choice for a few seconds and the press picks again, so a missing pet or shard never holds the rotation back. A cast that costs no
// global cooldown (trinkets, racial, Amplify Curse) is followed by the next pick in the same press.

#include "LazyLock.h"

#include "Combat/LazyLock.h"
#include "Creature.h"
#include "DB2Stores.h"
#include "GameTime.h"
#include "Item.h"
#include "ItemTemplate.h"
#include "Log.h"
#include "Map.h"
#include "Pet.h"
#include "PlayerbotAPI.h"
#include "Player.h"
#include "SharedDefines.h"
#include "Spell.h"
#include "SpellHistory.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "World.h"

#include <array>
#include <unordered_map>

namespace Playerbot {

namespace {

constexpr uint32 kSoulShard      = 6265;
constexpr uint32 kShadowTrance   = 17941;   // Nightfall proc
constexpr uint32 kShoot          = 5019;
constexpr float  kCastRange      = 30.0f;

// Rank 1 ids; the API resolves the highest rank the warlock knows.
constexpr uint32 kDemonSkin = 687, kDemonArmor = 706, kImp = 688, kVoidwalker = 697, kBloodFury = 20572, kBerserking = 20554;
constexpr uint32 kTongues = 1714, kFear = 5782, kHowl = 5484;
constexpr float  kCloseRange = 8.0f;    // an enemy this near is hitting the warlock in melee
constexpr float  kHowlRange  = 10.0f;

// How long a refused spell stays out of the choice (ms): a pet that failed to summon is not retried every press.
constexpr uint32 kBlockMs     = 6000;
constexpr uint32 kBlockPetMs  = 60000;
constexpr int    kMaxPicks    = 6;

int32 Pct(int64 value, int64 max) { return max > 0 ? int32(value * 100 / max) : 100; }

using BlockTimes = std::array<uint32, Lazy::Count>;
std::unordered_map<uint64, BlockTimes> s_blocked;   // per warlock: game time (ms) until which a spell is left out

char const* SpellText(uint32 id)
{
    SpellInfo const* info = sSpellMgr->GetSpellInfo(id, DIFFICULTY_NONE);
    char const* name = info && info->SpellName ? (*info->SpellName)[sWorld->GetDefaultDbcLocale()] : nullptr;
    return name && *name ? name : "spell";
}

} // namespace

std::string LazyLockPress(Player* p)
{
    if (!p || p->GetClass() != CLASS_WARLOCK)
        return "Lazy lock is for warlocks.";
    if (!p->IsAlive() || !p->GetMap())
        return {};

    API api(p);
    using namespace Lazy;

    Unit* target = p->GetSelectedUnit();
    bool const valid_target = target && target->IsAlive() && p->IsValidAttackTarget(target);

    auto known = [&](uint32 id) { return p->HasSpell(api.highest_known_rank(id)); };
    auto ready = [&](uint32 id) { return api.cast_ready_in_ms(id) == 0; };

    // Which spell id each decision name stands for on this warlock.
    uint32 ids[Count] = {};
    ids[DemonArmor]       = known(kDemonArmor) ? kDemonArmor : kDemonSkin;
    ids[SummonPet]        = known(kVoidwalker) ? kVoidwalker : kImp;
    ids[DeathCoil]        = 6789;
    ids[DrainLife]        = 689;
    ids[LifeTap]          = 1454;
    ids[ShadowBoltTrance] = 686;
    ids[Racial]           = p->HasSpell(kBloodFury) ? kBloodFury : kBerserking;
    ids[AmplifyCurse]     = 18288;
    ids[CurseOfTongues]   = kTongues;
    ids[CurseOfAgony]     = 980;
    ids[Corruption]       = 172;
    ids[Immolate]         = 348;
    ids[SiphonLife]       = 18265;
    ids[Conflagrate]      = 17962;
    ids[Shadowburn]       = 17877;
    ids[DrainSoul]        = 1120;
    ids[ShadowBolt]       = 686;
    ids[Wand]             = kShoot;
    ids[Fear]             = kFear;
    ids[HowlOfTerror]     = kHowl;

    uint32 in_known_items = 0, in_ready_items = 0;
    // On-use trinkets: their use spell and whether it is ready (they are items, not spells in the book).
    auto trinket = [&](uint8 slot, Cast c)
    {
        Item const* item = p->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
        ItemTemplate const* tmpl = item ? item->GetTemplate() : nullptr;
        if (!tmpl)
            return;
        for (ItemEffectEntry const* eff : tmpl->Effects)
        {
            if (!eff || eff->TriggerType != ITEM_SPELLTRIGGER_ON_USE)
                continue;
            SpellInfo const* info = sSpellMgr->GetSpellInfo(eff->SpellID, DIFFICULTY_NONE);
            if (!info)
                return;
            ids[c] = uint32(eff->SpellID);
            in_known_items |= Bit(c);
            if (!p->GetSpellHistory()->HasCooldown(info, item->GetEntry()))
                in_ready_items |= Bit(c);
            return;
        }
    };

    uint32 const now = GameTime::GetGameTimeMS();
    BlockTimes& blocked_until = s_blocked[p->GetGUID().GetCounter()];

    trinket(EQUIPMENT_SLOT_TRINKET1, Trinket1);
    trinket(EQUIPMENT_SLOT_TRINKET2, Trinket2);
    auto is_item = [](uint32 s) { return s == Trinket1 || s == Trinket2; };

    Input in;
    in.known = in_known_items;
    in.ready = in_ready_items;
    for (uint32 s = DemonArmor; s < Count; ++s)
    {
        if (is_item(s))
            continue;
        if (known(ids[s]))
            in.known |= Bit(Cast(s));
        if (ready(ids[s]))
            in.ready |= Bit(Cast(s));
        if (valid_target && target->HasAura(api.highest_known_rank(ids[s]), p->GetGUID()))
            in.on_target |= Bit(Cast(s));
    }

    in.casting        = p->IsNonMeleeSpellCast(false, false, true);
    in.gcd            = !(in.ready & Bit(ShadowBolt)) && (in.known & Bit(ShadowBolt));   // Shadow Bolt has no cooldown of its own
    in.in_combat      = p->IsInCombat();
    in.has_target     = valid_target;
    in.has_pet        = p->GetPet() && p->GetPet()->IsAlive();
    in.armor_up       = p->HasAura(api.highest_known_rank(kDemonArmor)) || p->HasAura(api.highest_known_rank(kDemonSkin));
    in.trance         = p->HasAura(kShadowTrance);
    in.wand_equipped  = p->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_RANGED) != nullptr;
    in.wand_shooting  = p->GetCurrentSpell(CURRENT_AUTOREPEAT_SPELL) != nullptr;
    in.hp_pct         = Pct(p->GetHealth(), p->GetMaxHealth());
    in.mana_pct       = Pct(p->GetPower(POWER_MANA), p->GetMaxPower(POWER_MANA));
    in.soul_shards    = int32(p->GetItemCount(kSoulShard));

    if (valid_target)
    {
        Creature const* c = target->ToCreature();
        in.target_caster   = target->GetMaxPower(POWER_MANA) > 0;
        in.target_boss     = (c && (c->IsDungeonBoss() || c->isWorldBoss())) || target->GetMaxHealth() >= 100000;
        in.target_in_range = p->GetDistance(target) <= kCastRange;
        in.target_hp_pct   = Pct(target->GetHealth(), target->GetMaxHealth());
        in.another_tongues = target->HasAura(api.highest_known_rank(kTongues)) && !target->HasAura(api.highest_known_rank(kTongues), p->GetGUID());
        in.target_player   = target->IsPlayer();
        in.target_close    = target->GetVictim() == p && p->GetDistance(target) <= kCloseRange;
        in.target_feared   = target->HasAuraType(SPELL_AURA_MOD_FEAR);
        in.fear_safe       = !p->GetMap()->IsDungeon();
        for (Unit const* attacker : p->getAttackers())
            if (attacker->IsAlive() && p->GetDistance(attacker) <= kHowlRange)
                ++in.attackers_close;

        // The pet fights what the warlock fights; the order costs no global cooldown.
        Pet* pet = p->GetPet();
        if (pet && pet->IsAlive() && p->IsInCombat() && pet->GetVictim() != target)
            api.pet_attack(target->GetGUID());
    }

    std::string refused;
    for (int attempt = 0; attempt < kMaxPicks; ++attempt)
    {
        in.blocked = 0;
        for (uint32 s = DemonArmor; s < Count; ++s)
            if (blocked_until[s] > now)
                in.blocked |= Bit(Cast(s));

        char const* why = "";
        Cast const pick = Pick(in, &why);
        if (pick == None)
        {
            TC_LOG_DEBUG("playerbot.v2", "[lazy] {}: nothing ({}) hp={} mana={} target_hp={} shards={} blocked={:#x}", p->GetName(), why, in.hp_pct, in.mana_pct,
                         in.target_hp_pct, in.soul_shards, in.blocked);
            return refused;
        }

        bool const self_cast = pick == DemonArmor || pick == SummonPet || pick == LifeTap || pick == Racial || pick == AmplifyCurse || pick == HowlOfTerror;
        ObjectGuid const cast_at = self_cast ? ObjectGuid::Empty : target->GetGUID();
        Result const r = is_item(pick) ? api.use_item_by_slot(INVENTORY_SLOT_BAG_0, pick == Trinket1 ? EQUIPMENT_SLOT_TRINKET1 : EQUIPMENT_SLOT_TRINKET2)
                                       : api.cast_spell(ids[pick], cast_at);
        TC_LOG_INFO("playerbot.v2", "[lazy] {}: {} ({}) -> {} hp={} mana={} target_hp={} shards={} pet={}", p->GetName(),
                    SpellText(is_item(pick) ? ids[pick] : api.highest_known_rank(ids[pick])), why, uint32(r), in.hp_pct, in.mana_pct, in.target_hp_pct, in.soul_shards,
                    in.has_pet);
        if (r == Result::Ok && OffGlobalCooldown(pick))
        {
            // No global cooldown spent: the same press goes on to the next spell.
            in.ready &= ~Bit(pick);
            continue;
        }
        switch (r)
        {
            case Result::Ok:
            case Result::NotReady:
            case Result::Locked:
            case Result::OutOfRange:
                return refused;
            default:
                // Refused (no shard, no mana, wrong target...): leave it out for a few seconds and pick the next step.
                blocked_until[pick] = now + (pick == SummonPet ? kBlockPetMs : kBlockMs);
                in.blocked |= Bit(pick);
                if (r == Result::NotEnoughResource)
                    refused = "Lazy lock: out of mana or reagents for " + std::string(SpellText(api.highest_known_rank(ids[pick]))) + ", skipping it.";
                break;
        }
    }
    return refused;
}

} // namespace Playerbot
