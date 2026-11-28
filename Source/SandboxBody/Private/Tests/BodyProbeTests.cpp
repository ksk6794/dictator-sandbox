#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "SandboxBodyProbe.h"

namespace
{
	const EAutomationTestFlags ProbeTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	/** Twice the signed area: positive for a counter-clockwise polygon. */
	double TwiceArea(const TArray<FVector2D>& Polygon)
	{
		double Sum = 0.0;
		for (int32 Index = 0; Index < Polygon.Num(); ++Index)
		{
			Sum += FVector2D::CrossProduct(Polygon[Index], Polygon[(Index + 1) % Polygon.Num()]);
		}
		return Sum;
	}
}

/** The geometry behind the support polygon: the hull of the soles and how far a point is inside it. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBodyProbeSupportTest, "DictatorSandbox.Body.Probe.SupportPolygonGeometry", ProbeTestFlags)
bool FBodyProbeSupportTest::RunTest(const FString& Parameters)
{
	// Two feet as the corners of two soles, 10 cm by 25 cm, 16 cm apart, with points inside and on edges.
	const TArray<FVector2D> Soles = {
		FVector2D(8, -12), FVector2D(18, -12), FVector2D(18, 13), FVector2D(8, 13),
		FVector2D(-8, -12), FVector2D(-18, -12), FVector2D(-18, 13), FVector2D(-8, 13),
		FVector2D(0, 0), FVector2D(13, 0), FVector2D(0, 13), FVector2D(18, 0), FVector2D(18, 13) };
	TArray<FVector2D> Hull;
	FSandboxBodyProbe::ConvexHull2D(Soles, Hull);
	if (!TestEqual(TEXT("the hull of two soles is a rectangle"), Hull.Num(), 4))
	{
		return false;
	}
	TestTrue(TEXT("the hull is counter-clockwise"), TwiceArea(Hull) > 0.0);
	TestEqual(TEXT("its area is 36 cm by 25 cm"), TwiceArea(Hull) * 0.5, 900.0, 1.0e-9);
	for (const FVector2D& Corner : { FVector2D(18, -12), FVector2D(18, 13), FVector2D(-18, 13), FVector2D(-18, -12) })
	{
		TestTrue(FString::Printf(TEXT("corner (%g, %g) is on the hull"), Corner.X, Corner.Y), Hull.Contains(Corner));
	}

	// The margin: distance to the nearest edge, positive inside.
	TestEqual(TEXT("the middle is 12 cm from the front edge"), FSandboxBodyProbe::MarginInsidePolygon(Hull, FVector2D(0, 0)), 12.0, 1.0e-9);
	TestEqual(TEXT("a point near the side is that far inside"), FSandboxBodyProbe::MarginInsidePolygon(Hull, FVector2D(15, 0)), 3.0, 1.0e-9);
	TestEqual(TEXT("a point on an edge has no margin"), FSandboxBodyProbe::MarginInsidePolygon(Hull, FVector2D(18, 5)), 0.0, 1.0e-9);
	TestEqual(TEXT("a point beyond an edge is outside by its distance"), FSandboxBodyProbe::MarginInsidePolygon(Hull, FVector2D(0, 20)), -7.0, 1.0e-9);
	TestEqual(TEXT("a point beyond a corner is outside by its distance to the corner"), FSandboxBodyProbe::MarginInsidePolygon(Hull, FVector2D(21, 17)), -5.0, 1.0e-9);

	// The nearest point of the polygon: where a centre of pressure can go toward a capture point outside.
	TestEqual(TEXT("a point inside is its own nearest point"), FSandboxBodyProbe::NearestPointOfPolygon(Hull, FVector2D(3, 4)), FVector2D(3, 4));
	TestEqual(TEXT("beyond an edge the nearest point is on that edge"), FSandboxBodyProbe::NearestPointOfPolygon(Hull, FVector2D(5, 30)), FVector2D(5, 13));
	TestEqual(TEXT("beyond a corner it is the corner"), FSandboxBodyProbe::NearestPointOfPolygon(Hull, FVector2D(21, 17)), FVector2D(18, 13));
	TestEqual(TEXT("to the edge along x"), FSandboxBodyProbe::DistanceToEdgeAlong(Hull, FVector2D(3, 4), FVector2D(2, 0)), 15.0, 1.0e-9);
	TestEqual(TEXT("to the edge along -y"), FSandboxBodyProbe::DistanceToEdgeAlong(Hull, FVector2D(3, 4), FVector2D(0, -1)), 16.0, 1.0e-9);
	TestEqual(TEXT("to the nearer edge along a diagonal"), FSandboxBodyProbe::DistanceToEdgeAlong(Hull, FVector2D(3, 4), FVector2D(1, 1)), 9.0 * FMath::Sqrt(2.0), 1.0e-9);
	TestEqual(TEXT("no way out from outside"), FSandboxBodyProbe::DistanceToEdgeAlong(Hull, FVector2D(30, 4), FVector2D(1, 0)), 0.0);

	// Fewer than three distinct points have no inside.
	TArray<FVector2D> Edge;
	FSandboxBodyProbe::ConvexHull2D(TArray<FVector2D>{ FVector2D(0, 0), FVector2D(10, 0), FVector2D(0, 0), FVector2D(5, 0) }, Edge);
	TestEqual(TEXT("collinear points give a segment"), Edge.Num(), 2);
	TestEqual(TEXT("a point over a segment is outside by its height"), FSandboxBodyProbe::MarginInsidePolygon(Edge, FVector2D(5, 3)), -3.0, 1.0e-9);
	TestTrue(TEXT("a point on a segment is not inside either"), FSandboxBodyProbe::MarginInsidePolygon(Edge, FVector2D(5, 0)) <= 0.0);

	TArray<FVector2D> Nothing;
	FSandboxBodyProbe::ConvexHull2D(TArray<FVector2D>(), Nothing);
	TestEqual(TEXT("no points give no hull"), Nothing.Num(), 0);
	TestTrue(TEXT("and no margin"), FSandboxBodyProbe::MarginInsidePolygon(Nothing, FVector2D(0, 0)) < -1.0e30);

	// Without a mesh there are no soles.
	TArray<FVector2D> Polygon;
	FSandboxBodyProbe::SupportPolygon(nullptr, TArray<FName>{ FName(TEXT("foot_l")) }, 1.5, Polygon);
	TestEqual(TEXT("no mesh gives no polygon"), Polygon.Num(), 0);
	return true;
}

/** The capture point, the body's speed as a whole and its energy, on bodies written out by hand. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBodyProbeEnergyTest, "DictatorSandbox.Body.Probe.CapturePointAndEnergy", ProbeTestFlags)
bool FBodyProbeEnergyTest::RunTest(const FString& Parameters)
{
	// A pendulum 98 cm tall under 980 cm/s^2 has a time constant of sqrt(0.1) s: at 100 cm/s the capture point
	// is 31.6 cm ahead of the centre of mass. Vertical speed does not move it.
	const double TimeS = FMath::Sqrt(0.1);
	const FVector2D Ahead = FSandboxBodyProbe::CapturePoint(FVector(10.0, 20.0, 98.0), FVector(100.0, -50.0, 300.0), 98.0, 980.0);
	TestEqual(TEXT("capture point, x"), Ahead.X, 10.0 + 100.0 * TimeS, 1.0e-9);
	TestEqual(TEXT("capture point, y"), Ahead.Y, 20.0 - 50.0 * TimeS, 1.0e-9);
	const FVector2D AtRest = FSandboxBodyProbe::CapturePoint(FVector(10.0, 20.0, 98.0), FVector::ZeroVector, 98.0, 980.0);
	TestEqual(TEXT("a body at rest has its capture point under its centre of mass"), AtRest, FVector2D(10.0, 20.0));
	const FVector2D OnTheFloor = FSandboxBodyProbe::CapturePoint(FVector(10.0, 20.0, 5.0), FVector(100.0, 0.0, 0.0), -3.0, 980.0);
	TestEqual(TEXT("a body below its support has no pendulum"), OnTheFloor, FVector2D(10.0, 20.0));

	// Two bodies: 60 kg moving at 1 m/s along x, 20 kg at rest but spinning at 2 rad/s about an axis with
	// 5000 kg*cm^2 (0.5 kg*m^2) of inertia, its principal axes turned 90 degrees about z.
	TArray<FSandboxBodySample> Bodies;
	FSandboxBodySample& Trunk = Bodies.AddDefaulted_GetRef();
	Trunk.MassKg = 60.0;
	Trunk.CenterOfMass = FVector(0.0, 0.0, 100.0);
	Trunk.LinearVelocity = FVector(100.0, 0.0, 0.0);
	FSandboxBodySample& Limb = Bodies.AddDefaulted_GetRef();
	Limb.MassKg = 20.0;
	Limb.CenterOfMass = FVector(40.0, 0.0, 50.0);
	Limb.AngularVelocityRad = FVector(0.0, 2.0, 0.0);
	Limb.InertiaKgCm2 = FVector(5000.0, 100.0, 100.0);
	Limb.InertiaRotation = FQuat(FVector::UpVector, UE_DOUBLE_HALF_PI);

	TestEqual(TEXT("centre of mass"), FSandboxBodyProbe::CenterOfMass(Bodies), FVector(10.0, 0.0, 87.5));
	TestEqual(TEXT("velocity of the centre of mass"), FSandboxBodyProbe::CenterOfMassVelocity(Bodies), FVector(75.0, 0.0, 0.0));
	TestEqual(TEXT("translational energy: 30 J"), FSandboxBodyProbe::TranslationalKineticEnergyJ(Bodies), 30.0, 1.0e-9);
	// The spin about world y is a spin about the limb's own x axis: 0.5 * 0.5 kg*m^2 * 4 = 1 J.
	TestEqual(TEXT("kinetic energy with rotation: 31 J"), FSandboxBodyProbe::KineticEnergyJ(Bodies), 31.0, 1.0e-9);
	// 60 kg at 1 m and 20 kg at 0.5 m under 9.8 m/s^2.
	TestEqual(TEXT("gravitational energy above the floor"), FSandboxBodyProbe::PotentialEnergyJ(Bodies, 980.0, 0.0), 686.0, 1.0e-9);
	TestEqual(TEXT("and above a floor 10 cm up"), FSandboxBodyProbe::PotentialEnergyJ(Bodies, 980.0, 10.0), 686.0 - 78.4, 1.0e-9);

	double LinearMps = 0.0;
	double AngularRadS = 0.0;
	FSandboxBodyProbe::MassWeightedSpeeds(Bodies, LinearMps, AngularRadS);
	TestEqual(TEXT("mass-weighted linear speed"), LinearMps, FMath::Sqrt(60.0 / 80.0), 1.0e-9);
	TestEqual(TEXT("mass-weighted angular speed"), AngularRadS, FMath::Sqrt(20.0 * 4.0 / 80.0), 1.0e-9);

	// Nothing to measure gives nothing.
	FSandboxPenetration Penetration;
	Penetration.WorldCm = 5.0;
	FSandboxBodyProbe::MeasurePenetration(nullptr, TConstArrayView<const AActor*>(), Penetration);
	TestEqual(TEXT("no mesh has no penetration"), Penetration.WorldCm, 0.0);
	return true;
}

/** R3: the time until the trunk meets the floor, against free fall worked by hand (g 980 cm/s^2). */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBodyProbeTimeToFloorTest, "DictatorSandbox.Body.Probe.TimeToFloor", ProbeTestFlags)
bool FBodyProbeTimeToFloorTest::RunTest(const FString& Parameters)
{
	constexpr double G = 980.0;
	// From rest, 122.5 cm above the radius: t = sqrt(2 h / g) = 0.5 s.
	TestEqual(TEXT("from rest"), FSandboxBodyProbe::TimeToFloorS(134.5, 0.0, G, 12.0), 0.5, 1.0e-9);
	// Thrown up at 490 cm/s from the radius's height: back at t = 2 v / g = 1.0 s.
	TestEqual(TEXT("thrown up from the floor's height"), FSandboxBodyProbe::TimeToFloorS(12.0 + 1.0e-9, 490.0, G, 12.0), 1.0, 1.0e-6);
	// Moving down at 245 cm/s from 91.875 cm above: 245 t + 490 t^2 = 91.875 at t = 0.25 s.
	TestEqual(TEXT("moving down"), FSandboxBodyProbe::TimeToFloorS(103.875, -245.0, G, 12.0), 0.25, 1.0e-9);
	// Already within the radius: now.
	TestEqual(TEXT("there already"), FSandboxBodyProbe::TimeToFloorS(10.0, -100.0, G, 12.0), 0.0, 1.0e-12);
	// No gravity: by its speed alone.
	TestEqual(TEXT("no gravity"), FSandboxBodyProbe::TimeToFloorS(112.0, -200.0, 0.0, 12.0), 0.5, 1.0e-12);
	return true;
}

