#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "SandboxDesktopTargetSource.h"
#include "SandboxInteractionTuning.h"
#include "SandboxParamStore.h"
#include "SandboxScriptedTargetSource.h"
#include "SandboxTargetRecording.h"
#include "SandboxWorldSpring.h"

namespace
{
	const EAutomationTestFlags InteractionTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	/** A store of its own on the repo's tuning files, with the schema every module has declared. */
	bool LoadRepoTuning(FSandboxParamStore& Store, FString& OutError)
	{
		Store = FSandboxParamStore::Get().CloneSchema();
		Store.SetDirectory(FPaths::Combine(FPaths::ProjectConfigDir(), TEXT("Tuning")));
		return Store.Load(OutError);
	}

	FSandboxPathKey Key(double TimeS, const FVector& Location)
	{
		FSandboxPathKey Out;
		Out.TimeS = TimeS;
		Out.Location = Location;
		return Out;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInteractionTuningRepoFilesTest, "DictatorSandbox.Interaction.Tuning.RepoFilesAreValid", InteractionTestFlags)
bool FInteractionTuningRepoFilesTest::RunTest(const FString& Parameters)
{
	// The code holds no values: an empty tuning must not pass for a usable one.
	FString Error;
	TestFalse(TEXT("a default-constructed tuning does not validate"), FSandboxInteractionTuning().Validate(Error));

	FSandboxParamStore Store;
	FSandboxInteractionTuning Tuning;
	if (!TestTrue(TEXT("Config/Tuning loads"), LoadRepoTuning(Store, Error)) ||
		!TestTrue(TEXT("and gives a valid tuning"), FSandboxInteractionTuning::FromParams(Store, Tuning, Error)))
	{
		AddError(Error);
		return false;
	}

	// A hand is a body with mass on a spring with a cap; none of the three may be missing.
	TestTrue(TEXT("the hand has mass"), Tuning.Hand.MassKg > 0.0);
	TestTrue(TEXT("the hand has a spring"), Tuning.Hand.StiffnessNPerM > 0.0);
	TestTrue(TEXT("the hand has a force cap"), Tuning.Hand.MaxForceN > 0.0);
	TestTrue(TEXT("the kind of an effector finds its tuning"), Tuning.Effector(SandboxEffectors::KindOf(SandboxEffectors::HandLeft)) == &Tuning.Hand);
	TestTrue(TEXT("an unknown kind has none"), Tuning.Effector(TEXT("tail")) == nullptr);

	// Every listed value comes from the files, under the name it is listed with.
	TArray<TPair<FString, double>> Values;
	Tuning.ListValues(Values);
	TSet<FString> Names;
	for (const TPair<FString, double>& Value : Values)
	{
		Names.Add(Value.Key);
		double InStore = 0.0;
		TestTrue(FString::Printf(TEXT("%s is in the files"), *Value.Key), Store.FindNumber(Value.Key, FString(), InStore));
		TestEqual(FString::Printf(TEXT("%s is read as written"), *Value.Key), Value.Value, InStore);
	}
	TestEqual(TEXT("value names are unique"), Names.Num(), Values.Num());
	TestTrue(TEXT("effector values are listed"), Names.Contains(TEXT("effectors.hand.max_force_n")));
	TestTrue(TEXT("tool values are listed"), Names.Contains(TEXT("tools.impulse.impulse_ns")));

	// The module's checks guard the store.
	TestFalse(TEXT("a hand without a force cap is rejected"), Store.SetOverrides(TEXT("effectors.hand.max_force_n=0"), Error));
	TestFalse(TEXT("a rejection carries a reason"), Error.IsEmpty());
	TestFalse(TEXT("a hand without mass is rejected"), Store.SetOverrides(TEXT("effectors.hand.mass_kg=0"), Error));
	TestFalse(TEXT("a negative spring is rejected"), Store.SetOverrides(TEXT("effectors.hand.stiffness_n_per_m=-5"), Error));
	TestFalse(TEXT("a slow-motion divisor that is not whole is rejected"), Store.SetOverrides(TEXT("tools.time.slow_motion_divisor=2.5"), Error));
	TestFalse(TEXT("a wheel factor of 1 is rejected"), Store.SetOverrides(TEXT("tools.impulse.wheel_factor=1"), Error));
	TestFalse(TEXT("an unknown kind of effector is rejected"), Store.SetOverrides(TEXT("effectors.tail.mass_kg=1"), Error));

	// The player's side has no states: a value set for a state of the body changes nothing here.
	FSandboxInteractionTuning WithState;
	TestTrue(TEXT("a state's value is accepted by the store"), Store.SetOverrides(TEXT("states.falling.effectors.hand.max_force_n=5"), Error));
	TestTrue(TEXT("the tuning still reads"), FSandboxInteractionTuning::FromParams(Store, WithState, Error));
	TestTrue(TEXT("and is what the files say"), WithState.HasSameValues(Tuning));
	TestTrue(TEXT("overrides can be cleared"), Store.ClearOverrides(FString(), Error));

	TestTrue(TEXT("a valid override applies"), Store.SetOverrides(TEXT("effectors.hand.max_force_n=120,tools.impulse.impulse_ns=80"), Error));
	FSandboxInteractionTuning Changed;
	TestTrue(TEXT("the overridden tuning reads"), FSandboxInteractionTuning::FromParams(Store, Changed, Error));
	TestEqual(TEXT("effector value"), Changed.Hand.MaxForceN, 120.0);
	TestEqual(TEXT("tool value"), Changed.Tools.ImpulseNs, 80.0);
	TestFalse(TEXT("the tuning differs from the files"), Changed.HasSameValues(Tuning));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInteractionSpringCapTest, "DictatorSandbox.Interaction.Spring.CapIsTheSameInEveryDirection", InteractionTestFlags)
bool FInteractionSpringCapTest::RunTest(const FString& Parameters)
{
	// The engine caps a drive axis by axis. The caps FSandboxWorldSpring hands it must let through the
	// full force in the direction the spring pulls, and never more than the cap (plus the quarter percent
	// the smallest share costs), whatever that direction is.
	const double MaxForceN = 300.0;
	const FVector Directions[] = {
		FVector(1, 0, 0), FVector(0, -1, 0), FVector(0, 0, 1), FVector(1, 1, 0), FVector(1, -1, 1), FVector(-3, 0.1, 0.02),
		FVector(0.5, 0.5, -7), FVector(2, 3, 5), FVector(-1, -1, -1) };
	for (const FVector& Direction : Directions)
	{
		const FVector Unit = Direction.GetSafeNormal();
		for (const double DemandN : { 10.0, 300.0, 5000.0 })
		{
			const FVector Caps = FSandboxWorldSpring::AxisCapsN(Unit * DemandN, MaxForceN);
			const FString Case = FString::Printf(TEXT("direction %s, demand %g N"), *Direction.ToString(), DemandN);
			TestTrue(Case + TEXT(": the caps never add up to more than the cap"), Caps.Size() <= MaxForceN * 1.0026);
			// The cap-sized force along the demand fits inside the box of the three caps.
			const FVector Full = Unit.GetAbs() * MaxForceN;
			TestTrue(Case + TEXT(": the full force fits along the pull"), Full.X <= Caps.X + 1.0e-9 && Full.Y <= Caps.Y + 1.0e-9 && Full.Z <= Caps.Z + 1.0e-9);
			TestTrue(Case + TEXT(": every axis keeps a share"), Caps.GetMin() > 0.0);
		}
	}
	// Without a demand there is no direction: the cap is shared evenly and still adds up to itself.
	TestEqual(TEXT("no demand: the caps add up to the cap"), FSandboxWorldSpring::AxisCapsN(FVector::ZeroVector, MaxForceN).Size(), MaxForceN, 1.0e-9);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInteractionScriptedSourceTest, "DictatorSandbox.Interaction.Source.ScriptedPathsInterpolate", InteractionTestFlags)
bool FInteractionScriptedSourceTest::RunTest(const FString& Parameters)
{
	FSandboxScriptedTargetSource Source;
	FString Error;
	const FName Hand = SandboxEffectors::HandRight;

	// 60 cm in 0.2 s, a rest of 0.3 s, and back in 0.5 s. The path begins at 3 s.
	TestTrue(TEXT("a path is accepted"), Source.AddPath(Hand, 3.0, {
		Key(0.0, FVector(-70, 0, 135)), Key(0.2, FVector(-10, 0, 135)), Key(0.5, FVector(-10, 0, 135)), Key(1.0, FVector(-70, 0, 135)) }, Error));

	FSandboxEffectorTarget Target;
	TestTrue(TEXT("the effector has a target before its path begins"), Source.Evaluate(Hand, 0.0, Target));
	TestEqual(TEXT("which rests on the first key"), Target.Location, FVector(-70, 0, 135));
	TestEqual(TEXT("and does not move"), Target.Velocity, FVector::ZeroVector);

	Source.Evaluate(Hand, 3.1, Target);
	TestEqual(TEXT("half way along the first leg"), Target.Location, FVector(-40, 0, 135), 1.0e-6f);
	TestEqual(TEXT("at the leg's speed, 3 m/s"), Target.Velocity, FVector(300, 0, 0), 1.0e-6f);

	Source.Evaluate(Hand, 3.3, Target);
	TestEqual(TEXT("resting between two equal keys"), Target.Location, FVector(-10, 0, 135), 1.0e-6f);
	TestEqual(TEXT("without velocity"), Target.Velocity, FVector::ZeroVector, 1.0e-6f);

	Source.Evaluate(Hand, 3.75, Target);
	TestEqual(TEXT("half way back"), Target.Location, FVector(-40, 0, 135), 1.0e-6f);
	TestEqual(TEXT("at 1.2 m/s the other way"), Target.Velocity, FVector(-120, 0, 0), 1.0e-6f);

	Source.Evaluate(Hand, 9.0, Target);
	TestEqual(TEXT("after the path the target stays on the last key"), Target.Location, FVector(-70, 0, 135));
	TestEqual(TEXT("at rest"), Target.Velocity, FVector::ZeroVector);

	// 270 frames of 1/90 s are 3 s, give or take the last bit: the path has begun.
	double Summed = 0.0;
	for (int32 Frame = 0; Frame < 270; ++Frame)
	{
		Summed += 1.0 / 90.0;
	}
	Source.Evaluate(Hand, Summed, Target);
	TestEqual(TEXT("a time summed from frames counts as the start"), Target.Velocity, FVector(300, 0, 0), 1.0e-6f);

	// The source answers for the time its owner set.
	Source.SetTime(3.1);
	TestTrue(TEXT("GetTarget answers"), Source.GetTarget(Hand, Target));
	TestEqual(TEXT("for the time that was set"), Target.Location, FVector(-40, 0, 135), 1.0e-6f);

	// A later path takes over from the moment it begins.
	TestTrue(TEXT("a second path is accepted"), Source.AddPath(Hand, 3.6, { Key(0.0, FVector(0, 50, 100)), Key(0.4, FVector(0, 10, 100)) }, Error));
	Source.Evaluate(Hand, 3.5, Target);
	TestEqual(TEXT("before it begins the first path still plays"), Target.Location, FVector(-10, 0, 135), 1.0e-6f);
	Source.Evaluate(Hand, 3.8, Target);
	TestEqual(TEXT("then the second one does"), Target.Location, FVector(0, 30, 100), 1.0e-6f);

	TestFalse(TEXT("an effector without a path has no target"), Source.Evaluate(SandboxEffectors::HandLeft, 1.0, Target));
	TArray<FName> Names;
	Source.GetEffectors(Names);
	TestEqual(TEXT("one effector has paths"), Names.Num(), 1);

	TestFalse(TEXT("an empty path is rejected"), Source.AddPath(Hand, 0.0, {}, Error));
	TestFalse(TEXT("a path that does not begin at 0 is rejected"), Source.AddPath(Hand, 0.0, { Key(0.1, FVector::ZeroVector), Key(0.2, FVector::ZeroVector) }, Error));
	TestFalse(TEXT("keys out of order are rejected"), Source.AddPath(Hand, 0.0, { Key(0.0, FVector::ZeroVector), Key(0.5, FVector::ZeroVector), Key(0.5, FVector::ZeroVector) }, Error));
	TestFalse(TEXT("a rejection carries a reason"), Error.IsEmpty());

	TestEqual(TEXT("the kind of a hand"), SandboxEffectors::KindOf(SandboxEffectors::HandLeft), FName(TEXT("hand")));
	TestTrue(TEXT("both hands are effectors"), SandboxEffectors::IsKnown(SandboxEffectors::HandLeft) && SandboxEffectors::IsKnown(SandboxEffectors::HandRight));
	TestFalse(TEXT("a tail is not"), SandboxEffectors::IsKnown(TEXT("tail_left")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInteractionDesktopSourceTest, "DictatorSandbox.Interaction.Source.DesktopHandsReachAndReturn", InteractionTestFlags)
bool FInteractionDesktopSourceTest::RunTest(const FString& Parameters)
{
	FSandboxToolTuning Tools;
	Tools.HandRestForwardCm = 40.0;
	Tools.HandRestSideCm = 20.0;
	Tools.HandRestDownCm = 20.0;
	Tools.HandReachSpeedMps = 3.0;
	const double ReachCm = 80.0;
	const float Dt = 1.0f / 90.0f;

	// A camera 150 cm up, looking along +y: its right is -x.
	const FTransform Anchor(FRotator(0.0, 90.0, 0.0), FVector(0.0, 0.0, 150.0));
	FSandboxDesktopTargetSource Source;
	FSandboxEffectorTarget Left, Right;
	TestFalse(TEXT("no target before the first update"), Source.GetTarget(SandboxEffectors::HandRight, Right));

	Source.Update(Anchor, false, false, Tools, ReachCm, Dt, /*bContinuous*/ false);
	TestTrue(TEXT("the right hand has a target"), Source.GetTarget(SandboxEffectors::HandRight, Right));
	TestTrue(TEXT("the left hand has a target"), Source.GetTarget(SandboxEffectors::HandLeft, Left));
	TestEqual(TEXT("the right hand rests ahead, to the right and below"), Right.Location, FVector(-20.0, 40.0, 130.0), 1.0e-6f);
	TestEqual(TEXT("the left hand is its mirror image"), Left.Location, FVector(20.0, 40.0, 130.0), 1.0e-6f);
	TestEqual(TEXT("where the pawn creates it"), FSandboxDesktopTargetSource::RestPose(Anchor, /*bLeft*/ true, Tools).GetLocation(), Left.Location, 1.0e-6f);
	TestEqual(TEXT("a first target has no velocity"), Right.Velocity, FVector::ZeroVector);

	// One frame of reaching: the target moves 3 m/s toward the point straight ahead.
	Source.Update(Anchor, false, /*bReachRight*/ true, Tools, ReachCm, Dt, /*bContinuous*/ true);
	Source.GetTarget(SandboxEffectors::HandRight, Right);
	Source.GetTarget(SandboxEffectors::HandLeft, Left);
	TestEqual(TEXT("the reaching hand's target travels at the reach speed"), Right.Velocity.Size(), 300.0, 0.01);
	TestEqual(TEXT("the other hand stays"), Left.Location, FVector(20.0, 40.0, 130.0), 1.0e-6f);

	for (int32 Frame = 0; Frame < 90; ++Frame)
	{
		Source.Update(Anchor, false, true, Tools, ReachCm, Dt, true);
	}
	Source.GetTarget(SandboxEffectors::HandRight, Right);
	TestEqual(TEXT("a held reach ends straight ahead of the camera"), Right.Location, FVector(0.0, 80.0, 150.0), 1.0e-6f);
	TestEqual(TEXT("and stops there"), Right.Velocity, FVector::ZeroVector, 1.0e-6f);

	for (int32 Frame = 0; Frame < 90; ++Frame)
	{
		Source.Update(Anchor, false, false, Tools, ReachCm, Dt, true);
	}
	Source.GetTarget(SandboxEffectors::HandRight, Right);
	TestEqual(TEXT("let go, the hand comes back to rest"), Right.Location, FVector(-20.0, 40.0, 130.0), 1.0e-6f);

	// The camera moves: the hands' targets move with it, and say so.
	const FTransform Moved(FRotator(0.0, 90.0, 0.0), FVector(0.0, 9.0, 150.0));
	Source.Update(Moved, false, false, Tools, ReachCm, Dt, true);
	Source.GetTarget(SandboxEffectors::HandRight, Right);
	TestEqual(TEXT("the target carries the camera's velocity"), Right.Velocity, FVector(0.0, 810.0, 0.0), 0.1f);
	Source.Update(Anchor, false, false, Tools, ReachCm, Dt, /*bContinuous*/ false);
	Source.GetTarget(SandboxEffectors::HandRight, Right);
	TestEqual(TEXT("but not across a pause"), Right.Velocity, FVector::ZeroVector);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInteractionRecordingTest, "DictatorSandbox.Interaction.Source.RecordingComesBackBitForBit", InteractionTestFlags)
bool FInteractionRecordingTest::RunTest(const FString& Parameters)
{
	// Numbers that have no short decimal form: a recording that came back rounded would replay another motion.
	FSandboxTargetRecording Made;
	Made.Fps = 90;
	Made.Effectors = { SandboxEffectors::HandLeft, SandboxEffectors::HandRight };
	for (int32 Frame = 0; Frame < 5; ++Frame)
	{
		for (int32 Hand = 0; Hand < 2; ++Hand)
		{
			FSandboxEffectorTarget Target;
			Target.Location = FVector(1.0 / 3.0 + Frame, -70.1 * (Hand + 1), 128.0 + Frame * UE_DOUBLE_PI);
			Target.Rotation = FQuat(FVector(0.3, -0.5, 0.81).GetSafeNormal(), 0.1 + 0.37 * Frame);
			Target.Velocity = FVector(600.0 / 7.0 * Frame, 0.0, -1.0e-3 * Hand);
			Target.Grip = 0.1f * Frame;
			Made.Targets.Add(Target);
		}
	}
	const FString File = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Tests"), TEXT("recording_round_trip.json"));
	FString Error;
	FSandboxTargetRecording Read;
	if (!TestTrue(TEXT("a recording is written"), Made.Save(File, Error)) ||
		!TestTrue(TEXT("and read again"), FSandboxTargetRecording::Load(File, Read, Error)))
	{
		AddError(Error);
		return false;
	}
	TestEqual(TEXT("the frame rate comes back"), Read.Fps, Made.Fps);
	TestTrue(TEXT("the effectors come back in their order"), Read.Effectors == Made.Effectors);
	TestEqual(TEXT("every frame comes back"), Read.NumFrames(), 5);
	bool bSame = Read.Targets.Num() == Made.Targets.Num();
	for (int32 Index = 0; bSame && Index < Made.Targets.Num(); ++Index)
	{
		const FSandboxEffectorTarget& A = Made.Targets[Index];
		const FSandboxEffectorTarget& B = Read.Targets[Index];
		bSame = A.Location == B.Location && A.Velocity == B.Velocity && A.Grip == B.Grip &&
			A.Rotation.X == B.Rotation.X && A.Rotation.Y == B.Rotation.Y && A.Rotation.Z == B.Rotation.Z && A.Rotation.W == B.Rotation.W;
	}
	TestTrue(TEXT("every number comes back bit for bit"), bSame);

	// Played back: frame for frame, waiting on the first frame before the start and resting on the last after the end.
	const TSharedRef<FSandboxTargetRecording> Shared = MakeShared<FSandboxTargetRecording>(Read);
	FSandboxRecordedTargetSource Source(Shared);
	FSandboxEffectorTarget Target;
	Source.SetFrame(3);
	TestTrue(TEXT("a recorded hand has a target"), Source.GetTarget(SandboxEffectors::HandRight, Target));
	TestTrue(TEXT("frame 3 of the right hand is what was recorded"), Target.Location == Made.Targets[3 * 2 + 1].Location && Target.Velocity == Made.Targets[3 * 2 + 1].Velocity);
	Source.SetFrame(-10);
	Source.GetTarget(SandboxEffectors::HandLeft, Target);
	TestTrue(TEXT("before the start a hand waits on the first frame, at rest"), Target.Location == Made.Targets[0].Location && Target.Velocity.IsZero());
	Source.SetFrame(99);
	Source.GetTarget(SandboxEffectors::HandLeft, Target);
	TestTrue(TEXT("after the end a hand stays on the last frame, at rest"), Target.Location == Made.Targets[4 * 2].Location && Target.Velocity.IsZero());

	FSandboxTargetRecording Empty;
	TestFalse(TEXT("an empty recording has no target"), Empty.Find(SandboxEffectors::HandLeft, 0, Target));
	TestFalse(TEXT("and is not written"), Empty.Save(File, Error));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
