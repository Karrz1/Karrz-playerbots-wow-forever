/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

// Classic Alterac Valley (map 30), the 1.12 battle on the classic map's own spawns.
//
// The stock script (Battlegrounds/AlteracValley) drives the retail map with per-node CAPTURE objects and spawn groups.
// The classic map here carries the VMaNGOS spawns instead, and VMaNGOS decides which of them exist through battleground
// events: every graveyard banner and aura, every guard of every upgrade tier, every tower marshal, mine creature, captain
// and commander belongs to one event (event1) in one state (event2), and exists only while that event is in that state.
// The world DB spawns all of them at once, so this script carries VMaNGOS's event table
// (classic_battleground_alterac_valley_spawns.h), keeps the active state per event, suppresses inactive spawns through
// the zone script entry hooks, and spawns or despawns the members of an event when its state changes.
//
// Rules (VMaNGOS BattleGroundAV): 15 nodes, seven graveyards and eight towers. A graveyard or tower held by a team is
// assaulted by an enemy click on its banner; after five minutes the assault succeeds: a graveyard changes hands, a tower
// is destroyed (-75 reinforcements for its builders). A click by the previous owners on the assaulted banner defends
// it. Snowfall starts neutral. Towers are never rebuilt. Each team starts with 600 reinforcements, loses one per death
// and 100 for its captain; the two mines add one every 45 s to their owner. Killing a general ends the battle; as in
// 1.12, running out of reinforcements does not. The node world states are the ones the bots' snapshot reads.

#include "classic_battleground_alterac_valley_spawns.h"
#include "Battleground.h"
#include "BattlegroundMgr.h"
#include "BattlegroundScript.h"
#include "ChatPackets.h"
#include "Containers.h"
#include "Creature.h"
#include "CreatureAI.h"
#include "GameObject.h"
#include "GameObjectAI.h"
#include "Log.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "Random.h"
#include "ScriptMgr.h"
#include "SpellMgr.h"
#include "StringFormat.h"
#include <unordered_map>

namespace ClassicAlteracValley
{
    static constexpr uint32 MapId = 30;
    static constexpr int32 InitialReinforcements = 600;
    static constexpr uint32 CaptureTime = 5 * MINUTE * IN_MILLISECONDS;
    static constexpr uint32 MineTickTime = 45 * IN_MILLISECONDS;
    static constexpr uint32 MineReclaimTime = 20 * MINUTE * IN_MILLISECONDS;
    static constexpr uint32 SnivvleYellTime = 70 * IN_MILLISECONDS;
    // ZoneScript::DoAction id the banners send; unique, see classic_battleground_warsong_gulch.cpp.
    static constexpr uint32 ActionUseBanner = 0x41560001;
    // The retail quest scripts (Battlegrounds/AlteracValley/alterac_valley.h ACTION_TURN_IN_SCRAPS) report armor scrap
    // turn-ins to the zone script with this id.
    static constexpr uint32 ActionTurnInScraps = 3;

    static constexpr uint8 EVENT_NONE = 0xFF;
    static constexpr uint8 NodeCount = 15;
    static constexpr uint8 GraveyardCount = 7;
    static constexpr uint8 MineCount = 2;

    // VMaNGOS BattleGroundAV event ids (event1).
    enum Events : uint8
    {
        EVENT_NODE_DEFENDERS    = 15,   // + node: graveyard guards (nodes 0-6), tower marshals (nodes 7-14)
        EVENT_TOWER_DEFENDERS   = 23,   // + node: tower archers (nodes 7-14, i.e. 30-37)
        EVENT_MINE_BOSS         = 46,   // + mine
        EVENT_CAPTAIN_ALLIANCE  = 48,
        EVENT_CAPTAIN_HORDE     = 49,
        EVENT_MINE              = 50,   // + mine
        EVENT_COMMANDER_FIRST   = 52,
        EVENT_COMMANDER_LAST    = 59,
        EVENT_HERALD            = 60,
        EVENT_BOSS_ALLIANCE     = 61,
        EVENT_BOSS_HORDE        = 62,
        EVENT_CAPTAIN_DEAD_ALLIANCE = 63,
        EVENT_CAPTAIN_DEAD_HORDE    = 64,
        EVENT_SNIVVLE           = 65,
        EVENT_EXPLOSIVES_EXPERT_ALLIANCE = 66,
        EVENT_EXPLOSIVES_EXPERT_HORDE    = 67,
        EVENT_LIEUTENANT_ALLIANCE = 68,
        EVENT_LIEUTENANT_HORDE    = 69,
        EVENT_LANDMINES_HORDE     = 100,
        EVENT_LANDMINES_ALLIANCE  = 101,
        EVENT_DOORS               = 254
    };

    // Owner index as VMaNGOS uses it: 0 Alliance, 1 Horde, 2 neutral (Snowfall, the mines).
    enum Owner : uint8
    {
        OWNER_ALLIANCE  = 0,
        OWNER_HORDE     = 1,
        OWNER_NEUTRAL   = 2
    };

    enum NodeState : uint8
    {
        STATE_ASSAULTED     = 0,
        STATE_CONTROLLED    = 1
    };

    enum Creatures : uint32
    {
        NPC_LANDMINES_LAYER_ALLIANCE    = 13356,
        NPC_LANDMINES_LAYER_HORDE       = 13357,
        NPC_EXPLOSIVES_EXPERT_HORDE     = 13597,
        NPC_EXPLOSIVES_EXPERT_ALLIANCE  = 13598
    };

    enum GameObjects : uint32
    {
        GO_GATE         = 180424,
        GO_GHOST_GATE   = 180322
    };

    enum Spells : uint32
    {
        SPELL_SPIRIT_OF_REDEMPTION      = 27827,
        SPELL_GENERAL_KILLED_QUEST      = 23658,
        SPELL_CAPTAIN_BUFF_ALLIANCE     = 23693,
        SPELL_CAPTAIN_BUFF_HORDE        = 22751,
        SPELL_SEASONED_UNITS            = 28418,
        SPELL_VETERAN_UNITS             = 28419,
        SPELL_CHAMPION_UNITS            = 28420
    };

    enum Factions : uint32
    {
        FACTION_FROSTWOLF_CLAN  = 729,
        FACTION_STORMPIKE_GUARD = 730
    };

