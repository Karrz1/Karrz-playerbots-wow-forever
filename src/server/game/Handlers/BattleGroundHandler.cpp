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
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "WorldSession.h"
#include "ArenaTeam.h"
#include "ArenaTeamMgr.h"
#include "Battlefield.h"
#include "BattlefieldMgr.h"
#include "Battleground.h"
#include "BattlegroundMgr.h"
#include "BattlegroundPackets.h"
#include "Chat.h"
#include "Common.h"
#include "Creature.h"
#include "DB2Stores.h"
#include "DisableMgr.h"
#include "GameTime.h"
#include "GossipDef.h"
#include "Group.h"
#include "Language.h"
#include "Log.h"
#include "NPCPackets.h"
#include "Playerbot/PlayerbotHooks.h"
#include "Object.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "SpellAuras.h"
#include "SpellMgr.h"
#include "SpellInfo.h"
#include "StringFormat.h"
#include "World.h"

void WorldSession::HandleBattlemasterHelloOpcode(WorldPackets::NPC::Hello& hello)
{
    Creature* unit = GetPlayer()->GetNPCIfCanInteractWith(hello.Unit, UNIT_NPC_FLAG_BATTLEMASTER, UNIT_NPC_FLAG_2_NONE);
    if (!unit)
        return;

    // Stop the npc if moving
    if (uint32 pause = unit->GetMovementTemplate().GetInteractionPauseTimer())
        unit->PauseMovement(pause);
    unit->SetHomePosition(unit->GetPosition());

    BattlegroundTypeId bgTypeId = sBattlegroundMgr->GetBattleMasterBG(unit->GetEntry());

    if (!_player->GetBGAccessByLevel(bgTypeId))
    {
                                                            // temp, must be gossip message...
        SendNotification(LANG_YOUR_BG_LEVEL_REQ_ERROR);
        return;
    }

    _lastBattlemasterBgType = uint32(bgTypeId);
    sBattlegroundMgr->SendBattlegroundList(_player, hello.Unit, bgTypeId);
}

void WorldSession::HandleBattlemasterJoinOpcode(WorldPackets::Battleground::BattlemasterJoin& battlemasterJoin)
{
    if (battlemasterJoin.QueueIDs.empty())
    {
        TC_LOG_ERROR("network", "Battleground: no bgtype received. possible cheater? {}", _player->GetGUID().ToString());
        return;
    }

    JoinBattlegroundQueue(BattlegroundQueueTypeId::FromPacked(battlemasterJoin.QueueIDs[0]));
}

// The 70205 client's Join Battle button sends the opcode this build names CMSG_BATTLEMASTER_JOIN_RATED_BG_BLITZ (0x3E00C0), never
// CMSG_BATTLEMASTER_JOIN, and a 5-byte packet on CMSG_BATTLEMASTER_JOIN_ARENA. The layout is not known, so the payload is logged and
// read defensively: any 64-bit value that decodes to a valid battleground queue id wins (the standard BattlemasterJoin layout puts
// it at offset 13, but every offset is tried); otherwise the player's open battlemaster supplies the battleground type.
void WorldSession::HandleBattlemasterJoinUnknownLayout(WorldPackets::Battleground::BattlemasterJoinRaw& packet)
{
    std::vector<uint8> const& data = packet.Data;
    std::string hex;
    for (size_t i = 0; i < data.size() && i < 48; ++i)
        hex += Trinity::StringFormat("{:02X} ", uint32(data[i]));
    TC_LOG_INFO("playerbot.v2", "[bg_join] opcode 0x{:X} from {}: size {} bytes {}", uint32(packet.GetOpcode()), _player->GetName(), data.size(), hex);

    BattlegroundQueueTypeId queueId = BATTLEGROUND_QUEUE_NONE;
    for (size_t offset = 0; offset + sizeof(uint64) <= data.size() && queueId == BATTLEGROUND_QUEUE_NONE; ++offset)
    {
        uint64 value = 0;
        memcpy(&value, data.data() + offset, sizeof(value));
        // A queue id always carries the 0x1F10 marker in its top 16 bits (BattlegroundQueueTypeId::GetPacked). Without that check any
        // little count or flag such as 01 00 00 00 reads as battleground 1 (Alterac Valley): 2026-10-07, Warsong Gulch queued AV.
        if ((value >> 48) != 0x1F10)
            continue;
        BattlegroundQueueTypeId candidate = BattlegroundQueueTypeId::FromPacked(value);
        if (candidate.Type == uint8(BattlegroundQueueIdType::Battleground) && !candidate.Rated && !candidate.TeamSize && candidate.BattlemasterListId
            && BattlegroundMgr::IsValidQueueId(candidate) && sBattlegroundMgr->GetBattlegroundTemplateByTypeId(BattlegroundTypeId(candidate.BattlemasterListId)))
            queueId = candidate;
    }

    if (queueId == BATTLEGROUND_QUEUE_NONE)
    {
        InteractionData const& interaction = _player->PlayerTalkClass->GetInteractionData();
        if (interaction.Type == PlayerInteractionType::BattleMaster && !interaction.SourceGuid.IsEmpty())
            if (Creature const* master = ObjectAccessor::GetCreature(*_player, interaction.SourceGuid))
                if (BattlegroundTypeId bgTypeId = sBattlegroundMgr->GetBattleMasterBG(master->GetEntry()))
                    queueId = BattlegroundMgr::BGQueueTypeId(uint16(bgTypeId), BattlegroundQueueIdType::Battleground, false, 0);
    }

    if (queueId == BATTLEGROUND_QUEUE_NONE && _lastBattlemasterBgType)
        queueId = BattlegroundMgr::BGQueueTypeId(uint16(_lastBattlemasterBgType), BattlegroundQueueIdType::Battleground, false, 0);

    if (queueId == BATTLEGROUND_QUEUE_NONE)
    {
        TC_LOG_ERROR("playerbot.v2", "[bg_join] {} sent opcode 0x{:X} with no queue id and no open battlemaster", _player->GetName(), uint32(packet.GetOpcode()));
        ChatHandler(this).PSendSysMessage("The battleground join could not be read. Talk to the battlemaster again, or use .playerbot bgjoin wsg|ab|av.");
        return;
    }

    TC_LOG_INFO("playerbot.v2", "[bg_join] {} queues for battleground {}", _player->GetName(), uint32(queueId.BattlemasterListId));
    JoinBattlegroundQueue(queueId);
}

void WorldSession::HandleBattlemasterJoinBlitz(WorldPackets::Battleground::BattlemasterJoinBlitz& packet)
{
    HandleBattlemasterJoinUnknownLayout(packet);
}

