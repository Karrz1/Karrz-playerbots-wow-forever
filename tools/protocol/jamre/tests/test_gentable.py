import os

from jamre.gentable import generate, render
from jamre.legacy import legacy_70245, load_core, load_overrides

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "../../../.."))
OPC = f"{ROOT}/src/server/game/Server/Protocol/Opcodes.h"
CPP = f"{ROOT}/src/server/game/Server/Protocol/ClassicOpcodes.cpp"


def test_legacy_matches_documented_cases():
    core, ov = load_core(OPC), load_overrides(CPP)
    # connection/main: plain +1 group; override entries win; chat +2 from index 4; guild -1 from 0x1C
    assert legacy_70245("SMSG_PARTY_UPDATE", core["SMSG_PARTY_UPDATE"], ov) == 0x460097
    assert legacy_70245("SMSG_ACCOUNT_DATA_TIMES", core["SMSG_ACCOUNT_DATA_TIMES"], ov) == 0x4601B5
    assert legacy_70245("SMSG_CHANNEL_NOTIFY_JOINED", core["SMSG_CHANNEL_NOTIFY_JOINED"], ov) == 0x4B001B


def test_generate_with_one_inserted_message():
    core, ov = load_core(OPC), load_overrides(CPP)
    # main group 0x46 -> 0x47, one message inserted at index 0x95: indices >= 0x95 move up by one
    m = {i: (i if i < 0x95 else i + 1) for i in range(0x600)}
    entries, unresolved = generate(core, ov, {0x46: {"new_group": 0x47, "map": m}})
    got = dict((n, w) for n, w, _ in entries)
    assert got["SMSG_PARTY_UPDATE"] == 0x470098
    assert got["SMSG_BATTLE_PET_JOURNAL"] == 0x470092
    assert any("SMSG_CHANNEL_NOTIFY_JOINED" in u for u in unresolved)  # chat group not aligned in this test
    assert "{ SMSG_PARTY_UPDATE, 0x470098 }" in render(entries)