    // 1.12 rewards (VMaNGOS BattleGroundAV.h). Honor counts are kills for GetBonusHonorFromKill().
    enum Rewards : uint32
    {
        KILL_BOSS                   = 6,
        REP_BOSS                    = 350,
        KILL_CAPTAIN                = 3,
        REP_CAPTAIN                 = 125,
        RES_CAPTAIN                 = 100,
        KILL_COMMANDER              = 1,
        REP_COMMANDER               = 12,
        KILL_TOWER                  = 2,
        REP_TOWER                   = 12,
        RES_TOWER                   = 75,
        KILL_SURVIVING_TOWER        = 3,
        REP_SURVIVING_TOWER         = 12,
        KILL_SURVIVING_CAPTAIN      = 3,
        REP_SURVIVING_CAPTAIN       = 125,
        KILL_SURVIVING_GRAVE        = 1,
        REP_OWNED_GRAVE             = 12,
        KILL_SURVIVING_MINE         = 1,
        REP_OWNED_MINE              = 24
    };

    enum Quests : uint32
    {
        QUEST_TOWERS_ALLIANCE       = 7102,
        QUEST_TOWERS_HORDE          = 7101,
        QUEST_GRAVEYARDS_ALLIANCE   = 7081,
        QUEST_GRAVEYARDS_HORDE      = 7082,
        QUEST_MINE_ALLIANCE         = 7122,
        QUEST_MINE_HORDE            = 7124,
        QUEST_COMMANDER_ALLIANCE    = 7281,     // Karl Philips
        QUEST_COMMANDER_HORDE       = 7282      // Louis Philips
    };

    enum PvpStats : uint32
    {
        PVP_STAT_TOWERS_ASSAULTED       = 61,
        PVP_STAT_GRAVEYARDS_ASSAULTED   = 63,
        PVP_STAT_TOWERS_DEFENDED        = 64,
        PVP_STAT_GRAVEYARDS_DEFENDED    = 65
    };

    enum Sounds : uint32
    {
        SOUND_ALLIANCE_ASSAULTS     = 8212,
        SOUND_HORDE_ASSAULTS        = 8174,
        SOUND_ALLIANCE_GOOD         = 8173,
        SOUND_HORDE_GOOD            = 8213,
        SOUND_BOTH_TOWER_DEFEND     = 8192
    };

    enum WorldStates : int32
    {
        WS_ALLIANCE_REINFORCEMENTS      = 3127,
        WS_HORDE_REINFORCEMENTS         = 3128,
        WS_SHOW_HORDE_REINFORCEMENTS    = 3133,
        WS_SHOW_ALLIANCE_REINFORCEMENTS = 3134,
        WS_SNOWFALL_NEUTRAL             = 1966,
        WS_DREKTHAR_ALIVE               = 601,
        WS_VANNDAR_ALIVE                = 602,
        WS_BALINDA_ALIVE                = 1351,
        WS_GALVANGAR_ALIVE              = 1352
    };

    struct NodeInfo
    {
        std::string_view Name;
        // Alliance controlled, Alliance assaulting, Horde controlled, Horde assaulting. For a tower, "controlled" by the
        // team that did not build it means destroyed.
        int32 WorldStates[4];
    };

    // Node order and world states as VMaNGOS (BG_AV_Nodes, BG_AV_NodeWorldStates); the same ids as the retail script.
    static NodeInfo const Nodes[NodeCount] =
    {
        { "Stormpike Aid Station",      { 1325, 1326, 1327, 1328 } },
        { "Stormpike Graveyard",        { 1333, 1335, 1334, 1336 } },
        { "Stonehearth Graveyard",      { 1302, 1304, 1301, 1303 } },
        { "Snowfall Graveyard",         { 1341, 1343, 1342, 1344 } },
        { "Iceblood Graveyard",         { 1346, 1348, 1347, 1349 } },
        { "Frostwolf Graveyard",        { 1337, 1339, 1338, 1340 } },
        { "Frostwolf Relief Hut",       { 1329, 1331, 1330, 1332 } },
        { "Dun Baldar South Bunker",    { 1361, 1375, 1370, 1378 } },
        { "Dun Baldar North Bunker",    { 1362, 1374, 1371, 1379 } },
        { "Icewing Bunker",             { 1363, 1376, 1372, 1380 } },
        { "Stonehearth Bunker",         { 1364, 1377, 1373, 1381 } },
        { "Iceblood Tower",             { 1368, 1390, 1385, 1395 } },
        { "Tower Point",                { 1367, 1389, 1384, 1394 } },
        { "East Frostwolf Tower",       { 1366, 1388, 1383, 1393 } },
        { "West Frostwolf Tower",       { 1365, 1387, 1382, 1392 } },
    };

    struct MineInfo
    {
        std::string_view Name;
        int32 WorldStates[3];   // Alliance, Horde, neutral
    };

    static MineInfo const Mines[MineCount] =
    {
        { "Irondeep Mine",  { 1358, 1359, 1360 } },
        { "Coldtooth Mine", { 1355, 1356, 1357 } },
    };

    // VMaNGOS event table, indexed both ways.
    struct SpawnIndex
    {
        std::unordered_map<ObjectGuid::LowType, std::pair<uint8, uint8>> CreatureEvents;
        std::unordered_map<ObjectGuid::LowType, std::pair<uint8, uint8>> GameObjectEvents;
        std::unordered_map<uint16, std::vector<ObjectGuid::LowType>> CreaturesByEvent;
        std::unordered_map<uint16, std::vector<ObjectGuid::LowType>> GameObjectsByEvent;
    };

    static uint16 EventKey(uint8 event1, uint8 event2) { return uint16(event1) << 8 | event2; }

    static SpawnIndex const& GetSpawnIndex()
    {
        static SpawnIndex const index = []
        {
            SpawnIndex result;
            uint32 missing = 0;
            for (ClassicAvSpawnEvent const& spawn : CreatureSpawnEvents)
            {
                CreatureData const* data = sObjectMgr->GetCreatureData(spawn.SpawnId);
                if (!data || data->mapId != MapId)
                {
                    ++missing;
                    continue;
                }
                result.CreatureEvents.emplace(spawn.SpawnId, std::make_pair(spawn.Event1, spawn.Event2));
                result.CreaturesByEvent[EventKey(spawn.Event1, spawn.Event2)].push_back(spawn.SpawnId);
            }
            for (ClassicAvSpawnEvent const& spawn : GameObjectSpawnEvents)
            {
                GameObjectData const* data = sObjectMgr->GetGameObjectData(spawn.SpawnId);
                if (!data || data->mapId != MapId)
                {
                    ++missing;
                    continue;
                }
                result.GameObjectEvents.emplace(spawn.SpawnId, std::make_pair(spawn.Event1, spawn.Event2));
                result.GameObjectsByEvent[EventKey(spawn.Event1, spawn.Event2)].push_back(spawn.SpawnId);
            }
            if (missing)
                TC_LOG_ERROR("bg.battleground", "Classic Alterac Valley: {} spawns of the event table are not map {} spawns in the world DB; they are left out.",
                    missing, MapId);
            return result;
        }();
        return index;
    }

