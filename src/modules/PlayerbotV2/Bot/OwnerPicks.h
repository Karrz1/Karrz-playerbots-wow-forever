// OwnerPicks.h - the owner's spell picks for one bot: "prefer blessing might", "prefer aura fire", "prefer earth stoneskin",
// "prefer aspect monkey", "prefer demon voidwalker", "prefer <kind> auto", "prefer clear", plus "pet off|on" and "stealth off|on".
//
// A pick replaces the group plan for that one choice only: a paladin with "prefer blessing might" gives Might to everyone in reach
// and the other paladins leave Might out of their split (PickBlessing); a pinned aura is taken before the party's aura plan runs
// (PickPaladinAura); a pinned totem stands in for that element's plan (PickTotem); a hunter holds the pinned aspect (Cheetah and
// Pack only out of combat, Hawk in a fight); a warlock summons the pinned demon. "pet off" dismisses the pet and keeps it away,
// "stealth off" keeps a rogue from opening out of Stealth. The picks live on BotAI (memory only, like "aoe" and the crowd-control
// orders: a relog or server restart clears them) and are mirrored into the bot's snapshot and its group member summary.
// Pure text and spell ids: no Player, no SpellMgr. The spells are the rank-1 ids; the bot casts its highest known rank.
#pragma once

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace Playerbot::OwnerPicks {

// The totem slots follow ClassicUpkeep's Element order (EARTH, FIRE, WATER, AIR): TotemEarth + element.
enum Slot : uint8_t { Blessing, Aura, TotemEarth, TotemFire, TotemWater, TotemAir, Aspect, Demon, kSlotCount };

struct Picks
{
    std::array<uint32_t, kSlotCount> spell{};   // rank-1 spell id per slot, 0 = the bot's own plan
    bool pet_off = false;                        // "pet off": dismissed and not summoned again
    bool stealth_off = false;                    // "stealth off": no Stealth opener

    uint32_t operator[](Slot s) const { return spell[s]; }
    bool operator==(Picks const&) const = default;
};

struct Choice
{
    Slot slot;
    uint32_t spell;
    std::string_view label;                    // what the bot answers with
    std::array<std::string_view, 3> words;     // the words that name it after the slot word (first = the add-on's word)
};

inline constexpr Choice kChoices[] = {
    { Blessing, 19740, "Blessing of Might",       { "might" } },
    { Blessing, 19742, "Blessing of Wisdom",      { "wisdom" } },
    { Blessing, 20217, "Blessing of Kings",       { "kings" } },
    { Blessing, 1038,  "Blessing of Salvation",   { "salvation", "salv" } },
    { Blessing, 19977, "Blessing of Light",       { "light" } },
    { Blessing, 20911, "Blessing of Sanctuary",   { "sanctuary" } },

    { Aura, 465,   "Devotion Aura",               { "devotion" } },
    { Aura, 7294,  "Retribution Aura",            { "retribution", "ret" } },
    { Aura, 19746, "Concentration Aura",          { "concentration" } },
    { Aura, 19876, "Shadow Resistance Aura",      { "shadow", "shadow resistance" } },
    { Aura, 19888, "Frost Resistance Aura",       { "frost", "frost resistance" } },
    { Aura, 19891, "Fire Resistance Aura",        { "fire", "fire resistance" } },
    { Aura, 20218, "Sanctity Aura",               { "sanctity" } },

    { TotemEarth, 8075, "Strength of Earth Totem", { "strength", "strength of earth" } },
    { TotemEarth, 8071, "Stoneskin Totem",         { "stoneskin" } },
    { TotemEarth, 8143, "Tremor Totem",            { "tremor" } },
    { TotemEarth, 2484, "Earthbind Totem",         { "earthbind" } },
    { TotemEarth, 5730, "Stoneclaw Totem",         { "stoneclaw" } },

    { TotemFire, 3599, "Searing Totem",            { "searing" } },
    { TotemFire, 1535, "Fire Nova Totem",          { "nova", "fire nova" } },
    { TotemFire, 8190, "Magma Totem",              { "magma" } },
    { TotemFire, 8227, "Flametongue Totem",        { "flametongue" } },
    { TotemFire, 8181, "Frost Resistance Totem",   { "frost", "frost resistance" } },

    { TotemWater, 5675, "Mana Spring Totem",       { "mana", "mana spring" } },
    { TotemWater, 5394, "Healing Stream Totem",    { "healing", "healing stream" } },
    { TotemWater, 8166, "Poison Cleansing Totem",  { "poison", "poison cleansing" } },
    { TotemWater, 8170, "Disease Cleansing Totem", { "disease", "disease cleansing" } },
    { TotemWater, 8184, "Fire Resistance Totem",   { "fire", "fire resistance" } },

    { TotemAir, 8512,  "Windfury Totem",           { "windfury" } },
    { TotemAir, 8835,  "Grace of Air Totem",       { "grace", "grace of air" } },
    { TotemAir, 8177,  "Grounding Totem",          { "grounding" } },
    { TotemAir, 10595, "Nature Resistance Totem",  { "nature", "nature resistance" } },
    { TotemAir, 15107, "Windwall Totem",           { "windwall" } },
    { TotemAir, 25908, "Tranquil Air Totem",       { "tranquil", "tranquil air" } },

    { Aspect, 13165, "Aspect of the Hawk",         { "hawk" } },
    { Aspect, 13163, "Aspect of the Monkey",       { "monkey" } },
    { Aspect, 20043, "Aspect of the Wild",         { "wild" } },
    { Aspect, 5118,  "Aspect of the Cheetah",      { "cheetah" } },
    { Aspect, 13159, "Aspect of the Pack",         { "pack" } },

    { Demon, 688, "Imp",                           { "imp" } },
    { Demon, 697, "Voidwalker",                    { "voidwalker", "void" } },
    { Demon, 712, "Succubus",                      { "succubus", "succ" } },
    { Demon, 691, "Felhunter",                     { "felhunter", "fel" } },
};

// The class a slot belongs to (TrinityCore Classes values): paladin 2, hunter 3, shaman 7, warlock 9.
inline uint8_t SlotClass(Slot s)
{
    switch (s)
    {
        case Blessing: case Aura: return 2;
        case Aspect: return 3;
        case Demon: return 9;
        default: return 7;
    }
}

inline std::string_view SlotName(Slot s)
{
    switch (s)
    {
        case Blessing:   return "blessing";
        case Aura:       return "aura";
        case TotemEarth: return "earth totem";
        case TotemFire:  return "fire totem";
        case TotemWater: return "water totem";
        case TotemAir:   return "air totem";
        case Aspect:     return "aspect";
        case Demon:      return "demon";
        default:         return "?";
    }
}

inline Choice const* FindChoice(uint32_t spell)
{
    for (Choice const& c : kChoices)
        if (c.spell == spell)
            return &c;
    return nullptr;
}

// Do the cheetah / pack aspects daze the group when it is hit? Those are held out of combat only.
inline bool TravelAspect(uint32_t spell) { return spell == 5118 || spell == 13159; }

// One "prefer ..." line: a slot and its spell (0 = back to the bot's own plan), or every slot cleared ("prefer clear").
struct Order
{
    bool clear_all = false;
    Slot slot = Blessing;
    uint32_t spell = 0;
    bool operator==(Order const&) const = default;
};

namespace Detail {
inline std::string Normalize(std::string_view in)
{
    std::string out;
    bool space = false;
    for (char c : in)
    {
        if (std::isspace(static_cast<unsigned char>(c))) { space = !out.empty(); continue; }
        if (space) { out.push_back(' '); space = false; }
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return out;
}
inline bool TakeWord(std::string_view& s, std::string_view word)
{
    if (s.substr(0, word.size()) != word) return false;
    std::string_view const rest = s.substr(word.size());
    if (!rest.empty() && rest.front() != ' ') return false;
    s = rest.empty() ? rest : rest.substr(1);
    return true;
}
}

// "prefer blessing might", "prefer earth stoneskin" (also "prefer totem earth stoneskin", "prefer earth totem stoneskin"),
// "prefer aura fire resistance", "prefer aspect auto", "prefer clear". Case and extra spaces do not matter. Anything else: nullopt.
inline std::optional<Order> ParseOrder(std::string_view line)
{
    std::string const norm = Detail::Normalize(line);
    std::string_view s = norm;
    if (!Detail::TakeWord(s, "prefer"))
        return std::nullopt;
    if (s == "clear" || s == "auto" || s == "reset")
        return Order{ true, Blessing, 0 };

    static constexpr std::pair<std::string_view, Slot> kSlotWords[] = {
        { "blessing", Blessing }, { "bless", Blessing }, { "aura", Aura },
        { "earth", TotemEarth }, { "fire", TotemFire }, { "water", TotemWater }, { "air", TotemAir },
        { "aspect", Aspect }, { "demon", Demon }, { "pet", Demon },
    };
    bool const totem_first = Detail::TakeWord(s, "totem");
    std::optional<Slot> slot;
    for (auto const& [word, sl] : kSlotWords)
        if (Detail::TakeWord(s, word)) { slot = sl; break; }
    if (!slot)
        return std::nullopt;
    bool const totem_slot = *slot >= TotemEarth && *slot <= TotemAir;
    if (totem_first && !totem_slot)
        return std::nullopt;
    if (totem_slot && !totem_first)
        Detail::TakeWord(s, "totem");
    if (s == "auto" || s == "clear" || s == "none" || s == "default")
        return Order{ false, *slot, 0 };
    for (Choice const& c : kChoices)
    {
        if (c.slot != *slot) continue;
        for (std::string_view w : c.words)
            if (!w.empty() && (s == w || (totem_slot && s.size() == w.size() + 6 && s.substr(0, w.size()) == w && s.substr(w.size()) == " totem")))
                return Order{ false, *slot, c.spell };
    }
    return std::nullopt;
}

// "on" / "off" after "pet" or "stealth": true = on, false = off, nullopt = neither.
inline std::optional<bool> ParseSwitch(std::string_view tail)
{
    std::string const t = Detail::Normalize(tail);
    if (t == "on" || t == "1" || t == "yes") return true;
    if (t == "off" || t == "0" || t == "no") return false;
    return std::nullopt;
}

} // namespace Playerbot::OwnerPicks