// Arena join slot. On the 70205 client 0x3F00C1 also arrives once at login with a single 00 byte, which is not a join, and the
// battleground join has its own opcode (0x3F00C0 -> CMSG_BATTLEMASTER_JOIN): only log it.
void WorldSession::HandleBattlemasterJoinArenaRaw(WorldPackets::Battleground::BattlemasterJoinArenaRaw& packet)
{
    TC_LOG_INFO("playerbot.v2", "[bg_join] arena opcode from {}: size {} (ignored)", _player->GetName(), packet.Data.size());
}

void WorldSession::JoinBattlegroundQueue(BattlegroundQueueTypeId bgQueueTypeId)
{
    bool isPremade = false;

    if (!BattlegroundMgr::IsValidQueueId(bgQueueTypeId))
    {
        TC_LOG_ERROR("network", "Battleground: invalid bg queue {{ BattlemasterListId: {}, Type: {}, Rated: {}, TeamSize: {} }} received. possible cheater? {}",
            bgQueueTypeId.BattlemasterListId, uint32(bgQueueTypeId.Type), bgQueueTypeId.Rated ? "true" : "false", uint32(bgQueueTypeId.TeamSize),
            _player->GetGUID().ToString());
        return;
    }

    BattlemasterListEntry const* battlemasterListEntry = sBattlemasterListStore.AssertEntry(bgQueueTypeId.BattlemasterListId);

    if (DisableMgr::IsDisabledFor(DISABLE_TYPE_BATTLEGROUND, bgQueueTypeId.BattlemasterListId, nullptr) || battlemasterListEntry->GetFlags().HasFlag(BattlemasterListFlags::InternalOnly))
    {
        ChatHandler(this).PSendSysMessage(LANG_BG_DISABLED);
        return;
    }

    BattlegroundTypeId bgTypeId = BattlegroundTypeId(bgQueueTypeId.BattlemasterListId);

    // ignore if player is already in BG
    if (_player->InBattleground())
        return;

    BattlegroundTemplate const* bgTemplate = sBattlegroundMgr->GetBattlegroundTemplateByTypeId(bgTypeId);
    if (!bgTemplate)
    {
        TC_LOG_ERROR("bg.battleground", "Battleground: no battleground_template for type {}, join by {} ignored", uint32(bgTypeId), _player->GetName());
        ChatHandler(this).PSendSysMessage("This battleground is not available on this server.");
        return;
    }

    // expected bracket entry
    PVPDifficultyEntry const* bracketEntry = DB2Manager::GetBattlegroundBracketByLevel(bgTemplate->MapIDs.front(), _player->GetLevel());
    if (!bracketEntry)
    {
        TC_LOG_ERROR("bg.battleground", "Battleground: no level bracket for map {} at level {}, join by {} ignored", bgTemplate->MapIDs.front(), uint32(_player->GetLevel()), _player->GetName());
        ChatHandler(this).PSendSysMessage("No level bracket exists for this battleground at your level.");
        return;
    }

    GroupJoinBattlegroundResult err = ERR_BATTLEGROUND_NONE;

    Group const* grp = _player->GetGroup();

    // Playerbot: a human leader whose group holds companion bots outside the leader's level bracket (a level 47 companion next to a
    // level 60 player) would fail Group::CanJoinBattlegroundQueue with ERR_BATTLEGROUND_JOIN_RANGE_INDEX and the client closes the
    // window without queueing. Bots cannot be levelled to fit, so the human queues alone and OnPlayerJoinedBgQueue fills the match.
    if (grp && !IsBot() && grp->GetLeaderGUID() == _player->GetGUID())
    {
        bool botOutOfBracket = false, humanOutOfBracket = false;
        for (GroupReference const& itr : grp->GetMembers())
        {
            Player const* member = itr.GetSource();
            if (!member || member == _player)
                continue;
            if (DB2Manager::GetBattlegroundBracketByLevel(bracketEntry->MapID, member->GetLevel()) == bracketEntry)
                continue;
            (member->GetSession()->IsBot() ? botOutOfBracket : humanOutOfBracket) = true;
        }
        if (botOutOfBracket && !humanOutOfBracket)
            grp = nullptr;
    }

    auto getQueueTeam = [&]() -> Team
    {
        // mercenary applies only to unrated battlegrounds
        if (!bgQueueTypeId.Rated && !bgTemplate->IsArena())
        {
            if (_player->HasAura(SPELL_MERCENARY_CONTRACT_HORDE))
                return HORDE;

            if (_player->HasAura(SPELL_MERCENARY_CONTRACT_ALLIANCE))
                return ALLIANCE;
        }

        return Team(_player->GetTeam());
    };

    // check queue conditions
    if (!grp)
    {
        if (GetPlayer()->isUsingLfg())
        {
            WorldPackets::Battleground::BattlefieldStatusFailed battlefieldStatus;
            BattlegroundMgr::BuildBattlegroundStatusFailed(&battlefieldStatus, bgQueueTypeId, _player, 0, ERR_LFG_CANT_USE_BATTLEGROUND);
            SendPacket(battlefieldStatus.Write());
            return;
        }

        // check RBAC permissions
        if (!_player->CanJoinToBattleground(bgTemplate))
        {
            WorldPackets::Battleground::BattlefieldStatusFailed battlefieldStatus;
            BattlegroundMgr::BuildBattlegroundStatusFailed(&battlefieldStatus, bgQueueTypeId, _player, 0, ERR_BATTLEGROUND_JOIN_TIMED_OUT);
            SendPacket(battlefieldStatus.Write());
            return;
        }

        // check Deserter debuff
        if (_player->IsDeserter())
        {
            WorldPackets::Battleground::BattlefieldStatusFailed battlefieldStatus;
            BattlegroundMgr::BuildBattlegroundStatusFailed(&battlefieldStatus, bgQueueTypeId, _player, 0, ERR_GROUP_JOIN_BATTLEGROUND_DESERTERS);
            SendPacket(battlefieldStatus.Write());
            return;
        }

        bool isInRandomBgQueue = _player->InBattlegroundQueueForBattlegroundQueueType(BattlegroundMgr::BGQueueTypeId(BATTLEGROUND_RB, BattlegroundQueueIdType::Battleground, false, 0))
            || _player->InBattlegroundQueueForBattlegroundQueueType(BattlegroundMgr::BGQueueTypeId(BATTLEGROUND_RANDOM_EPIC, BattlegroundQueueIdType::Battleground, false, 0));
        if (!BattlegroundMgr::IsRandomBattleground(bgTypeId) && isInRandomBgQueue)
        {
            // player is already in random queue
            WorldPackets::Battleground::BattlefieldStatusFailed battlefieldStatus;
            BattlegroundMgr::BuildBattlegroundStatusFailed(&battlefieldStatus, bgQueueTypeId, _player, 0, ERR_IN_RANDOM_BG);
            SendPacket(battlefieldStatus.Write());
            return;
        }

        if (_player->InBattlegroundQueue(true) && !isInRandomBgQueue && BattlegroundMgr::IsRandomBattleground(bgTypeId))
        {
            // player is already in queue, can't start random queue
            WorldPackets::Battleground::BattlefieldStatusFailed battlefieldStatus;
            BattlegroundMgr::BuildBattlegroundStatusFailed(&battlefieldStatus, bgQueueTypeId, _player, 0, ERR_IN_NON_RANDOM_BG);
            SendPacket(battlefieldStatus.Write());
            return;
        }

        // check if already in queue
        if (_player->GetBattlegroundQueueIndex(bgQueueTypeId) < PLAYER_MAX_BATTLEGROUND_QUEUES)
            // player is already in this queue
            return;

        // check if has free queue slots
        if (!_player->HasFreeBattlegroundQueueId())
        {
            WorldPackets::Battleground::BattlefieldStatusFailed battlefieldStatus;
            BattlegroundMgr::BuildBattlegroundStatusFailed(&battlefieldStatus, bgQueueTypeId, _player, 0, ERR_BATTLEGROUND_TOO_MANY_QUEUES);
            SendPacket(battlefieldStatus.Write());
            return;
        }

        // check Freeze debuff
        if (_player->HasAura(9454))
            return;

        BattlegroundQueue& bgQueue = sBattlegroundMgr->GetBattlegroundQueue(bgQueueTypeId);
        GroupQueueInfo* ginfo = bgQueue.AddGroup(_player, nullptr, getQueueTeam(), bracketEntry, false, isPremade, 0);
        uint32 avgTime = bgQueue.GetAverageQueueWaitTime(ginfo, bracketEntry->GetBracketId());
        uint32 queueSlot = _player->AddBattlegroundQueueId(bgQueueTypeId);

        WorldPackets::Battleground::BattlefieldStatusQueued battlefieldStatus;
        // The ticket time must be the one the invite and port statuses use (Player::GetBattlegroundQueueJoinTime, in seconds). The group's
        // JoinTime is in milliseconds, so the queued and the invite status looked like two different tickets to the 70205 client: two
        // Warsong Gulch entries, and Enter on the invite did nothing.
        BattlegroundMgr::BuildBattlegroundStatusQueued(&battlefieldStatus, _player, queueSlot, _player->GetBattlegroundQueueJoinTime(bgQueueTypeId), bgQueueTypeId, avgTime, false);
        SendPacket(battlefieldStatus.Write());

        TC_LOG_DEBUG("bg.battleground", "Battleground: player joined queue for bg queue {{ BattlemasterListId: {}, Type: {}, Rated: {}, TeamSize: {} }}, {}, NAME {}",
            bgQueueTypeId.BattlemasterListId, uint32(bgQueueTypeId.Type), bgQueueTypeId.Rated ? "true" : "false", uint32(bgQueueTypeId.TeamSize),
            _player->GetGUID().ToString(), _player->GetName());

        // Playerbot V2: solo player just joined a BG queue. Auto-fill the
        // remaining slots with bots (online preferred, JIT-spawn the rest).
        // Only fire for non-bot human players.
        if (!IsBot())
            Playerbot::Hooks::OnPlayerJoinedBgQueue(_player, bgTypeId, bracketEntry->GetBracketId());
    }
    else
    {
        if (grp->GetLeaderGUID() != _player->GetGUID())
            return;

        ObjectGuid errorGuid;
        err = grp->CanJoinBattlegroundQueue(bgTemplate, bgQueueTypeId, 0, bgTemplate->GetMaxPlayersPerTeam(), false, 0, errorGuid);
        isPremade = (grp->GetMembersCount() >= bgTemplate->GetMinPlayersPerTeam());

        BattlegroundQueue& bgQueue = sBattlegroundMgr->GetBattlegroundQueue(bgQueueTypeId);
        GroupQueueInfo* ginfo = nullptr;
        uint32 avgTime = 0;

        if (!err)
        {
            TC_LOG_DEBUG("bg.battleground", "Battleground: the following players are joining as group:");
            ginfo = bgQueue.AddGroup(_player, grp, getQueueTeam(), bracketEntry, false, isPremade, 0);
            avgTime = bgQueue.GetAverageQueueWaitTime(ginfo, bracketEntry->GetBracketId());
        }

        if (err)
        {
            // The client closes the join window on a failed status without saying why; say it in chat as well.
            Player const* culprit = errorGuid.IsEmpty() ? nullptr : ObjectAccessor::FindPlayer(errorGuid);
            ChatHandler(this).SendSysMessage(Trinity::StringFormat("Cannot join the battleground queue as a group: {} (error {}).{}",
                err == ERR_BATTLEGROUND_JOIN_RANGE_INDEX ? "group members are in different level brackets" :
                err == ERR_GROUP_JOIN_BATTLEGROUND_DESERTERS ? "a group member is a deserter" :
                err == ERR_BATTLEGROUND_TOO_MANY_QUEUES ? "a group member has no free queue slot" : "a group member is not eligible",
                uint32(err), culprit ? " Check " + culprit->GetName() + "." : std::string()));
            TC_LOG_ERROR("bg.battleground", "Battleground: group queue of {} rejected, error {}, member {}", _player->GetName(), uint32(err), errorGuid.ToString());
        }

        for (GroupReference const& itr : grp->GetMembers())
        {
            Player* member = itr.GetSource();
            if (err)
            {
                WorldPackets::Battleground::BattlefieldStatusFailed battlefieldStatus;
                BattlegroundMgr::BuildBattlegroundStatusFailed(&battlefieldStatus, bgQueueTypeId, _player, 0, err, &errorGuid);
                member->SendDirectMessage(battlefieldStatus.Write());
                continue;
            }

            // add to queue
            uint32 queueSlot = member->AddBattlegroundQueueId(bgQueueTypeId);

            WorldPackets::Battleground::BattlefieldStatusQueued battlefieldStatus;
            BattlegroundMgr::BuildBattlegroundStatusQueued(&battlefieldStatus, member, queueSlot, member->GetBattlegroundQueueJoinTime(bgQueueTypeId), bgQueueTypeId, avgTime, true);
            member->SendDirectMessage(battlefieldStatus.Write());
            TC_LOG_DEBUG("bg.battleground", "Battleground: player joined queue for bg queue {{ BattlemasterListId: {}, Type: {}, Rated: {}, TeamSize: {} }}, {}, NAME {}",
                bgQueueTypeId.BattlemasterListId, uint32(bgQueueTypeId.Type), bgQueueTypeId.Rated ? "true" : "false", uint32(bgQueueTypeId.TeamSize),
                member->GetGUID().ToString(), member->GetName());
        }
        TC_LOG_DEBUG("bg.battleground", "Battleground: group end");

        // Playerbot V2: a human leader queued the group (bots in it are queued with it). Fill the other team and the free
        // slots the same way as for a solo human; the bot-leader group queue in PlayerbotAPI::bg_queue does the same.
        if (!err && !IsBot())
            Playerbot::Hooks::OnPlayerJoinedBgQueue(_player, bgTypeId, bracketEntry->GetBracketId());
    }

    sBattlegroundMgr->ScheduleQueueUpdate(0, bgQueueTypeId, bracketEntry->GetBracketId());
}

