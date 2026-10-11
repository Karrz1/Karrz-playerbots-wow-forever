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

// Classic Arathi Basin (map 529), the 1.12 node game on the classic map's own banners.
//
// The stock script (Battlegrounds/ArathiBasin) drives the retail remake on map 2107 with CAPTURE_POINT (42) objects.
// The classic map carries the 1.12 banners instead: a neutral banner per node (180087-180091, GOOBER), the Alliance and
// Horde banners (180058 / 180060) and their contested versions (180059 / 180061), plus a banner aura object per state
// (180100 Alliance, 180101 Horde, 180102 neutral). In 1.12 they were grouped per node and state by battleground events
// (VMaNGOS BattleGroundAB); the world DB here spawns every variant on every node at once, so this script suppresses
// those spawns and places exactly one banner and one aura per node itself, swapping them as the node changes hands.
// A click on any banner comes in through go_classic_ab_banner.
//
// Rules (VMaNGOS BattleGroundAB): a neutral node becomes contested by the clicker's team; an enemy-occupied node becomes
// contested by the clicker's team; a node contested by the enemy goes back to the clicker's team as occupied if it was
// theirs before the assault (a defence), else becomes contested by the clicker's team. A contested node is occupied
// after 60 s. Resources tick per occupied node count (12/9/6/3/1 s for 1-5 nodes, 10 points, 30 for five); 2000 wins.
// The node ownership world states (1767 and on) are the ones the bots' snapshot reads.

#include "Battleground.h"
#include "BattlegroundMgr.h"
#include "BattlegroundScript.h"
#include "classic_battleground_buffs.h"
#include "ChatPackets.h"
#include "GameObject.h"
#include "GameObjectAI.h"
#include "Log.h"
#include "Map.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "SpellMgr.h"
#include "StringFormat.h"

namespace ClassicArathiBasin
{
    static constexpr uint32 MapId = 529;
    static constexpr uint32 MaxTeamScore = 2000;
    static constexpr uint32 NearVictoryScore = 1800;
    static constexpr int32 CaptureTime = 60 * IN_MILLISECONDS;
    static constexpr uint32 TickIntervals[6] = { 0, 12000, 9000, 6000, 3000, 1000 };
    static constexpr uint32 TickPoints[6] = { 0, 10, 10, 10, 10, 30 };
    static constexpr uint32 NodeCount = 5;
    // ZoneScript::DoAction id the banners send; unique, see classic_battleground_warsong_gulch.cpp.
    static constexpr uint32 ActionUseBanner = 0x41424201;

    enum GameObjects : uint32
    {
        GO_BANNER_ALLIANCE              = 180058,
        GO_BANNER_CONTESTED_ALLIANCE    = 180059,
        GO_BANNER_HORDE                 = 180060,
        GO_BANNER_CONTESTED_HORDE       = 180061,
        GO_BANNER_STABLES               = 180087,   // the five neutral banners, in node order
        GO_BANNER_GOLD_MINE             = 180091,
        GO_AURA_ALLIANCE                = 180100,
        GO_AURA_HORDE                   = 180101,
        GO_AURA_NEUTRAL                 = 180102,
        GO_ALLIANCE_DOOR                = 180255,
        GO_HORDE_DOOR                   = 180256,
        GO_GHOST_GATE                   = 180322
    };

    // Node status, as in 1.12: 0 neutral, 1/2 contested by the Alliance/Horde, 3/4 occupied by the Alliance/Horde.
    enum NodeStatus : uint8
    {
        NODE_NEUTRAL            = 0,
        NODE_TYPE_CONTESTED     = 1,
        NODE_ALLIANCE_CONTESTED = 1,
        NODE_HORDE_CONTESTED    = 2,
        NODE_TYPE_OCCUPIED      = 3,
        NODE_ALLIANCE_OCCUPIED  = 3,
        NODE_HORDE_OCCUPIED     = 4
    };

