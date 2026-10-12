#include "tc_catch2.h"
#include "ByteBuffer.h"
#include "ObjectGuid.h"
#include "PartyPackets.h"
#include "WorldPacket.h"

// PARTY_UPDATE as the Classic 1.60.1.70291 client reads it. The reader below follows the client's own reader function
// (WowB.exe 70291, rva 0x805c30, one call per field) and is independent of the packet code under test: if the writer and this
// reader disagree on a single byte, the sizes or the remaining bytes expose it. Field order of the client reader:
//   u16 flags, u8 index, u8 type, i32 myIndex, guid party, i32 sequence, i8 (new), guid leader, u8 faction, i32 ping, u64 (new), i32 count
//   member: bits 6 (name size) + 9 (voice size incl. NUL) + 3 flags -> 3 bytes, guid, u8 subgroup, u8 flags, u8 roles, u8 rolesUnk,
//           i8 class, u8 faction, leaver (guid, f32, u32 u32 u32 i32, u64 u64, i32, 1 bit), name bytes, voice bytes + NUL if size > 1
//   tail: 4 optional bits (challenge mode, lfg, loot, difficulty) in one byte, then the present blocks in that order
namespace
{
using namespace WorldPackets::Party;

struct ReadMember
{
    std::string Name;
    std::string Voice;
    ObjectGuid Guid;
    uint8 Subgroup = 0, Flags = 0, Roles = 0, RolesUnk = 0, Class = 0, Faction = 0;
    bool Connected = false, SocialQueue = false, VoiceSilenced = false;
};

struct ReadParty
{
    uint16 Flags = 0;
    uint8 Index = 0, Type = 0, LeaderFaction = 0;
    int32 MyIndex = 0, Sequence = 0, Ping = 0;
    int8 NewByte = 0;
    uint64 NewU64 = 0;
    ObjectGuid Party, Leader;
    std::vector<ReadMember> Members;
    uint8 LootMethod = 0, LootThreshold = 0;
    ObjectGuid LootMaster;
    int16 Difficulty[3] = { };
    uint8 OptionalBits = 0;
};

ReadParty ReadClient70291(WorldPacket const& packet)
{
    ByteBuffer in(packet);
    ReadParty r;
    r.Flags = in.read<uint16>();
    r.Index = in.read<uint8>();
    r.Type = in.read<uint8>();
    r.MyIndex = in.read<int32>();
    in >> r.Party;
    r.Sequence = in.read<int32>();
    r.NewByte = in.read<int8>();
    in >> r.Leader;
    r.LeaderFaction = in.read<uint8>();
    r.Ping = in.read<int32>();
    r.NewU64 = in.read<uint64>();
    uint32 count = in.read<uint32>();

    for (uint32 i = 0; i < count; ++i)
    {
        ReadMember& m = r.Members.emplace_back();
        uint32 nameSize = in.ReadBits(6);
        uint32 voiceSize = in.ReadBits(9);
        m.Connected = in.ReadBit();
        m.SocialQueue = in.ReadBit();
        m.VoiceSilenced = in.ReadBit();
        in.ResetBitPos();
        in >> m.Guid;
        m.Subgroup = in.read<uint8>();
        m.Flags = in.read<uint8>();
        m.Roles = in.read<uint8>();
        m.RolesUnk = in.read<uint8>();
        m.Class = in.read<uint8>();
        m.Faction = in.read<uint8>();
        // leaver
        ObjectGuid bnet;
        in >> bnet;
        (void)in.read<float>();
        for (int n = 0; n < 4; ++n)
            (void)in.read<uint32>();
        (void)in.read<uint64>();
        (void)in.read<uint64>();
        (void)in.read<int32>();
        (void)in.ReadBit();
        in.ResetBitPos();
        m.Name = in.ReadString(nameSize);
        if (voiceSize > 1)
        {
            m.Voice = in.ReadString(voiceSize - 1);
            REQUIRE(in.read<uint8>() == 0);     // the client asserts a NUL as the last byte
        }
    }

    bool challenge = in.ReadBit();
    bool lfg = in.ReadBit();
    bool loot = in.ReadBit();
    bool difficulty = in.ReadBit();
    in.ResetBitPos();
    r.OptionalBits = (challenge ? 8 : 0) | (lfg ? 4 : 0) | (loot ? 2 : 0) | (difficulty ? 1 : 0);
    REQUIRE_FALSE(challenge);
    REQUIRE_FALSE(lfg);
    if (loot)
    {
        r.LootMethod = in.read<uint8>();
        in >> r.LootMaster;
        r.LootThreshold = in.read<uint8>();
    }
    if (difficulty)
        for (int16& value : r.Difficulty)
            value = in.read<int16>();

    CHECK(in.rpos() == in.size());      // every byte consumed, none missing
    return r;
}

// Fills a caller-owned packet: ServerPacket is neither copyable nor returnable by value.
void MakeParty(PartyUpdate& update, bool classic70291)
{
    update.Classic70291 = classic70291;
    update.PartyFlags = 0x0001;
    update.PartyIndex = 0;
    update.PartyType = 1;
    update.MyIndex = 1;
    update.SequenceNum = 7;
    update.PartyGUID = ObjectGuid::Create<HighGuid::Player>(500);
    update.LeaderGUID = ObjectGuid::Create<HighGuid::Player>(1);
    update.LeaderFactionGroup = 1;
    update.Classic70291_BeforeLeader = 0;
    update.Classic70291_BeforeMemberCount = 0;

    PartyPlayerInfo& a = update.PlayerList.emplace_back();
    a.GUID = ObjectGuid::Create<HighGuid::Player>(1);
    a.Name = "Karrz";
    a.Class = 9;
    a.Subgroup = 0;
    a.Flags = 1;
    a.RolesAssigned = 4;
    a.FactionGroup = 1;
    a.Connected = true;

    PartyPlayerInfo& b = update.PlayerList.emplace_back();
    b.GUID = ObjectGuid::Create<HighGuid::Player>(2);
    b.Name = "Fatih";
    b.VoiceStateID = "voice-state-0123456789";      // longer than the 6 bit range of retail: only a 9 bit size field carries this
    b.Class = 1;
    b.Subgroup = 1;
    b.RolesAssigned = 2;
    b.FactionGroup = 1;
    b.Connected = true;
    b.FromSocialQueue = false;
    b.VoiceChatSilenced = true;

    update.LootSettings.emplace();
    update.LootSettings->Method = 3;
    update.LootSettings->LootMaster = ObjectGuid::Create<HighGuid::Player>(1);
    update.LootSettings->Threshold = 2;
    update.DifficultySettings.emplace();
    update.DifficultySettings->DungeonDifficultyID = 1;
    update.DifficultySettings->RaidDifficultyID = 9;
    update.DifficultySettings->LegacyRaidDifficultyID = 3;
}
}