void WorldSession::HandlePVPLogDataOpcode(WorldPackets::Battleground::PVPLogDataRequest& /*pvpLogDataRequest*/)
{
    Battleground* bg = _player->GetBattleground();
    if (!bg)
        return;

    // Prevent players from sending BuildPvpLogDataPacket in an arena except for when sent in BattleGround::EndBattleGround.
    if (bg->isArena())
        return;

    WorldPackets::Battleground::PVPMatchStatisticsMessage pvpMatchStatistics;
    bg->BuildPvPLogDataPacket(pvpMatchStatistics.Data);
    SendPacket(pvpMatchStatistics.Write());
}

void WorldSession::HandleBattlefieldListOpcode(WorldPackets::Battleground::BattlefieldListRequest& battlefieldList)
{
    BattlemasterListEntry const* battlemasterListEntry = sBattlemasterListStore.LookupEntry(battlefieldList.ListID);
    if (!battlemasterListEntry)
    {
        TC_LOG_DEBUG("bg.battleground", "BattlegroundHandler: invalid bgtype ({}) with player (Name: {}, {}) received.", battlefieldList.ListID, _player->GetName(), _player->GetGUID().ToString());
        return;
    }

    sBattlegroundMgr->SendBattlegroundList(_player, ObjectGuid::Empty, BattlegroundTypeId(battlefieldList.ListID));
}