    static std::string_view TeamName(uint8 owner) { return owner == OWNER_ALLIANCE ? "Alliance" : "Horde"; }
    static Team TeamOf(uint8 owner) { return owner == OWNER_ALLIANCE ? ALLIANCE : HORDE; }
    static uint32 FactionOf(uint8 owner) { return owner == OWNER_ALLIANCE ? FACTION_STORMPIKE_GUARD : FACTION_FROSTWOLF_CLAN; }
    static bool IsTower(uint8 node) { return node >= GraveyardCount; }

struct classic_battleground_alterac_valley : BattlegroundScript
{
    explicit classic_battleground_alterac_valley(BattlegroundMap* map) : BattlegroundScript(map)
    {
        _activeEvents.fill(EVENT_NONE);
        _respawnStopped.fill(false);

        // Initial state (VMaNGOS BattleGroundAV::Reset / InitNode).
        for (uint8 node = 0; node < NodeCount; ++node)
        {
            uint8 const owner = node == 3 ? OWNER_NEUTRAL : (node <= 2 || (node >= 7 && node <= 10)) ? OWNER_ALLIANCE : OWNER_HORDE;
            NodeData& data = _nodes[node];
            data.TotalOwner = data.Owner = data.PrevOwner = owner;
            data.State = STATE_CONTROLLED;
            data.Timer = 0;
            _activeEvents[node] = owner * 2 + STATE_CONTROLLED;
            if (IsTower(node))
            {
                _activeEvents[EVENT_NODE_DEFENDERS + node] = owner * 2 + STATE_CONTROLLED;
                _activeEvents[EVENT_TOWER_DEFENDERS + node] = owner * 4;
            }
            else
                _activeEvents[EVENT_NODE_DEFENDERS + node] = owner * 4;
        }

        for (uint8 mine = 0; mine < MineCount; ++mine)
        {
            _mineOwner[mine] = OWNER_NEUTRAL;
            _mineTimer[mine] = MineTickTime;
            _mineReclaimTimer[mine] = 0;
            _activeEvents[EVENT_MINE_BOSS + mine] = OWNER_NEUTRAL;
            _activeEvents[EVENT_MINE + mine] = OWNER_NEUTRAL;
        }

        for (uint8 event : { uint8(EVENT_CAPTAIN_ALLIANCE), uint8(EVENT_CAPTAIN_HORDE), uint8(EVENT_HERALD), uint8(EVENT_SNIVVLE),
            uint8(EVENT_BOSS_ALLIANCE), uint8(EVENT_BOSS_HORDE), uint8(EVENT_LANDMINES_ALLIANCE), uint8(EVENT_LANDMINES_HORDE),
            uint8(EVENT_EXPLOSIVES_EXPERT_ALLIANCE), uint8(EVENT_EXPLOSIVES_EXPERT_HORDE), uint8(EVENT_LIEUTENANT_ALLIANCE),
            uint8(EVENT_LIEUTENANT_HORDE), uint8(EVENT_DOORS) })
            _activeEvents[event] = 0;
        for (uint8 event = EVENT_COMMANDER_FIRST; event <= EVENT_COMMANDER_LAST; ++event)
            _activeEvents[event] = 0;

        _reinforcements = { InitialReinforcements, InitialReinforcements };
        _scraps = { 0, 0 };
        _captainBuffTimer = { NextCaptainBuff(), NextCaptainBuff() };
        _snivvleTimer = SnivvleYellTime;
    }

    void OnInit() override
    {
        // Build the event index now rather than on the first spawn hook.
        GetSpawnIndex();

        for (uint8 node = 0; node < NodeCount; ++node)
            UpdateNodeWorldStates(node);
        for (uint8 mine = 0; mine < MineCount; ++mine)
            UpdateMineWorldStates(mine);
        UpdateWorldState(WS_SNOWFALL_NEUTRAL, 1);
        UpdateWorldState(WS_ALLIANCE_REINFORCEMENTS, _reinforcements[OWNER_ALLIANCE]);
        UpdateWorldState(WS_HORDE_REINFORCEMENTS, _reinforcements[OWNER_HORDE]);
        UpdateWorldState(WS_SHOW_ALLIANCE_REINFORCEMENTS, 0);
        UpdateWorldState(WS_SHOW_HORDE_REINFORCEMENTS, 0);
        UpdateWorldState(WS_BALINDA_ALIVE, 1);
        UpdateWorldState(WS_GALVANGAR_ALIVE, 1);
        UpdateWorldState(WS_VANNDAR_ALIVE, 1);
        UpdateWorldState(WS_DREKTHAR_ALIVE, 1);
    }

    // A spawn of the event table exists only while its event is in its state and respawning is not stopped. This also
    // keeps a dead guard of an assaulted graveyard from respawning, and a DB respawn of an inactive spawn from happening.
    uint32 GetCreatureEntry(ObjectGuid::LowType spawnId, CreatureData const* data) override
    {
        if (spawnId)
        {
            SpawnIndex const& index = GetSpawnIndex();
            auto itr = index.CreatureEvents.find(spawnId);
            if (itr != index.CreatureEvents.end() && !CanSpawn(itr->second.first, itr->second.second))
                return 0;
        }
        return data->id;
    }

    uint32 GetGameObjectEntry(ObjectGuid::LowType spawnId, uint32 entry) override
    {
        if (spawnId)
        {
            SpawnIndex const& index = GetSpawnIndex();
            auto itr = index.GameObjectEvents.find(spawnId);
            if (itr != index.GameObjectEvents.end() && !CanSpawn(itr->second.first, itr->second.second))
                return 0;
        }
        return entry;
    }

    void OnGameObjectCreate(GameObject* gameObject) override
    {
        switch (gameObject->GetEntry())
        {
            case GO_GATE:
                _doors.push_back(gameObject->GetGUID());
                break;
            case GO_GHOST_GATE:
                _ghostGates.push_back(gameObject->GetGUID());
                break;
            default:
                break;
        }
    }

