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

// Classic Warsong Gulch (map 489), the 1.12 flag game on the classic map's own objects.
//
// The stock script (Battlegrounds/WarsongGulch) drives the retail remake on map 2106 with NEW_FLAG (36) objects and
// server-side capture area triggers. The classic map carries the 1.12 objects instead: a FLAGSTAND (24) per base
// (Silverwing Flag 179830, Warsong Flag 179831) and a FLAGDROP (26) object for a dropped flag (179785 / 179786). Their
// use goes through go_classic_wsg_flag into this script, which runs the game the way 1.12 did (VMaNGOS
// BattleGroundWS): the enemy takes a flag from its stand (Silverwing Flag 23335 or Warsong Flag 23333 on the carrier),
// a carrier who dies, drops the aura or leaves drops it on the ground, the owners return a dropped flag with a click
// and the enemy picks it up again, a dropped flag goes home after 10 s, and a carrier standing on its own base's
// capture area trigger (3646 / 3647) while its own flag is home scores. Both flags then leave for 23 s. Three
// captures win. Carriers are polled rather than hooked so that bots, which send no area trigger packets, capture the
// same way as players.
//
// The stands are spawned by this script when the gates open (the world DB spawns are suppressed), so a stand that
// was taken can never come back from the DB on a grid reload.

#include "Battleground.h"
#include "BattlegroundPackets.h"
#include "BattlegroundScript.h"
#include "classic_battleground_buffs.h"
#include "DB2Stores.h"
#include "GameObject.h"
#include "GameObjectAI.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "SpellAuras.h"

namespace ClassicWarsongGulch
{
    static constexpr uint32 MapId = 489;
    static constexpr uint32 MaxTeamScore = 3;
    static constexpr int32 FlagRespawnTime = 23 * IN_MILLISECONDS;
    static constexpr int32 FlagDropTime = 10 * IN_MILLISECONDS;
    static constexpr uint32 CarrierCheckInterval = 250;
    static constexpr float CaptureFallbackRadius = 4.0f;    // only used if the client data lacks the area trigger
    // ZoneScript::DoAction id the flag objects send. Unique so that a classic flag object spawned on some other map
    // can never be mistaken for another script's action.
    static constexpr uint32 ActionUseFlag = 0x57534701;

    enum GameObjects : uint32
    {
        GO_SILVERWING_FLAG          = 179830,   // Alliance flag on its stand
        GO_WARSONG_FLAG             = 179831,   // Horde flag on its stand
        GO_SILVERWING_FLAG_DROPPED  = 179785,
        GO_WARSONG_FLAG_DROPPED     = 179786,
        GO_HORDE_GATE_1             = 179916,
        GO_HORDE_GATE_2             = 179917,
        GO_ALLIANCE_GATE_1          = 179918,
        GO_ALLIANCE_GATE_2          = 179919,
        GO_ALLIANCE_GATE_3          = 179920,
        GO_ALLIANCE_GATE_4          = 179921,
        GO_GHOST_GATE               = 180322
    };

    enum Spells : uint32
    {
        SPELL_WARSONG_FLAG          = 23333,    // carrying the Horde flag
        SPELL_SILVERWING_FLAG       = 23335     // carrying the Alliance flag
    };

    enum AreaTriggers : uint32
    {
        AT_ALLIANCE_FLAG_SPAWN      = 3646,     // the Alliance scores here
        AT_HORDE_FLAG_SPAWN         = 3647
    };

    enum WorldStates : int32
    {
        WS_FLAG_TAKEN_ALLIANCE      = 1545,     // Alliance flag: 0 home, 1 carried, -1 on the ground
        WS_FLAG_TAKEN_HORDE         = 1546,
        WS_FLAG_CAPTURES_ALLIANCE   = 1581,
        WS_FLAG_CAPTURES_HORDE      = 1582,
        WS_FLAG_CAPTURES_MAX        = 1601,
        WS_FLAG_STATE_HORDE         = 2338,     // 2 while the Horde carries the Alliance flag, else 1
        WS_FLAG_STATE_ALLIANCE      = 2339
    };