void WorldSession::HandleBattleFieldPortOpcode(WorldPackets::Battleground::BattlefieldPort& battlefieldPort)
{
    TC_LOG_INFO("playerbot.v2", "[bg_port] {} ticket id {} type {} time {} accepted {} queues: {}", _player->GetName(), battlefieldPort.Ticket.Id,
        uint32(battlefieldPort.Ticket.Type), battlefieldPort.Ticket.Time.AsUnderlyingType(), uint32(battlefieldPort.AcceptedInvite),
        [&]
        {
            std::string slots;
            for (uint8 i = 0; i < PLAYER_MAX_BATTLEGROUND_QUEUES; ++i)
                if (BattlegroundQueueTypeId q = _player->GetBattlegroundQueueTypeId(i); q != BATTLEGROUND_QUEUE_NONE)
                    slots += Trinity::StringFormat("{}={} ", uint32(i), uint32(q.BattlemasterListId));
            return slots;
        }());

    if (!_player->InBattlegroundQueue())
    {
        TC_LOG_DEBUG("bg.battleground", "CMSG_BATTLEFIELD_PORT {} Slot: {}, Unk: {}, Time: {}, AcceptedInvite: {}. Player not in queue!",
            GetPlayerInfo(), battlefieldPort.Ticket.Id, uint32(battlefieldPort.Ticket.Type), battlefieldPort.Ticket.Time.AsUnderlyingType(), uint32(battlefieldPort.AcceptedInvite));
        Playerbot::Hooks::OnBGPortFailed(_player, /*not_in_queue*/ 1, 0);
        return;
    }

    // A leave request whose ticket slot names no queue of ours (the 70205 client keeps its own idea of which queue it joined) removes the
    // player from the queues they really are in instead of leaving them stuck in a queue the client cannot leave.
    auto leaveAllQueues = [&]
    {
        for (uint8 i = 0; i < PLAYER_MAX_BATTLEGROUND_QUEUES; ++i)
        {
            BattlegroundQueueTypeId queued = _player->GetBattlegroundQueueTypeId(i);
            if (queued == BATTLEGROUND_QUEUE_NONE)
                continue;
            TC_LOG_ERROR("playerbot.v2", "[bg_port] {} leave with ticket slot {} matched no queue: removed from queue slot {} (battleground {})",
                _player->GetName(), battlefieldPort.Ticket.Id, uint32(i), uint32(queued.BattlemasterListId));
            WorldPackets::Battleground::BattlefieldStatusNone battlefieldStatus;
            battlefieldStatus.Ticket = battlefieldPort.Ticket;
            battlefieldStatus.Ticket.Id = i;
            SendPacket(battlefieldStatus.Write());
            _player->RemoveBattlegroundQueueId(queued);
            sBattlegroundMgr->GetBattlegroundQueue(queued).RemovePlayer(_player->GetGUID(), true);
        }
    };

    // An accept whose ticket slot is not the invited queue (2026-10-07: the client listed two Warsong Gulch entries, only one with Enter,
    // and Enter did nothing) is pointed at the queue the player was really invited from.
    if (battlefieldPort.AcceptedInvite)
    {
        auto isInvited = [&](BattlegroundQueueTypeId queued)
        {
            GroupQueueInfo info;
            return queued != BATTLEGROUND_QUEUE_NONE && sBattlegroundMgr->GetBattlegroundQueue(queued).GetPlayerGroupInfoData(_player->GetGUID(), &info)
                && info.IsInvitedToBGInstanceGUID;
        };
        if (!isInvited(_player->GetBattlegroundQueueTypeId(battlefieldPort.Ticket.Id)))
            for (uint8 i = 0; i < PLAYER_MAX_BATTLEGROUND_QUEUES; ++i)
                if (isInvited(_player->GetBattlegroundQueueTypeId(i)))
                {
                    TC_LOG_ERROR("playerbot.v2", "[bg_port] {} accepted with ticket slot {} but the invite is in queue slot {}: using that slot",
                        _player->GetName(), battlefieldPort.Ticket.Id, uint32(i));
                    battlefieldPort.Ticket.Id = i;
                    break;
                }
    }

    BattlegroundQueueTypeId bgQueueTypeId = _player->GetBattlegroundQueueTypeId(battlefieldPort.Ticket.Id);
    if (bgQueueTypeId == BATTLEGROUND_QUEUE_NONE)
    {
        TC_LOG_DEBUG("bg.battleground", "CMSG_BATTLEFIELD_PORT {} Slot: {}, Unk: {}, Time: {}, AcceptedInvite: {}. Invalid queueSlot!",
            GetPlayerInfo(), battlefieldPort.Ticket.Id, uint32(battlefieldPort.Ticket.Type), battlefieldPort.Ticket.Time.AsUnderlyingType(), uint32(battlefieldPort.AcceptedInvite));
        Playerbot::Hooks::OnBGPortFailed(_player, /*invalid_queue_slot*/ 2, 0);
        if (!battlefieldPort.AcceptedInvite)
            leaveAllQueues();
        return;
    }

    BattlegroundQueue& bgQueue = sBattlegroundMgr->GetBattlegroundQueue(bgQueueTypeId);

    //we must use temporary variable, because GroupQueueInfo pointer can be deleted in BattlegroundQueue::RemovePlayer() function
    GroupQueueInfo ginfo;
    if (!bgQueue.GetPlayerGroupInfoData(_player->GetGUID(), &ginfo))
    {
        TC_LOG_DEBUG("bg.battleground", "CMSG_BATTLEFIELD_PORT {} Slot: {}, Unk: {}, Time: {}, AcceptedInvite: {}. Player not in queue (No player Group Info)!",
            GetPlayerInfo(), battlefieldPort.Ticket.Id, uint32(battlefieldPort.Ticket.Type), battlefieldPort.Ticket.Time.AsUnderlyingType(), uint32(battlefieldPort.AcceptedInvite));
        Playerbot::Hooks::OnBGPortFailed(_player, /*no_group_info*/ 3, 0);
        if (!battlefieldPort.AcceptedInvite)
            leaveAllQueues();
        return;
    }
    // if action == 1, then player must have been invited to join
    if (!ginfo.IsInvitedToBGInstanceGUID && battlefieldPort.AcceptedInvite)
    {
        TC_LOG_DEBUG("bg.battleground", "CMSG_BATTLEFIELD_PORT {} Slot: {}, Unk: {}, Time: {}, AcceptedInvite: {}. Player is not invited to any bg!",
            GetPlayerInfo(), battlefieldPort.Ticket.Id, uint32(battlefieldPort.Ticket.Type), battlefieldPort.Ticket.Time.AsUnderlyingType(), uint32(battlefieldPort.AcceptedInvite));
        Playerbot::Hooks::OnBGPortFailed(_player, /*ginfo_not_invited*/ 4, 0);
        return;
    }

    BattlegroundTypeId bgTypeId = BattlegroundTypeId(bgQueueTypeId.BattlemasterListId);
    BattlegroundTemplate const* bgTemplate = sBattlegroundMgr->GetBattlegroundTemplateByTypeId(bgTypeId);
    if (!bgTemplate)
    {
        TC_LOG_ERROR("network", "BattlegroundHandle: BattlegroundTemplate not found for type id {}.", bgTypeId);
        return;
    }

    uint32 mapId = bgTemplate->MapIDs.front();

    // BGTemplateId returns BATTLEGROUND_AA when it is arena queue.
    // Do instance id search as there is no AA bg instances.
    Battleground* bg = sBattlegroundMgr->GetBattleground(ginfo.IsInvitedToBGInstanceGUID, bgTypeId == BATTLEGROUND_AA ? BATTLEGROUND_TYPE_NONE : bgTypeId);
    if (!bg && battlefieldPort.AcceptedInvite)
    {
        TC_LOG_DEBUG("bg.battleground", "CMSG_BATTLEFIELD_PORT {} Slot: {}, Unk: {}, Time: {}, AcceptedInvite: {}. Cant find BG with id {}!",
            GetPlayerInfo(), battlefieldPort.Ticket.Id, uint32(battlefieldPort.Ticket.Type), battlefieldPort.Ticket.Time.AsUnderlyingType(), uint32(battlefieldPort.AcceptedInvite), ginfo.IsInvitedToBGInstanceGUID);
        Playerbot::Hooks::OnBGPortFailed(_player, /*bg_instance_gone*/ 5, ginfo.IsInvitedToBGInstanceGUID);
        return;
    }
    else if (bg)
        mapId = bg->GetMapId();

    TC_LOG_DEBUG("bg.battleground", "CMSG_BATTLEFIELD_PORT {} Slot: {}, Unk: {}, Time: {}, AcceptedInvite: {}.",
        GetPlayerInfo(), battlefieldPort.Ticket.Id, uint32(battlefieldPort.Ticket.Type), battlefieldPort.Ticket.Time.AsUnderlyingType(), uint32(battlefieldPort.AcceptedInvite));

    // expected bracket entry
    PVPDifficultyEntry const* bracketEntry = DB2Manager::GetBattlegroundBracketByLevel(mapId, _player->GetLevel());
    if (!bracketEntry)
    {
        Playerbot::Hooks::OnBGPortFailed(_player, /*no_bracket_entry*/ 6,
            ginfo.IsInvitedToBGInstanceGUID);
        return;
    }

    //some checks if player isn't cheating - it is not exactly cheating, but we cannot allow it
    if (battlefieldPort.AcceptedInvite && bgQueue.GetQueueId().TeamSize == 0)
    {
        //if player is trying to enter battleground (not arena!) and he has deserter debuff, we must just remove him from queue
        if (_player->IsDeserter())
        {
            //send bg command result to show nice message
            WorldPackets::Battleground::BattlefieldStatusFailed battlefieldStatus;
            BattlegroundMgr::BuildBattlegroundStatusFailed(&battlefieldStatus, bgQueueTypeId, _player, battlefieldPort.Ticket.Id, ERR_GROUP_JOIN_BATTLEGROUND_DESERTERS);
            SendPacket(battlefieldStatus.Write());
            battlefieldPort.AcceptedInvite = false;
            TC_LOG_DEBUG("bg.battleground", "Player {} {} has a deserter debuff, do not port him to battleground!", _player->GetName(), _player->GetGUID().ToString());
        }
        //if player don't match battleground max level, then do not allow him to enter! (this might happen when player leveled up during his waiting in queue
        if (_player->GetLevel() > bg->GetMaxLevel())
        {
            TC_LOG_ERROR("network", "Player {} {} has level ({}) higher than maxlevel ({}) of battleground ({})! Do not port him to battleground!",
                _player->GetName(), _player->GetGUID().ToString(), _player->GetLevel(), bg->GetMaxLevel(), bg->GetTypeID());
            battlefieldPort.AcceptedInvite = false;
        }
    }

    if (battlefieldPort.AcceptedInvite)
    {
        // check Freeze debuff
        if (_player->HasAura(9454))
        {
            Playerbot::Hooks::OnBGPortFailed(_player, /*freeze_debuff*/ 7,
                ginfo.IsInvitedToBGInstanceGUID);
            return;
        }

        if (!_player->IsInvitedForBattlegroundQueueType(bgQueueTypeId))
        {
            Playerbot::Hooks::OnBGPortFailed(_player, /*invite_flag_cleared*/ 8,
                ginfo.IsInvitedToBGInstanceGUID);
            return;                                 // cheating?
        }

        if (!_player->InBattleground())
            _player->SetBattlegroundEntryPoint();

        // resurrect the player
        if (!_player->IsAlive())
        {
            _player->ResurrectPlayer(1.0f);
            _player->SpawnCorpseBones();
        }
        // stop taxi flight at port
        _player->FinishTaxiFlight();

        WorldPackets::Battleground::BattlefieldStatusActive battlefieldStatus;
        BattlegroundMgr::BuildBattlegroundStatusActive(&battlefieldStatus, bg, _player, battlefieldPort.Ticket.Id, _player->GetBattlegroundQueueJoinTime(bgQueueTypeId), bgQueueTypeId);
        SendPacket(battlefieldStatus.Write());

        // remove battleground queue status from BGmgr
        bgQueue.RemovePlayer(_player->GetGUID(), false);
        // this is still needed here if battleground "jumping" shouldn't add deserter debuff
        // also this is required to prevent stuck at old battleground after SetBattlegroundId set to new
        if (Battleground* currentBg = _player->GetBattleground())
            currentBg->RemovePlayerAtLeave(_player->GetGUID(), false, true);

        // set the destination instance id
        _player->SetBattlegroundId(bg->GetInstanceID(), bg->GetTypeID(), bgQueueTypeId);
        // set the destination team
        _player->SetBGTeam(ginfo.Team);

        // bg->HandleBeforeTeleportToBattleground(_player);
        BattlegroundMgr::SendToBattleground(_player, bg);
        // add only in HandleMoveWorldPortAck()
        // bg->AddPlayer(_player, team);
        TC_LOG_DEBUG("bg.battleground", "Battleground: player {} ({}) joined battle for bg {}, bgtype {}, queue {{ BattlemasterListId: {}, Type: {}, Rated: {}, TeamSize: {} }}.",
            _player->GetName(), _player->GetGUID().ToString(), bg->GetInstanceID(), bg->GetTypeID(),
            bgQueueTypeId.BattlemasterListId, uint32(bgQueueTypeId.Type), bgQueueTypeId.Rated ? "true" : "false", uint32(bgQueueTypeId.TeamSize));
    }
    else // leave queue
    {
        WorldPackets::Battleground::BattlefieldStatusNone battlefieldStatus;
        battlefieldStatus.Ticket = battlefieldPort.Ticket;
        SendPacket(battlefieldStatus.Write());

        _player->RemoveBattlegroundQueueId(bgQueueTypeId);  // must be called this way, because if you move this call to queue->removeplayer, it causes bugs
        bgQueue.RemovePlayer(_player->GetGUID(), true);
        // player left queue, we should update it - do not update Arena Queue
        if (!bgQueue.GetQueueId().TeamSize)
            sBattlegroundMgr->ScheduleQueueUpdate(ginfo.ArenaMatchmakerRating, bgQueueTypeId, bracketEntry->GetBracketId());

        // The leave-queue branch must not require a live Battleground
        // instance. The early-return at the top of this function only
        // discards the case `!bg && AcceptedInvite` — a leave-queue port
        // (AcceptedInvite=false) is allowed to proceed with bg=null when
        // the BG instance has been destroyed since the queue entry was
        // created (typical paths: instance timed out before anyone ported,
        // last member departed, BATTLEGROUND_AA queue with no live arena).
        // All the actual cleanup above (queue removal, status packet,
        // schedule update) is correct without bg; only this debug log was
        // dereferencing it unconditionally. Use the queue's logical type
        // (bgTypeId is derived from bgQueueTypeId.BattlemasterListId at
        // the top of the function) — it's the correct identifier for the
        // queue the player was leaving, regardless of instance liveness.
        BattlegroundTypeId const loggedBgType = bg ? bg->GetTypeID() : bgTypeId;
        TC_LOG_DEBUG("bg.battleground", "Battleground: player {} ({}) left queue for bgtype {}, queue {{ BattlemasterListId: {}, Type: {}, Rated: {}, TeamSize: {} }}.",
            _player->GetName(), _player->GetGUID().ToString(), loggedBgType,
            bgQueueTypeId.BattlemasterListId, uint32(bgQueueTypeId.Type), bgQueueTypeId.Rated ? "true" : "false", uint32(bgQueueTypeId.TeamSize));
    }
}