    enum WorldStates : int32
    {
        WS_OCCUPIED_BASES_HORDE     = 1778,
        WS_OCCUPIED_BASES_ALLIANCE  = 1779,
        WS_RESOURCES_ALLIANCE       = 1776,
        WS_RESOURCES_HORDE          = 1777,
        WS_RESOURCES_MAX            = 1780,
        WS_RESOURCES_WARNING        = 1955,
        WS_UNKNOWN_1861             = 1861      // 1.12 sends 2
    };

    enum Sounds : uint32
    {
        SOUND_NODE_CLAIMED              = 8192,
        SOUND_NODE_CAPTURED_ALLIANCE    = 8173,
        SOUND_NODE_CAPTURED_HORDE       = 8213,
        SOUND_NODE_ASSAULTED_ALLIANCE   = 8212,
        SOUND_NODE_ASSAULTED_HORDE      = 8174,
        SOUND_NEAR_VICTORY_ALLIANCE     = 8457,
        SOUND_NEAR_VICTORY_HORDE        = 8456
    };

    enum Texts : uint32
    {
        TEXT_ALLIANCE_NEAR_VICTORY      = 10598,
        TEXT_HORDE_NEAR_VICTORY         = 10599
    };

    enum PvpStats : uint32
    {
        PVP_STAT_BASES_ASSAULTED    = 926,
        PVP_STAT_BASES_DEFENDED     = 927
    };

    enum Misc : uint32
    {
        FACTION_LEAGUE_OF_ARATHOR       = 509,
        FACTION_DEFILERS                = 510,
        SPELL_QUEST_REWARD_4_BASES      = 24061,
        SPELL_QUEST_REWARD_5_BASES      = 24064
    };

    struct NodeInfo
    {
        char const* Name;
        uint32 NeutralBanner;
        Position Pos;
        int32 WorldStateIcon;           // shown while neutral
        int32 WorldStateBase;           // Alliance occupied; +1 Horde occupied, +2/+3 contested by the Alliance/Horde
        uint32 Credit;                  // quest kill credit for assaulting it
    };

    // VMaNGOS node order (BG_AB_Nodes); positions are the world DB banner spawns.
    static std::array<NodeInfo, NodeCount> const Nodes =
    {{
        { "Stables",     180087, { 1166.79f, 1200.13f,  -56.7086f,  0.907571f }, 1842, 1767, 15001 },
        { "Blacksmith",  180088, {  977.08f, 1046.54f,  -44.8303f,  3.6652f   }, 1846, 1782, 15002 },
        { "Farm",        180089, {  806.182f, 874.272f, -55.9937f, -2.30383f  }, 1845, 1772, 15003 },
        { "Lumber Mill", 180090, {  856.142f, 1148.9f,   11.1847f, -2.30383f  }, 1844, 1792, 15004 },
        { "Gold Mine",   180091, { 1146.92f,  848.178f,-110.917f,  -0.733038f }, 1843, 1787, 15005 }
    }};

    // World state offset from WorldStateBase per status (1.12 plusArray).
    static constexpr int32 StatusOffset[5] = { 0, 2, 3, 0, 1 };

    static bool IsBanner(uint32 entry)
    {
        return (entry >= GO_BANNER_ALLIANCE && entry <= GO_BANNER_CONTESTED_HORDE)
            || (entry >= GO_BANNER_STABLES && entry <= GO_BANNER_GOLD_MINE);
    }

    static Team TeamOf(uint8 teamIndex) { return teamIndex == TEAM_ALLIANCE ? ALLIANCE : HORDE; }
    static char const* TeamName(uint8 teamIndex) { return teamIndex == TEAM_ALLIANCE ? "Alliance" : "Horde"; }
    static ChatMsg TeamChat(uint8 teamIndex) { return teamIndex == TEAM_ALLIANCE ? CHAT_MSG_BG_SYSTEM_ALLIANCE : CHAT_MSG_BG_SYSTEM_HORDE; }
}

using namespace ClassicArathiBasin;