    enum Texts : uint32
    {
        TEXT_CAPTURED_HORDE_FLAG    = 9801,
        TEXT_CAPTURED_ALLIANCE_FLAG = 9802,
        TEXT_FLAGS_PLACED           = 9803,
        TEXT_ALLIANCE_FLAG_PICKED_UP = 9804,
        TEXT_ALLIANCE_FLAG_DROPPED  = 9805,
        TEXT_HORDE_FLAG_DROPPED     = 9806,
        TEXT_HORDE_FLAG_PICKED_UP   = 9807,
        TEXT_ALLIANCE_FLAG_RETURNED = 9808,
        TEXT_HORDE_FLAG_RETURNED    = 9809,
        TEXT_ALLIANCE_FLAG_RESPAWNED = 10022,
        TEXT_HORDE_FLAG_RESPAWNED   = 10023
    };

    enum Sounds : uint32
    {
        SOUND_FLAG_CAPTURED_ALLIANCE  = 8173,
        SOUND_FLAG_CAPTURED_HORDE     = 8213,
        SOUND_FLAG_RETURNED           = 8192,
        SOUND_HORDE_FLAG_PICKED_UP    = 8212,
        SOUND_ALLIANCE_FLAG_PICKED_UP = 8174,
        SOUND_FLAGS_RESPAWNED         = 8232
    };

    enum PvpStats : uint32
    {
        PVP_STAT_FLAG_CAPTURES      = 928,
        PVP_STAT_FLAG_RETURNS       = 929
    };

    enum Factions : uint32
    {
        FACTION_WARSONG_OUTRIDERS   = 889,
        FACTION_SILVERWING_SENTINELS = 890
    };

    enum class WsgFlagState : uint8
    {
        NotSpawned,     // before the gates open
        InBase,
        Carried,
        Dropped,
        Respawning      // after a capture
    };

    // Indexed by the team that OWNS the flag (defends it); the enemy carries it.
    struct FlagInfo
    {
        uint32 Stand;
        uint32 Dropped;
        uint32 CarrierAura;
        Position StandPosition;
        uint32 CaptureAreaTrigger;      // where the owning team scores with the enemy flag
        int32 WorldStateTaken;
        int32 WorldStateCarrier;        // indexed by the carrier's team
        int32 WorldStateCaptures;       // the owning team's captures
        uint32 TextPickedUp, TextDropped, TextReturned, TextRespawned, TextCapturedBy;
        ChatMsg OwnerChat, CarrierChat;
        uint32 SoundPickedUp, SoundCapturedBy;
        uint32 Reputation;              // the owning team's faction
    };

    static std::array<FlagInfo, PVP_TEAMS_COUNT> const Flags =
    {{
        {
            GO_SILVERWING_FLAG, GO_SILVERWING_FLAG_DROPPED, SPELL_SILVERWING_FLAG,
            { 1540.42f, 1481.32f, 351.828f, 3.08918f }, AT_ALLIANCE_FLAG_SPAWN,
            WS_FLAG_TAKEN_ALLIANCE, WS_FLAG_STATE_HORDE, WS_FLAG_CAPTURES_ALLIANCE,
            TEXT_ALLIANCE_FLAG_PICKED_UP, TEXT_ALLIANCE_FLAG_DROPPED, TEXT_ALLIANCE_FLAG_RETURNED, TEXT_ALLIANCE_FLAG_RESPAWNED,
            TEXT_CAPTURED_HORDE_FLAG, CHAT_MSG_BG_SYSTEM_ALLIANCE, CHAT_MSG_BG_SYSTEM_HORDE,
            SOUND_ALLIANCE_FLAG_PICKED_UP, SOUND_FLAG_CAPTURED_ALLIANCE, FACTION_SILVERWING_SENTINELS
        },
        {
            GO_WARSONG_FLAG, GO_WARSONG_FLAG_DROPPED, SPELL_WARSONG_FLAG,
            { 916.023f, 1434.4f, 345.413f, 0.0174525f }, AT_HORDE_FLAG_SPAWN,
            WS_FLAG_TAKEN_HORDE, WS_FLAG_STATE_ALLIANCE, WS_FLAG_CAPTURES_HORDE,
            TEXT_HORDE_FLAG_PICKED_UP, TEXT_HORDE_FLAG_DROPPED, TEXT_HORDE_FLAG_RETURNED, TEXT_HORDE_FLAG_RESPAWNED,
            TEXT_CAPTURED_ALLIANCE_FLAG, CHAT_MSG_BG_SYSTEM_HORDE, CHAT_MSG_BG_SYSTEM_ALLIANCE,
            SOUND_HORDE_FLAG_PICKED_UP, SOUND_FLAG_CAPTURED_HORDE, FACTION_WARSONG_OUTRIDERS
        }
    }};