    void OnCreatureCreate(Creature* creature) override
    {
        if (std::pair<uint8, uint8> const* event = CreatureEvent(creature))
            if (event->first == EVENT_HERALD || event->first == EVENT_CAPTAIN_ALLIANCE || event->first == EVENT_CAPTAIN_HORDE
                || event->first == EVENT_SNIVVLE)
                _eventCreature[event->first] = creature->GetGUID();
    }

    void OnStart() override
    {
        UpdateWorldState(WS_SHOW_ALLIANCE_REINFORCEMENTS, 1);
        UpdateWorldState(WS_SHOW_HORDE_REINFORCEMENTS, 1);

        for (ObjectGuid const& guid : _doors)
            if (GameObject* door = battlegroundMap->GetGameObject(guid))
                door->UseDoorOrButton();
        for (ObjectGuid const& guid : _ghostGates)
            if (GameObject* gate = battlegroundMap->GetGameObject(guid))
                gate->DespawnOrUnsummon(0ms, 24h);  // forced respawn time: the ghost gate spawns respawn after 25 s otherwise
    }

    void OnUpdate(uint32 diff) override
    {
        if (_snivvleTimer)
        {
            if (_snivvleTimer <= diff)
            {
                _snivvleTimer = 0;
                CreatureYell(EVENT_SNIVVLE, "Snivvle is here! Snivvle claims the Coldtooth Mine!");
            }
            else
                _snivvleTimer -= diff;
        }

        if (battleground->GetStatus() != STATUS_IN_PROGRESS)
            return;

        // Captains rally their team every two to six minutes while they live.
        for (uint8 owner = OWNER_ALLIANCE; owner <= OWNER_HORDE; ++owner)
        {
            if (_captainBuffTimer[owner] > diff)
            {
                _captainBuffTimer[owner] -= diff;
                continue;
            }
            _captainBuffTimer[owner] = NextCaptainBuff();
            if (IsActive(owner == OWNER_ALLIANCE ? EVENT_CAPTAIN_DEAD_ALLIANCE : EVENT_CAPTAIN_DEAD_HORDE, 0))
                continue;
            CastOnTeam(owner == OWNER_ALLIANCE ? SPELL_CAPTAIN_BUFF_ALLIANCE : SPELL_CAPTAIN_BUFF_HORDE, owner);
            CreatureYell(owner == OWNER_ALLIANCE ? EVENT_CAPTAIN_ALLIANCE : EVENT_CAPTAIN_HORDE, owner == OWNER_ALLIANCE
                ? "Take heart, Alliance! Throw these villains from Alterac Valley!" : "Now is the time to attack! For the Horde!");
        }

        // Owned mines feed reinforcements, and fall back to their creatures after a while.
        for (uint8 mine = 0; mine < MineCount; ++mine)
        {
            uint8 const owner = _mineOwner[mine];
            if (owner == OWNER_NEUTRAL)
                continue;
            if (_mineTimer[mine] <= diff)
            {
                _mineTimer[mine] = MineTickTime;
                UpdateScore(owner, 1);
            }
            else
                _mineTimer[mine] -= diff;

            if (_mineReclaimTimer[mine] > diff)
                _mineReclaimTimer[mine] -= diff;
            else
                ChangeMineOwner(mine, OWNER_NEUTRAL);
        }

        // An assault that holds for five minutes succeeds.
        for (uint8 node = 0; node < NodeCount; ++node)
        {
            NodeData& data = _nodes[node];
            if (data.State != STATE_ASSAULTED)
                continue;
            if (data.Timer > diff)
                data.Timer -= diff;
            else
                NodeCaptured(node);
        }
    }

    void OnPlayerKilled(Player* victim, Player* /*killer*/) override
    {
        if (battleground->GetStatus() != STATUS_IN_PROGRESS || victim->HasAura(SPELL_SPIRIT_OF_REDEMPTION))
            return;
        UpdateScore(OwnerOf(victim), -1);
    }

