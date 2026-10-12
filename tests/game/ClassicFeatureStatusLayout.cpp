#include "tc_catch2.h"
#include "ByteBuffer.h"
#include "ObjectGuid.h"
#include "SystemPackets.h"
#include "WorldPacket.h"
#include <array>

// SMSG_FEATURE_SYSTEM_STATUS as the Classic 1.60.1.70291 client reads it (WowB.exe 70291, dispatch case 0x63, reader rva 0x7ff4a0-0x7fffd7). The
// reader below follows the client's reads one for one and is independent of FeatureSystemStatus::Write. Client order:
//   u8 complaint, 8 x u32 (realm, rec, 5 x raf, commerce poll), quick join (22 x u32 + 1 flag byte), u64, u32 u32, squelch (flag byte + 2 guids),
//   u32 social restriction, u32 content set, u32 mode count, u32 rule count, u32 u32 timerunning, u16 u16, 12 x u32/float, modes (u8 u32 u32),
//   rules (u32 u32 u32), then the flag bytes:
//     bytes 1-4   32 flags (bit 1 = Europa block present, bit 4 = session alert present)
//     byte 5      7 flags, then the first bit of a 10 bit string length
//     bytes 6, 7  the rest of that length, then 7 flags
//     byte 8      1 flag (the remaining 7 bits unused)
//   then Europa (flag byte with 4 flags + 2 throttle structs of 4 x u32) if present, session alert (3 x u32) if present, then `length` string bytes.
// The 10 bit string length sits two bits earlier than in the retail layout of this struct, where two guild flags stand: the Classic client has
// no such fields. A non-zero length makes the client read that many bytes past the packet, so the writer always sends 0.
namespace
{
using namespace WorldPackets::System;

struct BitReader
{
    explicit BitReader(ByteBuffer& buffer) : _buffer(buffer) { }

    bool Bit()
    {
        if (_left == 0)
        {
            _byte = _buffer.read<uint8>();
            _left = 8;
        }
        --_left;
        return (_byte >> _left) & 1;
    }

    uint32 Bits(int count)
    {
        uint32 value = 0;
        while (count-- > 0)
            value = (value << 1) | uint32(Bit());
        return value;
    }

    void Reset() { _left = 0; }

    ByteBuffer& _buffer;
    uint8 _byte = 0;
    int _left = 0;
};

struct ClientFeatures
{
    std::array<bool, 32> Flags = { };
    bool EuropaPresent = false, SessionAlertPresent = false;
    std::array<bool, 7> GroupB = { };           // flags of byte 5 (client struct 0x18c..0x192)
    uint32 StringLength = 0;                    // 10 bit field, client struct 0x198
    std::array<bool, 7> GroupC = { };           // client struct 0x5b4..0x5b8, 0x5c8, 0x5c9
    bool FlagD = false;                         // client struct 0x5ca
    bool Europa[4] = { };
    uint32 Throttle[8] = { };
    uint32 Alert[3] = { };
    uint32 TrailingBits = 0;
};

ClientFeatures ReadClient70291(WorldPacket const& packet, uint32 expectedModes, uint32 expectedRules)
{
    ByteBuffer in(packet);
    ClientFeatures r;

    (void)in.read<uint8>();
    for (int i = 0; i < 8; ++i)
        (void)in.read<uint32>();
    for (int i = 0; i < 22; ++i)
        (void)in.read<uint32>();
    (void)in.read<uint8>();                     // quick join flag byte
    (void)in.read<uint64>();
    (void)in.read<uint32>();
    (void)in.read<uint32>();
    (void)in.read<uint8>();                     // squelch flag byte
    ObjectGuid bnet, guild;
    in >> bnet >> guild;
    (void)in.read<uint32>();                    // social restriction
    (void)in.read<uint32>();                    // content set
    uint32 const modes = in.read<uint32>();
    uint32 const rules = in.read<uint32>();
    CHECK(modes == expectedModes);
    CHECK(rules == expectedRules);
    (void)in.read<uint32>();
    (void)in.read<uint32>();
    (void)in.read<uint16>();
    (void)in.read<uint16>();
    for (int i = 0; i < 12; ++i)
        (void)in.read<uint32>();
    for (uint32 i = 0; i < modes; ++i)
    {
        (void)in.read<uint8>();
        (void)in.read<uint32>();
        (void)in.read<uint32>();
    }
    for (uint32 i = 0; i < rules; ++i)
        for (int n = 0; n < 3; ++n)
            (void)in.read<uint32>();

    BitReader bits(in);
    for (size_t i = 0; i < r.Flags.size(); ++i)
        r.Flags[i] = bits.Bit();
    r.EuropaPresent = r.Flags[1];
    r.SessionAlertPresent = r.Flags[4];
    for (bool& flag : r.GroupB)
        flag = bits.Bit();
    r.StringLength = bits.Bits(10);
    for (bool& flag : r.GroupC)
        flag = bits.Bit();
    r.FlagD = bits.Bit();
    // the writer pads the last flag byte with its remaining flags; the client leaves them unread
    for (int i = 0; i < 7 && bits._left > 0; ++i)
        r.TrailingBits = (r.TrailingBits << 1) | uint32(bits.Bit());
    bits.Reset();

    if (r.EuropaPresent)
    {
        BitReader europa(in);
        for (bool& flag : r.Europa)
            flag = europa.Bit();
        for (uint32& value : r.Throttle)
            value = in.read<uint32>();
    }
    if (r.SessionAlertPresent)
        for (uint32& value : r.Alert)
            value = in.read<uint32>();
    for (uint32 i = 0; i < r.StringLength; ++i)
        (void)in.read<uint8>();

    CHECK(in.rpos() == in.size());              // every byte consumed, none missing
    return r;
}

void Make(FeatureSystemStatus& features)
{
    features.VoiceEnabled = true;
    features.BpayStoreAvailable = false;
    features.WarModeEnabled = false;
    features.ClubFinderEnabled = true;
    features.TimerunningEnabled = true;
    features.LfdEnabled = true;
    features.LfrEnabled = false;
    features.PetHappinessEnabled = false;
    features.ClassicFlagsAfterLength = 2;       // the official value
    features.LobbyMatchmakerQueueFromMainlineEnabled = true;
    features.EuropaTicketSystemStatus.emplace();
    features.EuropaTicketSystemStatus->BugsEnabled = true;
    features.EuropaTicketSystemStatus->ComplaintsEnabled = true;
    features.EuropaTicketSystemStatus->SuggestionsEnabled = true;
    features.EuropaTicketSystemStatus->ThrottleState = { .MaxTries = 10, .PerMilliseconds = 60000, .TryCount = 0, .LastResetTimeBeforeNow = 130929 };
    features.EuropaTicketSystemStatus->ExpensiveThrottleState = { .MaxTries = 1, .PerMilliseconds = 60000, .TryCount = 0, .LastResetTimeBeforeNow = 130929 };
    features.SessionAlert.emplace();
    features.SessionAlert->Delay = 11;
    features.SessionAlert->Period = 22;
    features.SessionAlert->DisplayTime = 33;
    features.GameRules.push_back({ 93, 2, 0.0f });
    features.GameRules.push_back({ 242, 1, 0.0f });
    features.DisabledGameModes.push_back({ 1, 2, 3 });
}
}