void WorldSession::HandleBattlefieldLeaveOpcode(WorldPackets::Battleground::BattlefieldLeave& /*battlefieldLeave*/)
{
    TC_LOG_INFO("playerbot.v2", "[bg_leave] {} CMSG_BATTLEFIELD_LEAVE in battleground {} queued {}", _player->GetName(), _player->InBattleground(), _player->InBattlegroundQueue());

    // not allow leave battleground in combat
    if (_player->IsInCombat())
        if (Battleground* bg = _player->GetBattleground())
            if (bg->GetStatus() != STATUS_WAIT_LEAVE)
                return;

    Battleground* leftBg = _player->GetBattleground();
    _player->LeaveBattleground();

    // A real player leaving takes his group's bots out of the same match with him (they return to where they queued from), so
    // the next queue starts from the open world with the whole group instead of leaving the bots behind in the old instance.
    if (leftBg && !IsBot())
        if (Group* group = _player->GetGroup())
            for (GroupReference const& itr : group->GetMembers())
            {
                Player* member = itr.GetSource();
                if (!member || member == _player || !member->GetSession() || !member->GetSession()->IsBot() || member->GetBattleground() != leftBg)
                    continue;
                member->LeaveBattleground(true, /*withoutDeserterDebuff*/ true);   // no Deserter: it would block the group's next queue
                for (uint8 i = 0; i < PLAYER_MAX_BATTLEGROUND_QUEUES; ++i)
                {
                    BattlegroundQueueTypeId qid = member->GetBattlegroundQueueTypeId(i);
                    if (qid == BATTLEGROUND_QUEUE_NONE)
                        continue;
                    sBattlegroundMgr->GetBattlegroundQueue(qid).RemovePlayer(member->GetGUID(), true);
                    member->RemoveBattlegroundQueueId(qid);
                }
                TC_LOG_INFO("playerbot.v2", "[bg_leave] {} leaves with group owner {}", member->GetName(), _player->GetName());
            }
}