    void OnUnitKilled(Creature* victim, Unit* killer) override
    {
        if (battleground->GetStatus() != STATUS_IN_PROGRESS)
            return;

        switch (victim->GetEntry())
        {
            case NPC_LANDMINES_LAYER_ALLIANCE:
            case NPC_LANDMINES_LAYER_HORDE:
                // The layer is dead: the mines it laid stay, but are not laid again.
                _respawnStopped[victim->GetEntry() == NPC_LANDMINES_LAYER_ALLIANCE ? EVENT_LANDMINES_ALLIANCE : EVENT_LANDMINES_HORDE] = true;
                return;
            case NPC_EXPLOSIVES_EXPERT_ALLIANCE:
            case NPC_EXPLOSIVES_EXPERT_HORDE:
                // The expert is dead: his team's mines are cleared.
                DespawnEvent(victim->GetEntry() == NPC_EXPLOSIVES_EXPERT_ALLIANCE ? EVENT_LANDMINES_ALLIANCE : EVENT_LANDMINES_HORDE, true);
                return;
            default:
                break;
        }

        std::pair<uint8, uint8> const* event = CreatureEvent(victim);
        Player* player = killer ? killer->GetCharmerOrOwnerPlayerOrPlayerItself() : nullptr;
        if (!event || !player)
            return;

        uint8 const killerOwner = OwnerOf(player);
        switch (event->first)
        {
            case EVENT_BOSS_ALLIANCE:
            case EVENT_BOSS_HORDE:
            {
                uint8 const winner = event->first == EVENT_BOSS_ALLIANCE ? OWNER_HORDE : OWNER_ALLIANCE;
                UpdateWorldState(winner == OWNER_HORDE ? WS_VANNDAR_ALIVE : WS_DREKTHAR_ALIVE, 0);
                CastOnTeam(SPELL_GENERAL_KILLED_QUEST, winner);
                battleground->RewardReputationToTeam(FactionOf(winner), REP_BOSS, TeamOf(winner));
                battleground->RewardHonorToTeam(battleground->GetBonusHonorFromKill(KILL_BOSS), TeamOf(winner));
                HeraldYell(winner == OWNER_HORDE ? "The Stormpike General is Dead!" : "The Frostwolf General is Dead!");
                battleground->EndBattleground(TeamOf(winner));
                break;
            }
            case EVENT_CAPTAIN_ALLIANCE:
            case EVENT_CAPTAIN_HORDE:
            {
                uint8 const loser = event->first == EVENT_CAPTAIN_ALLIANCE ? OWNER_ALLIANCE : OWNER_HORDE;
                uint8 const deadEvent = loser == OWNER_ALLIANCE ? EVENT_CAPTAIN_DEAD_ALLIANCE : EVENT_CAPTAIN_DEAD_HORDE;
                if (IsActive(deadEvent, 0))
                    break;
                uint8 const winner = 1 - loser;
                UpdateWorldState(loser == OWNER_ALLIANCE ? WS_BALINDA_ALIVE : WS_GALVANGAR_ALIVE, 0);
                battleground->RewardReputationToTeam(FactionOf(winner), REP_CAPTAIN, TeamOf(winner));
                battleground->RewardHonorToTeam(battleground->GetBonusHonorFromKill(KILL_CAPTAIN), TeamOf(winner));
                UpdateScore(loser, -int32(RES_CAPTAIN));
                SpawnEvent(deadEvent, 0, true);     // the garrison burns
                break;
            }
            case EVENT_MINE_BOSS:
            case EVENT_MINE_BOSS + 1:
            {
                uint8 const mine = event->first - EVENT_MINE_BOSS;
                ChangeMineOwner(mine, killerOwner);
                CompleteQuestForAll(killerOwner == OWNER_ALLIANCE ? QUEST_MINE_ALLIANCE : QUEST_MINE_HORDE);
                break;
            }
            case EVENT_LIEUTENANT_ALLIANCE:
            case EVENT_LIEUTENANT_HORDE:
            {
                uint8 const winner = event->first == EVENT_LIEUTENANT_ALLIANCE ? OWNER_HORDE : OWNER_ALLIANCE;
                battleground->RewardReputationToTeam(FactionOf(winner), REP_COMMANDER, TeamOf(winner));
                battleground->RewardHonorToTeam(battleground->GetBonusHonorFromKill(KILL_COMMANDER), TeamOf(winner));
                break;
            }
            default:
                if (event->first >= EVENT_COMMANDER_FIRST && event->first <= EVENT_COMMANDER_LAST)
                {
                    // 52-55 are the Alliance commanders, 56-59 the Horde ones; they and their escort do not come back.
                    uint8 const winner = event->first < EVENT_COMMANDER_FIRST + 4 ? OWNER_HORDE : OWNER_ALLIANCE;
                    battleground->RewardReputationToTeam(FactionOf(winner), REP_COMMANDER, TeamOf(winner));
                    battleground->RewardHonorToTeam(battleground->GetBonusHonorFromKill(KILL_COMMANDER), TeamOf(winner));
                    _respawnStopped[event->first] = true;
                    if (event->first == EVENT_COMMANDER_FIRST + 2)
                        CompleteQuestForAll(QUEST_COMMANDER_ALLIANCE);
                    else if (event->first == EVENT_COMMANDER_FIRST + 5)
                        CompleteQuestForAll(QUEST_COMMANDER_HORDE);
                }
                break;
        }
    }

    void DoAction(uint32 actionId, WorldObject* source, WorldObject* target) override
    {
        switch (actionId)
        {
            case ActionUseBanner:
                UseBanner(Object::ToGameObject(source), Object::ToPlayer(target));
                break;
            case ActionTurnInScraps:
                if (Player* player = Object::ToPlayer(source))
                    TurnInScraps(OwnerOf(player));
                break;
            default:
                break;
        }
    }

    void OnEnd(Team /*winner*/) override
    {
        std::array<uint32, PVP_TEAMS_COUNT> towers = { 0, 0 };
        std::array<uint32, PVP_TEAMS_COUNT> graves = { 0, 0 };
        std::array<uint32, PVP_TEAMS_COUNT> mines = { 0, 0 };

        for (uint8 node = GraveyardCount; node < NodeCount; ++node)
            if (_nodes[node].State == STATE_CONTROLLED && _nodes[node].TotalOwner == (node <= 10 ? OWNER_ALLIANCE : OWNER_HORDE))
                ++towers[_nodes[node].TotalOwner];
        // The aid station and the relief hut are base graveyards and do not count.
        for (uint8 node = 1; node < GraveyardCount - 1; ++node)
            if (_nodes[node].State == STATE_CONTROLLED && _nodes[node].Owner != OWNER_NEUTRAL)
                ++graves[_nodes[node].Owner];
        for (uint8 mine = 0; mine < MineCount; ++mine)
            if (_mineOwner[mine] != OWNER_NEUTRAL)
                ++mines[_mineOwner[mine]];

        for (uint8 owner = OWNER_ALLIANCE; owner <= OWNER_HORDE; ++owner)
        {
            uint32 reputation = towers[owner] * REP_SURVIVING_TOWER + graves[owner] * REP_OWNED_GRAVE + mines[owner] * REP_OWNED_MINE;
            uint32 kills = towers[owner] * KILL_SURVIVING_TOWER + graves[owner] * KILL_SURVIVING_GRAVE + mines[owner] * KILL_SURVIVING_MINE;
            if (!IsActive(owner == OWNER_ALLIANCE ? EVENT_CAPTAIN_DEAD_ALLIANCE : EVENT_CAPTAIN_DEAD_HORDE, 0))
            {
                reputation += REP_SURVIVING_CAPTAIN;
                kills += KILL_SURVIVING_CAPTAIN;
            }
            if (reputation)
                battleground->RewardReputationToTeam(FactionOf(owner), reputation, TeamOf(owner));
            if (kills)
                battleground->RewardHonorToTeam(battleground->GetBonusHonorFromKill(kills), TeamOf(owner));
        }
    }

    Team GetPrematureWinner() override
    {
        if (_reinforcements[OWNER_ALLIANCE] > _reinforcements[OWNER_HORDE])
            return ALLIANCE;
        if (_reinforcements[OWNER_HORDE] > _reinforcements[OWNER_ALLIANCE])
            return HORDE;
        return BattlegroundScript::GetPrematureWinner();
    }

private:
    struct NodeData
    {
        uint8 TotalOwner;       // who holds it for good: the builder of a standing tower, the last capturer
        uint8 Owner;            // who holds it now (the assaulting team while assaulted)
        uint8 PrevOwner;        // the holder before the running assault, who can defend it
        uint8 State;
        uint32 Timer;
    };

    // --- banners and nodes (VMaNGOS EventPlayerClickedOnFlag and on) ---