struct classic_battleground_arathi_basin final : BattlegroundScript
{
    explicit classic_battleground_arathi_basin(BattlegroundMap* map) : BattlegroundScript(map),
        _status({ }), _prevStatus({ }), _timer({ }), _lastTick({ 0, 0 }), _honorScoreTics({ 0, 0 }), _reputationScoreTics({ 0, 0 }),
        _isInformedNearVictory(false)
    {
        bool const isBGWeekend = sBattlegroundMgr->IsBGWeekend(battleground->GetTypeID());
        _honorTics = isBGWeekend ? 200 : 330;
        _reputationTics = isBGWeekend ? 150 : 200;
    }

    void OnInit() override
    {
        UpdateWorldState(WS_RESOURCES_MAX, MaxTeamScore);
        UpdateWorldState(WS_RESOURCES_WARNING, NearVictoryScore);
        UpdateWorldState(WS_RESOURCES_ALLIANCE, 0);
        UpdateWorldState(WS_RESOURCES_HORDE, 0);
        UpdateWorldState(WS_OCCUPIED_BASES_ALLIANCE, 0);
        UpdateWorldState(WS_OCCUPIED_BASES_HORDE, 0);
        UpdateWorldState(WS_UNKNOWN_1861, 2);

        for (uint8 node = 0; node < NodeCount; ++node)
        {
            SpawnNodeObjects(node);
            UpdateNodeWorldStates(node);
        }
    }

    // Every banner and banner aura comes from SpawnNodeObjects(); the DB spawns every state on every node at once.
    uint32 GetGameObjectEntry(ObjectGuid::LowType spawnId, uint32 entry) override
    {
        if (spawnId && (IsBanner(entry) || (entry >= GO_AURA_ALLIANCE && entry <= GO_AURA_NEUTRAL)))
            return 0;
        return entry;
    }

    void OnGameObjectCreate(GameObject* gameObject) override
    {
        switch (gameObject->GetEntry())
        {
            case GO_ALLIANCE_DOOR:
            case GO_HORDE_DOOR:
                _doors.push_back(gameObject->GetGUID());
                break;
            case GO_GHOST_GATE:
                _ghostGates.push_back(gameObject->GetGUID());
                break;
            default:
                break;
        }
    }

    void OnStart() override
    {
        // The banners were first created in OnInit, before the map's grids were loaded. Make sure every node has its banner and
        // aura now that the match is live (Replace keeps a banner that exists and re-creates one that does not).
        for (uint8 node = 0; node < NodeCount; ++node)
            SpawnNodeObjects(node);
        for (ObjectGuid const& guid : _doors)
            if (GameObject* door = battlegroundMap->GetGameObject(guid))
                door->UseDoorOrButton();
        for (ObjectGuid const& guid : _ghostGates)
            if (GameObject* gate = battlegroundMap->GetGameObject(guid))
                gate->DespawnOrUnsummon(0ms, 24h);  // forced respawn time: the ghost gate spawns respawn after 25 s otherwise
    }

