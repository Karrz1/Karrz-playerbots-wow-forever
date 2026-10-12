#include "tc_catch2.h"
#include "ByteBuffer.h"
#include "CharacterPackets.h"
#include "BattlegroundPackets.h"
#include "ChatPackets.h"
#include "ClassicOpcodes.h"
#include "ClassicWireSpecReader.h"
#include "CombatPackets.h"
#include "ItemPackets.h"
#include "MiscPackets.h"
#include "MovementPackets.h"
#include "ObjectGuid.h"
#include "Opcodes.h"
#include "PartyPackets.h"
#include "WorldPacket.h"
#include "WorldStatePackets.h"
#include <bit>
#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

// Golden byte tests: the packet our server writes must be read to the last byte by the 70291 client's own reader.
//
// The reader specs come from the client binary (main dispatch 0x8e5a90, one reader per message, see tools/protocol/analysis and
// doc/classic-70291-protocol.md) and are generated into data/Classic70291MainGroupSpecs.inc by tools/protocol/analysis/gen_specs.py.
// A spec is the flat read sequence: u8 u16 u32 u64 GUID (a packed guid: two mask bytes + one byte per set bit). Bit fields are read by the
// client as whole bytes, so they appear as u8. A loop body appears once: a probe packet holds ONE element per container.
// "?" = a field the analysis did not resolve yet, "!" = the reader page was not decrypted in the dump: both are the to-decode list
// (hidden test [.ClassicWireGaps] lists them; run it explicitly).
namespace
{
using ClassicOpcodes::Build;

using ClassicWireSpec::SpecEntry;
using ClassicWireSpec::FindSpec;
using ClassicWireSpec::ReadSpec;
using ClassicWireSpec::MainGroupSpecs;

template <typename Packet>
void ExpectConsumed(char const* name, Packet& packet)
{
    WorldPacket const* data = packet.Write();
    uint32 const wire = ClassicOpcodes::TranslateServerOpcode(Build::Build70291, packet.GetOpcode());
    INFO(name << " wire opcode 0x" << std::hex << wire);

    SpecEntry const* entry = FindSpec(wire);
    CHECK(entry != nullptr);
    if (!entry)
        return;
    std::string_view const spec = entry->Spec;
    INFO("spec: " << spec);
    // "?" / "!": not decoded yet, so the packet cannot be probed: take it out of the list until the analysis resolves it
    CHECK(spec != "?");
    CHECK(spec != "!");
    if (spec == "?" || spec == "!")
        return;

    std::string const error = ReadSpec(spec, data->data(), data->size());
    INFO(error);
    CHECK(error.empty());
}
}

TEST_CASE("The spec reader itself", "[ClassicWireSpec]")
{
    uint8 const guid[] = { 0x03, 0x00, 0xAA, 0xBB };    // mask 0x0003: two bytes
    CHECK(ReadSpec("GUID", guid, 4).empty());
    CHECK_FALSE(ReadSpec("GUID", guid, 3).empty());     // shorter than the mask says
    CHECK_FALSE(ReadSpec("GUID u8", guid, 4).empty());  // the client wants one more byte
    CHECK_FALSE(ReadSpec("u8", guid, 4).empty());       // the packet has bytes the client never reads
    CHECK(ReadSpec("", nullptr, 0).empty());
    std::vector<uint8> const zeros(14, 0);
    CHECK(ReadSpec("u16 u32 u64", zeros.data(), zeros.size()).empty());
}

TEST_CASE("70291 main group specs are consistent with the translated opcodes", "[ClassicWireSpec]")
{
    // the case index of a translated opcode must exist in the generated table, and nothing may be listed twice
    std::map<uint32, int> seen;
    for (SpecEntry const& entry : MainGroupSpecs)
        CHECK(++seen[entry.Index] == 1);
    CHECK(seen.size() == 911);      // the main dispatcher has 911 cases
    CHECK(FindSpec(ClassicOpcodes::TranslateServerOpcode(Build::Build70291, SMSG_PARTY_UPDATE)) != nullptr);
}