void WorldSession::HandleRequestBattlefieldStatusOpcode(WorldPackets::Battleground::RequestBattlefieldStatus& /*requestBattlefieldStatus*/)
{
    // we must update all queues here
    Battleground* bg = nullptr;
    for (uint8 i = 0; i < PLAYER_MAX_BATTLEGROUND_QUEUES; ++i)
    {
        BattlegroundQueueTypeId bgQueueTypeId = _player->GetBattlegroundQueueTypeId(i);
        if (bgQueueTypeId == BATTLEGROUND_QUEUE_NONE)
            continue;
        BattlegroundTypeId bgTypeId = BattlegroundTypeId(bgQueueTypeId.BattlemasterListId);
        bg = _player->GetBattleground();
        if (bg)
        {
            BattlegroundPlayer const* bgPlayer = bg->GetBattlegroundPlayerData(_player->GetGUID());
            if (bgPlayer && bgPlayer->queueTypeId == bgQueueTypeId)
            {
                //i cannot check any variable from player class because player class doesn't know if player is in 2v2 / 3v3 or 5v5 arena
                //so i must use bg pointer to get that information
                WorldPackets::Battleground::BattlefieldStatusActive battlefieldStatus;
                BattlegroundMgr::BuildBattlegroundStatusActive(&battlefieldStatus, bg, _player, i, _player->GetBattlegroundQueueJoinTime(bgQueueTypeId), bgQueueTypeId);
                SendPacket(battlefieldStatus.Write());
                continue;
            }
        }

        //we are sending update to player about queue - he can be invited there!
        //get GroupQueueInfo for queue status
        BattlegroundQueue& bgQueue = sBattlegroundMgr->GetBattlegroundQueue(bgQueueTypeId);
        GroupQueueInfo ginfo;
        if (!bgQueue.GetPlayerGroupInfoData(_player->GetGUID(), &ginfo))
            continue;
        if (ginfo.IsInvitedToBGInstanceGUID)
        {
            bg = sBattlegroundMgr->GetBattleground(ginfo.IsInvitedToBGInstanceGUID, bgTypeId);
            if (!bg)
                continue;

            WorldPackets::Battleground::BattlefieldStatusNeedConfirmation battlefieldStatus;
            BattlegroundMgr::BuildBattlegroundStatusNeedConfirmation(&battlefieldStatus, bg, _player, i, _player->GetBattlegroundQueueJoinTime(bgQueueTypeId), getMSTimeDiff(GameTime::GetGameTimeMS(), ginfo.RemoveInviteTime), bgQueueTypeId);
            SendPacket(battlefieldStatus.Write());
        }
        else
        {
            BattlegroundTemplate const* bgTemplate  = sBattlegroundMgr->GetBattlegroundTemplateByTypeId(bgTypeId);
            if (!bgTemplate)
                continue;

            // expected bracket entry
            PVPDifficultyEntry const* bracketEntry = DB2Manager::GetBattlegroundBracketByLevel(bgTemplate->MapIDs.front(), _player->GetLevel());
            if (!bracketEntry)
                continue;

            uint32 avgTime = bgQueue.GetAverageQueueWaitTime(&ginfo, bracketEntry->GetBracketId());
            WorldPackets::Battleground::BattlefieldStatusQueued battlefieldStatus;
            BattlegroundMgr::BuildBattlegroundStatusQueued(&battlefieldStatus, _player, i, _player->GetBattlegroundQueueJoinTime(bgQueueTypeId), bgQueueTypeId, avgTime, ginfo.Players.size() > 1);
            SendPacket(battlefieldStatus.Write());
        }
    }
}