    void OnUpdate(uint32 diff) override
    {
        if (battleground->GetStatus() != STATUS_IN_PROGRESS)
            return;

        _buffCheckTimer += diff;
        if (_buffCheckTimer >= 250)
        {
            _buffCheckTimer = 0;
            ClassicBattlegroundBuffs::Poll(battleground, battlegroundMap);
        }

        // Safety net: a node whose banner or aura is not in the map (never created, or lost with an unloaded grid) gets it back.
        _bannerCheckTimer += diff;
        if (_bannerCheckTimer >= 5000)
        {
            _bannerCheckTimer = 0;
            for (uint8 node = 0; node < NodeCount; ++node)
            {
                GameObject* banner = battlegroundMap->GetGameObject(_banner[node]);
                GameObject* aura = battlegroundMap->GetGameObject(_aura[node]);
                if (!banner || !banner->IsInWorld() || !aura || !aura->IsInWorld())
                {
                    TC_LOG_ERROR("playerbot.v2", "[ab_banner] node {} ({}) has no live banner/aura in the map (banner={} aura={}); respawning", uint32(node), Nodes[node].Name, banner ? 1 : 0, aura ? 1 : 0);
                    SpawnNodeObjects(node);
                }
            }
        }

        std::array<uint32, PVP_TEAMS_COUNT> occupied = { 0, 0 };
        for (uint8 node = 0; node < NodeCount; ++node)
        {
            if (_timer[node] > 0)
            {
                _timer[node] -= int32(diff);
                if (_timer[node] <= 0)
                {
                    // Contested for a minute: the assaulting team owns it now.
                    _timer[node] = 0;
                    uint8 const teamIndex = _status[node] - NODE_TYPE_CONTESTED;
                    SetNodeStatus(node, NodeStatus(_status[node] + 2));
                    Announce(TeamChat(teamIndex), Trinity::StringFormat("The {} has taken the {}!", TeamName(teamIndex), Nodes[node].Name));
                    battleground->PlaySoundToAll(teamIndex == TEAM_ALLIANCE ? SOUND_NODE_CAPTURED_ALLIANCE : SOUND_NODE_CAPTURED_HORDE);
                    NodeOccupied(teamIndex);
                }
            }

            if (_status[node] == NODE_ALLIANCE_OCCUPIED)
                ++occupied[TEAM_ALLIANCE];
            else if (_status[node] == NODE_HORDE_OCCUPIED)
                ++occupied[TEAM_HORDE];
        }

        for (uint8 team = 0; team < PVP_TEAMS_COUNT; ++team)
        {
            uint32 const points = occupied[team];
            if (!points)
                continue;
            _lastTick[team] += diff;
            if (_lastTick[team] <= TickIntervals[points])
                continue;
            _lastTick[team] -= TickIntervals[points];

            battleground->AddPoint(TeamOf(team), TickPoints[points]);
            _honorScoreTics[team] += TickPoints[points];
            _reputationScoreTics[team] += TickPoints[points];
            if (_reputationScoreTics[team] >= _reputationTics)
            {
                battleground->RewardReputationToTeam(team == TEAM_ALLIANCE ? FACTION_LEAGUE_OF_ARATHOR : FACTION_DEFILERS, 10, TeamOf(team));
                _reputationScoreTics[team] -= _reputationTics;
            }
            if (_honorScoreTics[team] >= _honorTics)
            {
                battleground->RewardHonorToTeam(battleground->GetBonusHonorFromKill(1), TeamOf(team));
                _honorScoreTics[team] -= _honorTics;
            }

            uint32 score = battleground->GetTeamScore(TeamId(team));
            if (!_isInformedNearVictory && score > NearVictoryScore)
            {
                battleground->SendBroadcastText(team == TEAM_ALLIANCE ? TEXT_ALLIANCE_NEAR_VICTORY : TEXT_HORDE_NEAR_VICTORY, CHAT_MSG_BG_SYSTEM_NEUTRAL);
                battleground->PlaySoundToAll(team == TEAM_ALLIANCE ? SOUND_NEAR_VICTORY_ALLIANCE : SOUND_NEAR_VICTORY_HORDE);
                _isInformedNearVictory = true;
            }
            if (score > MaxTeamScore)
            {
                battleground->SetTeamPoint(TeamOf(team), MaxTeamScore);
                score = MaxTeamScore;
            }
            UpdateWorldState(team == TEAM_ALLIANCE ? WS_RESOURCES_ALLIANCE : WS_RESOURCES_HORDE, score);
        }

        uint32 const alliance = battleground->GetTeamScore(TEAM_ALLIANCE);
        uint32 const horde = battleground->GetTeamScore(TEAM_HORDE);
        if (alliance >= MaxTeamScore && horde >= MaxTeamScore)
            battleground->EndBattleground(TEAM_OTHER);
        else if (alliance >= MaxTeamScore)
            battleground->EndBattleground(ALLIANCE);
        else if (horde >= MaxTeamScore)
            battleground->EndBattleground(HORDE);
    }

