// MigrationLint - a check of a sql/playerbot_v2 migration's text before any of it runs (pure, tested in
// tests/game/ClassicBotMigrations.cpp over every shipped migration).
//
// PlayerbotMigrationMgr sends a statement to the character database unless a `-- @target: shared` line came before it, and only
// substitutes {SHARED} in statements for the shared schema. A {SHARED} table name in a character-database statement reached
// MySQL as it was written and the server stopped at startup on an assert (core-0041, 2026-11-17). The migration manager now
// refuses such a file with an error naming the line, and the unit test catches it before a build.

#pragma once

#include <cctype>
#include <cstddef>
#include <sstream>
#include <string>

namespace Playerbot::MigrationLint {

// The 1-based number of the first line that names {SHARED} while the target in force is the character database, or 0 when there
// is none. A `-- @target: shared|characters` line switches the target, as the migration manager reads it.
inline size_t SharedTokenOutsideShared(std::string const& sql)
{
    bool shared = false;
    std::istringstream in(sql);
    std::string line;
    size_t n = 0;
    while (std::getline(in, line))
    {
        ++n;
        size_t i = 0;
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t'))
            ++i;
        if (i + 1 < line.size() && line[i] == '-' && line[i + 1] == '-')
        {
            size_t const at = line.find("@target:", i);
            if (at == std::string::npos)
                continue;
            std::string v;
            for (size_t k = at + 8; k < line.size(); ++k)
                if (!std::isspace(static_cast<unsigned char>(line[k])))
                    v.push_back(char(std::tolower(static_cast<unsigned char>(line[k]))));
            if (v == "shared")
                shared = true;
            else if (v == "characters")
                shared = false;
            continue;
        }
        if (!shared && line.find("{SHARED}") != std::string::npos)
            return n;
    }
    return 0;
}

} // namespace Playerbot::MigrationLint