void WorldSession::HandleBattlemasterJoinArena(WorldPackets::Battleground::BattlemasterJoinArena& packet)
{
    // ignore if we already in BG or BG queue
    if (_player->InBattleground())
        return;

    uint8 arenatype = ArenaTeam::GetTypeBySlot(packet.TeamSizeIndex);

    //check existence
    BattlegroundTemplate const* bgTemplate = sBattlegroundMgr->GetBattlegroundTemplateByTypeId(BATTLEGROUND_AA);
    if (!bgTemplate)
    {
        TC_LOG_ERROR("network", "Battleground: template bg (all arenas) not found");
        return;
    }

    if (DisableMgr::IsDisabledFor(DISABLE_TYPE_BATTLEGROUND, BATTLEGROUND_AA, nullptr))
    {
        ChatHandler(this).PSendSysMessage(LANG_ARENA_DISABLED);
        return;
    }

    BattlegroundTypeId bgTypeId = bgTemplate->Id;
    BattlegroundQueueTypeId bgQueueTypeId = BattlegroundMgr::BGQueueTypeId(bgTypeId, BattlegroundQueueIdType::Arena, true, arenatype);
    PVPDifficultyEntry const* bracketEntry = DB2Manager::GetBattlegroundBracketByLevel(bgTemplate->MapIDs.front(), _player->GetLevel());
    if (!bracketEntry)
        return;

    Group* grp = _player->GetGroup();
    if (!grp)
    {
        grp = new Group();
        grp->Create(_player);
    }

    // no group found, error
    if (!grp)
        return;

    if (grp->GetLeaderGUID() != _player->GetGUID())
        return;

    // get the team rating for queuing
    uint32 arenaRating = 1; //at->GetRating();
    uint32 matchmakerRating = 1; //at->GetAverageMMR(grp);

    if (arenaRating <= 0)
        arenaRating = 1;

    BattlegroundQueue& bgQueue = sBattlegroundMgr->GetBattlegroundQueue(bgQueueTypeId);

    uint32 avgTime = 0;
    GroupQueueInfo* ginfo = nullptr;

    ObjectGuid errorGuid;
    GroupJoinBattlegroundResult err = ERR_BATTLEGROUND_NONE;
    if (!sBattlegroundMgr->isArenaTesting())
        err = grp->CanJoinBattlegroundQueue(bgTemplate, bgQueueTypeId, arenatype, arenatype, true, packet.TeamSizeIndex, errorGuid);

    if (!err)
    {
        TC_LOG_DEBUG("bg.battleground", "Battleground: arena team id {}, leader {} queued with matchmaker rating {} for type {}", _player->GetArenaTeamId(packet.TeamSizeIndex), _player->GetName(), matchmakerRating, arenatype);

        ginfo = bgQueue.AddGroup(_player, grp, Team(_player->GetTeam()), bracketEntry, false, arenaRating, matchmakerRating);
        avgTime = bgQueue.GetAverageQueueWaitTime(ginfo, bracketEntry->GetBracketId());
    }

    for (GroupReference const& itr : grp->GetMembers())
    {
        Player* member = itr.GetSource();
        if (err)
        {
            WorldPackets::Battleground::BattlefieldStatusFailed battlefieldStatus;
            BattlegroundMgr::BuildBattlegroundStatusFailed(&battlefieldStatus, bgQueueTypeId, _player, 0, err, &errorGuid);
            member->SendDirectMessage(battlefieldStatus.Write());
            continue;
        }

        if (!_player->CanJoinToBattleground(bgTemplate))
        {
            WorldPackets::Battleground::BattlefieldStatusFailed battlefieldStatus;
            BattlegroundMgr::BuildBattlegroundStatusFailed(&battlefieldStatus, bgQueueTypeId, _player, 0, ERR_BATTLEGROUND_JOIN_FAILED, &errorGuid);
            member->SendDirectMessage(battlefieldStatus.Write());
            return;
        }

        // add to queue
        uint32 queueSlot = member->AddBattlegroundQueueId(bgQueueTypeId);

        WorldPackets::Battleground::BattlefieldStatusQueued battlefieldStatus;
        BattlegroundMgr::BuildBattlegroundStatusQueued(&battlefieldStatus, member, queueSlot, member->GetBattlegroundQueueJoinTime(bgQueueTypeId), bgQueueTypeId, avgTime, true);
        member->SendDirectMessage(battlefieldStatus.Write());

        TC_LOG_DEBUG("bg.battleground", "Battleground: player joined queue for arena as group bg queue {{ BattlemasterListId: {}, Type: {}, Rated: {}, TeamSize: {} }}, {}, NAME {}",
            bgQueueTypeId.BattlemasterListId, uint32(bgQueueTypeId.Type), bgQueueTypeId.Rated ? "true" : "false", uint32(bgQueueTypeId.TeamSize),
            member->GetGUID().ToString(), member->GetName());
    }

    sBattlegroundMgr->ScheduleQueueUpdate(matchmakerRating, bgQueueTypeId, bracketEntry->GetBracketId());
}