    void DoAction(uint32 actionId, WorldObject* source, WorldObject* target) override
    {
        if (actionId != ActionUseBanner)
            return;

        GameObject* banner = Object::ToGameObject(source);
        Player* player = Object::ToPlayer(target);
        if (!banner || !player || battleground->GetStatus() != STATUS_IN_PROGRESS || !player->IsAlive())
        {
            TC_LOG_INFO("playerbot.v2", "[ab_banner] use ignored: banner={} player={} bg_status={} alive={}", banner ? 1 : 0, player ? 1 : 0,
                uint32(battleground->GetStatus()), player && player->IsAlive() ? 1 : 0);
            return;
        }

        uint8 node = 0;
        while (node < NodeCount && _banner[node] != banner->GetGUID())
            ++node;
        if (node == NodeCount)
        {
            // A banner that is in the map but not the one this script tracks (a stale or DB-spawned one): adopt it by position.
            for (uint8 n = 0; n < NodeCount && node == NodeCount; ++n)
                if (banner->GetExactDist2d(Nodes[n].Pos) < 3.0f)
                    node = n;
            TC_LOG_ERROR("playerbot.v2", "[ab_banner] {} used banner entry {} guid {} that is not a tracked node banner; matched node {}", player->GetName(),
                banner->GetEntry(), banner->GetGUID().ToString(), node == NodeCount ? -1 : int32(node));
            if (node == NodeCount)
                return;
        }

        uint8 const teamIndex = Battleground::GetTeamIndexByTeamId(battleground->GetPlayerTeam(player->GetGUID()));
        uint8 const status = _status[node];
        // Neutral, or held/contested by the other team.
        if (status != NODE_NEUTRAL && teamIndex != status % 2)
        {
            TC_LOG_INFO("playerbot.v2", "[ab_banner] {} click on {} ignored: node status {} is already theirs", player->GetName(), Nodes[node].Name, uint32(status));
            return;
        }

        player->KilledMonsterCredit(Nodes[node].Credit);
        std::string_view const nodeName = Nodes[node].Name;
        uint32 sound = 0;

        if (status == NODE_NEUTRAL)
        {
            battleground->UpdatePvpStat(player, PVP_STAT_BASES_ASSAULTED, 1);
            Contest(node, teamIndex);
            Announce(TeamChat(teamIndex), Trinity::StringFormat("{} claims the {}! If left unchallenged, the {} will control it in 1 minute!",
                player->GetName(), nodeName, TeamName(teamIndex)));
            sound = SOUND_NODE_CLAIMED;
        }
        else if (status == NODE_ALLIANCE_CONTESTED || status == NODE_HORDE_CONTESTED)
        {
            if (_prevStatus[node] < NODE_TYPE_OCCUPIED)
            {
                // It was never ours: assault it in turn.
                battleground->UpdatePvpStat(player, PVP_STAT_BASES_ASSAULTED, 1);
                Contest(node, teamIndex);
                Announce(TeamChat(teamIndex), Trinity::StringFormat("{} has assaulted the {}!", player->GetName(), nodeName));
            }
            else
            {
                // Ours before the assault: defended.
                battleground->UpdatePvpStat(player, PVP_STAT_BASES_DEFENDED, 1);
                _timer[node] = 0;
                SetNodeStatus(node, NodeStatus(teamIndex + NODE_TYPE_OCCUPIED));
                Announce(TeamChat(teamIndex), Trinity::StringFormat("{} has defended the {}!", player->GetName(), nodeName));
                Announce(TeamChat(teamIndex), Trinity::StringFormat("The {} has taken the {}!", TeamName(teamIndex), nodeName));
                NodeOccupied(teamIndex);
            }
            sound = teamIndex == TEAM_ALLIANCE ? SOUND_NODE_ASSAULTED_ALLIANCE : SOUND_NODE_ASSAULTED_HORDE;
        }
        else
        {
            battleground->UpdatePvpStat(player, PVP_STAT_BASES_ASSAULTED, 1);
            Contest(node, teamIndex);
            Announce(TeamChat(teamIndex), Trinity::StringFormat("{} has assaulted the {}!", player->GetName(), nodeName));
            sound = teamIndex == TEAM_ALLIANCE ? SOUND_NODE_ASSAULTED_ALLIANCE : SOUND_NODE_ASSAULTED_HORDE;
        }

        battleground->PlaySoundToAll(sound);
    }