    void UseBanner(GameObject* banner, Player* player)
    {
        if (!banner || !player || battleground->GetStatus() != STATUS_IN_PROGRESS || !player->IsAlive())
            return;

        SpawnIndex const& index = GetSpawnIndex();
        auto itr = index.GameObjectEvents.find(banner->GetSpawnId());
        if (itr == index.GameObjectEvents.end() || itr->second.first >= NodeCount || !IsActive(itr->second.first, itr->second.second))
            return;

        uint8 const node = itr->second.first;
        if (itr->second.second % 2 == STATE_CONTROLLED)
            AssaultPoint(player, node);
        else
            DefendPoint(player, node);
    }

    void AssaultPoint(Player* player, uint8 node)
    {
        uint8 const team = OwnerOf(player);
        NodeData& data = _nodes[node];
        if (data.Owner == team || data.TotalOwner == team)
            return;
        // Only an unclaimed node (Snowfall) can be assaulted while it is assaulted.
        if (data.State == STATE_ASSAULTED && data.TotalOwner != OWNER_NEUTRAL)
            return;

        data.Timer = CaptureTime;
        data.PrevOwner = data.Owner;
        data.Owner = team;
        data.State = STATE_ASSAULTED;
        UpdateNodeWorldStates(node);
        PopulateNode(node);

        if (IsTower(node))
        {
            HeraldYell(Trinity::StringFormat("The {} is under attack! If left unchecked, the {} will destroy it!", Nodes[node].Name, TeamName(team)));
            battleground->UpdatePvpStat(player, PVP_STAT_TOWERS_ASSAULTED, 1);
        }
        else
        {
            HeraldYell(Trinity::StringFormat("The {} is under attack! If left unchecked, the {} will capture it!", Nodes[node].Name, TeamName(team)));
            battleground->UpdatePvpStat(player, PVP_STAT_GRAVEYARDS_ASSAULTED, 1);
        }
        battleground->PlaySoundToAll(team == OWNER_ALLIANCE ? SOUND_ALLIANCE_ASSAULTS : SOUND_HORDE_ASSAULTS);
    }

    void DefendPoint(Player* player, uint8 node)
    {
        uint8 const team = OwnerOf(player);
        NodeData& data = _nodes[node];
        if (data.Owner == team || data.State != STATE_ASSAULTED)
            return;
        // Snowfall, never captured yet: a click on the other team's claim is an assault of its own.
        if (data.TotalOwner == OWNER_NEUTRAL)
        {
            AssaultPoint(player, node);
            return;
        }
        if (data.PrevOwner != team)
            return;

        data.PrevOwner = data.Owner;
        data.Owner = team;
        data.State = STATE_CONTROLLED;
        data.Timer = 0;
        PopulateNode(node);
        UpdateNodeWorldStates(node);

        HeraldYell(Trinity::StringFormat("The {} was taken by the {}!", Nodes[node].Name, TeamName(team)));
        if (IsTower(node))
        {
            battleground->UpdatePvpStat(player, PVP_STAT_TOWERS_DEFENDED, 1);
            battleground->PlaySoundToAll(SOUND_BOTH_TOWER_DEFEND);
        }
        else
        {
            battleground->UpdatePvpStat(player, PVP_STAT_GRAVEYARDS_DEFENDED, 1);
            battleground->PlaySoundToAll(team == OWNER_ALLIANCE ? SOUND_ALLIANCE_GOOD : SOUND_HORDE_GOOD);
        }
    }

    // The assault held: a graveyard changes hands, a tower is destroyed.
    void NodeCaptured(uint8 node)
    {
        NodeData& data = _nodes[node];
        uint8 const owner = data.Owner;
        data.TotalOwner = owner;
        data.PrevOwner = owner;
        data.State = STATE_CONTROLLED;
        data.Timer = 0;
        PopulateNode(node);
        UpdateNodeWorldStates(node);

        if (IsTower(node))
        {
            UpdateScore(1 - owner, -int32(RES_TOWER));
            battleground->RewardReputationToTeam(FactionOf(owner), REP_TOWER, TeamOf(owner));
            battleground->RewardHonorToTeam(battleground->GetBonusHonorFromKill(KILL_TOWER), TeamOf(owner));
            HeraldYell(Trinity::StringFormat("{} was destroyed by the {}!", Nodes[node].Name, TeamName(owner)));
            CompleteQuestForAll(owner == OWNER_ALLIANCE ? QUEST_TOWERS_ALLIANCE : QUEST_TOWERS_HORDE);
        }
        else
        {
            HeraldYell(Trinity::StringFormat("The {} was taken by the {}!", Nodes[node].Name, TeamName(owner)));
            CompleteQuestForAll(owner == OWNER_ALLIANCE ? QUEST_GRAVEYARDS_ALLIANCE : QUEST_GRAVEYARDS_HORDE);
        }
        battleground->PlaySoundToAll(owner == OWNER_ALLIANCE ? SOUND_ALLIANCE_GOOD : SOUND_HORDE_GOOD);
    }

    // Spawns and despawns the creatures and objects of a node for its new state (VMaNGOS PopulateNode).
    void PopulateNode(uint8 node)
    {
        NodeData const& data = _nodes[node];
        uint8 const newOwner = data.Owner;
        uint8 const tierNew = DefenderTier(newOwner);

        if (!IsTower(node))
        {
            uint8 const event = EVENT_NODE_DEFENDERS + node;
            if (data.State == STATE_CONTROLLED)
            {
                ForceRespawn(event, newOwner * 4 + tierNew);
                SpawnEvent(event, newOwner * 4 + tierNew, true);
            }
            else
                _respawnStopped[event] = true;     // the old guards fight on, but do not come back
        }
        else if (newOwner != OWNER_NEUTRAL)
        {
            uint8 const marshals = EVENT_NODE_DEFENDERS + node;
            uint8 const defenders = EVENT_TOWER_DEFENDERS + node;
            if (data.State == STATE_CONTROLLED)
            {
                ForceRespawn(marshals, newOwner * 2 + STATE_CONTROLLED);
                SpawnEvent(marshals, newOwner * 2 + STATE_CONTROLLED, true);
                ForceRespawn(defenders, newOwner * 4 + tierNew);
                SpawnEvent(defenders, newOwner * 4 + tierNew, true);
            }
            else
            {
                _respawnStopped[marshals] = true;
                _respawnStopped[defenders] = true;
            }
        }

        // Banners, auras, spirit guides and, for a destroyed tower, its fires.
        SpawnEvent(node, newOwner * 2 + data.State, true);
    }