// One probe per packet. Every container holds one element (loops are in the spec once). The comment names the client case.
TEST_CASE("70291 packets are read to the last byte by the client", "[ClassicWireSpec]")
{
    {   // 0x065 u64 7 x u32 u8: found by the writer scan (tools/protocol/analysis/writer_vs_reader.py), the writer sent 8 bytes less
        WorldPackets::Battleground::SeasonInfo packet;
        packet.Classic70291 = true;
        ExpectConsumed("SEASON_INFO", packet);
    }
    {   // 0x1F3 u32 u32 u8
        WorldPackets::WorldState::UpdateWorldState packet;
        packet.VariableID = 2245; packet.Value = 1; packet.Hidden = true;
        ExpectConsumed("UPDATE_WORLD_STATE", packet);
    }
    {   // 0x153 u32 u32 u8
        WorldPackets::Misc::Weather packet;
        ExpectConsumed("WEATHER", packet);
    }
    {   // 0x181 u32 u32 u8
        WorldPackets::Character::PlayedTime packet;
        packet.TotalTime = 5; packet.LevelTime = 3; packet.TriggerEvent = true;
        ExpectConsumed("PLAYED_TIME", packet);
    }
    {   // 0x190 GUID u32 u8 u32 u32
        WorldPackets::Character::LogXPGain packet;
        packet.Victim = ObjectGuid::Create<HighGuid::Creature>(1, 2, 3);
        packet.Original = 100; packet.Reason = 1; packet.Amount = 90; packet.GroupBonus = 0.5f;
        ExpectConsumed("LOG_XP_GAIN", packet);
    }
    {   // 0x127 GUID GUID u32 u32 GUID
        WorldPackets::Chat::STextEmote packet;
        packet.SourceGUID = ObjectGuid::Create<HighGuid::Player>(1);
        packet.TargetGUID = ObjectGuid::Create<HighGuid::Player>(2);
        packet.EmoteID = 10; packet.SoundIndex = 3;
        ExpectConsumed("TEXT_EMOTE", packet);
    }
    {   // 0x0DA GUID GUID u32 u32 u32
        WorldPackets::Misc::RandomRoll packet;
        packet.Roller = ObjectGuid::Create<HighGuid::Player>(1);
        packet.Min = 1; packet.Max = 100; packet.Result = 42;
        ExpectConsumed("RANDOM_ROLL", packet);
    }
    {   // 0x09A u8 GUID GUID u64
        WorldPackets::Party::ReadyCheckStarted packet;
        packet.PartyGUID = ObjectGuid::Create<HighGuid::Party>(1);
        packet.InitiatorGUID = ObjectGuid::Create<HighGuid::Player>(1);
        packet.Duration = Milliseconds(35000);
        ExpectConsumed("READY_CHECK_STARTED", packet);
    }
    {   // 0x09B GUID GUID u8
        WorldPackets::Party::ReadyCheckResponse packet;
        packet.PartyGUID = ObjectGuid::Create<HighGuid::Party>(1);
        packet.Player = ObjectGuid::Create<HighGuid::Player>(1);
        packet.IsReady = true;
        ExpectConsumed("READY_CHECK_RESPONSE", packet);
    }
    {   // 0x09C u8 GUID
        WorldPackets::Party::ReadyCheckCompleted packet;
        packet.PartyGUID = ObjectGuid::Create<HighGuid::Party>(1);
        ExpectConsumed("READY_CHECK_COMPLETED", packet);
    }
    {   // 0x021 u8 GUID GUID u8 u8
        WorldPackets::Party::RoleChangedInform packet;
        packet.From = ObjectGuid::Create<HighGuid::Player>(1);
        packet.ChangedUnit = ObjectGuid::Create<HighGuid::Player>(2);
        packet.OldRole = 0; packet.NewRole = 4;
        ExpectConsumed("ROLE_CHANGED_INFORM", packet);
    }
    {   // 0x23E u8
        WorldPackets::Party::GroupUninvite packet;
        packet.Reason = 1;
        ExpectConsumed("GROUP_UNINVITE", packet);
    }
    {   // 0x0D9 u8 u8 GUID GUID
        WorldPackets::Party::SendRaidTargetUpdateSingle packet;
        packet.Target = ObjectGuid::Create<HighGuid::Creature>(1, 2, 3);
        packet.ChangedBy = ObjectGuid::Create<HighGuid::Player>(1);
        packet.Symbol = 8;
        ExpectConsumed("SEND_RAID_TARGET_UPDATE_SINGLE", packet);
    }
    {   // 0x17B GUID u32 u32
        WorldPackets::Party::MinimapPing packet;
        packet.Sender = ObjectGuid::Create<HighGuid::Player>(1);
        packet.PositionX = 1.0f; packet.PositionY = 2.0f;
        ExpectConsumed("MINIMAP_PING", packet);
    }
    {   // 0x1C9 u8 u32
        WorldPackets::Misc::StandStateUpdate packet;
        ExpectConsumed("STAND_STATE_UPDATE", packet);
    }
    {   // 0x02B 12 x u32 u64
        WorldPackets::Movement::NewWorld packet;
        packet.MapID = 1;
        ExpectConsumed("NEW_WORLD", packet);
    }
    {   // 0x273 GUID u32 u32
        WorldPackets::Item::ItemCooldown packet;
        packet.ItemGuid = ObjectGuid::Create<HighGuid::Item>(1);
        packet.SpellID = 100; packet.Cooldown = 1000;
        ExpectConsumed("ITEM_COOLDOWN", packet);
    }
    {   // 0x189 GUID
        WorldPackets::Combat::CancelAutoRepeat packet;
        packet.Guid = ObjectGuid::Create<HighGuid::Player>(1);
        ExpectConsumed("CANCEL_AUTO_REPEAT", packet);
    }
}

// The to-decode list: every main group opcode the core can send whose reader is not resolved in the dump yet. Hidden: it fails by design
// (run `tests "[.ClassicWireGaps]"`), the failure message is the list.
TEST_CASE("70291 main group readers still to decode", "[.ClassicWireGaps]")
{
    opcodeTable.Initialize();
    std::ostringstream undecoded, undecrypted;
    uint32 countUndecoded = 0, countUndecrypted = 0;
    for (uint32 group = 0x45; group <= 0x6A; ++group)
    for (uint32 index = 0; index < 0x400; ++index)
    {
        OpcodeServer const opcode = OpcodeServer((group << 16) | index);
        if (!opcodeTable.IsValid(opcode) || !opcodeTable[opcode] || ClassicOpcodes::IsServerOpcodeBlocked(opcode))
            continue;
        uint32 const wire = ClassicOpcodes::TranslateServerOpcode(Build::Build70291, opcode);
        SpecEntry const* entry = FindSpec(wire);
        if (!entry)
            continue;
        std::string_view const spec = entry->Spec;
        if (spec == "?")
            undecoded << "\n  " << opcodeTable[opcode]->Name << " -> case 0x" << std::hex << entry->Index << std::dec, ++countUndecoded;
        else if (spec == "!")
            undecrypted << "\n  " << opcodeTable[opcode]->Name << " -> case 0x" << std::hex << entry->Index << std::dec, ++countUndecrypted;
    }
    INFO(countUndecoded << " opcodes with an unresolved field:" << undecoded.str());
    INFO(countUndecrypted << " opcodes whose reader page was not decrypted (needs a longer dump):" << undecrypted.str());
    FAIL("to decode");
}
