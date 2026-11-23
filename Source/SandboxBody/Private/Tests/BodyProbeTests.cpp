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

#endif // WITH_DEV_AUTOMATION_TESTS
