// One-button warlock: gathers the facts for Lazy::Pick (Combat/LazyLock.h) from the live Player and casts the answer through the
// bot API (highest known rank, range / facing / stun / silence gates). World thread only (the command and add-on handlers run there).

#include "LazyLock.h"

#include "PlayerbotAPI.h"
#include "Combat/LazyLock.h"
#include "Creature.h"
#include "Item.h"
#include "Pet.h"
#include "Player.h"
#include "SharedDefines.h"
#include "Spell.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "World.h"

namespace Playerbot {

namespace {

constexpr uint32 kSoulShard      = 6265;
constexpr uint32 kShadowTrance   = 17941;   // Nightfall proc
constexpr uint32 kShoot          = 5019;
constexpr float  kCastRange      = 30.0f;

// Rank 1 ids; the API resolves the highest rank the warlock knows.
constexpr uint32 kDemonSkin = 687, kDemonArmor = 706, kImp = 688, kVoidwalker = 697, kBloodFury = 20572, kBerserking = 20554;
constexpr uint32 kTongues = 1714;

int32 Pct(int64 value, int64 max) { return max > 0 ? int32(value * 100 / max) : 100; }

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

    Input in;
    for (uint32 s = DemonArmor; s < Count; ++s)
    {
        if (known(ids[s]))
            in.known |= Bit(Cast(s));
        if (ready(ids[s]))
            in.ready |= Bit(Cast(s));
        if (valid_target && target->HasAura(api.highest_known_rank(ids[s]), p->GetGUID()))
            in.on_target |= Bit(Cast(s));
    }

    in.casting        = p->IsNonMeleeSpellCast(false, false, true);
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

        // The pet fights what the warlock fights; the order costs no global cooldown.
        Pet* pet = p->GetPet();
        if (pet && pet->IsAlive() && p->IsInCombat() && pet->GetVictim() != target)
            api.pet_attack(target->GetGUID());
    }

    Cast const pick = Pick(in);
    if (pick == None)
        return {};

    ObjectGuid const cast_at = (pick == DemonArmor || pick == SummonPet || pick == LifeTap || pick == Racial || pick == AmplifyCurse) ? ObjectGuid::Empty
                                                                                                                                     : target->GetGUID();
    Result const r = api.cast_spell(ids[pick], cast_at);
    switch (r)
    {
        case Result::Ok:
        case Result::NotReady:
        case Result::Locked:
        case Result::OutOfRange:
            return {};
        case Result::NotEnoughResource:
            return "Lazy lock: out of mana or reagents.";
        default:
            break;
    }
    SpellInfo const* info = sSpellMgr->GetSpellInfo(api.highest_known_rank(ids[pick]), DIFFICULTY_NONE);
    char const* name = info && info->SpellName ? (*info->SpellName)[sWorld->GetDefaultDbcLocale()] : nullptr;
    return std::string("Lazy lock: ") + (name && *name ? name : "spell") + " refused.";
}

} // namespace Playerbot
