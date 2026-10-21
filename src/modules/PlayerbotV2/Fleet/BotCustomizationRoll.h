// BotCustomizationRoll.h: picks one choice per customization option for a new bot so that core's
// WorldSession::ValidateAppearance accepts it. Header only, no game types.
#pragma once

#include "Define.h"

#include <algorithm>
#include <random>
#include <set>
#include <utility>
#include <vector>

namespace Playerbot::V2
{

struct CustomizationRollChoice
{
    uint32 id = 0;
    // One entry per option the choice depends on: the choices of which at least one must be selected.
    std::vector<std::vector<uint32>> needsOneOf;
};

struct CustomizationRollOption
{
    uint32 id = 0;
    std::vector<CustomizationRollChoice> choices;   // already filtered to what a bot may use
};

// A choice is offered only once its dependencies are already selected, so options are rolled in
// passes until a pass adds nothing. An option with no offerable choice is left out (core accepts that).
// Result is (option, choice) pairs sorted by option id.
template <typename Rng>
std::vector<std::pair<uint32, uint32>> RollCustomizationChoices(std::vector<CustomizationRollOption> const& options, Rng& rng)
{
    std::vector<std::pair<uint32, uint32>> out;
    std::set<uint32> done;
    auto selected = [&](uint32 choiceId) { return std::ranges::any_of(out, [&](auto const& p) { return p.second == choiceId; }); };

    for (bool progress = true; progress;)
    {
        progress = false;
        for (CustomizationRollOption const& opt : options)
        {
            if (done.count(opt.id))
                continue;
            std::vector<uint32> candidates;
            for (CustomizationRollChoice const& c : opt.choices)
            {
                bool met = true;
                for (std::vector<uint32> const& anyOf : c.needsOneOf)
                    if (!std::ranges::any_of(anyOf, selected))
                    {
                        met = false;
                        break;
                    }
                if (met)
                    candidates.push_back(c.id);
            }
            if (candidates.empty())
                continue;   // may become offerable after another option is rolled
            std::uniform_int_distribution<size_t> pick(0, candidates.size() - 1);
            out.emplace_back(opt.id, candidates[pick(rng)]);
            done.insert(opt.id);
            progress = true;
        }
    }
    std::ranges::sort(out);
    return out;
}

} // namespace Playerbot::V2