/** R3's catch fall: where the floor will be in the chest's frame, against cases worked by hand. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBodyProbeCatchDirectionTest, "DictatorSandbox.Body.Probe.CatchDirection", ProbeTestFlags)
bool FBodyProbeCatchDirectionTest::RunTest(const FString& Parameters)
{
	const FVector Up = FVector::UpVector;
	// Upright, falling toward +x, foreseen lying flat (90 degrees): the floor, straight down then, is along the chest's +x now.
	TestTrue(TEXT("upright, flat at impact: the floor lies the way it falls"),
		FSandboxBodyProbe::CatchDirectionLocal(FQuat::Identity, Up, FVector2D(1.0, 0.0), 90.0, 0.0).Equals(FVector(1.0, 0.0, 0.0), 1.0e-6));
	// Leaning 60 of its foreseen 60 degrees already: nothing is left to tip, the floor is straight down in the world, seen from the chest.
	const FQuat Leaning(FVector(0.0, 1.0, 0.0), FMath::DegreesToRadians(60.0));
	const FVector Down = Leaning.UnrotateVector(-Up);
	TestTrue(TEXT("already leaning as foreseen: the world's down in the chest's frame"),
		FSandboxBodyProbe::CatchDirectionLocal(Leaning, Up, FVector2D(1.0, 0.0), 60.0, 0.0).Equals(Down, 1.0e-6));
	// The same answer from any lean on the way there: held in the chest's frame, the target does not move with the fall's progress.
	const FQuat Halfway(FVector(0.0, 1.0, 0.0), FMath::DegreesToRadians(25.0));
	const FVector Start = FSandboxBodyProbe::CatchDirectionLocal(FQuat::Identity, Up, FVector2D(1.0, 0.0), 60.0, 0.5);
	TestTrue(TEXT("the same in the chest's frame from upright and from half way"),
		FSandboxBodyProbe::CatchDirectionLocal(Halfway, Up, FVector2D(1.0, 0.0), 60.0, 0.5).Equals(Start, 1.0e-6));
	// A chest turned about the vertical keeps its turn: falling along its own front, the floor lies along its front.
	const FQuat Turned(Up, FMath::DegreesToRadians(90.0));
	TestTrue(TEXT("turned about the vertical: along its own front"),
		FSandboxBodyProbe::CatchDirectionLocal(Turned, Up, FVector2D(0.0, 1.0), 90.0, 0.0).Equals(FVector(1.0, 0.0, 0.0), 1.0e-6));
	// No way: straight down in the chest's frame.
	TestTrue(TEXT("no way: down"), FSandboxBodyProbe::CatchDirectionLocal(Leaning, Up, FVector2D::ZeroVector, 60.0, 0.5).Equals(-Up, 1.0e-12));
	return true;
}

/** The seam at 180 degrees: the feet's mean turn and the gaze's side (found on 27 November 2026, R2's step 7 and its review). */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBodyProbeSeamTest, "DictatorSandbox.Body.Probe.AnglesAcrossTheSeam", ProbeTestFlags)
bool FBodyProbeSeamTest::RunTest(const FString& Parameters)
{
	const double D = UE_DOUBLE_PI / 180.0;
	// Two feet at 175 and -175 degrees: the second taken as 185, the mean 180 (half round), not 0.
	TestEqual(TEXT("feet across the seam"), (175.0 * D + FSandboxBodyProbe::OnSideOf(-175.0 * D, 175.0 * D)) / 2.0, 180.0 * D, 1.0e-12);
	TestEqual(TEXT("and the other way round"), (-175.0 * D + FSandboxBodyProbe::OnSideOf(175.0 * D, -175.0 * D)) / 2.0, -180.0 * D, 1.0e-12);
	// Feet that do not straddle it are left exactly as they are.
	TestTrue(TEXT("feet on one side, unchanged to the bit"), FSandboxBodyProbe::OnSideOf(-30.0 * D, 20.0 * D) == -30.0 * D);
	TestTrue(TEXT("a quarter turn apart, unchanged"), FSandboxBodyProbe::OnSideOf(100.0 * D, 10.0 * D) == 100.0 * D);
	TestTrue(TEXT("the first foot, itself"), FSandboxBodyProbe::OnSideOf(2.0, 2.0) == 2.0);
	// The gaze, the head turning 0.7 of the way: a target behind on the right (-170) while the head turns left (+35) is nearer at
	// +190 (133 against -119), so it stays on the left as it crosses the seam.
	TestEqual(TEXT("behind, kept on the head's side"), FSandboxBodyProbe::GazeYawDeg(-170.0, 35.0, 0.7), 190.0, 1.0e-12);
	TestEqual(TEXT("behind on the left, head right"), FSandboxBodyProbe::GazeYawDeg(170.0, -35.0, 0.7), -190.0, 1.0e-12);
	// Just behind the right shoulder (-95) with the head nearly straight (+2): turned to, not away (the review's case).
	TestTrue(TEXT("just behind a shoulder"), FSandboxBodyProbe::GazeYawDeg(-95.0, 2.0, 0.7) == -95.0);
	// In front of the shoulders, or the head not turned, or on the same side: the angle itself.
	TestTrue(TEXT("in front of the shoulders"), FSandboxBodyProbe::GazeYawDeg(-80.0, 35.0, 0.7) == -80.0);
	TestTrue(TEXT("head not turned"), FSandboxBodyProbe::GazeYawDeg(-170.0, 0.0, 0.7) == -170.0);
	TestTrue(TEXT("same side"), FSandboxBodyProbe::GazeYawDeg(170.0, 35.0, 0.7) == 170.0);
	return true;
}