TEST_CASE("70291 FEATURE_SYSTEM_STATUS is read field for field by the client reader", "[ClassicFeatureStatus]")
{
    FeatureSystemStatus features;
    Make(features);
    ClientFeatures r = ReadClient70291(*features.Write(), 1, 2);

    CHECK(r.Flags[0]);                          // VoiceEnabled
    CHECK(r.EuropaPresent);
    CHECK(r.SessionAlertPresent);
    CHECK(r.Europa[0] == false);
    CHECK(r.Europa[1]);
    CHECK(r.Europa[2]);
    CHECK(r.Europa[3]);
    CHECK(r.Throttle[0] == 10);
    CHECK(r.Throttle[1] == 60000);
    CHECK(r.Throttle[3] == 130929);
    CHECK(r.Throttle[4] == 1);
    CHECK(r.Throttle[7] == 130929);
    CHECK(r.Alert[0] == 11);
    CHECK(r.Alert[1] == 22);
    CHECK(r.Alert[2] == 33);
}

// The official stream has a length of 0 and the flags 0x5b4 set, 0x5b5 clear (what the retail-layout decode called ClassicFlagBits10 = 2).
TEST_CASE("70291 string length is zero and the flags after it keep their positions", "[ClassicFeatureStatus]")
{
    FeatureSystemStatus features;
    Make(features);
    ClientFeatures r = ReadClient70291(*features.Write(), 1, 2);

    CHECK(r.StringLength == 0);
    CHECK(r.GroupC[0]);                         // client 0x5b4 = bit 1 of ClassicFlagsAfterLength
    CHECK_FALSE(r.GroupC[1]);                   // client 0x5b5 = bit 0
    CHECK(r.GroupC[2] == features.IsAccountCurrencyTransferEnabled);
    CHECK(r.GroupC[3] == features.NetEaseChatTelemetryEnabled);
    CHECK(r.GroupC[4] == features.LobbyMatchmakerQueueFromMainlineEnabled);
    CHECK(r.GroupC[5] == features.CanSendLobbyMatchmakerPartyCustomizations);
    CHECK(r.GroupC[6] == features.AddonProfilingEnabled);
    CHECK(r.FlagD == features.GlobalUserGeneratedContentMuteEnabled);
}

// (packets are not copyable: Make fills a packet in place)
// The struct defaults (what a server sends that does not set the official values) used to put two guild flags on the length field (length 768): the client
// read 768 bytes that were not there.
TEST_CASE("70291 default values keep the string length at zero", "[ClassicFeatureStatus]")
{
    FeatureSystemStatus features;
    ClientFeatures r = ReadClient70291(*features.Write(), 0, 0);
    CHECK(r.StringLength == 0);
}
