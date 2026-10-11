// What the owner reads when the raid lacks something the boss asks for (RaidBrain LogBossReadiness). Pure so the wording is tested.
// Class ids are the game's (SharedDefines.h Classes).

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Playerbot::Raid {

struct NeedGap
{
    std::string what;       // "tranquilizing shot (Frenzy)"
    uint32_t have = 0;
    uint32_t want = 0;
    uint32_t classMask = 0; // 1 << class id of the classes that can do it; 0 = not a class need (healers, tanks)
    std::string hint;       // overrides the class list: "Tremor Totem shaman or Fear Ward priest"
};

inline std::string ClassNamesOf(uint32_t mask)
{
    static char const* const names[] = { "", "Warrior", "Paladin", "Hunter", "Rogue", "Priest", "Death Knight", "Shaman", "Mage",
                                         "Warlock", "Monk", "Druid" };
    std::string out;
    for (uint32_t c = 1; c < sizeof(names) / sizeof(names[0]); ++c)
        if ((mask & (1u << c)) && names[c][0] && c != 6)
            out += (out.empty() ? "" : "/") + std::string(names[c]);
    return out;
}

// Healers below this average mana get a mention before the pull.
constexpr int kHealerManaWarnPct = 80;

// One line for the group leader's chat window; empty when nothing is missing. healerManaPct < 0 = not checked (the engage line).
// e.g. "Raid check for Magmadar: missing tranquilizing shot (Frenzy) 0/1 (Hunter). Healers at 58% average mana."
inline std::string ReadyWarning(std::string const& boss, std::vector<NeedGap> const& gaps, int healerManaPct = -1)
{
    std::string out;
    for (NeedGap const& g : gaps)
    {
        if (g.have >= g.want)
            continue;
        out += (out.empty() ? "" : "; ") + g.what + " " + std::to_string(g.have) + "/" + std::to_string(g.want);
        std::string const who = g.hint.empty() ? ClassNamesOf(g.classMask) : g.hint;
        if (!who.empty())
            out += " (" + who + ")";
    }
    bool const lowMana = healerManaPct >= 0 && healerManaPct < kHealerManaWarnPct;
    if (out.empty() && !lowMana)
        return {};
    std::string line = "Raid check for " + boss + ":";
    if (!out.empty())
        line += " missing " + out + ".";
    if (lowMana)
        line += std::string(out.empty() ? " Healers" : " Healers") + " at " + std::to_string(healerManaPct) + "% average mana.";
    return line;
}

// Once per boss per minute (<last> is the ms of the previous warning, 0 = never).
constexpr uint32_t kReadyWarnIntervalMs = 60000;
inline bool ReadyWarnDue(uint32_t last, uint32_t now, uint32_t intervalMs = kReadyWarnIntervalMs)
{
    return last == 0 || now - last >= intervalMs;
}

} // namespace Playerbot::Raid