/** How a lying body lies (R2's step 9, getup.roll): its side from the hips, its front from the trunk and the hips. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBodyProbeLyingSideTest, "DictatorSandbox.Body.Probe.LyingSide", ProbeTestFlags)
bool FBodyProbeLyingSideTest::RunTest(const FString& Parameters)
{
	double SideDeg = -1.0;
	bool bOnLeft = false;
	bool bFacingDown = false;
	// Standing, facing +X: the left hip at -Y. Level hips, the front not down.
	TestTrue(TEXT("standing reads"), FSandboxBodyProbe::LyingSide(FVector(0, -10, 90), FVector(0, 10, 90), FVector(0, 0, 90), FVector(0, 0, 140), SideDeg, bOnLeft, bFacingDown));
	TestEqual(TEXT("standing: hips level"), SideDeg, 0.0, 1.0e-9);
	TestFalse(TEXT("standing: front not down"), bFacingDown);
	// On the back, the head toward +X: seen from above (X up the page, Y to the right), the left hand of a body lying face up is on the
	// right of the page, +Y.
	TestTrue(TEXT("back reads"), FSandboxBodyProbe::LyingSide(FVector(0, 10, 10), FVector(0, -10, 10), FVector(0, 0, 10), FVector(50, 0, 10), SideDeg, bOnLeft, bFacingDown));
	TestEqual(TEXT("back: hips level"), SideDeg, 0.0, 1.0e-9);
	TestFalse(TEXT("back: front up"), bFacingDown);
	// On the front, the head toward +X: the left at -Y.
	FSandboxBodyProbe::LyingSide(FVector(0, -10, 10), FVector(0, 10, 10), FVector(0, 0, 10), FVector(50, 0, 10), SideDeg, bOnLeft, bFacingDown);
	TestTrue(TEXT("front: front down"), bFacingDown);
	// On the left side, the head toward +X: the left hip under the right.
	FSandboxBodyProbe::LyingSide(FVector(0, 0, 5), FVector(0, 0, 25), FVector(0, 0, 15), FVector(50, 0, 15), SideDeg, bOnLeft, bFacingDown);
	TestEqual(TEXT("left side: 90 degrees"), SideDeg, 90.0, 1.0e-9);
	TestTrue(TEXT("left side: the left is down"), bOnLeft);
	// From the back, turned 60 degrees onto the left side: the left hip goes down from +Y; the front still looks up.
	const double C = FMath::Cos(FMath::DegreesToRadians(60.0));
	const double S = FMath::Sin(FMath::DegreesToRadians(60.0));
	FSandboxBodyProbe::LyingSide(FVector(0, 10.0 * C, -10.0 * S), FVector(0, -10.0 * C, 10.0 * S), FVector::ZeroVector, FVector(50, 0, 0), SideDeg, bOnLeft, bFacingDown);
	TestEqual(TEXT("from the back onto the left: 60 degrees"), SideDeg, 60.0, 1.0e-9);
	TestTrue(TEXT("from the back onto the left: the left down"), bOnLeft);
	TestFalse(TEXT("from the back onto the left: the front up"), bFacingDown);
	// From the front, turned 60 degrees onto the left side: the left hip goes down from -Y; the front looks down.
	FSandboxBodyProbe::LyingSide(FVector(0, -10.0 * C, -10.0 * S), FVector(0, 10.0 * C, 10.0 * S), FVector::ZeroVector, FVector(50, 0, 0), SideDeg, bOnLeft, bFacingDown);
	TestTrue(TEXT("from the front onto the left: the left down"), bOnLeft);
	TestTrue(TEXT("from the front onto the left: the front down"), bFacingDown);
	// From the front onto the right side: the right hip down, the front down.
	FSandboxBodyProbe::LyingSide(FVector(0, -10.0 * C, 10.0 * S), FVector(0, 10.0 * C, -10.0 * S), FVector::ZeroVector, FVector(50, 0, 0), SideDeg, bOnLeft, bFacingDown);
	TestFalse(TEXT("from the front onto the right: the right down"), bOnLeft);
	TestTrue(TEXT("from the front onto the right: the front down"), bFacingDown);
	// No line: refused.
	TestFalse(TEXT("hips on one point"), FSandboxBodyProbe::LyingSide(FVector::ZeroVector, FVector::ZeroVector, FVector::ZeroVector, FVector(0, 0, 1), SideDeg, bOnLeft, bFacingDown));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
