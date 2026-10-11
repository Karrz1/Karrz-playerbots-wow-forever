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

#ifndef TRINITYCORE_CLASSIC_OPCODES_H
#define TRINITYCORE_CLASSIC_OPCODES_H

#include "Define.h"
#include <vector>

/// Opcode translation between the WoW Classic 1.60.1 client (build 70009) and the retail opcode numbers used by the core.
/// Entries are added as they are discovered; opcodes without an entry are passed through unchanged.
namespace ClassicOpcodes
{
    /// numbering families of the client builds (the wire numbers differ between them, see ClassicOpcodes.cpp)
    enum class Build : uint32
    {
        Build70245 = 70245,     // 70205 .. 70245
        Build70291 = 70291      // 70291 and later
    };

    /// the numbering family of the client builds this process serves
    TC_GAME_API Build CurrentBuild();

    /// core OpcodeServer value -> client number in the numbering of the given build.
    /// 70291: the explicit table ClassicServerOpcodes70291.inc (generated from the client) wins, the arithmetic translation
    /// below is only the fallback for entries the table does not cover yet.
    TC_GAME_API uint32 TranslateServerOpcode(Build build, uint32 coreOpcode);

    /// true if the explicit table of that build has an entry for the core opcode
    TC_GAME_API bool HasExplicitServerOpcode(Build build, uint32 coreOpcode);

    /// client number -> core OpcodeClient value
    TC_GAME_API uint32 TranslateClientOpcode(uint32 classicOpcode);

    /// core OpcodeServer value -> client number
    TC_GAME_API uint32 TranslateServerOpcode(uint32 coreOpcode);

    /// the same in the numbering of client 70245 (the tables); the two above add the newer builds' group shift
    TC_GAME_API uint32 TranslateClientOpcode70245(uint32 classicOpcode);
    TC_GAME_API uint32 TranslateServerOpcode70245(uint32 coreOpcode);

    /// client 70291 or later: also selects that build's changed packet and update field layouts
    TC_GAME_API bool IsBuild70291OrLater();

    /// server opcodes whose Classic number is not known yet and that crash the client when sent with the shifted retail number
    TC_GAME_API bool IsServerOpcodeBlocked(uint32 coreOpcode);

    /// server opcodes whose Classic layout differs and for which a fixed placeholder payload is sent instead (nullptr if none)
    TC_GAME_API std::vector<uint8> const* GetServerStubPayload(uint32 coreOpcode);
}

#endif // TRINITYCORE_CLASSIC_OPCODES_H