    uint8 DefenderTier(uint8 owner) const
    {
        if (owner == OWNER_NEUTRAL)
            return 0;
        uint32 const scraps = _scraps[owner];
        return scraps < 500 ? 0 : scraps < 1000 ? 1 : scraps < 1500 ? 2 : 3;
    }

    // Armor scraps upgrade a team's guards at 500, 1000 and 1500 (VMaNGOS UpgradeArmor).
    void TurnInScraps(uint8 owner)
    {
        if (owner == OWNER_NEUTRAL || battleground->GetStatus() != STATUS_IN_PROGRESS)
            return;
        uint8 const before = DefenderTier(owner);
        _scraps[owner] += 20;
        uint8 const after = DefenderTier(owner);
        if (after == before)
            return;

        static constexpr uint32 UpgradeSpells[4] = { 0, SPELL_SEASONED_UNITS, SPELL_VETERAN_UNITS, SPELL_CHAMPION_UNITS };
        CastOnTeam(UpgradeSpells[after], owner);
        for (uint8 node = 0; node < NodeCount; ++node)
            if (_nodes[node].Owner == owner && _nodes[node].State == STATE_CONTROLLED)
                PopulateNode(node);
    }

    void UpdateNodeWorldStates(uint8 node) const
    {
        NodeData const& data = _nodes[node];
        int32 const* ws = Nodes[node].WorldStates;
        UpdateWorldState(ws[0], data.Owner == OWNER_ALLIANCE && data.State == STATE_CONTROLLED ? 1 : 0);
        UpdateWorldState(ws[1], data.Owner == OWNER_ALLIANCE && data.State == STATE_ASSAULTED ? 1 : 0);
        UpdateWorldState(ws[2], data.Owner == OWNER_HORDE && data.State == STATE_CONTROLLED ? 1 : 0);
        UpdateWorldState(ws[3], data.Owner == OWNER_HORDE && data.State == STATE_ASSAULTED ? 1 : 0);
        if (node == 3)
            UpdateWorldState(WS_SNOWFALL_NEUTRAL, data.Owner == OWNER_NEUTRAL ? 1 : 0);
    }

    // --- mines ---

    void ChangeMineOwner(uint8 mine, uint8 owner)
    {
        _mineTimer[mine] = MineTickTime;
        if (_mineOwner[mine] == owner)
            return;

        _mineOwner[mine] = owner;
        UpdateMineWorldStates(mine);

        // The old occupants fight on without coming back; the new owner's miners and boss move in.
        SpawnEvent(EVENT_MINE + mine, owner, false);
        SpawnEvent(EVENT_MINE_BOSS + mine, owner, false);

        if (owner != OWNER_NEUTRAL)
        {
            _mineReclaimTimer[mine] = MineReclaimTime;
            battleground->PlaySoundToAll(owner == OWNER_ALLIANCE ? SOUND_ALLIANCE_GOOD : SOUND_HORDE_GOOD);
            HeraldYell(Trinity::StringFormat("The {} has taken the {}! Its supplies will now be used for reinforcements!", TeamName(owner), Mines[mine].Name));
        }
    }

    void UpdateMineWorldStates(uint8 mine) const
    {
        for (uint8 owner = OWNER_ALLIANCE; owner <= OWNER_NEUTRAL; ++owner)
            UpdateWorldState(Mines[mine].WorldStates[owner], _mineOwner[mine] == owner ? 1 : 0);
    }

    // --- reinforcements ---

    void UpdateScore(uint8 owner, int32 points)
    {
        if (owner > OWNER_HORDE)
            return;
        // 1.12: reaching zero does not end the battle; the generals do.
        _reinforcements[owner] = std::max(_reinforcements[owner] + points, 0);
        UpdateWorldState(owner == OWNER_ALLIANCE ? WS_ALLIANCE_REINFORCEMENTS : WS_HORDE_REINFORCEMENTS, _reinforcements[owner]);
    }

    // --- battleground events (VMaNGOS BattleGround::SpawnEvent / SetSpawnEventMode) ---

    bool IsActive(uint8 event1, uint8 event2) const { return _activeEvents[event1] == event2; }
    bool CanSpawn(uint8 event1, uint8 event2) const { return IsActive(event1, event2) && !_respawnStopped[event1]; }

    // Makes event2 the active state of event1: the members of the old state go (creatures only when despawnOld, else
    // they live on without respawning), the members of the new one appear.
    void SpawnEvent(uint8 event1, uint8 event2, bool despawnOld)
    {
        uint8 const old = _activeEvents[event1];
        if (old == event2)
            return;
        _activeEvents[event1] = event2;
        _respawnStopped[event1] = false;
        if (old != EVENT_NONE)
            DespawnMembers(event1, old, despawnOld);
        SpawnMembers(event1, event2);
    }

    void DespawnEvent(uint8 event1, bool despawnCreatures)
    {
        uint8 const old = _activeEvents[event1];
        if (old == EVENT_NONE)
            return;
        _activeEvents[event1] = EVENT_NONE;
        DespawnMembers(event1, old, despawnCreatures);
    }

    // Brings back the dead members of an already active state.
    void ForceRespawn(uint8 event1, uint8 event2)
    {
        if (!IsActive(event1, event2))
            return;
        _respawnStopped[event1] = false;
        SpawnMembers(event1, event2);
    }

    void SpawnMembers(uint8 event1, uint8 event2)
    {
        SpawnIndex const& index = GetSpawnIndex();
        uint16 const key = EventKey(event1, event2);

        if (auto itr = index.CreaturesByEvent.find(key); itr != index.CreaturesByEvent.end())
        {
            for (ObjectGuid::LowType spawnId : itr->second)
            {
                CreatureData const* data = sObjectMgr->GetCreatureData(spawnId);
                bool alive = false;
                for (auto const& [id, creature] : Trinity::Containers::MapEqualRange(battlegroundMap->GetCreatureBySpawnIdStore(), spawnId))
                    if (creature->IsAlive())
                        alive = true;
                if (alive)
                    continue;
                battlegroundMap->RemoveRespawnTime(SPAWN_TYPE_CREATURE, spawnId);
                // A grid not loaded yet spawns it through the entry hook when it loads.
                if (!battlegroundMap->IsGridLoaded(data->spawnPoint))
                    continue;
                battlegroundMap->DespawnAll(SPAWN_TYPE_CREATURE, spawnId);     // a corpse left behind
                Creature::CreateCreatureFromDB(spawnId, battlegroundMap, true, true);
            }
        }

        if (auto itr = index.GameObjectsByEvent.find(key); itr != index.GameObjectsByEvent.end())
        {
            for (ObjectGuid::LowType spawnId : itr->second)
            {
                if (battlegroundMap->GetGameObjectBySpawnIdStore().count(spawnId))
                    continue;
                battlegroundMap->RemoveRespawnTime(SPAWN_TYPE_GAMEOBJECT, spawnId);
                GameObjectData const* data = sObjectMgr->GetGameObjectData(spawnId);
                if (!battlegroundMap->IsGridLoaded(data->spawnPoint))
                    continue;
                GameObject::CreateGameObjectFromDB(spawnId, battlegroundMap, true);
            }
        }
    }