    void OnEnd(Team winner) override
    {
        bool const isBGWeekend = sBattlegroundMgr->IsBGWeekend(battleground->GetTypeID());
        if (winner == ALLIANCE || winner == HORDE)
            battleground->RewardHonorToTeam(battleground->GetBonusHonorFromKill(isBGWeekend ? 2 : 1), winner);
    }

    Team GetPrematureWinner() override
    {
        uint32 alliance = 0, horde = 0;
        for (uint8 status : _status)
        {
            if (status == NODE_ALLIANCE_OCCUPIED)
                ++alliance;
            else if (status == NODE_HORDE_OCCUPIED)
                ++horde;
        }
        if (alliance > horde)
            return ALLIANCE;
        if (horde > alliance)
            return HORDE;
        return BattlegroundScript::GetPrematureWinner();
    }

private:
    void Announce(ChatMsg type, std::string const& text) const
    {
        WorldPackets::Chat::Chat packet;
        packet.Initialize(type, LANG_UNIVERSAL, nullptr, nullptr, text);
        battleground->SendPacketToAll(packet.Write());
    }

    void Contest(uint8 node, uint8 teamIndex)
    {
        _timer[node] = CaptureTime;
        SetNodeStatus(node, NodeStatus(teamIndex + NODE_TYPE_CONTESTED));
    }

    void SetNodeStatus(uint8 node, NodeStatus status)
    {
        _prevStatus[node] = _status[node];
        _status[node] = status;
        SpawnNodeObjects(node);
        UpdateNodeWorldStates(node);
    }

    void SpawnNodeObjects(uint8 node)
    {
        NodeInfo const& info = Nodes[node];
        uint32 bannerEntry = info.NeutralBanner;
        uint32 auraEntry = GO_AURA_NEUTRAL;
        switch (_status[node])
        {
            case NODE_ALLIANCE_CONTESTED: bannerEntry = GO_BANNER_CONTESTED_ALLIANCE; break;
            case NODE_HORDE_CONTESTED:    bannerEntry = GO_BANNER_CONTESTED_HORDE; break;
            case NODE_ALLIANCE_OCCUPIED:  bannerEntry = GO_BANNER_ALLIANCE; auraEntry = GO_AURA_ALLIANCE; break;
            case NODE_HORDE_OCCUPIED:     bannerEntry = GO_BANNER_HORDE; auraEntry = GO_AURA_HORDE; break;
            default: break;
        }
        _banner[node] = Replace(_banner[node], bannerEntry, info.Pos);
        _aura[node] = Replace(_aura[node], auraEntry, info.Pos);
    }

    ObjectGuid Replace(ObjectGuid const& current, uint32 entry, Position const& pos) const
    {
        if (GameObject* old = battlegroundMap->GetGameObject(current))
        {
            if (old->GetEntry() == entry)
                return current;
            old->Delete();
        }

        QuaternionData const rotation = QuaternionData::fromEulerAnglesZYX(pos.GetOrientation(), 0.0f, 0.0f);
        GameObject* go = GameObject::CreateGameObject(entry, battlegroundMap, pos, rotation, 255, GO_STATE_READY);
        if (!go)
        {
            TC_LOG_ERROR("playerbot.v2", "[ab_banner] CreateGameObject failed: entry {} at ({:.1f},{:.1f},{:.1f})", entry, pos.GetPositionX(), pos.GetPositionY(), pos.GetPositionZ());
            return ObjectGuid::Empty;
        }
        // Not SetSpawnedByDefault(false): a runtime object keeps the default respawn delay (300 s), and with "not spawned by
        // default" GameObject::isSpawned() is false, which GameObject::IsInvisibleDueToDespawn() turns into "never sent to a
        // player" (bots still find it in the map, which is why they could capture while the player saw only a bare pole).
        if (!battlegroundMap->AddToMap(go))
        {
            TC_LOG_ERROR("playerbot.v2", "[ab_banner] AddToMap failed: entry {} at ({:.1f},{:.1f},{:.1f})", entry, pos.GetPositionX(), pos.GetPositionY(), pos.GetPositionZ());
            delete go;
            return ObjectGuid::Empty;
        }
        TC_LOG_INFO("playerbot.v2", "[ab_banner] spawned entry {} at ({:.1f},{:.1f},{:.1f})", entry, pos.GetPositionX(), pos.GetPositionY(), pos.GetPositionZ());
        return go->GetGUID();
    }

