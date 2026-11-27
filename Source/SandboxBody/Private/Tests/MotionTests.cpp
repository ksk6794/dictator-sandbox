#include "Misc/AutomationTest.h"
#include "PhysicsEngine/BodySetup.h"
#include "SandboxClipPose.h"
#include "SandboxMotionLibrary.h"
#include "SandboxMotionMatcher.h"
#include "UObject/Package.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	const EAutomationTestFlags MotionTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	double AngleApartDeg(const FQuat& A, const FQuat& B)
	{
		return FMath::RadiansToDegrees(A.AngularDistance(B));
	}
}

/**
 * R2, step 8: a clip read as its mirror image. A made-up pair of bones whose left and right differ in their reference orientations
 * (the two sides' axes laid differently, as the mannequin's are) and in how they are turned; the mirror of the left has to be the
 * right's motion seen in the mirror, not the left's own. The same check, run without the swap of sides, has to fail: a mirror that
 * forgot to change sides passed "mirrored twice it is itself" once (docs/pitfalls.md).
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMotionMirrorTest, "DictatorSandbox.Body.Motion.Mirror", MotionTestFlags)
bool FMotionMirrorTest::RunTest(const FString& Parameters)
{
	constexpr int32 Axis = 0;
	// The mirror of a rotation is the rotation seen in the mirror: M(v) = S(R(S v)) for every v.
	const FQuat Any = FQuat(FVector(0.3, -0.5, 0.8).GetSafeNormal(), 1.1);
	const FVector Probe(12.0, -3.0, 7.0);
	TestTrue(TEXT("a mirrored rotation turns as the rotation does in the mirror"),
		FSandboxClipPose::MirrorRotation(Any, Axis).RotateVector(Probe).Equals(FSandboxClipPose::MirrorPoint(Any.RotateVector(FSandboxClipPose::MirrorPoint(Probe, Axis)), Axis), 1.0e-9));
	TestTrue(TEXT("mirrored twice it is itself"), AngleApartDeg(FSandboxClipPose::MirrorRotation(FSandboxClipPose::MirrorRotation(Any, Axis), Axis), Any) < 1.0e-6);
	TestTrue(TEXT("a turn about the mirror's normal stays"), AngleApartDeg(FSandboxClipPose::MirrorRotation(FQuat(FVector::ForwardVector, 0.7), Axis), FQuat(FVector::ForwardVector, 0.7)) < 1.0e-6);
	TestTrue(TEXT("a turn about an axis in the mirror goes the other way"), AngleApartDeg(FSandboxClipPose::MirrorRotation(FQuat(FVector::UpVector, 0.7), Axis), FQuat(FVector::UpVector, -0.7)) < 1.0e-6);
	TestTrue(TEXT("a point is mirrored on its axis"), FSandboxClipPose::MirrorPoint(FVector(4.0, 5.0, 6.0), 1).Equals(FVector(4.0, -5.0, 6.0)));

	// A symmetric pair whose bone axes lie differently: the left bone points along its own x, the right one along its own -y.
	const FQuat LeftReference(FVector::UpVector, 0.3);
	const FVector LeftAlong = FVector::ForwardVector;
	const FVector RightAlong = -FVector::RightVector;
	const FQuat RightReference = FQuat::FindBetweenNormals(RightAlong, FSandboxClipPose::MirrorPoint(LeftReference.RotateVector(LeftAlong), Axis));
	// Each turned its own way.
	const FQuat LeftTurn(FVector(0.2, 0.9, -0.1).GetSafeNormal(), 0.8);
	const FQuat RightTurn(FVector(-0.6, 0.1, 0.7).GetSafeNormal(), 1.3);
	const FQuat Left = LeftTurn * LeftReference;
	const FQuat Right = RightTurn * RightReference;

	const FQuat MirroredLeft = FSandboxClipPose::MirrorTurn(Right, RightReference, LeftReference, Axis);
	TestTrue(TEXT("at the reference pose the mirror is the bone's own reference"),
		AngleApartDeg(FSandboxClipPose::MirrorTurn(RightReference, RightReference, LeftReference, Axis), LeftReference) < 1.0e-6);
	TestTrue(TEXT("the mirrored left bone points where the right one points, seen in the mirror"),
		MirroredLeft.RotateVector(LeftAlong).Equals(FSandboxClipPose::MirrorPoint(Right.RotateVector(RightAlong), Axis), 1.0e-9));
	TestTrue(TEXT("the mirrored left bone is turned from its reference as the right one is, seen in the mirror"),
		AngleApartDeg(MirroredLeft * LeftReference.Inverse(), FSandboxClipPose::MirrorRotation(RightTurn, Axis)) < 1.0e-6);
	// Broken on purpose: the left's own motion mirrored, the sides not swapped. It must not pass the check above.
	const FQuat NoSwap = FSandboxClipPose::MirrorTurn(Left, LeftReference, LeftReference, Axis);
	TestTrue(TEXT("a mirror without the swap of sides fails the check"),
		!NoSwap.RotateVector(LeftAlong).Equals(FSandboxClipPose::MirrorPoint(Right.RotateVector(RightAlong), Axis), 1.0e-3));

	TestEqual(TEXT("left to right"), FSandboxClipPose::MirrorBoneName(TEXT("thigh_l")), FString(TEXT("thigh_r")));
	TestEqual(TEXT("right to left"), FSandboxClipPose::MirrorBoneName(TEXT("hand_r")), FString(TEXT("hand_l")));
	TestEqual(TEXT("the middle is its own"), FSandboxClipPose::MirrorBoneName(TEXT("spine_04")), FString(TEXT("spine_04")));
	TestEqual(TEXT("a name that merely contains _l is its own"), FSandboxClipPose::MirrorBoneName(TEXT("ball_lr")), FString(TEXT("ball_lr")));
	TestEqual(TEXT("the plane's normal from the thighs"), FSandboxClipPose::MirrorAxis(FVector(-0.5, 18.0, 1.0)), 1);
	return true;
}

/** The turn about the vertical and the shift that lay one set of places on another, across the seam at 180 degrees too. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMotionFitTest, "DictatorSandbox.Body.Motion.Fit", MotionTestFlags)
bool FMotionFitTest::RunTest(const FString& Parameters)
{
	const TArray<FVector> From = { FVector(0, 0, 12), FVector(45, 3, 15), FVector(70, -20, 8), FVector(-60, 18, 10), FVector(-58, -16, 9) };
	const TArray<double> Weights = { 3, 1, 1, 1, 1 };
	const double TurnsDeg[] = { 0.0, 30.0, 179.0, -179.0, 180.0, -95.0 };
	for (const double TurnDeg : TurnsDeg)
	{
		const double Turn = FMath::DegreesToRadians(TurnDeg);
		const FQuat Rotation(FVector::UpVector, Turn);
		const FVector Shift(12.0, -7.0, 0.0);
		TArray<FVector> To;
		for (const FVector& Place : From)
		{
			To.Add(Rotation.RotateVector(Place) + Shift);
		}
		double FoundRad = 0.0;
		FVector2D FoundShift;
		FSandboxMotionMatcher::FitTurn(From, To, Weights, FoundRad, FoundShift);
		TestTrue(*FString::Printf(TEXT("the turn of %.0f degrees found"), TurnDeg), FMath::Abs(FMath::RadiansToDegrees(FMath::FindDeltaAngleRadians(FoundRad, Turn))) < 0.01);
		TestTrue(*FString::Printf(TEXT("the shift found at %.0f degrees"), TurnDeg), FoundShift.Equals(FVector2D(Shift.X, Shift.Y), 0.01));
	}
	// One place off by 40 cm: weighted heavily, the pelvis stays nearly where it is; weighted as the others, it is pulled off.
	TArray<FVector> Off = From;
	Off[3] += FVector(40.0, 0.0, 0.0);
	const auto PelvisMiss = [&](double PelvisWeight)
	{
		const TArray<double> Each = { PelvisWeight, 1, 1, 1, 1 };
		double Yaw = 0.0;
		FVector2D Shift;
		FSandboxMotionMatcher::FitTurn(From, Off, Each, Yaw, Shift);
		const FVector Laid = FQuat(FVector::UpVector, Yaw).RotateVector(From[0]) + FVector(Shift.X, Shift.Y, 0.0);
		return FVector::Dist2D(Laid, Off[0]);
	};
	TestTrue(TEXT("a heavy pelvis is kept on the body's"), PelvisMiss(1000.0) < 0.1);
	TestTrue(TEXT("a light one is pulled off it"), PelvisMiss(1.0) > 2.0);
	// Nothing weighed: no turn, no shift.
	double Yaw = 1.0;
	FVector2D Shift(1.0, 1.0);
	const TArray<double> None = { 0, 0, 0, 0, 0 };
	FSandboxMotionMatcher::FitTurn(From, From, None, Yaw, Shift);
	TestEqual(TEXT("no weight, no turn"), Yaw, 0.0);
	TestTrue(TEXT("no weight, no shift"), Shift.IsZero());
	return true;
}

/**
 * Three made-up motions of three moments each. A body laid as the second motion's middle moment, turned, moved over the floor and
 * lying on a support 5 cm up, is matched to that motion, moment and frame; the variant just given up is passed over for the next.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMotionMatcherTest, "DictatorSandbox.Body.Motion.Matcher", MotionTestFlags)
bool FMotionMatcherTest::RunTest(const FString& Parameters)
{
	const FQuat StartRotation(FVector::UpVector, -HALF_PI);
	TArray<FSandboxMatchSample> Samples;
	const FVector Ups[3] = { FVector::UpVector, -FVector::UpVector, FVector::RightVector };
	for (int32 Variant = 0; Variant < 3; ++Variant)
	{
		for (int32 Key = 0; Key < 3; ++Key)
		{
			FSandboxMatchSample& Sample = Samples.AddDefaulted_GetRef();
			Sample.Variant = Variant;
			Sample.TimeS = 0.1 * Key;
			// Root-space places, each motion its own shape, each moment a little moved; turned by the start's rotation as the library gives them.
			for (int32 Place = 0; Place < 5; ++Place)
			{
				const FVector Root(40.0 * FMath::Cos(Place * 1.3 + Variant), 30.0 * FMath::Sin(Place * 0.7 * (Variant + 1)) + 6.0 * Key, 10.0 + 4.0 * Place + 3.0 * Key);
				Sample.Places.Add(StartRotation.RotateVector(Root));
			}
			Sample.PelvisUp = Ups[Variant];
			Sample.ChestUp = Ups[Variant];
		}
	}
	const FSandboxMatchSample& Laid = Samples[3 * 1 + 1];
	const double Turn = 2.0;
	const FVector Shift(30.0, 40.0, 5.0);
	FSandboxMatchBody Body;
	for (const FVector& Place : Laid.Places)
	{
		Body.Places.Add(FQuat(FVector::UpVector, Turn).RotateVector(Place) + Shift);
	}
	Body.PelvisUp = Laid.PelvisUp;
	Body.ChestUp = Laid.ChestUp;
	Body.SupportZ = 5.0;
	FSandboxMatchSettings Settings;
	Settings.Weights = { 3, 1, 1, 1, 1 };
	Settings.UpCmPerDeg = 1.0;

	TArray<FSandboxMatch> Best;
	FSandboxMotionMatcher::BestByVariant(Body, Samples, 3, Settings, Best);
	FSandboxMatch Chosen;
	FSandboxMatch Next;
	const TArray<bool> All = { true, true, true };
	FSandboxMotionMatcher::Choose(Best, All, Chosen, Next);
	TestTrue(TEXT("a choice is made"), Chosen.bValid && Next.bValid);
	TestEqual(TEXT("the motion it lies as"), Chosen.Variant, 1);
	TestEqual(TEXT("the moment it lies as"), Chosen.TimeS, 0.1, 1.0e-9);
	TestTrue(TEXT("at no cost"), Chosen.CostCm < 1.0e-6);
	TestTrue(TEXT("the next costs more"), Next.CostCm > Chosen.CostCm);
	// The frame lays the clip's root-space places on the body.
	const FTransform Frame = FSandboxMotionMatcher::Frame(Chosen, StartRotation, Body.SupportZ);
	for (int32 Place = 0; Place < Laid.Places.Num(); ++Place)
	{
		const FVector Root = StartRotation.UnrotateVector(Laid.Places[Place]);
		TestTrue(*FString::Printf(TEXT("the frame lays place %d on the body"), Place), Frame.TransformPosition(Root).Equals(Body.Places[Place], 1.0e-6));
	}
	// The lean is counted: the same places with the pelvis and chest upside down cost 180 degrees more.
	FSandboxMatchSample Flipped = Laid;
	Flipped.PelvisUp = -Laid.PelvisUp;
	Flipped.ChestUp = -Laid.ChestUp;
	TestEqual(TEXT("a lean the other way costs its degrees"), FSandboxMotionMatcher::Score(Body, Flipped, Settings).CostCm, 180.0, 1.0e-6);
	// Decision 51: a late start costs its seconds. The moment it lies as (0.1 s) now costs 0.1 * 50 cm, and an earlier moment of the
	// same motion that lies 3 cm off would win over a later one at no cost.
	Settings.LateCmPerS = 50.0;
	TestEqual(TEXT("a late start costs its seconds"), FSandboxMotionMatcher::Score(Body, Laid, Settings).CostCm, 5.0, 1.0e-6);
	FSandboxMatchSample Early = Laid;
	Early.TimeS = 0.0;
	TestTrue(TEXT("the same place at time 0 costs nothing more"), FSandboxMotionMatcher::Score(Body, Early, Settings).CostCm < 1.0e-6);
	Settings.LateCmPerS = 0.0;
	// The variant just given up costs more, and the next is taken.
	Settings.PenalizedVariants = { 1 };
	Settings.PenaltyCm = 1000.0;
	FSandboxMotionMatcher::BestByVariant(Body, Samples, 3, Settings, Best);
	FSandboxMotionMatcher::Choose(Best, All, Chosen, Next);
	TestTrue(TEXT("given up, another is taken"), Chosen.bValid && Chosen.Variant != 1);
	const int32 Instead = Chosen.Variant;
	// Given up with its mirror image: neither is taken.
	Settings.PenalizedVariants = { 1, Instead };
	FSandboxMotionMatcher::BestByVariant(Body, Samples, 3, Settings, Best);
	FSandboxMotionMatcher::Choose(Best, All, Chosen, Next);
	TestTrue(TEXT("given up with its mirror, the third is taken"), Chosen.bValid && Chosen.Variant != 1 && Chosen.Variant != Instead);
	// Only the variants allowed.
	Settings.PenalizedVariants.Reset();
	FSandboxMotionMatcher::BestByVariant(Body, Samples, 3, Settings, Best);
	const TArray<bool> NotTheSecond = { true, false, true };
	FSandboxMotionMatcher::Choose(Best, NotTheSecond, Chosen, Next);
	TestTrue(TEXT("one not allowed is not chosen"), Chosen.bValid && Chosen.Variant != 1 && Next.Variant != 1);
	const TArray<bool> Nothing = { false, false, false };
	FSandboxMotionMatcher::Choose(Best, Nothing, Chosen, Next);
	TestFalse(TEXT("nothing allowed, nothing chosen"), Chosen.bValid);
	return true;
}

/** Contact marks: a track that comes down to its floor and leaves it, a stretch too fast to count, and a body's lowest point. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMotionContactsTest, "DictatorSandbox.Body.Motion.Contacts", MotionTestFlags)
bool FMotionContactsTest::RunTest(const FString& Parameters)
{
	const TArray<double> Lowest = { 10.0, 5.0, 2.0, 1.0, -0.5, 4.0, 1.0, 1.0, 9.0 };
	const TArray<double> Speed = { 0.0, 0.5, 0.1, 0.0, 0.0, 0.0, 0.6, 0.1, 0.0 };
	TArray<bool> Touch;
	TArray<FVector2D> Stretches;
	FSandboxMotionLibrary::MarkContacts(Lowest, Speed, 0.1, 3.0, 0.3, Touch, Stretches);
	const bool Expected[] = { false, false, true, true, true, false, false, true, false };
	for (int32 Key = 0; Key < Touch.Num(); ++Key)
	{
		TestEqual(*FString::Printf(TEXT("key %d"), Key), Touch[Key], Expected[Key]);
	}
	TestEqual(TEXT("two stretches"), Stretches.Num(), 2);
	if (Stretches.Num() == 2)
	{
		TestTrue(TEXT("the first from 0.2 to 0.4 s"), Stretches[0].Equals(FVector2D(0.2, 0.4), 1.0e-9));
		TestTrue(TEXT("the second at 0.7 s"), Stretches[1].Equals(FVector2D(0.7, 0.7), 1.0e-9));
	}
	// Touching to the last key closes the stretch there.
	const TArray<double> Down = { 5.0, 1.0, 1.0 };
	const TArray<double> Still = { 0.0, 0.0, 0.0 };
	FSandboxMotionLibrary::MarkContacts(Down, Still, 0.5, 3.0, 0.3, Touch, Stretches);
	TestTrue(TEXT("a stretch to the end"), Stretches.Num() == 1 && Stretches[0].Equals(FVector2D(0.5, 1.0), 1.0e-9));

	// The lowest point of a body's shapes: a sphere above the bone, a box on a corner, a capsule laid flat.
	UBodySetup* Setup = NewObject<UBodySetup>(GetTransientPackage());
	FKSphereElem Sphere(5.0f);
	Sphere.Center = FVector(0.0, 0.0, 10.0);
	Setup->AggGeom.SphereElems.Add(Sphere);
	TestEqual(TEXT("a sphere"), FSandboxMotionLibrary::LowestPointCm(*Setup, FTransform::Identity), 5.0, 1.0e-4);
	Setup->AggGeom.SphereElems.Reset();
	FKBoxElem Box(10.0f, 10.0f, 10.0f);
	Box.Rotation = FRotator(0.0, 0.0, 45.0);
	Setup->AggGeom.BoxElems.Add(Box);
	TestEqual(TEXT("a box on its edge"), FSandboxMotionLibrary::LowestPointCm(*Setup, FTransform::Identity), -5.0 * FMath::Sqrt(2.0), 1.0e-3);
	Setup->AggGeom.BoxElems.Reset();
	FKSphylElem Capsule(3.0f, 20.0f);
	Setup->AggGeom.SphylElems.Add(Capsule);
	TestEqual(TEXT("a capsule standing"), FSandboxMotionLibrary::LowestPointCm(*Setup, FTransform::Identity), -13.0, 1.0e-4);
	TestEqual(TEXT("a capsule laid flat and raised"), FSandboxMotionLibrary::LowestPointCm(*Setup, FTransform(FQuat(FVector::ForwardVector, HALF_PI), FVector(0.0, 0.0, 20.0))), 17.0, 1.0e-4);
	return true;
}

/** The library's list is read as Specs/README.md says, and a list with a slip in it is refused. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMotionSpecTest, "DictatorSandbox.Body.Motion.Spec", MotionTestFlags)
bool FMotionSpecTest::RunTest(const FString& Parameters)
{
	const FString Good = TEXT("{ \"folder\": \"/Game/Clips\", \"features\": { \"pelvis\": [\"pelvis\"], \"hands\": [\"hand_l\", \"hand_r\"] }, ")
		TEXT("\"clips\": [ { \"name\": \"M_up\", \"kind\": \"getup\", \"lies\": \"back\", \"mirror\": true, \"match\": true }, { \"name\": \"M_roll\", \"kind\": \"roll\", \"mirror\": false, \"match\": false } ] }");
	FSandboxMotionSpec Spec;
	FString Error;
	TestTrue(TEXT("a good list is read"), FSandboxMotionSpec::Parse(Good, Spec, Error));
	TestEqual(TEXT("its bones"), Spec.FeatureBones.Num(), 3);
	TestEqual(TEXT("the hands' group"), Spec.FeatureGroups.Num() == 3 ? Spec.FeatureGroups[2] : -1, 2);
	TestEqual(TEXT("its clips"), Spec.Clips.Num(), 2);
	TestEqual(TEXT("a clip's key"), Spec.KeyOf(0, false), FString(TEXT("/Game/Clips/M_up.M_up")));
	TestEqual(TEXT("its mirror's key"), Spec.KeyOf(0, true), FString(TEXT("/Game/Clips/M_up.M_up|mirror")));

	const auto Refused = [&](const FString& From, const FString& To, const TCHAR* What)
	{
		FSandboxMotionSpec Ignored;
		FString Reason;
		TestFalse(What, FSandboxMotionSpec::Parse(Good.Replace(*From, *To), Ignored, Reason));
	};
	Refused(TEXT("\"mirror\": true"), TEXT("\"mirror\": 1"), TEXT("a number for a boolean is refused"));
	Refused(TEXT("\"lies\": \"back\""), TEXT("\"lie\": \"back\""), TEXT("an unknown field of a clip is refused"));
	Refused(TEXT("\"hands\""), TEXT("\"palms\""), TEXT("an unknown group is refused"));
	Refused(TEXT("\"kind\": \"roll\", \"mirror\": false, \"match\": false"), TEXT("\"kind\": \"roll\", \"mirror\": false, \"match\": true"), TEXT("a roll to get up by is refused"));
	Refused(TEXT("\"pelvis\": [\"pelvis\"], "), TEXT(""), TEXT("a list without the pelvis's group is refused"));
	Refused(TEXT("\"folder\""), TEXT("\"folders\""), TEXT("an unknown field is refused"));
	return true;
}

#endif
