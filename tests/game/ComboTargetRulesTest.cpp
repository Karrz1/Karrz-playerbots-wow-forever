#include "tc_catch2.h"
#include "ComboTargetRules.h"

TEST_CASE("Combo points stay on their target and reset on a new one", "[ComboTarget]")
{
    ObjectGuid a = ObjectGuid::Create<HighGuid::Creature>(598, 1, 10);
    ObjectGuid b = ObjectGuid::Create<HighGuid::Creature>(598, 1, 11);

    SECTION("first point on a mob keeps nothing to reset")
    {
        CHECK_FALSE(ComboTargetRules::GainResetsPoints(ObjectGuid::Empty, a));
    }
    SECTION("more points on the same mob keep stacking")
    {
        CHECK_FALSE(ComboTargetRules::GainResetsPoints(a, a));
    }
    SECTION("a point on a different mob starts over")
    {
        CHECK(ComboTargetRules::GainResetsPoints(a, b));
    }
    SECTION("a dead or missing combo target drops the points")
    {
        CHECK(ComboTargetRules::ShouldDrop(a, false));
        CHECK_FALSE(ComboTargetRules::ShouldDrop(a, true));
        CHECK_FALSE(ComboTargetRules::ShouldDrop(ObjectGuid::Empty, false));
    }
}