    static Team TeamOf(TeamId id) { return id == TEAM_ALLIANCE ? ALLIANCE : HORDE; }
}

using namespace ClassicWarsongGulch;

struct classic_battleground_warsong_gulch final : BattlegroundScript
{
    explicit classic_battleground_warsong_gulch(BattlegroundMap* map) : BattlegroundScript(map),
        _state({ WsgFlagState::NotSpawned, WsgFlagState::NotSpawned }), _timer({ 0, 0 }), _checkTimer(0), _lastCaptureTeam(TEAM_OTHER)
    {
    }

    void OnInit() override
    {
        UpdateWorldState(WS_FLAG_CAPTURES_ALLIANCE, 0);
        UpdateWorldState(WS_FLAG_CAPTURES_HORDE, 0);
        UpdateWorldState(WS_FLAG_CAPTURES_MAX, MaxTeamScore);
        for (FlagInfo const& flag : Flags)
        {
            UpdateWorldState(flag.WorldStateTaken, 0);
            UpdateWorldState(flag.WorldStateCarrier, 1);
        }
    }

    // The stands come from SpawnStand(); the DB copies would duplicate them and could respawn a taken flag.
    uint32 GetGameObjectEntry(ObjectGuid::LowType spawnId, uint32 entry) override
    {
        if (spawnId && (entry == GO_SILVERWING_FLAG || entry == GO_WARSONG_FLAG))
            return 0;
        return entry;
    }