    void DespawnMembers(uint8 event1, uint8 event2, bool despawnCreatures)
    {
        SpawnIndex const& index = GetSpawnIndex();
        uint16 const key = EventKey(event1, event2);

        if (despawnCreatures)
            if (auto itr = index.CreaturesByEvent.find(key); itr != index.CreaturesByEvent.end())
                for (ObjectGuid::LowType spawnId : itr->second)
                {
                    battlegroundMap->RemoveRespawnTime(SPAWN_TYPE_CREATURE, spawnId);
                    battlegroundMap->DespawnAll(SPAWN_TYPE_CREATURE, spawnId);
                }

        if (auto itr = index.GameObjectsByEvent.find(key); itr != index.GameObjectsByEvent.end())
            for (ObjectGuid::LowType spawnId : itr->second)
            {
                battlegroundMap->RemoveRespawnTime(SPAWN_TYPE_GAMEOBJECT, spawnId);
                battlegroundMap->DespawnAll(SPAWN_TYPE_GAMEOBJECT, spawnId);
            }
    }

    std::pair<uint8, uint8> const* CreatureEvent(Creature const* creature) const
    {
        if (!creature->GetSpawnId())
            return nullptr;
        SpawnIndex const& index = GetSpawnIndex();
        auto itr = index.CreatureEvents.find(creature->GetSpawnId());
        return itr != index.CreatureEvents.end() ? &itr->second : nullptr;
    }

    // --- helpers ---

    uint8 OwnerOf(Player const* player) const
    {
        Team const team = battleground->GetPlayerTeam(player->GetGUID());
        return team == ALLIANCE ? OWNER_ALLIANCE : team == HORDE ? OWNER_HORDE : OWNER_NEUTRAL;
    }

    static uint32 NextCaptainBuff() { return 2 * MINUTE * IN_MILLISECONDS + urand(0, 4) * MINUTE * IN_MILLISECONDS; }

    void CastOnTeam(uint32 spellId, uint8 owner) const
    {
        if (spellId && sSpellMgr->GetSpellInfo(spellId, DIFFICULTY_NONE))
            battleground->CastSpellOnTeam(spellId, TeamOf(owner));
    }

    // As VMaNGOS CompleteQuestForAll: everyone in the battle who has the quest gets it completed.
    void CompleteQuestForAll(uint32 questId) const
    {
        for (auto const& [guid, bgPlayer] : battleground->GetPlayers())
            if (Player* player = ObjectAccessor::GetPlayer(battlegroundMap, guid))
                if (player->GetQuestStatus(questId) == QUEST_STATUS_INCOMPLETE)
                    player->CompleteQuest(questId);
    }

    // The herald's announcements reach the whole battle, as in 1.12.
    void HeraldYell(std::string const& text) const
    {
        Creature* herald = battlegroundMap->GetCreature(EventCreature(EVENT_HERALD));
        WorldPackets::Chat::Chat packet;
        if (herald)
            packet.Initialize(CHAT_MSG_MONSTER_YELL, LANG_UNIVERSAL, herald, nullptr, text);
        else
            packet.Initialize(CHAT_MSG_BG_SYSTEM_NEUTRAL, LANG_UNIVERSAL, nullptr, nullptr, text);
        battleground->SendPacketToAll(packet.Write());
    }

    void CreatureYell(uint8 event1, std::string const& text) const
    {
        Creature* creature = battlegroundMap->GetCreature(EventCreature(event1));
        if (!creature || !creature->IsAlive())
            return;
        WorldPackets::Chat::Chat packet;
        packet.Initialize(CHAT_MSG_MONSTER_YELL, LANG_UNIVERSAL, creature, nullptr, text);
        battleground->SendPacketToAll(packet.Write());
    }

    ObjectGuid EventCreature(uint8 event1) const
    {
        auto itr = _eventCreature.find(event1);
        return itr != _eventCreature.end() ? itr->second : ObjectGuid::Empty;
    }

    std::array<uint8, 256> _activeEvents;
    std::array<bool, 256> _respawnStopped;
    std::array<NodeData, NodeCount> _nodes;
    std::array<uint8, MineCount> _mineOwner;
    std::array<uint32, MineCount> _mineTimer;
    std::array<uint32, MineCount> _mineReclaimTimer;
    std::array<int32, PVP_TEAMS_COUNT> _reinforcements;
    std::array<uint32, PVP_TEAMS_COUNT> _scraps;
    std::array<uint32, PVP_TEAMS_COUNT> _captainBuffTimer;
    uint32 _snivvleTimer;
    std::unordered_map<uint8, ObjectGuid> _eventCreature;
    GuidVector _doors;
    GuidVector _ghostGates;
};

// The clickable 1.12 banners: graveyards 178365 / 178364 (Alliance / Horde), 179286 / 179287 (assaulted by the
// Alliance / Horde), Snowfall 180418; bunkers 178925 and 179435 (assaulted by the Horde), towers 178943 and 178940
// (assaulted by the Alliance).
struct go_classic_av_banner : GameObjectAI
{
    explicit go_classic_av_banner(GameObject* go) : GameObjectAI(go) { }

    bool OnGossipHello(Player* player) override
    {
        if (ZoneScript* zoneScript = me->GetZoneScript())
            zoneScript->DoAction(ActionUseBanner, me, player);
        return true;
    }
};
}

using namespace ClassicAlteracValley;

void AddSC_classic_battleground_alterac_valley()
{
    RegisterBattlegroundMapScript(classic_battleground_alterac_valley, MapId);
    RegisterGameObjectAI(go_classic_av_banner);
}