void WorldSession::HandleReportPvPAFK(WorldPackets::Battleground::ReportPvPPlayerAFK& reportPvPPlayerAFK)
{
    Player* reportedPlayer = ObjectAccessor::FindPlayer(reportPvPPlayerAFK.Offender);
    if (!reportedPlayer)
    {
        TC_LOG_INFO("bg.reportpvpafk", "WorldSession::HandleReportPvPAFK: {} [IP: {}] reported {}", _player->GetName(), _player->GetSession()->GetRemoteAddress(), reportPvPPlayerAFK.Offender.ToString());
        return;
    }

    TC_LOG_DEBUG("bg.battleground", "WorldSession::HandleReportPvPAFK: {} reported {}", _player->GetName(), reportedPlayer->GetName());

    reportedPlayer->ReportedAfkBy(_player);
}

void WorldSession::HandleRequestRatedPvpInfo(WorldPackets::Battleground::RequestRatedPvpInfo& /*packet*/)
{
    WorldPackets::Battleground::RatedPvpInfo ratedPvpInfo;
    SendPacket(ratedPvpInfo.Write());
}

void WorldSession::HandleGetPVPOptionsEnabled(WorldPackets::Battleground::GetPVPOptionsEnabled& /*getPvPOptionsEnabled*/)
{
    WorldPackets::Battleground::PVPOptionsEnabled pvpOptionsEnabled;
    pvpOptionsEnabled.RatedBattlegrounds = false;
    pvpOptionsEnabled.PugBattlegrounds = true;
    pvpOptionsEnabled.WargameBattlegrounds = false;
    pvpOptionsEnabled.WargameArenas = false;
    pvpOptionsEnabled.RatedArenas = true;
    pvpOptionsEnabled.ArenaSkirmish = false;
    pvpOptionsEnabled.SoloShuffle = false;
    pvpOptionsEnabled.RatedSoloShuffle = false;
    pvpOptionsEnabled.BattlegroundBlitz = false;
    pvpOptionsEnabled.RatedBattlegroundBlitz = false;
    SendPacket(pvpOptionsEnabled.Write());
}

void WorldSession::HandleRequestPvpReward(WorldPackets::Battleground::RequestPVPRewards& /*packet*/)
{
    _player->SendPvpRewards();
}

void WorldSession::HandleAreaSpiritHealerQueryOpcode(WorldPackets::Battleground::AreaSpiritHealerQuery& areaSpiritHealerQuery)
{
    Player* player = GetPlayer();
    Creature* spiritHealer = ObjectAccessor::GetCreature(*player, areaSpiritHealerQuery.HealerGuid);
    if (!spiritHealer)
        return;

    if (!spiritHealer->IsAreaSpiritHealer())
        return;

    if (!_player->IsWithinDistInMap(spiritHealer, MAX_AREA_SPIRIT_HEALER_RANGE))
        return;

    if (spiritHealer->IsAreaSpiritHealerIndividual())
    {
        if (Aura* aura = player->GetAura(SPELL_SPIRIT_HEAL_PLAYER_AURA))
        {
            player->SendAreaSpiritHealerTime(spiritHealer->GetGUID(), aura->GetDuration());
        }
        else if (SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(SPELL_SPIRIT_HEAL_PLAYER_AURA, DIFFICULTY_NONE))
        {
            spiritHealer->CastSpell(player, SPELL_SPIRIT_HEAL_PLAYER_AURA);
            player->SendAreaSpiritHealerTime(spiritHealer->GetGUID(), spellInfo->GetDuration());
            spiritHealer->CastSpell(nullptr, SPELL_SPIRIT_HEAL_CHANNEL_SELF);
        }
    }
    else
        _player->SendAreaSpiritHealerTime(spiritHealer);
}

void WorldSession::HandleAreaSpiritHealerQueueOpcode(WorldPackets::Battleground::AreaSpiritHealerQueue& areaSpiritHealerQueue)
{
    Creature* spiritHealer = ObjectAccessor::GetCreature(*GetPlayer(), areaSpiritHealerQueue.HealerGuid);
    if (!spiritHealer)
        return;

    if (!spiritHealer->IsAreaSpiritHealer())
        return;

    if (!_player->IsWithinDistInMap(spiritHealer, MAX_AREA_SPIRIT_HEALER_RANGE))
        return;

    _player->SetAreaSpiritHealer(spiritHealer);
}

void WorldSession::HandleHearthAndResurrect(WorldPackets::Battleground::HearthAndResurrect& /*hearthAndResurrect*/)
{
    if (_player->IsInFlight())
        return;

    if (Battlefield* bf = sBattlefieldMgr->GetBattlefieldToZoneId(_player->GetMap(), _player->GetZoneId()))
    {
        bf->PlayerAskToLeave(_player);
        return;
    }

    AreaTableEntry const* atEntry = sAreaTableStore.LookupEntry(_player->GetAreaId());
    if (!atEntry || !(atEntry->GetFlags().HasFlag(AreaFlags::AllowHearthAndRessurectFromArea)))
        return;

    _player->BuildPlayerRepop();
    _player->ResurrectPlayer(1.0f);
    _player->TeleportTo(_player->m_homebind);
}