    void OnGameObjectCreate(GameObject* gameObject) override
    {
        switch (gameObject->GetEntry())
        {
            case GO_HORDE_GATE_1:
            case GO_HORDE_GATE_2:
            case GO_ALLIANCE_GATE_1:
            case GO_ALLIANCE_GATE_2:
            case GO_ALLIANCE_GATE_3:
            case GO_ALLIANCE_GATE_4:
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
        for (ObjectGuid const& guid : _doors)
            if (GameObject* door = battlegroundMap->GetGameObject(guid))
                door->UseDoorOrButton();
        for (ObjectGuid const& guid : _ghostGates)
            if (GameObject* gate = battlegroundMap->GetGameObject(guid))
                gate->DespawnOrUnsummon(0ms, 24h);  // the ghost gate spawns carry a 25 s respawn: without a forced time the invisible gate came back mid-match

        for (uint8 team = 0; team < PVP_TEAMS_COUNT; ++team)
            SpawnStand(TeamId(team));
    }

    void OnUpdate(uint32 diff) override
    {
        if (battleground->GetStatus() != STATUS_IN_PROGRESS)
            return;

        for (uint8 team = 0; team < PVP_TEAMS_COUNT; ++team)
        {
            if (_state[team] != WsgFlagState::Dropped && _state[team] != WsgFlagState::Respawning)
                continue;
            _timer[team] -= int32(diff);
            if (_timer[team] > 0)
                continue;
            if (_state[team] == WsgFlagState::Dropped)
                ReturnDroppedFlag(TeamId(team));
            else
                RespawnFlags();
        }

        _checkTimer += diff;
        if (_checkTimer < CarrierCheckInterval)
            return;
        _checkTimer = 0;

        ClassicBattlegroundBuffs::Poll(battleground, battlegroundMap);

        for (uint8 team = 0; team < PVP_TEAMS_COUNT && battleground->GetStatus() == STATUS_IN_PROGRESS; ++team)
            CheckCarrier(TeamId(team));
    }

    void DoAction(uint32 actionId, WorldObject* source, WorldObject* target) override
    {
        if (actionId != ActionUseFlag)
            return;

        GameObject* flag = Object::ToGameObject(source);
        Player* player = Object::ToPlayer(target);
        if (!flag || !player || battleground->GetStatus() != STATUS_IN_PROGRESS || !player->IsAlive() || player->GetVehicle())
            return;

        TeamId const playerTeam = Battleground::GetTeamIndexByTeamId(battleground->GetPlayerTeam(player->GetGUID()));
        for (uint8 team = 0; team < PVP_TEAMS_COUNT; ++team)
        {
            FlagInfo const& info = Flags[team];
            if (flag->GetEntry() == info.Stand)
            {
                // Only the enemy lifts a flag off its stand.
                if (team != playerTeam && _state[team] == WsgFlagState::InBase)
                {
                    _flagObject[team].Clear();
                    flag->Delete();
                    TakeFlag(TeamId(team), player);
                }
                return;
            }
            if (flag->GetEntry() == info.Dropped)
            {
                if (_state[team] != WsgFlagState::Dropped || flag->GetGUID() != _flagObject[team])
                    return;
                _flagObject[team].Clear();
                flag->Delete();
                if (team == playerTeam)
                {
                    // Returned by its own team: straight back to the stand.
                    battleground->SendBroadcastText(info.TextReturned, info.OwnerChat, player);
                    battleground->PlaySoundToAll(SOUND_FLAG_RETURNED);
                    battleground->UpdatePvpStat(player, PVP_STAT_FLAG_RETURNS, 1);
                    SpawnStand(TeamId(team));
                }
                else
                    TakeFlag(TeamId(team), player);
                return;
            }
        }
    }

    void OnPlayerLeft(Player* player) override
    {
        for (uint8 team = 0; team < PVP_TEAMS_COUNT; ++team)
            if (_state[team] == WsgFlagState::Carried && _carrier[team] == player->GetGUID())
                DropFlag(TeamId(team), player);
    }

    void OnEnd(Team winner) override
    {
        for (uint8 team = 0; team < PVP_TEAMS_COUNT; ++team)
            if (_state[team] == WsgFlagState::Carried)
                if (Player* carrier = ObjectAccessor::GetPlayer(battlegroundMap, _carrier[team]))
                    carrier->RemoveAurasDueToSpell(Flags[team].CarrierAura);

        if (winner == ALLIANCE || winner == HORDE)
            battleground->RewardHonorToTeam(battleground->GetBonusHonorFromKill(1), winner);
        battleground->RewardHonorToTeam(battleground->GetBonusHonorFromKill(2), ALLIANCE);
        battleground->RewardHonorToTeam(battleground->GetBonusHonorFromKill(2), HORDE);
    }

    Team GetPrematureWinner() override
    {
        if (battleground->GetTeamScore(TEAM_ALLIANCE) > battleground->GetTeamScore(TEAM_HORDE))
            return ALLIANCE;
        if (battleground->GetTeamScore(TEAM_HORDE) > battleground->GetTeamScore(TEAM_ALLIANCE))
            return HORDE;
        if (_lastCaptureTeam != TEAM_OTHER)
            return _lastCaptureTeam;
        return BattlegroundScript::GetPrematureWinner();
    }

private:
    void SetFlagWorldStates(TeamId team, int32 taken, int32 carrierState) const
    {
        UpdateWorldState(Flags[team].WorldStateTaken, taken);
        UpdateWorldState(Flags[team].WorldStateCarrier, carrierState);
    }

    void SpawnStand(TeamId team)
    {
        FlagInfo const& info = Flags[team];
        _state[team] = WsgFlagState::InBase;
        _carrier[team].Clear();
        SetFlagWorldStates(team, 0, 1);

        QuaternionData const rotation = QuaternionData::fromEulerAnglesZYX(info.StandPosition.GetOrientation(), 0.0f, 0.0f);
        if (GameObject* stand = GameObject::CreateGameObject(info.Stand, battlegroundMap, info.StandPosition, rotation, 255, GO_STATE_READY))
        {
            // Not SetSpawnedByDefault(false): isSpawned() would be false and the stand never sent to a player (see the AB script).
            if (battlegroundMap->AddToMap(stand))
                _flagObject[team] = stand->GetGUID();
            else
                delete stand;
        }
    }

    void TakeFlag(TeamId team, Player* player)
    {
        FlagInfo const& info = Flags[team];
        player->RemoveAurasByType(SPELL_AURA_MOD_STEALTH);
        player->RemoveAurasByType(SPELL_AURA_MOD_INVISIBILITY);
        player->CastSpell(player, info.CarrierAura, true);
        // The map / minimap flag carrier icon (SMSG_BATTLEGROUND_PLAYER_POSITIONS). The classic carrier spells do not reliably carry
        // the position aura on this client's spell data (Karrz: no carrier on the Shift-M map), so the script registers the carrier.
        {
            WorldPackets::Battleground::BattlegroundPlayerPosition position;
            position.Guid = player->GetGUID();
            position.Pos = player->GetPosition();
            position.IconID = team == TEAM_ALLIANCE ? PLAYER_POSITION_ICON_ALLIANCE_FLAG : PLAYER_POSITION_ICON_HORDE_FLAG;   // the flag carried
            position.ArenaSlot = PLAYER_POSITION_ARENA_SLOT_NONE;   // the 70291 client draws a battleground carrier only for slot 1
            battleground->AddPlayerPosition(position);
        }

        _state[team] = WsgFlagState::Carried;
        _carrier[team] = player->GetGUID();
        SetFlagWorldStates(team, 1, 2);
        battleground->SendBroadcastText(info.TextPickedUp, info.CarrierChat, player);
        battleground->PlaySoundToAll(info.SoundPickedUp);
    }

    void DropFlag(TeamId team, Player* carrier)
    {
        FlagInfo const& info = Flags[team];
        _state[team] = WsgFlagState::Dropped;
        _carrier[team].Clear();
        _timer[team] = FlagDropTime;
        carrier->RemoveAurasDueToSpell(info.CarrierAura);
        battleground->RemovePlayerPosition(carrier->GetGUID());

        Position const pos = carrier->GetPosition();
        QuaternionData const rotation = QuaternionData::fromEulerAnglesZYX(pos.GetOrientation(), 0.0f, 0.0f);
        if (GameObject* dropped = GameObject::CreateGameObject(info.Dropped, battlegroundMap, pos, rotation, 255, GO_STATE_READY))
        {
            // Not SetSpawnedByDefault(false): same reason as the stand above.
            if (battlegroundMap->AddToMap(dropped))
                _flagObject[team] = dropped->GetGUID();
            else
                delete dropped;
        }

        SetFlagWorldStates(team, -1, 1);
        battleground->SendBroadcastText(info.TextDropped, info.OwnerChat, carrier);
    }

    // A dropped flag nobody touched for 10 s goes home.
    void ReturnDroppedFlag(TeamId team)
    {
        if (GameObject* dropped = battlegroundMap->GetGameObject(_flagObject[team]))
            dropped->Delete();
        _flagObject[team].Clear();
        battleground->SendBroadcastText(Flags[team].TextRespawned, CHAT_MSG_BG_SYSTEM_NEUTRAL);
        battleground->PlaySoundToAll(SOUND_FLAGS_RESPAWNED);
        SpawnStand(team);
    }

    void RespawnFlags()
    {
        for (uint8 team = 0; team < PVP_TEAMS_COUNT; ++team)
            if (_state[team] == WsgFlagState::Respawning)
                SpawnStand(TeamId(team));
        battleground->SendBroadcastText(TEXT_FLAGS_PLACED, CHAT_MSG_BG_SYSTEM_NEUTRAL);
        battleground->PlaySoundToAll(SOUND_FLAGS_RESPAWNED);
    }

    bool InCaptureArea(Player* player, TeamId scorer) const
    {
        FlagInfo const& home = Flags[scorer];
        if (AreaTriggerEntry const* trigger = sAreaTriggerStore.LookupEntry(home.CaptureAreaTrigger))
            return player->IsInAreaTrigger(trigger);
        return player->GetExactDist(&home.StandPosition) < CaptureFallbackRadius;
    }

    void CheckCarrier(TeamId team)
    {
        if (_state[team] != WsgFlagState::Carried)
            return;

        FlagInfo const& info = Flags[team];
        Player* carrier = ObjectAccessor::GetPlayer(battlegroundMap, _carrier[team]);
        if (!carrier || !carrier->IsInWorld())
        {
            // Gone without OnPlayerLeft (disconnect mid-update): nothing to drop it at, it goes home.
            battleground->RemovePlayerPosition(_carrier[team]);
            SpawnStand(team);
            return;
        }
        if (!carrier->IsAlive() || !carrier->HasAura(info.CarrierAura))
        {
            DropFlag(team, carrier);
            return;
        }

        TeamId const scorer = GetOtherTeam(team);
        if (_state[scorer] == WsgFlagState::InBase && InCaptureArea(carrier, scorer))
            CaptureFlag(team, carrier);
    }

    void CaptureFlag(TeamId team, Player* carrier)
    {
        FlagInfo const& captured = Flags[team];
        TeamId const scorer = GetOtherTeam(team);
        FlagInfo const& home = Flags[scorer];
        Team const scorerTeam = TeamOf(scorer);

        _state[team] = WsgFlagState::Respawning;
        _carrier[team].Clear();
        carrier->RemoveAurasDueToSpell(captured.CarrierAura);
        battleground->RemovePlayerPosition(carrier->GetGUID());

        // Both flags leave their stands until the respawn.
        _state[scorer] = WsgFlagState::Respawning;
        if (GameObject* stand = battlegroundMap->GetGameObject(_flagObject[scorer]))
            stand->Delete();
        _flagObject[scorer].Clear();
        _timer[TEAM_ALLIANCE] = _timer[TEAM_HORDE] = FlagRespawnTime;
        SetFlagWorldStates(TEAM_ALLIANCE, 0, 1);
        SetFlagWorldStates(TEAM_HORDE, 0, 1);

        if (battleground->GetTeamScore(scorer) < MaxTeamScore)
            battleground->AddPoint(scorerTeam, 1);
        UpdateWorldState(home.WorldStateCaptures, battleground->GetTeamScore(scorer));
        _lastCaptureTeam = scorerTeam;

        battleground->SendBroadcastText(captured.TextCapturedBy, home.OwnerChat, carrier);
        battleground->PlaySoundToAll(home.SoundCapturedBy);
        battleground->UpdatePvpStat(carrier, PVP_STAT_FLAG_CAPTURES, 1);
        battleground->RewardReputationToTeam(home.Reputation, 35, scorerTeam);
        battleground->RewardHonorToTeam(battleground->GetBonusHonorFromKill(2), scorerTeam);

        if (battleground->GetTeamScore(scorer) >= MaxTeamScore)
            battleground->EndBattleground(scorerTeam);
    }

    std::array<WsgFlagState, PVP_TEAMS_COUNT> _state;
    std::array<int32, PVP_TEAMS_COUNT> _timer;
    std::array<ObjectGuid, PVP_TEAMS_COUNT> _carrier;
    std::array<ObjectGuid, PVP_TEAMS_COUNT> _flagObject;   // the stand while home, the ground flag while dropped
    uint32 _checkTimer;
    Team _lastCaptureTeam;
    GuidVector _doors;
    GuidVector _ghostGates;
};

// 179830 Silverwing Flag, 179831 Warsong Flag (FLAGSTAND), 179785 / 179786 their dropped copies (FLAGDROP).
struct go_classic_wsg_flag : GameObjectAI
{
    explicit go_classic_wsg_flag(GameObject* go) : GameObjectAI(go) { }

    bool OnGossipHello(Player* player) override
    {
        if (ZoneScript* zoneScript = me->GetZoneScript())
            zoneScript->DoAction(ActionUseFlag, me, player);
        return true;
    }
};

void AddSC_classic_battleground_warsong_gulch()
{
    RegisterBattlegroundMapScript(classic_battleground_warsong_gulch, MapId);
    RegisterGameObjectAI(go_classic_wsg_flag);
}
