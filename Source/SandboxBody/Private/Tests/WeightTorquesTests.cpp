#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "SandboxWeightTorques.h"

namespace
{
	constexpr EAutomationTestFlags WeightTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;
	const FVector Gravity(0.0, 0.0, -9.8);
}

/** A hanging chain against its analytic torques, a leg under a known load, and that what is applied adds up to nothing. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWeightTorquesTest, "DictatorSandbox.Body.WeightTorques.HoldWhatHangsAndCarryWhatPushes", WeightTestFlags)
bool FWeightTorquesTest::RunTest(const FString& Parameters)
{
	// An arm held out along +y from a root at the origin: shoulder at 10 cm, elbow at 40, the two parts' centres of mass at 25 and 60.
	TArray<FSandboxWeightTorques::FBody> Arm;
	Arm.SetNum(3);
	Arm[0].MassKg = 30.0;
	Arm[1].Parent = 0;
	Arm[1].JointCm = FVector(0.0, 10.0, 100.0);
	Arm[1].CenterOfMassCm = FVector(0.0, 25.0, 100.0);
	Arm[1].MassKg = 2.0;
	Arm[2].Parent = 1;
	Arm[2].JointCm = FVector(0.0, 40.0, 100.0);
	Arm[2].CenterOfMassCm = FVector(0.0, 60.0, 100.0);
	Arm[2].MassKg = 1.5;
	TArray<FVector> Torques;
	TestTrue(TEXT("a chain in order is worked out"), FSandboxWeightTorques::Compute(Arm, Gravity, Torques));
	// Weight down at a lever of r along +y: the moment r x (0, 0, -mg) is (-r m g, 0, 0); the muscle's is the opposite.
	const double Elbow = 0.20 * 1.5 * 9.8;
	const double Shoulder = 0.15 * 2.0 * 9.8 + 0.50 * 1.5 * 9.8;
	TestEqual(TEXT("the elbow holds the forearm up"), Torques[2], FVector(Elbow, 0.0, 0.0), 1.0e-6f);
	TestEqual(TEXT("the shoulder holds both up"), Torques[1], FVector(Shoulder, 0.0, 0.0), 1.0e-6f);
	TestEqual(TEXT("the root has no joint"), Torques[0], FVector::ZeroVector);
	// Applied as a torque on the child, the turn it makes lifts the end: w = tau, v = w x r, r along +y: (tau, 0, 0) x (0, r, 0) = (0, 0, tau r).
	TestTrue(TEXT("and it turns the arm up, not down"), FVector::CrossProduct(Torques[1], FVector(0.0, 1.0, 0.0)).Z > 0.0);

	// The hand rests on a table that carries 5 N of it, at 80 cm.
	Arm[2].PushN = FVector(0.0, 0.0, 5.0);
	Arm[2].PushAtCm = FVector(0.0, 80.0, 100.0);
	FSandboxWeightTorques::Compute(Arm, Gravity, Torques);
	TestEqual(TEXT("a push under the hand is taken off the elbow"), Torques[2].X, Elbow - 0.40 * 5.0, 1.0e-9);
	TestEqual(TEXT("and off the shoulder"), Torques[1].X, Shoulder - 0.70 * 5.0, 1.0e-9);

	// A leg: root (the rest of the body) above, thigh and foot below; the floor pushes the foot up with 400 N, 6 cm ahead of the ankle.
	TArray<FSandboxWeightTorques::FBody> Leg;
	Leg.SetNum(3);
	Leg[0].MassKg = 70.0;
	Leg[1].Parent = 0;
	Leg[1].JointCm = FVector(0.0, 0.0, 90.0);
	Leg[1].CenterOfMassCm = FVector(0.0, 0.0, 50.0);
	Leg[1].MassKg = 12.0;
	Leg[2].Parent = 1;
	Leg[2].JointCm = FVector(0.0, 0.0, 8.0);
	Leg[2].CenterOfMassCm = FVector(0.0, 4.0, 3.0);
	Leg[2].MassKg = 1.0;
	Leg[2].PushN = FVector(0.0, 0.0, 400.0);
	Leg[2].PushAtCm = FVector(0.0, 6.0, 0.0);
	FSandboxWeightTorques::Compute(Leg, Gravity, Torques);
	// The push turns the toes up, (0, 0.06, *) x (0, 0, 400) = (24, 0, 0); the foot's weight the other way, 0.04 * 9.8.
	TestEqual(TEXT("the ankle presses the toes down against the floor's push"), Torques[2].X, -(0.06 * 400.0 - 0.04 * 9.8), 1.0e-9);
	TestEqual(TEXT("the hip carries the same push about its own place"), Torques[1].X, -(0.06 * 400.0 - 0.04 * 9.8), 1.0e-9);

	TArray<FSandboxWeightTorques::FBody> Wrong = Arm;
	Wrong[1].Parent = 2;
	TestFalse(TEXT("a body before its parent is refused"), FSandboxWeightTorques::Compute(Wrong, Gravity, Torques));
	return true;
}

/** How the weight is shared between two feet, and where each foot's push goes. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWeightShareTest, "DictatorSandbox.Body.WeightTorques.ShareBetweenFeet", WeightTestFlags)
bool FWeightShareTest::RunTest(const FString& Parameters)
{
	// Soles 10 by 26 cm, their middles 30 cm apart along x.
	const auto Sole = [](double X, double Y)
	{
		return TArray<FVector2D>{ FVector2D(X - 5.0, Y - 13.0), FVector2D(X + 5.0, Y - 13.0), FVector2D(X + 5.0, Y + 13.0), FVector2D(X - 5.0, Y + 13.0) };
	};
	const TArray<FVector2D> Left = Sole(15.0, 0.0);
	const TArray<FVector2D> Right = Sole(-15.0, 0.0);
	double Share = 0.0;
	FVector2D At[2];
	FSandboxWeightTorques::ShareBetweenFeet(Left, Right, FVector2D(0.0, 0.0), Share, At);
	TestEqual(TEXT("over the middle each foot has half"), Share, 0.5, 1.0e-9);
	FSandboxWeightTorques::ShareBetweenFeet(Left, Right, FVector2D(9.0, 4.0), Share, At);
	TestEqual(TEXT("nearer the left foot it has more"), Share, 0.8, 1.0e-9);
	TestEqual(TEXT("and both push from as far ahead as the body is"), FVector2D::Distance(At[0], FVector2D(15.0, 4.0)), 0.0, 1.0e-9);
	TestEqual(TEXT("the right one too"), FVector2D::Distance(At[1], FVector2D(-15.0, 4.0)), 0.0, 1.0e-9);
	TestEqual(TEXT("so that together they carry the weight where it is"), FVector2D::Distance(At[0] * Share + At[1] * (1.0 - Share), FVector2D(9.0, 4.0)), 0.0, 1.0e-9);
	FSandboxWeightTorques::ShareBetweenFeet(Left, Right, FVector2D(0.0, 40.0), Share, At);
	TestEqual(TEXT("no further ahead than the toes"), At[0].Y, 13.0, 1.0e-9);
	FSandboxWeightTorques::ShareBetweenFeet(Left, Right, FVector2D(60.0, 0.0), Share, At);
	TestEqual(TEXT("beyond a foot all of it is on that foot"), Share, 1.0, 1.0e-9);
	FSandboxWeightTorques::ShareBetweenFeet(Left, TArray<FVector2D>(), FVector2D(0.0, 0.0), Share, At);
	TestEqual(TEXT("with one foot all of it is there"), Share, 1.0, 1.0e-9);
	TestEqual(TEXT("at the edge nearest the body"), FVector2D::Distance(At[0], FVector2D(10.0, 0.0)), 0.0, 1.0e-9);
	TestEqual(TEXT("a point inside a sole stays where it is"), FVector2D::Distance(FSandboxWeightTorques::NearestInside(Left, FVector2D(12.0, -7.0)), FVector2D(12.0, -7.0)), 0.0, 1.0e-9);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
