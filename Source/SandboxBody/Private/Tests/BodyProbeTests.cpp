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

#endif // WITH_DEV_AUTOMATION_TESTS
