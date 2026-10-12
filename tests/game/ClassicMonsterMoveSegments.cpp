#include "tc_catch2.h"
#include "MovementPackets.h"
#include "MoveSpline.h"
#include "MoveSplineInitArgs.h"

// The 70291 client divides by the length of every segment of a moving path (client RVA 0x27DFC5E, FLT_DIVIDE_BY_ZERO); a segment
// whose ends coincide, counting the unit's start position (MonsterMove::Pos) as node 0, is what FindZeroLengthSegment reports.
using namespace WorldPackets::Movement;

namespace
{
void Fill(MonsterMove& packet, Position start, std::initializer_list<Position> points)
{
    packet.Pos = start;
    for (Position const& p : points)
        packet.SplineData.Move.Points.emplace_back(p.GetPositionX(), p.GetPositionY(), p.GetPositionZ());
}
}

TEST_CASE("MonsterMove segments: a stop carries no points and is fine", "[ClassicMonsterMove]")
{
    MonsterMove packet;
    packet.Pos = Position(10, 10, 5);
    CHECK_FALSE(FindZeroLengthSegment(packet).has_value());
}

TEST_CASE("MonsterMove segments: first point on the start position is a zero segment", "[ClassicMonsterMove]")
{
    MonsterMove packet;
    Fill(packet, Position(10, 10, 5), { Position(10, 10, 5), Position(20, 10, 5) });
    auto zero = FindZeroLengthSegment(packet);
    REQUIRE(zero.has_value());
    CHECK(zero->Index == 0);
    CHECK(zero->Nodes == 3);
}

TEST_CASE("MonsterMove segments: a repeated middle point is found", "[ClassicMonsterMove]")
{
    MonsterMove packet;
    Fill(packet, Position(0, 0, 0), { Position(5, 0, 0), Position(5, 0, 0), Position(9, 0, 0) });
    auto zero = FindZeroLengthSegment(packet);
    REQUIRE(zero.has_value());
    CHECK(zero->Index == 1);
}

TEST_CASE("MonsterMove segments: distinct points pass", "[ClassicMonsterMove]")
{
    MonsterMove packet;
    Fill(packet, Position(0, 0, 0), { Position(5, 0, 0), Position(5, 5, 0), Position(9, 5, 1) });
    CHECK_FALSE(FindZeroLengthSegment(packet).has_value());
}

TEST_CASE("MonsterMove segments: compressed path points that quantize onto each other are found", "[ClassicMonsterMove]")
{
    // start (0,0,0), last (20,0,0): middle (10,0,0). Intermediate points are sent as middle - point in quarter yards. Two intermediate
    // points 0.1 yd apart land on the same quarter, i.e. the same node for the client.
    MonsterMove packet;
    packet.Pos = Position(0, 0, 0);
    packet.SplineData.Move.Points.emplace_back(20.0f, 0.0f, 0.0f);
    packet.SplineData.Move.PackedDeltas.emplace_back(2.0f, 0.0f, 0.0f);     // node at x = 8
    packet.SplineData.Move.PackedDeltas.emplace_back(2.1f, 0.0f, 0.0f);     // 2.1 -> 2.0 : x = 8 again
    auto zero = FindZeroLengthSegment(packet);
    REQUIRE(zero.has_value());
    CHECK(zero->Index == 1);

    MonsterMove spread;
    spread.Pos = Position(0, 0, 0);
    spread.SplineData.Move.Points.emplace_back(20.0f, 0.0f, 0.0f);
    spread.SplineData.Move.PackedDeltas.emplace_back(4.0f, 1.0f, 0.0f);
    spread.SplineData.Move.PackedDeltas.emplace_back(-3.0f, 1.0f, 0.0f);
    CHECK_FALSE(FindZeroLengthSegment(spread).has_value());
}

TEST_CASE("MonsterMove from a real spline: the Thorium Point path no longer sends two intermediate points that quantize together", "[ClassicMonsterMove]")
{
    // The crash (client RVA 0x27DFC5E) had nodes start (-6524.45,-1194.68), two identical intermediate nodes (-6524.48,-1194.54),
    // (-6525.48,-1190.79) and end (-6527.02,-1193.41): the second of the first two points was 0.11 yd from the first and both
    // quantized onto the same quarter-yard node of middle - delta.
    Movement::MoveSplineInitArgs args;
    args.path = { G3D::Vector3(-6524.4478f, -1194.6764f, 309.2556f), G3D::Vector3(-6524.4429f, -1194.5545f, 309.2556f),
        G3D::Vector3(-6524.4229f, -1194.5645f, 309.2556f), G3D::Vector3(-6525.4829f, -1190.7945f, 309.2556f),
        G3D::Vector3(-6527.0181f, -1193.4127f, 309.2556f) };
    args.velocity = 7.0f;
    args.HasVelocity = true;
    args.splineId = 1;
    Movement::MoveSpline moveSpline;
    moveSpline.Initialize(args);

    MonsterMove packet;
    packet.Pos = Position(-6524.4478f, -1194.6764f, 309.2556f);
    packet.InitializeSplineData(moveSpline);

    CHECK(packet.SplineData.Move.PackedDeltas.size() == 2);     // three intermediate points in, the near-duplicate left out
    CHECK_FALSE(FindZeroLengthSegment(packet).has_value());
}
