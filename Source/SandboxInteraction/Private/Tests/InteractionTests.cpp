#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "SandboxDesktopTargetSource.h"
#include "SandboxInteractionTuning.h"
#include "SandboxLocomotion.h"
#include "SandboxParamStore.h"
#include "SandboxScriptedTargetSource.h"
#include "SandboxTargetRecording.h"
#include "SandboxTrackedTargetSource.h"
#include "SandboxWorldSpring.h"
#include "SandboxXRControllers.h"

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

	/** Controllers that are where a test says they are. */
	class FMadeUpControllers : public ISandboxControllerTracker
	{
	public:
		TMap<FName, FSandboxControllerState> States;

		virtual bool GetController(FName Effector, FSandboxControllerState& OutState) const override
		{
			const FSandboxControllerState* Found = States.Find(Effector);
			if (Found)
			{
				OutState = *Found;
			}
			return Found != nullptr;
		}
	};
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
	TestTrue(TEXT("the player's body has a tuning of its own"), Tuning.Effector(SandboxEffectors::KindOf(SandboxEffectors::Body)) == &Tuning.Body);
	TestTrue(TEXT("which makes it an upright capsule, not a ball"), Tuning.Body.HalfHeightCm > Tuning.Body.RadiusCm);
	TestTrue(TEXT("and a hand has no half height: it stays a ball"), Tuning.Hand.HalfHeightCm == 0.0 && Tuning.Foot.HalfHeightCm == 0.0);

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
	TestEqual(TEXT("a scripted hand is a fist unless a key says otherwise"), Target.Grip, 1.0f);

	// A path whose keys open the hand and close it again.
	{
		FSandboxScriptedTargetSource Opening;
		FSandboxPathKey Fist = Key(0.0, FVector(0, 70, 128));
		FSandboxPathKey Open = Key(0.5, FVector(0, 70, 128));
		Open.Grip = 0.0f;
		FSandboxPathKey Closed = Key(1.0, FVector(0, 70, 128));
		TestTrue(TEXT("a path with grips is accepted"), Opening.AddPath(SandboxEffectors::HandLeft, 1.0, { Fist, Open, Closed }, Error));
		FSandboxEffectorTarget Gripped;
		Opening.Evaluate(SandboxEffectors::HandLeft, 0.0, Gripped);
		TestEqual(TEXT("before its path the hand has the first key's grip"), Gripped.Grip, 1.0f);
		Opening.Evaluate(SandboxEffectors::HandLeft, 1.25, Gripped);
		TestEqual(TEXT("between two keys the grip goes from the one to the other"), Gripped.Grip, 0.5f, 1.0e-6f);
		Opening.Evaluate(SandboxEffectors::HandLeft, 1.5, Gripped);
		TestEqual(TEXT("on a key it is the key's"), Gripped.Grip, 0.0f, 1.0e-6f);
		Opening.Evaluate(SandboxEffectors::HandLeft, 9.0, Gripped);
		TestEqual(TEXT("after the path it stays the last key's"), Gripped.Grip, 1.0f);
	}
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
	TestTrue(TEXT("the player's own body is an effector"), SandboxEffectors::IsKnown(SandboxEffectors::Body));
	TestEqual(TEXT("of a kind of its own"), SandboxEffectors::KindOf(SandboxEffectors::Body), FName(TEXT("body")));
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

