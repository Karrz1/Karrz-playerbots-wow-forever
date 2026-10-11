#ifndef ComboTargetRules_h__
#define ComboTargetRules_h__

#include "ObjectGuid.h"

// Vanilla combo point ownership. The points belong to one unit (the combo target the Classic client reads from the owner-only
// UnitData guid); GetComboPoints("player", "target") only shows them while the current target is that unit.
namespace ComboTargetRules
{
    // Gaining a point on `victim` while the points belong to another unit throws the old points away first.
    inline bool GainResetsPoints(ObjectGuid const& comboTarget, ObjectGuid const& victim)
    {
        return !comboTarget.IsEmpty() && !victim.IsEmpty() && comboTarget != victim;
    }

    // The points go when the combo target is gone or dead; with no target set there is nothing to drop.
    inline bool ShouldDrop(ObjectGuid const& comboTarget, bool targetFoundAndAlive)
    {
        return !comboTarget.IsEmpty() && !targetFoundAndAlive;
    }
}

#endif