TEST_CASE("70291 PARTY_UPDATE is read field for field by the client reader", "[ClassicPartyUpdate]")
{
    PartyUpdate update;
    MakeParty(update, true);
    ReadParty r = ReadClient70291(*update.Write());

    CHECK(r.Flags == 0x0001);
    CHECK(r.Type == 1);
    CHECK(r.MyIndex == 1);
    CHECK(r.Sequence == 7);
    CHECK(r.Party == update.PartyGUID);
    CHECK(r.Leader == update.LeaderGUID);
    CHECK(r.LeaderFaction == 1);
    CHECK(r.NewByte == 0);
    CHECK(r.NewU64 == 0);

    REQUIRE(r.Members.size() == 2);
    CHECK(r.Members[0].Name == "Karrz");
    CHECK(r.Members[0].Voice.empty());
    CHECK(r.Members[0].Guid == update.PlayerList[0].GUID);
    CHECK(r.Members[0].Class == 9);
    CHECK(r.Members[0].Roles == 4);
    CHECK(r.Members[0].Connected);
    CHECK(r.Members[1].Name == "Fatih");
    CHECK(r.Members[1].Voice == "voice-state-0123456789");
    CHECK(r.Members[1].Guid == update.PlayerList[1].GUID);
    CHECK(r.Members[1].Subgroup == 1);
    CHECK(r.Members[1].VoiceSilenced);

    CHECK(r.OptionalBits == 0b0011);
    CHECK(r.LootMethod == 3);
    CHECK(r.LootThreshold == 2);
    CHECK(r.LootMaster == update.LootSettings->LootMaster);
    CHECK(r.Difficulty[0] == 1);
    CHECK(r.Difficulty[1] == 9);
    CHECK(r.Difficulty[2] == 3);
}

// The retail / 70245 layout is two header fields and 3 bits per member shorter; the 70291 one must not leak into it.
TEST_CASE("70245 PARTY_UPDATE keeps the retail layout", "[ClassicPartyUpdate]")
{
    PartyUpdate retail;
    MakeParty(retail, false);
    PartyUpdate classic;
    MakeParty(classic, true);
    retail.PlayerList[1].VoiceStateID.clear();
    classic.PlayerList[1].VoiceStateID.clear();

    size_t const retailSize = retail.Write()->size();
    size_t const classicSize = classic.Write()->size();
    // 1 byte + 8 bytes in the header, one extra header byte per member (2 members)
    CHECK(classicSize == retailSize + 1 + 8 + 2);
}