// The desktop kick: the foot rests below a camera taken as level, goes to the point that is asked for and a
// little way through it, no further than its reach, and comes back; it has its strength while it kicks and
// for a moment after, and little of it at rest.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInteractionDesktopKickTest, "DictatorSandbox.Interaction.Source.DesktopKickGoesOutAndComesHome", InteractionTestFlags)
bool FInteractionDesktopKickTest::RunTest(const FString& Parameters)
{
	FSandboxToolTuning Tools;
	Tools.HandRestForwardCm = 40.0;
	Tools.HandRestSideCm = 20.0;
	Tools.HandRestDownCm = 20.0;
	Tools.HandReachSpeedMps = 3.0;
	Tools.KickRestForwardCm = 20.0;
	Tools.KickRestRightCm = 10.0;
	Tools.KickRestDownCm = 100.0;
	Tools.KickSpeedMps = 9.0;
	Tools.KickThroughCm = 15.0;
	Tools.KickChamberCm = 50.0;
	Tools.KickReachCm = 130.0;
	Tools.KickSettleS = 0.1;
	const float Dt = 1.0f / 90.0f;

	// A camera 150 cm up that looks along +y and 30 degrees down: the foot rests as if it looked level.
	const FTransform Anchor(FRotator(-30.0, 90.0, 0.0), FVector(0.0, 0.0, 150.0));
	const FVector Rest(-10.0, 20.0, 50.0);
	TestEqual(TEXT("the foot rests ahead, to the right and below a level camera"), FSandboxDesktopTargetSource::FootRestPose(Anchor, Tools).GetLocation(), Rest, 1.0e-4f);

	FSandboxDesktopTargetSource Source;
	Source.SetFootRestStrength(0.02);
	FSandboxEffectorTarget Foot;
	Source.Update(Anchor, false, false, Tools, 80.0, Dt, /*bContinuous*/ false);
	TestTrue(TEXT("the foot has a target"), Source.GetTarget(SandboxEffectors::FootRight, Foot));
	TestEqual(TEXT("at its rest"), Foot.Location, Rest, 1.0e-4f);
	TestEqual(TEXT("a first target is followed with the whole strength: the foot may have to come from somewhere"), Foot.Strength, 1.0f);
	for (int32 Frame = 0; Frame < 12; ++Frame)
	{
		Source.Update(Anchor, false, false, Tools, 80.0, Dt, true);
	}
	Source.GetTarget(SandboxEffectors::FootRight, Foot);
	TestEqual(TEXT("at rest, later, it is held weakly"), Foot.Strength, 0.02f, 1.0e-6f);
	TestEqual(TEXT("and does not move"), Foot.Velocity, FVector::ZeroVector, 1.0e-4f);

	// A kick at a point 100 cm ahead of the camera and 120 cm up. The foot comes up to that height 50 cm before
	// the point, (0, 50, 120), which is 76.81 cm from where it rests, and is thrust level from there to 15 cm
	// through the point, (0, 115, 120): 141.81 cm out, 10 cm a frame.
	const FVector Point(0.0, 100.0, 120.0);
	const FVector Chamber(0.0, 50.0, 120.0);
	const FVector Strike(0.0, 115.0, 120.0);
	Source.Kick(Point);
	TestTrue(TEXT("a kick is under way"), Source.IsKicking());
	Source.Kick(Point + FVector(100.0, 0.0, 0.0));
	Source.Update(Anchor, false, false, Tools, 80.0, Dt, true);
	Source.GetTarget(SandboxEffectors::FootRight, Foot);
	TestEqual(TEXT("one frame out: 10 cm along the line to where the thrust begins"), Foot.Location, Rest + (Chamber - Rest).GetSafeNormal() * 10.0, 1.0e-3f);
	TestEqual(TEXT("at the kick's speed"), Foot.Velocity.Size(), 900.0, 1.0e-1);
	TestEqual(TEXT("with the whole strength"), Foot.Strength, 1.0f);
	double FurthestCm = 0.0;
	double FurthestAheadCm = 0.0;
	bool bThrustIsLevel = true;
	int32 Frames = 1;
	while (Source.IsKicking() && Frames < 200)
	{
		Source.Update(Anchor, false, false, Tools, 80.0, Dt, true);
		Source.GetTarget(SandboxEffectors::FootRight, Foot);
		FurthestCm = FMath::Max(FurthestCm, FVector::Dist(Foot.Location, Rest));
		FurthestAheadCm = FMath::Max(FurthestAheadCm, Foot.Location.Y);
		// Past where the thrust begins the foot is on the level line through the point.
		bThrustIsLevel = bThrustIsLevel && (Foot.Location.Y <= 50.0 + 1.0e-3 || (FMath::Abs(Foot.Location.Z - 120.0) < 1.0e-3 && FMath::Abs(Foot.Location.X) < 1.0e-3));
		++Frames;
	}
	TestTrue(TEXT("it turns back 15 cm through the point, not at the second point that was asked for meanwhile"), FurthestAheadCm > 105.0 && FurthestAheadCm <= 115.0 + 1.0e-3);
	TestTrue(TEXT("which is no further from its rest than the end of the thrust"), FurthestCm <= FVector::Dist(Strike, Rest) + 1.0e-3);
	TestTrue(TEXT("the thrust is level, at the height of the point and the way the camera looks"), bThrustIsLevel);
	TestEqual(TEXT("141.81 cm out and back at 10 cm a frame"), Frames, 29);
	TestEqual(TEXT("home"), Foot.Location, Rest, 1.0e-4f);
	TestEqual(TEXT("still strong as it arrives"), Foot.Strength, 1.0f);
	for (int32 Frame = 0; Frame < 12; ++Frame)
	{
		Source.Update(Anchor, false, false, Tools, 80.0, Dt, true);
	}
	Source.GetTarget(SandboxEffectors::FootRight, Foot);
	TestEqual(TEXT("and weak again once it has settled"), Foot.Strength, 0.02f, 1.0e-6f);

	// Aimed at a wall across the room, the foot goes that way for its reach and no further.
	Source.Kick(Rest + FVector(0.0, 900.0, 0.0));
	FurthestCm = 0.0;
	Frames = 0;
	while ((Source.IsKicking() || Frames == 0) && Frames < 200)
	{
		Source.Update(Anchor, false, false, Tools, 80.0, Dt, true);
		Source.GetTarget(SandboxEffectors::FootRight, Foot);
		FurthestCm = FMath::Max(FurthestCm, FVector::Dist(Foot.Location, Rest));
		++Frames;
	}
	TestTrue(TEXT("no further than the reach"), FurthestCm > 120.0 && FurthestCm <= 130.0 + 1.0e-3);

	// A kick that is called off (a new take) is over at once, and no velocity is made of the jump home.
	Source.Kick(Point);
	Source.Update(Anchor, false, false, Tools, 80.0, Dt, true);
	Source.Update(Anchor, false, false, Tools, 80.0, Dt, true);
	Source.StopKick();
	TestFalse(TEXT("called off"), Source.IsKicking());
	Source.Update(Anchor, false, false, Tools, 80.0, Dt, true);
	Source.GetTarget(SandboxEffectors::FootRight, Foot);
	TestEqual(TEXT("the target is home"), Foot.Location, Rest, 1.0e-4f);
	TestEqual(TEXT("at rest"), Foot.Velocity, FVector::ZeroVector);

	// A rest that jumps (a view that was set) is followed with the whole strength; one that only moves is not.
	for (int32 Frame = 0; Frame < 12; ++Frame)
	{
		Source.Update(Anchor, false, false, Tools, 80.0, Dt, true);
	}
	FTransform Walked = Anchor;
	Walked.AddToTranslation(FVector(2.0, 0.0, 0.0));
	Source.Update(Walked, false, false, Tools, 80.0, Dt, true);
	Source.GetTarget(SandboxEffectors::FootRight, Foot);
	TestEqual(TEXT("a walking player's foot stays weak"), Foot.Strength, 0.02f, 1.0e-6f);
	Walked.AddToTranslation(FVector(300.0, 0.0, 0.0));
	Source.Update(Walked, false, false, Tools, 80.0, Dt, true);
	Source.GetTarget(SandboxEffectors::FootRight, Foot);
	TestEqual(TEXT("a jump is followed with the whole strength"), Foot.Strength, 1.0f);
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
			Target.Strength = 1.0f - 0.2f * Frame;
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
		bSame = A.Location == B.Location && A.Velocity == B.Velocity && A.Grip == B.Grip && A.Strength == B.Strength &&
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInteractionTrackedSourceTest, "DictatorSandbox.Interaction.Source.TrackedHandsFollowControllers", InteractionTestFlags)
bool FInteractionTrackedSourceTest::RunTest(const FString& Parameters)
{
	// The whole way from a controller's pose to a hand's target, on poses made up here: no headset is asked.
	const float Dt = 1.0f / 90.0f;
	FMadeUpControllers Controllers;
	FSandboxTrackedTargetSource Source;
	FSandboxEffectorTuning Hand;
	FSandboxToolTuning Tools;
	Tools.HeadsetMaxHandSpeedMps = 20.0;
	Tools.HeadsetCatchUpSpeedMps = 1.5;
	Tools.HeadsetRuntimeVelocityShare = 1.0;
	Tools.HeadsetEstimatedFollowS = 0.0;
	FSandboxEffectorTarget Left, Right;

	// The play space: its middle at (100, 200) on the floor, facing +y, so that its right is -x.
	const FTransform Origin(FRotator(0.0, 90.0, 0.0), FVector(100.0, 200.0, 0.0));

	Source.Update(Controllers, Origin, Hand, Tools, Dt, /*bContinuous*/ false);
	TestFalse(TEXT("no target before a controller has a pose"), Source.GetTarget(SandboxEffectors::HandRight, Right));
	TestFalse(TEXT("and the hand is not tracked"), Source.IsTracked(SandboxEffectors::HandRight));
	TestTrue(TEXT("a tail has no stats"), Source.GetStats(TEXT("tail_left")) == nullptr);

	// The right controller 30 cm ahead, 20 to the right, 120 up, moving ahead at 6 m/s, half squeezed.
	FSandboxControllerState State;
	State.bTracked = true;
	State.Location = FVector(30.0, 20.0, 120.0);
	State.bHasLinearVelocity = true;
	State.LinearVelocity = FVector(600.0, 0.0, 0.0);
	State.bHasAngularVelocity = true;
	State.Squeeze = 0.4f;
	Controllers.States.Add(SandboxEffectors::HandRight, State);
	Source.Update(Controllers, Origin, Hand, Tools, Dt, false);
	TestTrue(TEXT("a tracked controller gives its hand a target"), Source.GetTarget(SandboxEffectors::HandRight, Right));
	TestFalse(TEXT("the other hand still has none"), Source.GetTarget(SandboxEffectors::HandLeft, Left));
	TestEqual(TEXT("the pose is carried into the world through the play space's origin"), Right.Location, FVector(80.0, 230.0, 120.0), 1.0e-6f);
	TestEqual(TEXT("the runtime's velocity is turned with it, from the first frame on"), Right.Velocity, FVector(0.0, 600.0, 0.0), 1.0e-6f);
	TestTrue(TEXT("and so is the orientation"), Right.Rotation.Equals(Origin.GetRotation(), 1.0e-9));
	TestEqual(TEXT("the grip is the squeeze"), Right.Grip, 0.4f);
	TestTrue(TEXT("the hand is tracked"), Source.IsTracked(SandboxEffectors::HandRight));

	// The hand's body off the grip pose: the same three numbers put the two hands at mirrored places.
	Hand.OffsetThumbCm = 2.0;
	Hand.OffsetPalmCm = 3.0;
	Hand.OffsetKnucklesCm = 5.0;
	TestEqual(TEXT("the right hand's body in the grip pose's axes"), FSandboxTrackedTargetSource::BodyOffset(Hand, /*bLeft*/ false), FVector(2.0, -3.0, -5.0));
	TestEqual(TEXT("the left hand's is its mirror image"), FSandboxTrackedTargetSource::BodyOffset(Hand, /*bLeft*/ true), FVector(2.0, 3.0, -5.0));

	// The hand's own frame is the grip pose's turned by the tuning: pitched down a quarter turn, the hand's x
	// (the fingers) lies along the grip pose's -z and its z (the thumb) along the grip pose's x.
	{
		FSandboxEffectorTuning Turned;
		Turned.GripPitchDeg = -90.0;
		FSandboxTrackedTargetSource Framed;
		FSandboxControllerState Held;
		Held.bTracked = true;
		Held.bHasLinearVelocity = true;
		Held.bHasAngularVelocity = true;
		Controllers.States.Add(SandboxEffectors::HandRight, Held);
		Framed.Update(Controllers, FTransform::Identity, Turned, Tools, Dt, false);
		FSandboxEffectorTarget Hand2;
		Framed.GetTarget(SandboxEffectors::HandRight, Hand2);
		TestEqual(TEXT("the hand's fingers lie along the grip pose's -z"), Hand2.Rotation.GetAxisX(), FVector(0.0, 0.0, -1.0), 1.0e-6f);
		TestEqual(TEXT("its thumb along the grip pose's x"), Hand2.Rotation.GetAxisZ(), FVector(1.0, 0.0, 0.0), 1.0e-6f);
		TestEqual(TEXT("and the palm's normal stays the grip pose's y"), Hand2.Rotation.GetAxisY(), FVector(0.0, 1.0, 0.0), 1.0e-6f);
	}

	// A controller that turns on the spot moves a body that is off its grip pose: v = w x r.
	FSandboxTrackedTargetSource Turning;
	State = FSandboxControllerState();
	State.bTracked = true;
	State.Location = FVector(0.0, 0.0, 100.0);
	State.bHasLinearVelocity = true;
	State.bHasAngularVelocity = true;
	State.AngularVelocity = FVector(0.0, 0.0, 2.0);
	Controllers.States.Add(SandboxEffectors::HandRight, State);
	Turning.Update(Controllers, FTransform::Identity, Hand, Tools, Dt, false);
	Turning.GetTarget(SandboxEffectors::HandRight, Right);
	TestEqual(TEXT("the body is where the offset puts it"), Right.Location, FVector(2.0, -3.0, 95.0), 1.0e-6f);
	TestEqual(TEXT("and moves as the turning controller carries it"), Right.Velocity, FVector(6.0, 4.0, 0.0), 1.0e-6f);

	// The velocity handed to the spring is the velocity the target really has: a controller that flies and
	// turns for a short step moves the target by that velocity times the step. It ties the sense of the
	// angular velocity to the sense of the rotations, about an axis that is none of the three.
	{
		const FVector Spin = FVector(1.0, -2.0, 0.5).GetSafeNormal() * 9.0;
		const FVector Flight(250.0, -400.0, 120.0);
		const FQuat StartRotation(FVector(0.2, 0.9, -0.3).GetSafeNormal(), 0.7);
		const float Step = 1.0e-4f;
		const FTransform Tilted(FRotator(0.0, -35.0, 0.0), FVector(-40.0, 15.0, 0.0));
		FSandboxTrackedTargetSource Flying;
		State = FSandboxControllerState();
		State.bTracked = true;
		State.Location = FVector(10.0, 20.0, 130.0);
		State.Rotation = StartRotation;
		State.bHasLinearVelocity = true;
		State.LinearVelocity = Flight;
		State.bHasAngularVelocity = true;
		State.AngularVelocity = Spin;
		Controllers.States.Add(SandboxEffectors::HandLeft, State);
		Flying.Update(Controllers, Tilted, Hand, Tools, Step, false);
		FSandboxEffectorTarget Before, After;
		Flying.GetTarget(SandboxEffectors::HandLeft, Before);
		State.Location += Flight * Step;
		State.Rotation = FQuat(Spin.GetSafeNormal(), Spin.Size() * Step) * StartRotation;
		Controllers.States.Add(SandboxEffectors::HandLeft, State);
		Flying.Update(Controllers, Tilted, Hand, Tools, Step, true);
		Flying.GetTarget(SandboxEffectors::HandLeft, After);
		const FVector Moved = (After.Location - Before.Location) / Step;
		TestEqual(TEXT("the target moves by the velocity it reports"), Moved, (Before.Velocity + After.Velocity) * 0.5, 0.05f);
		TestTrue(TEXT("of which the turning is a part worth testing"), FVector::Dist(Before.Velocity, Tilted.GetRotation().RotateVector(Flight)) > 30.0);
	}

	// A runtime that gives no velocity: the difference of two poses stands in, and the frames are counted.
	{
		FSandboxTrackedTargetSource Differenced;
		FSandboxEffectorTuning Plain;
		Plain.OffsetKnucklesCm = 10.0;
		State = FSandboxControllerState();
		State.bTracked = true;
		State.Location = FVector(0.0, 0.0, 100.0);
		Controllers.States.Add(SandboxEffectors::HandRight, State);
		Differenced.Update(Controllers, FTransform::Identity, Plain, Tools, Dt, false);
		Differenced.GetTarget(SandboxEffectors::HandRight, Right);
		TestEqual(TEXT("one pose has no velocity"), Right.Velocity, FVector::ZeroVector);
		State.Location = FVector(1.0, 0.0, 100.0);
		Controllers.States.Add(SandboxEffectors::HandRight, State);
		Differenced.Update(Controllers, FTransform::Identity, Plain, Tools, Dt, true);
		Differenced.GetTarget(SandboxEffectors::HandRight, Right);
		TestEqual(TEXT("two poses a step apart give one"), Right.Velocity, FVector(90.0, 0.0, 0.0), 1.0e-3f);
		// A turn of 0.01 rad about y in one step is 0.9 rad/s, and the body is 10 cm along -z of the grip pose:
		// w x r = (0, 0.9, 0) x (0, 0, -10) = (-9, 0, 0), but for the 0.01 rad the arm has turned by.
		State.Rotation = FQuat(FVector(0.0, 1.0, 0.0), 0.01);
		Controllers.States.Add(SandboxEffectors::HandRight, State);
		Differenced.Update(Controllers, FTransform::Identity, Plain, Tools, Dt, true);
		Differenced.GetTarget(SandboxEffectors::HandRight, Right);
		TestEqual(TEXT("a turn between two poses moves the body off the grip pose"), Right.Velocity, FVector(-9.0, 0.0, 0.0), 0.1f);
		Differenced.Update(Controllers, FTransform::Identity, Plain, Tools, Dt, /*bContinuous*/ false);
		Differenced.GetTarget(SandboxEffectors::HandRight, Right);
		TestEqual(TEXT("but not across a pause"), Right.Velocity, FVector::ZeroVector);
		const FSandboxTrackedHandStats* Stats = Differenced.GetStats(SandboxEffectors::HandRight);
		TestTrue(TEXT("every frame without the runtime's velocity is counted"), Stats && Stats->TrackedFrames == 4 && Stats->NoLinearVelocityFrames == 4 && Stats->NoAngularVelocityFrames == 4);

		// Lost: the hand stays where it was last seen, at rest, and a pose that comes back is a first pose again.
		const FVector Seen = Right.Location;
		State.bTracked = false;
		Controllers.States.Add(SandboxEffectors::HandRight, State);
		Differenced.Update(Controllers, FTransform::Identity, Plain, Tools, Dt, true);
		TestTrue(TEXT("a lost controller's hand keeps a target"), Differenced.GetTarget(SandboxEffectors::HandRight, Right));
		TestEqual(TEXT("where it was last seen"), Right.Location, Seen);
		TestEqual(TEXT("at rest"), Right.Velocity, FVector::ZeroVector);
		TestFalse(TEXT("and is not tracked"), Differenced.IsTracked(SandboxEffectors::HandRight));
		// A pose the runtime gives without tracking the controller is its guess: not followed, and counted.
		State.bEstimated = true;
		State.Location = FVector(300.0, 0.0, 100.0);
		Controllers.States.Add(SandboxEffectors::HandRight, State);
		Differenced.Update(Controllers, FTransform::Identity, Plain, Tools, Dt, true);
		Differenced.GetTarget(SandboxEffectors::HandRight, Right);
		TestEqual(TEXT("a pose that is not tracked is not followed"), Right.Location, Seen);
		TestTrue(TEXT("the lost frames are counted, and the guessed pose among them"), Stats && Stats->LostFrames == 2 && Stats->EstimatedFrames == 1 && Stats->TrackedFrames == 4);

		// Found again 49 cm away, at rest: the target does not jump there. It goes at the catch-up speed, says
		// so in its velocity, and follows again once it has arrived.
		State.bTracked = true;
		State.bEstimated = false;
		State.Rotation = FQuat::Identity;
		State.Location = Seen + FVector(49.0, 0.0, 10.0);
		Controllers.States.Add(SandboxEffectors::HandRight, State);
		const FQuat TurnBefore = Right.Rotation;
		Differenced.Update(Controllers, FTransform::Identity, Plain, Tools, Dt, true);
		Differenced.GetTarget(SandboxEffectors::HandRight, Right);
		TestTrue(TEXT("found again, the target is catching up"), Differenced.IsCatchingUp(SandboxEffectors::HandRight));
		TestTrue(TEXT("and its turn has gone a step of the way, not all of it"), !Right.Rotation.Equals(FQuat::Identity, 1.0e-6) && !Right.Rotation.Equals(TurnBefore, 1.0e-6));
		TestEqual(TEXT("it has moved one step at the catch-up speed"), FVector::Dist(Right.Location, Seen), 150.0 / 90.0, 1.0e-6);
		TestEqual(TEXT("and reports that speed, not one from before it was lost"), Right.Velocity.Size(), 150.0, 1.0e-6);
		int32 Frames = 1;
		while (Differenced.IsCatchingUp(SandboxEffectors::HandRight) && Frames < 200)
		{
			Differenced.Update(Controllers, FTransform::Identity, Plain, Tools, Dt, true);
			++Frames;
		}
		Differenced.GetTarget(SandboxEffectors::HandRight, Right);
		TestEqual(TEXT("it arrives after the distance over the speed: 49 cm at 1.67 cm a frame"), Frames, 30);
		TestEqual(TEXT("on the controller"), Right.Location, Seen + FVector(49.0, 0.0, 0.0), 1.0e-6f);
		TestTrue(TEXT("turned as the controller is"), Right.Rotation.Equals(FQuat::Identity, 1.0e-9));
		TestEqual(TEXT("at rest, as the controller is"), Right.Velocity, FVector::ZeroVector, 1.0e-6f);
		TestTrue(TEXT("one catch-up is counted"), Stats && Stats->CatchUps == 1);

		// A tracked pose that moves further in a frame than a hand does has jumped: the same catching up.
		State.Location += FVector(0.0, 116.0, 0.0);
		Controllers.States.Add(SandboxEffectors::HandRight, State);
		Differenced.Update(Controllers, FTransform::Identity, Plain, Tools, Dt, true);
		Differenced.GetTarget(SandboxEffectors::HandRight, Right);
		TestTrue(TEXT("a jump of the tracking is caught up with, not followed"), Differenced.IsCatchingUp(SandboxEffectors::HandRight) && Right.Velocity.Size() < 2000.0);
		// A fast hand is not a jump: 12 m/s is 13 cm in a frame.
		FSandboxTrackedTargetSource Fast;
		State = FSandboxControllerState();
		State.bTracked = true;
		State.bHasLinearVelocity = true;
		State.bHasAngularVelocity = true;
		State.LinearVelocity = FVector(1200.0, 0.0, 0.0);
		Controllers.States.Add(SandboxEffectors::HandRight, State);
		Fast.Update(Controllers, FTransform::Identity, Plain, Tools, Dt, false);
		State.Location = FVector(1200.0 / 90.0, 0.0, 0.0);
		Controllers.States.Add(SandboxEffectors::HandRight, State);
		Fast.Update(Controllers, FTransform::Identity, Plain, Tools, Dt, true);
		Fast.GetTarget(SandboxEffectors::HandRight, Right);
		TestTrue(TEXT("a punch at 12 m/s is followed as it is"), !Fast.IsCatchingUp(SandboxEffectors::HandRight) && Right.Velocity.Equals(FVector(1200.0, 0.0, 0.0), 1.0e-6));

		// The velocity can be taken from the poses though the runtime gives one: all of it, or a share.
		FSandboxToolTuning FromPoses = Tools;
		FromPoses.HeadsetRuntimeVelocityShare = 0.25;
		FSandboxTrackedTargetSource Shared;
		State = FSandboxControllerState();
		State.bTracked = true;
		State.bHasLinearVelocity = true;
		State.bHasAngularVelocity = true;
		State.LinearVelocity = FVector(400.0, 0.0, 0.0);
		Controllers.States.Add(SandboxEffectors::HandRight, State);
		Shared.Update(Controllers, FTransform::Identity, FSandboxEffectorTuning(), FromPoses, Dt, false);
		Shared.GetTarget(SandboxEffectors::HandRight, Right);
		TestEqual(TEXT("with one pose the runtime's velocity is all there is"), Right.Velocity, FVector(400.0, 0.0, 0.0), 1.0e-6f);
		State.Location = FVector(0.0, 2.0, 0.0);
		Controllers.States.Add(SandboxEffectors::HandRight, State);
		Shared.Update(Controllers, FTransform::Identity, FSandboxEffectorTuning(), FromPoses, Dt, true);
		Shared.GetTarget(SandboxEffectors::HandRight, Right);
		TestEqual(TEXT("with two, a quarter of the runtime's and three quarters of the poses' difference"), Right.Velocity, FVector(100.0, 135.0, 0.0), 1.0e-3f);

		// A pose the runtime only guesses is followed for a moment after the controller was tracked, and no longer.
		{
			FSandboxToolTuning Patient = Tools;
			Patient.HeadsetEstimatedFollowS = 2.5f * Dt;
			FSandboxTrackedTargetSource Swing;
			FSandboxControllerState Seen2;
			Seen2.bTracked = true;
			Seen2.bHasLinearVelocity = true;
			Seen2.bHasAngularVelocity = true;
			Controllers.States.Add(SandboxEffectors::HandRight, Seen2);
			Swing.Update(Controllers, FTransform::Identity, FSandboxEffectorTuning(), Patient, Dt, false);
			Seen2.bTracked = false;
			Seen2.bEstimated = true;
			FSandboxEffectorTarget Followed;
			for (int32 Frame = 1; Frame <= 4; ++Frame)
			{
				Seen2.Location = FVector(5.0 * Frame, 0.0, 0.0);
				Controllers.States.Add(SandboxEffectors::HandRight, Seen2);
				Swing.Update(Controllers, FTransform::Identity, FSandboxEffectorTuning(), Patient, Dt, true);
				Swing.GetTarget(SandboxEffectors::HandRight, Followed);
				if (Frame <= 2)
				{
					TestEqual(FString::Printf(TEXT("guessed frame %d is followed"), Frame), Followed.Location, Seen2.Location, 1.0e-6f);
				}
			}
			TestEqual(TEXT("after the moment the hand stays where the last followed pose had it"), Followed.Location, FVector(10.0, 0.0, 0.0), 1.0e-6f);
			const FSandboxTrackedHandStats* SwingStats = Swing.GetStats(SandboxEffectors::HandRight);
			TestTrue(TEXT("two guessed frames followed, two lost"), SwingStats && SwingStats->EstimatedFollowedFrames == 2 && SwingStats->LostFrames == 2 && SwingStats->EstimatedFrames == 2);
		}

		// A hand that waits somewhere before its controller is first tracked is taken to it the same way.
		FSandboxTrackedTargetSource Waiting;
		Waiting.SetWaitingPose(SandboxEffectors::HandLeft, FTransform(FVector(0.0, 0.0, 100.0)));
		TestTrue(TEXT("a waiting hand has its waiting place for a target"), Waiting.GetTarget(SandboxEffectors::HandLeft, Left) && Left.Location == FVector(0.0, 0.0, 100.0));
		State = FSandboxControllerState();
		State.bTracked = true;
		State.Location = FVector(30.0, 0.0, 100.0);
		Controllers.States.Add(SandboxEffectors::HandLeft, State);
		Waiting.Update(Controllers, FTransform::Identity, FSandboxEffectorTuning(), Tools, Dt, false);
		Waiting.GetTarget(SandboxEffectors::HandLeft, Left);
		TestTrue(TEXT("and catches up with the first pose from there"), Waiting.IsCatchingUp(SandboxEffectors::HandLeft) && Left.Location.Equals(FVector(150.0 / 90.0, 0.0, 100.0), 1.0e-6));
	}

	// What a controller vibrates at: in proportion to the force its hand presses with, full at the tuning's force.
	TestEqual(TEXT("a hand that touches nothing does not vibrate"), FSandboxXRControllers::HapticAmplitude(0.0, 300.0), 0.0f);
	TestEqual(TEXT("half the full force is half the strength"), FSandboxXRControllers::HapticAmplitude(150.0, 300.0), 0.5f);
	TestEqual(TEXT("a blow beyond the full force is full strength"), FSandboxXRControllers::HapticAmplitude(1270.0, 300.0), 1.0f);

	// The squeeze axis of a controller is not held to 0..1 by anybody else.
	State = FSandboxControllerState();
	State.bTracked = true;
	State.Squeeze = 1.7f;
	Controllers.States.Add(SandboxEffectors::HandLeft, State);
	Source.Update(Controllers, Origin, Hand, Tools, Dt, true);
	Source.GetTarget(SandboxEffectors::HandLeft, Left);
	TestEqual(TEXT("a grip is at most closed"), Left.Grip, 1.0f);
	State.Squeeze = -0.2f;
	Controllers.States.Add(SandboxEffectors::HandLeft, State);
	Source.Update(Controllers, Origin, Hand, Tools, Dt, true);
	Source.GetTarget(SandboxEffectors::HandLeft, Left);
	TestEqual(TEXT("and at least open"), Left.Grip, 0.0f);
	const FSandboxTrackedHandStats* LeftStats = Source.GetStats(SandboxEffectors::HandLeft);
	TestTrue(TEXT("the most a grip was closed is kept"), LeftStats && LeftStats->MaxGrip == 1.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInteractionTrackedPlaySpaceTest, "DictatorSandbox.Interaction.Source.TrackedHandsRideThePlaySpace", InteractionTestFlags)
bool FInteractionTrackedPlaySpaceTest::RunTest(const FString& Parameters)
{
	// A play space that moves carries the hands with it, and one that jumps (a snap turn) is caught up with.
	const float Dt = 1.0f / 90.0f;
	FMadeUpControllers Controllers;
	FSandboxToolTuning Tools;
	Tools.HeadsetMaxHandSpeedMps = 20.0;
	Tools.HeadsetCatchUpSpeedMps = 1.5;
	Tools.HeadsetRuntimeVelocityShare = 1.0;
	FSandboxControllerState State;
	State.bTracked = true;
	State.Location = FVector(30.0, 0.0, 120.0);
	State.bHasLinearVelocity = true;
	State.bHasAngularVelocity = true;
	Controllers.States.Add(SandboxEffectors::HandRight, State);
	FSandboxEffectorTarget Right;

	FSandboxPlaySpaceMotion Walking;
	Walking.VelocityCmS = FVector(140.0, 0.0, 0.0);
	FSandboxTrackedTargetSource Source;
	Source.Update(Controllers, FTransform::Identity, FSandboxEffectorTuning(), Tools, Dt, false, Walking);
	Source.GetTarget(SandboxEffectors::HandRight, Right);
	TestEqual(TEXT("a still controller's hand moves with the walking play space"), Right.Velocity, FVector(140.0, 0.0, 0.0), 1.0e-6f);

	// Turned by 30 degrees about the middle, the hand's place jumps 15.5 cm: less than a hand moves in a frame, so only
	// the play space's word that it jumped stops the hand from being flung after it.
	const FTransform Turned(FRotator(0.0, 30.0, 0.0), FVector::ZeroVector);
	{
		FSandboxTrackedTargetSource Unflagged = Source;
		Unflagged.Update(Controllers, Turned, FSandboxEffectorTuning(), Tools, Dt, true);
		Unflagged.GetTarget(SandboxEffectors::HandRight, Right);
		TestFalse(TEXT("unflagged, a jump of 15 cm would be followed at once"), Unflagged.IsCatchingUp(SandboxEffectors::HandRight));
	}
	FSandboxPlaySpaceMotion Snapped;
	Snapped.bJumped = true;
	const FVector Before = FVector(30.0, 0.0, 120.0);
	Source.Update(Controllers, Turned, FSandboxEffectorTuning(), Tools, Dt, true, Snapped);
	Source.GetTarget(SandboxEffectors::HandRight, Right);
	TestTrue(TEXT("a snap of the play space is caught up with"), Source.IsCatchingUp(SandboxEffectors::HandRight));
	TestEqual(TEXT("one step at the catch-up speed"), FVector::Dist(Right.Location, Before), 150.0 / 90.0, 1.0e-6);

	// The player's own body: the source says where it is to be once it has been told.
	FSandboxTrackedTargetSource Fresh;
	FSandboxEffectorTarget Body;
	TestFalse(TEXT("no body before it is told of one"), Fresh.GetTarget(SandboxEffectors::Body, Body));
	FSandboxEffectorTarget Told;
	Told.Location = FVector(1.0, 2.0, 82.0);
	Told.Velocity = FVector(140.0, 0.0, 0.0);
	Fresh.SetBodyTarget(Told);
	TestTrue(TEXT("and then where it was told"), Fresh.GetTarget(SandboxEffectors::Body, Body) && Body.Location == Told.Location && Body.Velocity == Told.Velocity);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInteractionLocomotionTest, "DictatorSandbox.Interaction.Locomotion.StickTurnAndBody", InteractionTestFlags)
bool FInteractionLocomotionTest::RunTest(const FString& Parameters)
{
	// The player's movement in a headset, on made-up sticks and heads: no headset, no world.
	const float Dt = 1.0f / 90.0f;
	FSandboxToolTuning Tools;
	Tools.HeadsetMoveSpeedMps = 1.4;
	Tools.HeadsetMoveDeadZone = 0.15;
	Tools.HeadsetMoveEaseS = 0.2;
	Tools.HeadsetSnapTurnDeg = 30.0;
	Tools.HeadsetSnapPress = 0.7;
	Tools.HeadsetSnapRelease = 0.3;
	Tools.HeadsetBodyLeanCm = 20.0;
	Tools.HeadsetBodyFollowSpeedMps = 2.0;
	Tools.HeadsetBodyFloorGapCm = 2.0;
	Tools.HeadsetBodyBlockedN = 100.0;
	Tools.HeadsetBodyStoppedN = 250.0;
	FSandboxToolTuning AtOnce = Tools;
	AtOnce.HeadsetMoveEaseS = 0.0;
	FSandboxEffectorTuning Body;
	Body.RadiusCm = 22.0;
	Body.HalfHeightCm = 80.0;

	// The dead zone, and the tilt past it.
	TestEqual(TEXT("inside the dead zone the stick is nothing"), FSandboxLocomotion::PastDeadZone(FVector2D(0.1, 0.05), 0.15), FVector2D::ZeroVector);
	TestTrue(TEXT("at full tilt it is all of it"), FSandboxLocomotion::PastDeadZone(FVector2D(0.0, 1.0), 0.15).Equals(FVector2D(0.0, 1.0), 1.0e-9));
	TestTrue(TEXT("halfway past the dead zone it is half, in the stick's own direction"),
		FSandboxLocomotion::PastDeadZone(FVector2D(0.575, 0.575) / FMath::Sqrt(2.0), 0.15).Equals(FVector2D(0.5, 0.5) / FMath::Sqrt(2.0), 1.0e-9));
	TestEqual(TEXT("a stick that reads beyond its full tilt is held to it"), FSandboxLocomotion::PastDeadZone(FVector2D(1.0, 1.0), 0.15).Size(), 1.0, 1.0e-9);

	// A head 30 cm ahead and 40 to the right of the play space's middle, looking left and a little down, is put over the
	// stand point looking along +y.
	{
		const FVector HeadLocal(30.0, 40.0, 170.0);
		const FQuat HeadTurn = FRotator(-10.0, -90.0, 0.0).Quaternion();
		const FTransform Placed = FSandboxLocomotion::PlaceHead(HeadLocal, HeadTurn, FVector(100.0, 200.0, 0.0), 90.0);
		const FVector Head = Placed.TransformPositionNoScale(HeadLocal);
		TestEqual(TEXT("the head stands over the stand point"), FVector(Head.X, Head.Y, 0.0), FVector(100.0, 200.0, 0.0), 1.0e-6f);
		TestEqual(TEXT("at its own height"), Head.Z, 170.0, 1.0e-6);
		TestEqual(TEXT("looking the way asked"), FSandboxLocomotion::LevelForward(Placed.GetRotation() * HeadTurn), FVector(0.0, 1.0, 0.0), 1.0e-6f);
	}

	// Walking: the head looks along +x. The speed grows to full in 0.2 s and falls back in as long, and the body's target
	// goes along with the play space.
	FSandboxLocomotionInput Input;
	Input.HeadLocation = FVector(0.0, 0.0, 170.0);
	Input.MoveStick = FVector2D(0.0, 1.0);
	FSandboxLocomotion Walk;
	Walk.Reset(FTransform::Identity, Input.HeadLocation);
	Walk.Update(Input, Tools, Dt);
	TestEqual(TEXT("one frame of easing is a ninetieth of 0.2 s's way to full speed"), Walk.GetOriginVelocity(), FVector(140.0 / 18.0, 0.0, 0.0), 1.0e-6f);
	for (int32 Frame = 2; Frame <= 18; ++Frame)
	{
		Walk.Update(Input, Tools, Dt);
	}
	TestEqual(TEXT("full speed after 0.2 s"), Walk.GetOriginVelocity(), FVector(140.0, 0.0, 0.0), 1.0e-6f);
	TestEqual(TEXT("having gone as far as the easing says"), Walk.GetOrigin().GetLocation().X, 140.0 * 19.0 / 180.0, 1.0e-6);
	const FSandboxEffectorTarget Carried = Walk.GetBodyTarget(Body, Tools);
	TestEqual(TEXT("the body's target went with it, its bottom 2 cm above the floor"), Carried.Location, FVector(140.0 * 19.0 / 180.0, 0.0, 82.0), 1.0e-6f);
	TestEqual(TEXT("and moves as it does"), Carried.Velocity, FVector(140.0, 0.0, 0.0), 1.0e-6f);
	TestEqual(TEXT("the speed is all of the full speed"), Walk.GetSpeedShare(Tools), 1.0, 1.0e-9);
	Input.MoveStick = FVector2D::ZeroVector;
	for (int32 Frame = 1; Frame <= 18; ++Frame)
	{
		Walk.Update(Input, Tools, Dt);
	}
	TestEqual(TEXT("let go, the stick brings the player to rest in 0.2 s"), Walk.GetOriginVelocity(), FVector::ZeroVector, 1.0e-6f);
	// 14.78 cm speeding up and 13.22 slowing down: as far as 0.2 s at full speed.
	TestEqual(TEXT("the walk is counted"), Walk.GetStats().WalkedCm, 140.0 * 18.0 / 90.0, 1.0e-6);

	// Relative to the head: looking along +y and almost straight down, right is -x; straight down, forward is +y still.
	{
		FSandboxLocomotion Side;
		Side.Reset(FTransform::Identity, FVector(0.0, 0.0, 170.0));
		FSandboxLocomotionInput Looking;
		Looking.HeadLocation = FVector(0.0, 0.0, 170.0);
		Looking.HeadRotation = FRotator(-85.0, 90.0, 0.0).Quaternion();
		Looking.MoveStick = FVector2D(1.0, 0.0);
		Side.Update(Looking, AtOnce, Dt);
		TestEqual(TEXT("the right of a head that looks along +y is -x"), Side.GetOriginVelocity(), FVector(-140.0, 0.0, 0.0), 1.0e-6f);
		Looking.HeadRotation = FRotator(-90.0, 90.0, 0.0).Quaternion();
		Looking.MoveStick = FVector2D(0.0, 1.0);
		Side.Update(Looking, AtOnce, Dt);
		TestEqual(TEXT("a head that looks straight down goes the way its face is turned"), Side.GetOriginVelocity(), FVector(0.0, 140.0, 0.0), 1.0e-6f);
	}

	// A snap turn, about a head that is 50 cm off the play space's middle: the head stays where it is in the world.
	{
		FSandboxLocomotion Turning;
		FSandboxLocomotionInput Snap;
		Snap.HeadLocation = FVector(50.0, 0.0, 170.0);
		Turning.Reset(FTransform::Identity, Snap.HeadLocation);
		const FVector HeadBefore = Turning.GetOrigin().TransformPositionNoScale(Snap.HeadLocation);
		const FVector BodyBefore = Turning.GetBodyTarget(Body, Tools).Location;
		Snap.TurnStick = FVector2D(0.8, 0.0);
		Turning.Update(Snap, Tools, Dt);
		TestTrue(TEXT("a tilt past the press turns"), Turning.HasSnapped());
		TestEqual(TEXT("the head stays where it was in the world"), Turning.GetOrigin().TransformPositionNoScale(Snap.HeadLocation), HeadBefore, 1.0e-6f);
		TestEqual(TEXT("and looks 30 degrees to the right"), FSandboxLocomotion::LevelForward(Turning.GetOrigin().GetRotation()),
			FVector(FMath::Cos(FMath::DegreesToRadians(30.0)), FMath::Sin(FMath::DegreesToRadians(30.0)), 0.0), 1.0e-6f);
		TestEqual(TEXT("the body's target does not move with a turn"), Turning.GetBodyTarget(Body, Tools).Location, BodyBefore, 1.0e-6f);
		Turning.Update(Snap, Tools, Dt);
		TestFalse(TEXT("held, the stick turns once"), Turning.HasSnapped());
		Snap.TurnStick = FVector2D(0.5, 0.0);
		Turning.Update(Snap, Tools, Dt);
		Snap.TurnStick = FVector2D(-0.9, 0.0);
		Turning.Update(Snap, Tools, Dt);
		TestFalse(TEXT("not back under the release, it does not turn again"), Turning.HasSnapped());
		Snap.TurnStick = FVector2D(0.2, 0.0);
		Turning.Update(Snap, Tools, Dt);
		Snap.TurnStick = FVector2D(-0.9, 0.0);
		Turning.Update(Snap, Tools, Dt);
		TestTrue(TEXT("back under it, a tilt the other way turns the other way"), Turning.HasSnapped());
		TestEqual(TEXT("back to looking along +x"), FSandboxLocomotion::LevelForward(Turning.GetOrigin().GetRotation()), FVector(1.0, 0.0, 0.0), 1.0e-6f);
		TestTrue(TEXT("one turn each way is counted"), Turning.GetStats().SnapsRight == 1 && Turning.GetStats().SnapsLeft == 1);
	}

	// The body stays while the head leans, and follows it beyond the lean, no faster than 2 m/s.
	{
		FSandboxLocomotion Lean;
		FSandboxLocomotionInput Leaning;
		Leaning.HeadLocation = FVector(0.0, 0.0, 170.0);
		Lean.Reset(FTransform::Identity, Leaning.HeadLocation);
		Leaning.HeadLocation = FVector(15.0, 0.0, 170.0);
		Lean.Update(Leaning, Tools, Dt);
		TestEqual(TEXT("a lean of 15 cm leaves the body where it is"), Lean.GetBodyTarget(Body, Tools).Location.X, 0.0, 1.0e-9);
		Leaning.HeadLocation = FVector(35.0, 0.0, 170.0);
		Lean.Update(Leaning, Tools, Dt);
		TestEqual(TEXT("a head further away draws the body a walk's step"), Lean.GetBodyTarget(Body, Tools).Location.X, 200.0 / 90.0, 1.0e-6);
		TestEqual(TEXT("and says how fast"), Lean.GetBodyTarget(Body, Tools).Velocity, FVector(200.0, 0.0, 0.0), 1.0e-6f);
		for (int32 Frame = 0; Frame < 20; ++Frame)
		{
			Lean.Update(Leaning, Tools, Dt);
		}
		TestEqual(TEXT("until the head is a lean away from it"), Lean.GetBodyTarget(Body, Tools).Location.X, 15.0, 1.0e-6);
		TestEqual(TEXT("and then it rests"), Lean.GetBodyTarget(Body, Tools).Velocity, FVector::ZeroVector, 1.0e-6f);
	}

	// Pressed back: the body is pushed toward -x while the player walks along +x.
	{
		FSandboxLocomotion Blocked;
		FSandboxLocomotionInput Pressed;
		Pressed.HeadLocation = FVector(0.0, 0.0, 170.0);
		Pressed.MoveStick = FVector2D(0.0, 1.0);
		Pressed.bHasBody = true;
		Pressed.BodyLocation = FVector(0.0, 0.0, 82.0);
		Blocked.Reset(FTransform::Identity, Pressed.HeadLocation);
		Pressed.BodyContactN = FVector(-175.0, 0.0, 30.0);
		Blocked.Update(Pressed, AtOnce, Dt);
		TestEqual(TEXT("a push halfway between blocked and stopped takes half the walk into it"), Blocked.GetOriginVelocity(), FVector(70.0, 0.0, 0.0), 1.0e-6f);
		TestEqual(TEXT("and says so"), Blocked.GetHeldBack(), 0.5, 1.0e-9);
		Pressed.BodyContactN = FVector(-300.0, 0.0, 0.0);
		Blocked.Update(Pressed, AtOnce, Dt);
		TestEqual(TEXT("a push past stopped takes all of it"), Blocked.GetOriginVelocity(), FVector::ZeroVector, 1.0e-6f);
		Pressed.MoveStick = FVector2D(1.0, 1.0);
		Blocked.Update(Pressed, AtOnce, Dt);
		TestEqual(TEXT("the walk along what presses it back goes on"), Blocked.GetOriginVelocity(), FVector(0.0, 140.0 / FMath::Sqrt(2.0), 0.0), 1.0e-6f);
		Pressed.BodyContactN = FVector(-50.0, 0.0, 0.0);
		Blocked.Update(Pressed, AtOnce, Dt);
		TestEqual(TEXT("a push below blocked takes nothing"), Blocked.GetOriginVelocity(), FVector(1.0, 1.0, 0.0) * 140.0 / FMath::Sqrt(2.0), 1.0e-6f);
		TestTrue(TEXT("the frames held back are counted"), Blocked.GetStats().HeldBackFrames == 3 && Blocked.GetStats().MostHeldBack == 1.0);

		// Standing still, pressed back, the body rests against what touches it instead of leaning on it.
		FSandboxLocomotion Resting;
		FSandboxLocomotionInput Leaning;
		Leaning.HeadLocation = FVector(0.0, 0.0, 170.0);
		Leaning.bHasBody = true;
		Leaning.BodyLocation = FVector(-4.0, 0.0, 82.0);
		Leaning.BodyContactN = FVector(-300.0, 0.0, 0.0);
		Resting.Reset(FTransform::Identity, Leaning.HeadLocation);
		Resting.Update(Leaning, Tools, Dt);
		TestEqual(TEXT("a body 4 cm short of its target, pressed back, gets its target where it is"), Resting.GetBodyTarget(Body, Tools).Location.X, -4.0, 1.0e-6);
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
