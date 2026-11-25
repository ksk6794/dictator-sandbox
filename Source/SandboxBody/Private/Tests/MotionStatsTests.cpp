#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "SandboxMotionStats.h"

namespace
{
	const EAutomationTestFlags StatsTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;
}

/** Spread, share and path of made-up records whose answers are known. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMotionStatsSpreadTest, "DictatorSandbox.Body.MotionStats.SpreadAndPath", StatsTestFlags)
bool FMotionStatsSpreadTest::RunTest(const FString& Parameters)
{
	// A sine of amplitude 2 over whole periods about 5: its spread is 2 over the root of 2.
	TArray<double> Sine;
	TArray<FVector2D> Circle;
	for (int32 Index = 0; Index < 900; ++Index)
	{
		const double Phase = UE_DOUBLE_TWO_PI * Index / 90.0;
		Sine.Add(5.0 + 2.0 * FMath::Sin(Phase));
		Circle.Add(FVector2D(FMath::Cos(Phase), FMath::Sin(Phase)) * 3.0);
	}
	TestEqual(TEXT("mean of the sine"), FSandboxMotionStats::Mean(Sine), 5.0, 1.0e-9);
	TestEqual(TEXT("spread of the sine"), FSandboxMotionStats::RmsDeviation(Sine), 2.0 / FMath::Sqrt(2.0), 1.0e-9);
	// Round a circle of radius 3 once a second: 2 pi 3 a second, a little less by chords.
	TestEqual(TEXT("speed round the circle"), FSandboxMotionStats::PathSpeed(Circle, 1.0 / 90.0), UE_DOUBLE_TWO_PI * 3.0, 0.01);
	TestEqual(TEXT("a point does not move"), FSandboxMotionStats::PathSpeed(TArray<FVector2D>{ FVector2D(1, 1) }, 0.1), 0.0);

	TArray<double> Counting;
	for (int32 Value = 100; Value >= 1; --Value)
	{
		Counting.Add(Value);
	}
	TestEqual(TEXT("95 of 100 values are at most 95"), FSandboxMotionStats::Percentile(Counting, 0.95), 95.0);
	TestEqual(TEXT("the largest"), FSandboxMotionStats::Percentile(Counting, 1.0), 100.0);

	const TArray<FVector2D> Spike = { FVector2D(0, 0), FVector2D(0, 0), FVector2D(9, 0), FVector2D(0, 0), FVector2D(0, 0) };
	const TArray<FVector2D> Smooth = FSandboxMotionStats::MovingMean(Spike, 1);
	TestEqual(TEXT("a spike is spread over its neighbours"), Smooth[1].X, 3.0, 1.0e-9);
	TestEqual(TEXT("and over no more"), Smooth[0].X, 0.0, 1.0e-9);
	return true;
}

/** The period: a loop is found, a drift and noise are not one. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMotionStatsPeriodTest, "DictatorSandbox.Body.MotionStats.Period", StatsTestFlags)
bool FMotionStatsPeriodTest::RunTest(const FString& Parameters)
{
	const double StepS = 1.0 / 15.0;
	TArray<FVector2D> Loop;
	TArray<FVector2D> Drift;
	TArray<FVector2D> Noise;
	FRandomStream Random(7);
	FVector2D Walk = FVector2D::ZeroVector;
	for (int32 Index = 0; Index < 900; ++Index)
	{
		const double Phase = UE_DOUBLE_TWO_PI * Index * StepS / 7.5;
		// Two waves that come round together after 7.5 s; the faster one alone would come round after 2.5.
		Loop.Add(FVector2D(FMath::Sin(Phase) + 0.5 * FMath::Sin(3.0 * Phase), 0.3 * FMath::Cos(2.0 * Phase)));
		Drift.Add(FVector2D(0.01 * Index, 0.0));
		Walk += FVector2D(Random.FRandRange(-1.0f, 1.0f), Random.FRandRange(-1.0f, 1.0f));
		Noise.Add(FVector2D(Random.FRandRange(-1.0f, 1.0f), Random.FRandRange(-1.0f, 1.0f)));
	}
	double Correlation = 0.0;
	TestEqual(TEXT("the loop's period"), FSandboxMotionStats::Period(Loop, StepS, 1.0, 0.5, &Correlation), 7.5, StepS);
	TestTrue(TEXT("it comes round exactly"), Correlation > 0.99);
	TestEqual(TEXT("a drift has none"), FSandboxMotionStats::Period(Drift, StepS, 1.0, 0.5), 0.0);
	TestEqual(TEXT("noise has none"), FSandboxMotionStats::Period(Noise, StepS, 1.0, 0.5), 0.0);
	TestEqual(TEXT("a record too short to come round has none"), FSandboxMotionStats::Period(TArrayView<FVector2D>(Loop).Left(100), StepS, 1.0, 0.5), 0.0);
	return true;
}

/** The drive's torque by its law: stiffness times the turn, less damping times the speed, each axis capped by itself. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMotionStatsDriveTest, "DictatorSandbox.Body.MotionStats.DriveTorque", StatsTestFlags)
bool FMotionStatsDriveTest::RunTest(const FString& Parameters)
{
	const FQuat Parent(FVector(0, 0, 1), 0.7);
	const FQuat Target(FVector(0, 1, 0), 0.3);
	bool bAtCap = true;
	// On its target and still: nothing.
	FVector Torque = FSandboxMotionStats::DriveTorqueNm(Parent, Parent * Target, Target, FVector::ZeroVector, FVector::ZeroVector, FVector::ZeroVector, 100.0, 10.0, 50.0, bAtCap);
	TestTrue(TEXT("no torque on the target"), Torque.IsNearlyZero(1.0e-6));
	TestFalse(TEXT("and not at the cap"), bAtCap);

	// 0.2 rad past it about the child's own x: 100 N*m/rad give 20 N*m back (2 sin(0.1) for the solver's estimate of the angle).
	const FQuat Child = Parent * Target * FQuat(FVector(1, 0, 0), 0.2);
	Torque = FSandboxMotionStats::DriveTorqueNm(Parent, Child, Target, FVector::ZeroVector, FVector::ZeroVector, FVector::ZeroVector, 100.0, 0.0, 50.0, bAtCap);
	TestEqual(TEXT("the spring's torque"), Torque.Size(), 20.0, 0.05);
	TestTrue(TEXT("it turns the child back"), (Torque | Child.RotateVector(FVector(1, 0, 0))) < 0.0);
	TestFalse(TEXT("below the cap"), bAtCap);

	// The same with a cap of 15: cut to it.
	Torque = FSandboxMotionStats::DriveTorqueNm(Parent, Child, Target, FVector::ZeroVector, FVector::ZeroVector, FVector::ZeroVector, 100.0, 0.0, 15.0, bAtCap);
	TestEqual(TEXT("capped"), Torque.Size(), 15.0, 1.0e-6);
	TestTrue(TEXT("at the cap"), bAtCap);

	// On the target, turning at 2 rad/s against the parent: 10 N*m*s/rad brake with 20 N*m; a target that turns along does not.
	const FVector Speed(0, 0, 2);
	Torque = FSandboxMotionStats::DriveTorqueNm(Parent, Parent * Target, Target, FVector::ZeroVector, Speed, FVector::ZeroVector, 100.0, 10.0, 0.0, bAtCap);
	TestTrue(TEXT("the damping's torque"), Torque.Equals(FVector(0, 0, -20), 1.0e-6));
	TestFalse(TEXT("no cap, not at it"), bAtCap);
	Torque = FSandboxMotionStats::DriveTorqueNm(Parent, Parent * Target, Target, FVector::ZeroVector, Speed, Speed, 100.0, 10.0, 0.0, bAtCap);
	TestTrue(TEXT("nothing against a target that turns along"), Torque.IsNearlyZero(1.0e-6));

	TestTrue(TEXT("turning speed"), FSandboxMotionStats::TurnSpeed(FQuat::Identity, FQuat(FVector(0, 0, 1), 0.1), 0.05).Equals(FVector(0, 0, 2), 1.0e-6));
	return true;
}

/** A jump of a command against a motion that goes on, each with its known answer. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMotionStatsJumpTest, "DictatorSandbox.Body.MotionStats.CommandJump", StatsTestFlags)
bool FMotionStatsJumpTest::RunTest(const FString& Parameters)
{
	const FQuat Parent(FVector(0, 0, 1), 0.7);
	const FQuat Base(FVector(0, 1, 0), 0.3);
	const FQuat Child = Parent * Base;
	// Four commands by their targets' turns about the child's x, degrees, and their stiffnesses; the joint is on Base.
	typedef double FFour[4];
	const auto Jump = [&](const FFour& Deg, const FFour& Stiffness, double SpeedRadS, double DampingS, double& OutDeg)
	{
		FSandboxDriveCommand Commands[4];
		for (int32 Index = 0; Index < 4; ++Index)
		{
			Commands[Index].Target = Base * FQuat(FVector(1, 0, 0), FMath::DegreesToRadians(Deg[Index]));
			Commands[Index].StiffnessNmPerRad = Stiffness[Index];
			Commands[Index].DampingNmsPerRad = Stiffness[Index] * DampingS;
		}
		return FSandboxMotionStats::CommandJumpNm(Commands, Parent, Child, FVector::ZeroVector, Child.RotateVector(FVector(SpeedRadS, 0, 0)), OutDeg);
	};
	const FFour Even = { 100, 100, 100, 100 };
	const FFour Still = { 0, 0, 0, 0 };
	double Deg = -1.0;
	const double ThirtyDegNm = 100.0 * FMath::DegreesToRadians(30.0);
	// A target that stands, is elsewhere and stands: all of it is a jump. The frames before and after are none.
	const FFour Jumps = { 0, 0, 30, 30 };
	TestEqual(TEXT("a jump, N*m"), Jump(Jumps, Even, 0.0, 0.0, Deg), ThirtyDegNm, 1.0e-3);
	TestEqual(TEXT("a jump, degrees"), Deg, 30.0, 1.0e-3);
	const FFour BeforeIt = { 0, 0, 0, 30 };
	TestEqual(TEXT("before a jump"), Jump(BeforeIt, Even, 0.0, 0.0, Deg), 0.0, 1.0e-3);
	const FFour AfterIt = { 0, 30, 30, 30 };
	TestEqual(TEXT("after a jump"), Jump(AfterIt, Even, 0.0, 0.0, Deg), 0.0, 1.0e-3);
	// The same 30 degrees a frame as a motion that goes on, that begins and that ends: no jump.
	const FFour GoesOn = { -30, 0, 30, 60 };
	TestEqual(TEXT("a fast motion"), Jump(GoesOn, Even, 0.0, 0.0, Deg), 0.0, 1.0e-3);
	TestEqual(TEXT("a fast motion, degrees"), Deg, 0.0, 1.0e-3);
	const FFour Begins = { 0, 0, 30, 60 };
	TestEqual(TEXT("a motion that begins"), Jump(Begins, Even, 0.0, 0.0, Deg), 0.0, 1.0e-3);
	const FFour Ends = { -30, 0, 30, 30 };
	TestEqual(TEXT("a motion that ends"), Jump(Ends, Even, 0.0, 0.0, Deg), 0.0, 1.0e-3);
	// A jump of 20 degrees in the middle of a motion of 5 a frame: the 20 alone. One there and back: all of it.
	const FFour InMotion = { -5, 0, 25, 30 };
	Jump(InMotion, Even, 0.0, 0.0, Deg);
	TestEqual(TEXT("a jump in a motion, degrees"), Deg, 20.0, 1.0e-3);
	const FFour AndBack = { 0, 0, 30, 0 };
	Jump(AndBack, Even, 0.0, 0.0, Deg);
	TestEqual(TEXT("there and back, degrees"), Deg, 30.0, 1.0e-3);
	// A target that jumps while its muscle has no strength asks for nothing; so does one whose muscle comes up from none.
	const FFour Far = { 0, 0, 120, 120 };
	const FFour Held = { 120, 120, 120, 120 };
	const FFour FromNone = { 0, 0, 5, 10 };
	TestEqual(TEXT("without strength"), Jump(Far, Still, 0.0, 0.0, Deg), 0.0, 1.0e-3);
	TestEqual(TEXT("without strength, degrees"), Deg, 120.0, 1.0e-3);
	TestEqual(TEXT("coming up from none"), Jump(Held, FromNone, 0.0, 0.0, Deg), 0.0, 1.0e-3);
	// A strength halved in one frame on a joint that turns at 2 rad/s on its target: half of the damping's 20 N*m. The same loss over many frames: none.
	const FFour Halved = { 100, 100, 50, 50 };
	const FFour Fades = { 100, 90, 80, 70 };
	TestEqual(TEXT("a strength that drops"), Jump(Still, Halved, 2.0, 0.1, Deg), 10.0, 1.0e-3);
	TestEqual(TEXT("a strength that fades"), Jump(Still, Fades, 2.0, 0.1, Deg), 0.0, 1.0e-3);
	return true;
}

#endif