    void UpdateNodeWorldStates(uint8 node) const
    {
        NodeInfo const& info = Nodes[node];
        uint8 const status = _status[node];
        UpdateWorldState(info.WorldStateIcon, status == NODE_NEUTRAL ? 1 : 0);
        for (uint8 s = NODE_ALLIANCE_CONTESTED; s <= NODE_HORDE_OCCUPIED; ++s)
            UpdateWorldState(info.WorldStateBase + StatusOffset[s], status == s ? 1 : 0);

        uint32 alliance = 0, horde = 0;
        for (uint8 other : _status)
        {
            if (other == NODE_ALLIANCE_OCCUPIED)
                ++alliance;
            else if (other == NODE_HORDE_OCCUPIED)
                ++horde;
        }
        UpdateWorldState(WS_OCCUPIED_BASES_ALLIANCE, alliance);
        UpdateWorldState(WS_OCCUPIED_BASES_HORDE, horde);
    }

    // Quest credit for holding four or five bases (Arathi Basin "Control" quests).
    void NodeOccupied(uint8 teamIndex)
    {
        uint32 held = 0;
        for (uint8 node = 0; node < NodeCount; ++node)
            if (_status[node] == teamIndex + NODE_TYPE_OCCUPIED && !_timer[node])
                ++held;
        if (held >= 5 && sSpellMgr->GetSpellInfo(SPELL_QUEST_REWARD_5_BASES, DIFFICULTY_NONE))
            battleground->CastSpellOnTeam(SPELL_QUEST_REWARD_5_BASES, TeamOf(teamIndex));
        if (held >= 4 && sSpellMgr->GetSpellInfo(SPELL_QUEST_REWARD_4_BASES, DIFFICULTY_NONE))
            battleground->CastSpellOnTeam(SPELL_QUEST_REWARD_4_BASES, TeamOf(teamIndex));
    }

    std::array<uint8, NodeCount> _status;
    std::array<uint8, NodeCount> _prevStatus;
    std::array<int32, NodeCount> _timer;
    uint32 _bannerCheckTimer = 0;
    uint32 _buffCheckTimer = 0;
    std::array<ObjectGuid, NodeCount> _banner;
    std::array<ObjectGuid, NodeCount> _aura;
    std::array<uint32, PVP_TEAMS_COUNT> _lastTick;
    std::array<uint32, PVP_TEAMS_COUNT> _honorScoreTics;
    std::array<uint32, PVP_TEAMS_COUNT> _reputationScoreTics;
    bool _isInformedNearVictory;
    uint32 _honorTics;
    uint32 _reputationTics;
    GuidVector _doors;
    GuidVector _ghostGates;
};

// 180087-180091 neutral, 180058-180061 Alliance/Horde and contested banners.
struct go_classic_ab_banner : GameObjectAI
{
    explicit go_classic_ab_banner(GameObject* go) : GameObjectAI(go) { }

    bool OnGossipHello(Player* player) override
    {
        // The zone script is taken when the object is created; ask the map again if it was missing then.
        ZoneScript* zoneScript = me->GetZoneScript();
        if (!zoneScript)
            if (BattlegroundMap* bgMap = me->GetMap()->ToBattlegroundMap())
                zoneScript = reinterpret_cast<ZoneScript*>(bgMap->GetBattlegroundScript());
        if (zoneScript)
            zoneScript->DoAction(ActionUseBanner, me, player);
        else
            TC_LOG_ERROR("playerbot.v2", "[ab_banner] {} clicked banner entry {} but the map has no battleground script", player->GetName(), me->GetEntry());
        return true;
    }
};

void AddSC_classic_battleground_arathi_basin()
{
    RegisterBattlegroundMapScript(classic_battleground_arathi_basin, MapId);
    RegisterGameObjectAI(go_classic_ab_banner);
}
