#pragma once
// How much a piece of armor is worth to a wearer that could put on a heavier type: one tier under the best it can wear at its
// level (mail on a level 40 warrior, leather on a level 20 one, cloth on a rogue) loses the armor and the class bonuses, about a
// fifth of what it is worth, and again for each further tier (a tank two fifths: the armor is what it gears for). A piece of the
// wearer's own type, of no type (miscellaneous: rings, trinkets, held items) or an armor slot the type does not apply to (cloaks)
// keeps its full worth. Used by every place that ranks gear against the worn piece: the raid scorer (ScoreItemRaid), the levelling
// equip from the bags, the upgrade count and the need roll. Pure: tested in tests/game/ClassicArmorTypeFit.cpp.

#include <cstdint>

namespace Playerbot::Gear {

// ItemSubclassArmor: cloth 1, leather 2, mail 3, plate 4.
constexpr uint32_t kArmorCloth = 1;
constexpr uint32_t kArmorPlate = 4;

inline float ArmorTierFactor(uint32_t subclass, uint32_t wearable, bool tank)
{
    if (subclass < kArmorCloth || subclass > kArmorPlate || subclass >= wearable)
        return 1.0f;
    float const step = tank ? 0.6f : 0.8f;
    float factor = 1.0f;
    for (uint32_t t = subclass; t < wearable; ++t)
        factor *= step;
    return factor;
}

} // namespace Playerbot::Gear
