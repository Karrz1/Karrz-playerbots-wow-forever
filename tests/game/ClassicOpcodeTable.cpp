#include "tc_catch2.h"
#include "ClassicOpcodes.h"
#include "Opcodes.h"
#include <map>
#include <string>
#include <vector>

namespace
{
using ClassicOpcodes::Build;

// Every server opcode the core can send: registered in the opcode table (Initialize is idempotent for our purposes)
std::vector<uint32> RegisteredServerOpcodes()
{
    opcodeTable.Initialize();
    std::vector<uint32> opcodes;
    for (uint32 group = 0x45; group <= 0x6A; ++group)
    {
        for (uint32 index = 0; index < 0x400; ++index)
        {
            OpcodeServer const opcode = OpcodeServer((group << 16) | index);
            if (opcodeTable.IsValid(opcode) && opcodeTable[opcode])
                opcodes.push_back(opcode);
        }
    }
    return opcodes;
}
}

// Anchors taken from the official 70291 sniff (see the comment above ExtraGroupShift in ClassicOpcodes.cpp). They hold for
// the explicit table and for the arithmetic fallback alike; a generated table that breaks one of them is wrong.
TEST_CASE("70291 wire opcodes match the sniffed anchors", "[ClassicOpcodeTable]")
{
    auto wire = [](OpcodeServer opcode) { return ClassicOpcodes::TranslateServerOpcode(Build::Build70291, opcode); };

    CHECK(wire(SMSG_BATTLE_PET_JOURNAL) == 0x470092);           // unchanged index
    CHECK(wire(SMSG_START_ELAPSED_TIMERS) == 0x4700AE);         // one message inserted before it

    // Read from the 70291 client binary (main dispatch 0x8e5a90: 911 cases, id = 0x470000 + case, each class reports its own id): the
    // inserted message is case 0x98 (a single u8). PARTY_UPDATE is case 0x97 (reader 0x805c30), the messages after it moved up by one.
    CHECK(wire(SMSG_BATTLE_PET_DELETED) == 0x470093);           // before the insert: unchanged
    CHECK(wire(SMSG_BATTLE_PETS_HEALED) == 0x470096);
    CHECK(wire(SMSG_PARTY_UPDATE) == 0x470097);
    CHECK(wire(SMSG_UPDATE_RECENT_PLAYER_GUIDS) == 0x470099);   // after the insert: +1
    CHECK(wire(SMSG_INSTANCE_ABANDON_VOTE_PLAYER_LEFT) == 0x4700A0);    // guid reader
    CHECK(wire(SMSG_PET_BATTLE_PVP_CHALLENGE) == 0x4700A2);     // shares reader 0x98c380 with FINALIZE_LOCATION
    CHECK(wire(SMSG_PET_BATTLE_FINALIZE_LOCATION) == 0x4700A3);
    CHECK(wire(SMSG_SET_TIME_ZONE_INFORMATION) == 0x470124);
    CHECK(wire(SMSG_ACCOUNT_DATA_TIMES) == 0x4701B6);
    CHECK(wire(SMSG_SERVER_TIME_OFFSET) == 0x4701C0);
    CHECK(wire(SMSG_TUTORIAL_FLAGS) == 0x470269);
    CHECK(wire(SMSG_BATTLENET_RESPONSE) == 0x4702B0);
    CHECK(wire(SMSG_MIRROR_VARS) == 0x470372);
    CHECK(wire(SMSG_AUTH_CHALLENGE) == 0x4E0000);               // connection groups moved up by one
}

TEST_CASE("70245 numbering is unchanged by the build-aware translation", "[ClassicOpcodeTable]")
{
    CHECK(ClassicOpcodes::TranslateServerOpcode(Build::Build70245, SMSG_PARTY_UPDATE) == 0x460097);
    CHECK(ClassicOpcodes::TranslateServerOpcode(Build::Build70245, SMSG_ACCOUNT_DATA_TIMES) == 0x4601B5);
    CHECK(ClassicOpcodes::TranslateServerOpcode(Build::Build70245, SMSG_AUTH_CHALLENGE) == 0x4D0000);
    CHECK(ClassicOpcodes::TranslateServerOpcode(Build::Build70245, SMSG_CHANNEL_NOTIFY_JOINED) == 0x4B001B);
}

// Two core opcodes on one wire number make the client read one message as the other (this is how a wrong table crashes it).
// The two pairs below are known: the 70245 override table moved MIRROR_VARS and LAST_CATALOG_FETCH_RESPONSE onto the
// numbers of UNSET_INSTANCE_LEAVER and HOUSING_PHOTO_SHARING_AUTHORIZATION_CLEARED_RESULT. The server never builds either
// of those two packets, so nothing is sent wrongly today; the generated table has to resolve them.
TEST_CASE("No two server opcodes share a wire number", "[ClassicOpcodeTable]")
{
    std::vector<uint32> const opcodes = RegisteredServerOpcodes();
    REQUIRE(opcodes.size() > 1000);

    std::map<std::string, std::vector<std::string>> known =
    {
        { "SMSG_UNSET_INSTANCE_LEAVER|SMSG_MIRROR_VARS", {} },
        { "SMSG_HOUSING_PHOTO_SHARING_AUTHORIZATION_CLEARED_RESULT|SMSG_LAST_CATALOG_FETCH_RESPONSE", {} },
    };

    for (Build build : { Build::Build70245, Build::Build70291 })
    {
        CAPTURE(uint32(build));
        std::map<uint32, std::vector<uint32>> byWire;
        for (uint32 opcode : opcodes)
        {
            if (ClassicOpcodes::IsServerOpcodeBlocked(opcode))
                continue;
            byWire[ClassicOpcodes::TranslateServerOpcode(build, opcode)].push_back(opcode);
        }

        uint32 collisions = 0;
        for (auto const& [wireOpcode, cores] : byWire)
        {
            if (cores.size() < 2)
                continue;
            ++collisions;
            bool isKnown = cores.size() == 2 &&
                ((cores[0] == SMSG_UNSET_INSTANCE_LEAVER && cores[1] == SMSG_MIRROR_VARS) || (cores[1] == SMSG_UNSET_INSTANCE_LEAVER && cores[0] == SMSG_MIRROR_VARS) ||
                 (cores[0] == SMSG_HOUSING_PHOTO_SHARING_AUTHORIZATION_CLEARED_RESULT && cores[1] == SMSG_LAST_CATALOG_FETCH_RESPONSE) ||
                 (cores[1] == SMSG_HOUSING_PHOTO_SHARING_AUTHORIZATION_CLEARED_RESULT && cores[0] == SMSG_LAST_CATALOG_FETCH_RESPONSE));
            INFO("wire " << std::hex << wireOpcode << " is used by " << cores.size() << " core opcodes, first " << cores[0] << " second " << cores[1]);
            CHECK(isKnown);
        }
        CHECK(collisions <= known.size());
    }
}
