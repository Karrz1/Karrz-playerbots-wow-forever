#include "BotCharacterFactory.h"
#include "BotAccountMgr.h"
#include "BotIdentityRegistry.h"
#include "BotNamePool.h"
#include "../Services.h"
#include "../Session/BotSession.h"

#include "AccountMgr.h"
#include "CharacterCache.h"
#include "CharacterPackets.h"
#include "DatabaseEnv.h"
#include "DBCEnums.h"
#include "DB2Stores.h"
#include "DB2Structure.h"
#include "RaceMask.h"
#include <random>
#include "Log.h"
#include "MotionMaster.h"
#include "ObjectGuid.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "RealmList.h"
#include "ScriptMgr.h"
#include "SharedDefines.h"
#include "WorldSession.h"

#include <fmt/format.h>
#include <algorithm>
#include <map>
#include <memory>
#include <mutex>
#include <set>

namespace Playerbot::V2 {

namespace {

bool IsRacePlayable(uint8 race)
{
    ChrRacesEntry const* entry = sChrRacesStore.LookupEntry(race);
    if (!entry)
        return false;
    return !entry->GetFlags().HasFlag(ChrRacesFlag::NPCOnly);
}

bool IsClassPlayable(uint8 cls)
{
    return sChrClassesStore.LookupEntry(cls) != nullptr;
}

// Session-free evaluation of a ChrCustomizationReq for a bot of (race, class).
// The full WorldSession::MeetsChrCustomizationReq needs a live player/collection for its
// achievement/appearance/quest/dependent-choice gates; at bot-creation time we only have the
// race/class, so we evaluate the session-independent class- and race-mask gates and treat any
// account/character-scoped gate as unavailable. Every ChrCustomizationChoice in the 12.1 data
// carries a non-zero ChrCustomizationReqID, so a blanket "ReqID != 0 -> skip" filter rejected
// ALL choices and produced empty (client-crashing) appearances.
bool BotMeetsCustomizationReq(uint32 reqId, uint8 race, uint8 cls)
{
    if (!reqId)
        return true;

    ChrCustomizationReqEntry const* req = sChrCustomizationReqStore.LookupEntry(reqId);
    if (!req)
        return true;

    if (!req->GetFlags().HasFlag(ChrCustomizationReqFlag::HasRequirements))
        return true;

    if (req->ClassMask && !(req->ClassMask & (1 << (cls - 1))))
        return false;

    if (race != RACE_NONE && !req->RaceMask.IsEmpty()
        && req->RaceMask != RACEMASK_ALL_v<int32, 2> && !req->RaceMask.HasRace(Races(race)))
        return false;

    if (req->AchievementID || req->ItemModifiedAppearanceID || req->QuestID)
        return false;

    return true;
}

// The character creator only offers what the client itself can show. Beyond the
// race/class gates above it hides disabled options, NPC-only choices and anything
// tied to a non-default OverrideArchive. Classic 1.60 (Forever) ships ChrCustomization
// rows the client rejects (extra option 8522 on human males, newer skin choices);
// a bot carrying one crashed the owner's client (Invalid Option ID / JamVectorDelta).
bool BotCanUseCustomizationReq(uint32 reqId, uint8 race, uint8 cls)
{
    if (!BotMeetsCustomizationReq(reqId, race, cls))
        return false;
    if (ChrCustomizationReqEntry const* req = sChrCustomizationReqStore.LookupEntry(reqId))
        if (req->GetFlags().HasFlag(ChrCustomizationReqFlag::HasRequirements) && req->OverrideArchive > 0)
            return false;
    return true;
}

bool BotCanUseOption(ChrCustomizationOptionEntry const* opt, uint8 race, uint8 cls)
{
    if (opt->Flags & AsUnderlyingType(ChrCustomizationOptionFlag::Disabled))
        return false;
    return BotCanUseCustomizationReq(opt->ChrCustomizationReqID, race, cls);
}

bool BotCanUseChoice(ChrCustomizationChoiceEntry const* c, uint8 race, uint8 cls)
{
    if (c->Flags & 0x1)   // NPC-only choice
        return false;
    return BotCanUseCustomizationReq(c->ChrCustomizationReqID, race, cls);
}

// Appearances of client-made (non-bot) characters, by (race, gender): the options
// the client sends and every choice seen per option. When a race/gender has one,
// bots pick only from it: less variety, but nothing the client can reject. Each new
// client-made character of that race/gender widens the pool.
struct AppearanceTemplate
{
    std::map<uint32, std::set<uint32>> choicesByOption;
};

std::mutex g_templateLock;
std::map<std::pair<uint8, uint8>, AppearanceTemplate> g_templates;
bool g_templatesLoaded = false;

void LoadAppearanceTemplates_locked()
{
    g_templates.clear();
    g_templatesLoaded = true;
    QueryResult r = CharacterDatabase.Query(
        "SELECT c.race, c.gender, cc.chrCustomizationOptionID, cc.chrCustomizationChoiceID "
        "FROM characters c JOIN character_customizations cc ON cc.guid = c.guid "
        "WHERE c.account NOT IN (SELECT account_id FROM playerbot_v2_account)");
    if (!r)
        return;
    do
    {
        Field* f = r->Fetch();
        g_templates[{ f[0].GetUInt8(), f[1].GetUInt8() }].choicesByOption[f[2].GetUInt32()].insert(f[3].GetUInt32());
    } while (r->NextRow());
}

std::vector<UF::ChrCustomizationChoice> RollCustomizations(uint8 race, uint8 gender, uint8 cls)
{
    std::vector<UF::ChrCustomizationChoice> out;
    auto const* options = sDB2Manager.GetCustomiztionOptions(race, gender);
    if (!options)
        return out;

    AppearanceTemplate tmpl;
    bool haveTemplate = false;
    {
        std::lock_guard lock(g_templateLock);
        if (!g_templatesLoaded)
            LoadAppearanceTemplates_locked();
        if (auto itr = g_templates.find({ race, gender }); itr != g_templates.end())
        {
            tmpl = itr->second;
            haveTemplate = true;
        }
    }

    static thread_local std::mt19937 cust_rng{std::random_device{}()};
    for (ChrCustomizationOptionEntry const* opt : *options)
    {
        if (!opt)
            continue;
        std::set<uint32> const* seen = nullptr;
        if (haveTemplate)
        {
            auto itr = tmpl.choicesByOption.find(opt->ID);
            if (itr == tmpl.choicesByOption.end())
                continue;   // the client does not send this option
            seen = &itr->second;
        }
        else if (!BotCanUseOption(opt, race, cls))
            continue;

        auto const* choices = sDB2Manager.GetCustomiztionChoices(opt->ID);
        if (!choices || choices->empty())
            continue;

        std::vector<ChrCustomizationChoiceEntry const*> candidates;
        candidates.reserve(choices->size());
        for (ChrCustomizationChoiceEntry const* c : *choices)
            if (c && (seen ? seen->count(c->ID) != 0 : BotCanUseChoice(c, race, cls)))
                candidates.push_back(c);
        if (candidates.empty())
            continue;

        std::uniform_int_distribution<size_t> pick(0, candidates.size() - 1);
        UF::ChrCustomizationChoice entry;
        entry.ChrCustomizationOptionID = opt->ID;
        entry.ChrCustomizationChoiceID = candidates[pick(cust_rng)]->ID;
        out.push_back(entry);
    }
    std::ranges::sort(out, {}, &UF::ChrCustomizationChoice::ChrCustomizationOptionID);
    return out;
}

// Would RollCustomizations have produced this (option, choice) set's shape?
bool IsBotAppearanceValid(uint8 race, uint8 gender, uint8 cls, std::vector<std::pair<uint32, uint32>> const& current)
{
    auto const* options = sDB2Manager.GetCustomiztionOptions(race, gender);
    if (!options)
        return current.empty();

    AppearanceTemplate tmpl;
    bool haveTemplate = false;
    {
        std::lock_guard lock(g_templateLock);
        if (!g_templatesLoaded)
            LoadAppearanceTemplates_locked();
        if (auto itr = g_templates.find({ race, gender }); itr != g_templates.end())
        {
            tmpl = itr->second;
            haveTemplate = true;
        }
    }

    for (auto const& [optionId, choiceId] : current)
    {
        auto optItr = std::ranges::find(*options, optionId, &ChrCustomizationOptionEntry::ID);
        if (optItr == options->end())
            return false;
        std::set<uint32> const* seen = nullptr;
        if (haveTemplate)
        {
            auto itr = tmpl.choicesByOption.find(optionId);
            if (itr == tmpl.choicesByOption.end())
                return false;
            seen = &itr->second;
        }
        else if (!BotCanUseOption(*optItr, race, cls))
            return false;

        if (seen)
        {
            if (!seen->count(choiceId))
                return false;
            continue;
        }
        auto const* choices = sDB2Manager.GetCustomiztionChoices(optionId);
        if (!choices)
            return false;
        auto choiceItr = std::ranges::find_if(*choices, [&](ChrCustomizationChoiceEntry const* c) { return c && c->ID == choiceId; });
        if (choiceItr == choices->end() || !BotCanUseChoice(*choiceItr, race, cls))
            return false;
    }
    return true;
}

} // anonymous

BotCharacterFactory::Result BotCharacterFactory::Create(
    WorldSession*      ownerSession,
    std::string const& name,
    uint8              race,
    uint8              charClass,
    uint8              gender)
{
    if (!IsRacePlayable(race))
        return {false, fmt::format("race {} is not playable", race), {}};

    if (!IsClassPlayable(charClass))
        return {false, fmt::format("class {} is not playable", charClass), {}};

    if (gender != GENDER_MALE && gender != GENDER_FEMALE)
        return {false, fmt::format("gender {} must be 0 (male) or 1 (female)", gender), {}};

    // race/class pair must have a PlayerInfo (start position, items, spells)
    if (!sObjectMgr->GetPlayerInfo(race, charClass))
        return {false, fmt::format("race/class pair {}/{} has no PlayerInfo (invalid combination)", race, charClass), {}};

    // Build name: normalize, validate, and ensure uniqueness via the cache.
    // ownerSession is optional — null comes from headless callers (auto-spawn
    // on boot) where there's no GM session to read locale from. Default to
    // LOCALE_enUS in that case so the latin name validator runs the same
    // ruleset it would for an English-locale GM.
    std::string normalized = name;
    if (!normalizePlayerName(normalized))
        return {false, "invalid name (failed normalization)", {}};

    const LocaleConstant nameLocale = ownerSession
        ? ownerSession->GetSessionDbcLocale()
        : LOCALE_enUS;
    if (auto code = ObjectMgr::CheckPlayerName(normalized, nameLocale, true);
        code != CHAR_NAME_SUCCESS)
        return {false, fmt::format(
            "ObjectMgr::CheckPlayerName rejected '{}' with code={}", normalized, uint32(code)), {}};

    if (sObjectMgr->IsReservedName(normalized))
        return {false, "name is reserved", {}};

    if (sCharacterCache->GetCharacterCacheByName(normalized))
        return {false, "name already in use", {}};

    // Acquire a slot from the dedicated bot account pool. The new char
    // is owned by that pool account, NOT the GM's. AcquireSlot returns 0
    // only if pool starvation (account creation refused) — see log.
    const uint32 botAccountId = Services::Accounts().AcquireSlot();
    if (!botAccountId)
        return {false, "BotAccountMgr::AcquireSlot failed (pool starvation; see playerbot.v2 log)", {}};

    // Resolve the bot account name for the temporary BotSession's audit
    // string (used by WorldSession diagnostics). Falls back to a synthetic
    // tag if the account name lookup fails (which only happens if the
    // account was deleted between AcquireSlot and now — race-impossible
    // on the world thread).
    std::string botAccountName;
    if (!sAccountMgr->GetName(botAccountId, botAccountName))
        botAccountName = fmt::format("acct{}", botAccountId);

    // Build CharacterCreateInfo with DB2-driven random customizations.
    //
    // Previously Customizations was left empty. Player::Create accepts that
    // (ValidateAppearance treats an empty iterator as valid after confirming
    // the race/gender has DB2 options at all), but the resulting bot has
    // the bare default face/hair/skin — every bot of a given race+sex
    // looks identical, which reads as "obvious bot" in a city full of them.
    //
    // The client-side character creator picks one Choice per Option for the
    // chosen (race, sex). We mirror that by walking
    // sDB2Manager.GetCustomiztionOptions(race, sex) and rolling one valid
    // Choice per option. Skips choices flagged with NPC_ONLY (Flags bit 0)
    // to avoid awkward NPC-template appearances slipping into player bots.
    auto createInfo = std::make_shared<WorldPackets::Character::CharacterCreateInfo>();
    createInfo->Race  = race;
    createInfo->Class = charClass;
    createInfo->Sex   = gender;
    createInfo->Name  = normalized;

    for (UF::ChrCustomizationChoice const& c : RollCustomizations(race, gender, charClass))
        createInfo->Customizations.push_back(c);

    // Temporary BotSession owned by the bot account. Player::Create reads
    // GetSession()->GetAccountId() when writing the characters row, and
    // ValidateAppearance / IsARecruiter via GetSession() — all of which
    // are safe on a socketless BotSession. The session is discarded as
    // soon as this function returns; the actual login uses a fresh
    // BotSession created by BotSessionMgr::LoginBot.
    auto creationSession = std::make_shared<BotSession>(botAccountId, std::string(botAccountName));

    // Mirror the core's shared_ptr<Player> with cleanup deleter so the
    // Player is properly torn down (map, motion master, lock state) when it
    // goes out of scope.
    std::shared_ptr<Player> newChar(new Player(creationSession.get()), [](Player* p)
    {
        p->CleanupsBeforeDelete();
        delete p;
    });

    newChar->GetMotionMaster()->Initialize();

    ObjectGuid::LowType newGuidLow = sObjectMgr->GetGenerator<HighGuid::Player>().Generate();
    if (!newChar->Create(newGuidLow, createInfo.get()))
    {
        // Player::Create failed AFTER we may have acquired the name from
        // BotNamePool — release it so the slot returns to the pool.
        Fleet::BotNamePool::Release(normalized);
        return {false, "Player::Create failed (race/class/appearance validation)", {}};
    }

    newChar->SetAtLoginFlag(AT_LOGIN_FIRST);   // grants starting items + cinematic on first login

    // SaveToDB(true) commits asynchronously - the call returns before the
    // INSERT INTO characters has actually landed on the DB worker. Under
    // load (mass spawn after wipefleet), the caller's subsequent LoginBot
    // racing the async write produced "Player::LoadFromDB ... not found
    // in table characters". A previous fix attempted DirectCommitTransaction
    // for sync semantics but failed at runtime - several Player::SaveToDB
    // statements are flagged CONNECTION_ASYNC only and aren't loaded on the
    // sync connection (assertion failure: m_mStmt at MySQLConnection.cpp:224).
    //
    // The async path is the only one TC really supports for character save.
    // To avoid the LoginBot race, callers (BotPopulationManager::SpawnNew)
    // defer the LoginBot call by one world tick - by then the async worker
    // has committed and LoadFromDB sees the row. See BotPopulationManager
    // for the deferred-login queue.
    newChar->SaveToDB(true);

    ObjectGuid newGuid = ObjectGuid::Create<HighGuid::Player>(newGuidLow);

    // Mirror the core's post-save bookkeeping (script hook + cache entry).
    sScriptMgr->OnPlayerCreate(newChar.get());
    sCharacterCache->AddCharacterCacheEntry(
        newGuid,
        botAccountId,                    // owner is the bot account, NOT the GM
        normalized,
        Gender(gender),
        race,
        charClass,
        newChar->GetLevel(),
        false /*isDeleted*/);

    // Mark as a V2 bot. The shared_ptr deleter will run CleanupsBeforeDelete
    // on the Player when this function returns; the row in `characters` and
    // the entry in `playerbot_v2_character` persist.
    Services::Lifecycle().mark_as_bot(newGuidLow);

    // Bind the name pool entry to this character so future delete paths
    // (hygiene cron, .playerbot delete) can release the name on cleanup.
    // No-op if the name came from the syllable-gen fallback.
    Fleet::BotNamePool::BindToCharacter(normalized, newGuidLow);

    // Update the pool's character-count cache so the next AcquireSlot
    // accounts for this new char (preventing 11+ chars on one account).
    Services::Accounts().note_character_added(botAccountId);

    // Update realmcharacters so this bot account's character count stays
    // accurate. Mirrors the LOGIN_REP_REALM_CHARACTERS query that core's
    // HandleCharCreateOpcode runs at the end of finalizeCharacterCreation.
    // Counts ALL chars on the account via a fresh SELECT (cheaper than
    // tracking deltas across our path which doesn't see core deletes).
    if (auto countResult = CharacterDatabase.PQuery(
            "SELECT COUNT(*) FROM characters WHERE account = {}", botAccountId))
    {
        const uint32 newCharCount = uint32(countResult->Fetch()[0].GetUInt64());
        LoginDatabase.PExecute(
            "REPLACE INTO realmcharacters (numchars, acctid, realmid) VALUES ({}, {}, {})",
            newCharCount, botAccountId, sRealmList->GetCurrentRealmId().Realm);
    }

    TC_LOG_INFO("playerbot.v2",
        "[BotCharacterFactory] Created bot character '{}' (guid {}, race {} class {} gender {}) on bot account '{}' (id {}) — requested by GM account {}",
        normalized, newGuid.ToString(), race, charClass, gender,
        botAccountName, botAccountId,
        ownerSession ? ownerSession->GetAccountId() : 0);

    return {true, {}, newGuid};
}

uint32 BotCharacterFactory::RepairAppearances()
{
    {
        std::lock_guard lock(g_templateLock);
        LoadAppearanceTemplates_locked();
    }

    QueryResult r = CharacterDatabase.Query(
        "SELECT c.guid, c.race, c.class, c.gender, cc.chrCustomizationOptionID, cc.chrCustomizationChoiceID "
        "FROM characters c JOIN playerbot_v2_account a ON a.account_id = c.account "
        "LEFT JOIN character_customizations cc ON cc.guid = c.guid ORDER BY c.guid");
    if (!r)
        return 0;

    struct BotAppearance
    {
        uint8 race = 0, cls = 0, gender = 0;
        std::vector<std::pair<uint32, uint32>> choices;
    };
    std::map<uint64, BotAppearance> bots;
    do
    {
        Field* f = r->Fetch();
        BotAppearance& b = bots[f[0].GetUInt64()];
        b.race = f[1].GetUInt8();
        b.cls = f[2].GetUInt8();
        b.gender = f[3].GetUInt8();
        if (!f[4].IsNull())
            b.choices.emplace_back(f[4].GetUInt32(), f[5].GetUInt32());
    } while (r->NextRow());

    uint32 repaired = 0;
    for (auto const& [guid, b] : bots)
    {
        if (IsBotAppearanceValid(b.race, b.gender, b.cls, b.choices))
            continue;
        std::vector<UF::ChrCustomizationChoice> fresh = RollCustomizations(b.race, b.gender, b.cls);
        // Runs at module init, before any bot logs in: write synchronously so a
        // login can never load the old rows.
        CharacterDatabase.DirectPExecute("DELETE FROM character_customizations WHERE guid = {}", guid);
        for (UF::ChrCustomizationChoice const& c : fresh)
            CharacterDatabase.DirectPExecute(
                "INSERT INTO character_customizations (guid, chrCustomizationOptionID, chrCustomizationChoiceID) VALUES ({}, {}, {})",
                guid, c.ChrCustomizationOptionID, c.ChrCustomizationChoiceID);
        ++repaired;
    }

    TC_LOG_INFO("server.loading", "[PlayerbotV2] Bot appearances: {} of {} bot characters repaired", repaired, uint32(bots.size()));
    return repaired;
}

} // namespace Playerbot::V2
