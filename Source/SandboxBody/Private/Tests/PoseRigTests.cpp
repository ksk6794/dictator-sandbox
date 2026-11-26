#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "SandboxClipPose.h"
#include "SandboxPoseRig.h"

/**
 * A joint's angles turn the child about the joint's own axes: swing1 about the frame's Z, swing2 about its Y,
 * twist about its X, by the right-hand rule, and nothing at all for zeros.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPoseRigJointTurnTest, "DictatorSandbox.Body.Pose.JointTurnUsesTheJointAxes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FPoseRigJointTurnTest::RunTest(const FString& Parameters)
{
	// A frame turned off the bone's axes, as a real joint's is.
	const FQuat Frame = FQuat(FVector(1.0, 2.0, 3.0).GetSafeNormal(), 0.7);
	const FVector Twist = Frame.GetAxisX();
	const FVector Swing1 = Frame.GetAxisZ();
	const FVector Swing2 = Frame.GetAxisY();

	TestTrue(TEXT("zero angles turn nothing"), FSandboxPoseRig::JointTurn(Frame, FSandboxPoseAngles()).Equals(FQuat::Identity, 1.0e-12));

	FSandboxPoseAngles Bend;
	Bend.Swing1Deg = 90.0;
	const FQuat BendTurn = FSandboxPoseRig::JointTurn(Frame, Bend);
	TestTrue(TEXT("swing1 keeps its own axis"), BendTurn.RotateVector(Swing1).Equals(Swing1, 1.0e-9));
	TestTrue(TEXT("swing1 of +90 turns the limb (twist axis) onto the swing2 axis"), BendTurn.RotateVector(Twist).Equals(Swing2, 1.0e-9));

	FSandboxPoseAngles Side;
	Side.Swing2Deg = 90.0;
	TestTrue(TEXT("swing2 of +90 turns the swing1 axis onto the limb"), FSandboxPoseRig::JointTurn(Frame, Side).RotateVector(Swing1).Equals(Twist, 1.0e-9));

	FSandboxPoseAngles Roll;
	Roll.TwistDeg = 90.0;
	const FQuat RollTurn = FSandboxPoseRig::JointTurn(Frame, Roll);
	TestTrue(TEXT("twist keeps the limb where it is"), RollTurn.RotateVector(Twist).Equals(Twist, 1.0e-9));
	TestTrue(TEXT("twist of +90 turns swing2 onto swing1"), RollTurn.RotateVector(Swing2).Equals(Swing1, 1.0e-9));

	FSandboxPoseAngles Small;
	Small.Swing1Deg = 30.0;
	TestEqual(TEXT("the turn is as large as the angle"), FMath::RadiansToDegrees(FSandboxPoseRig::JointTurn(Frame, Small).GetAngle()), 30.0, 1.0e-9);
	return true;
}

/**
 * A child bone is brought into its joint's range: inside it the rotation comes back as given, bit for bit; a
 * twist beyond its half-range ends on it, and a swing beyond the ellipse ends on the ellipse in the same direction.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPoseRigClampTest, "DictatorSandbox.Body.Pose.ClampToJointLimits",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FPoseRigClampTest::RunTest(const FString& Parameters)
{
	FSandboxJointLimits Limits;
	Limits.bValid = true;
	Limits.ParentFrame = FQuat(FVector(0.3, -1.0, 0.4).GetSafeNormal(), 0.5);
	Limits.ChildFrame = FQuat(FVector(1.0, 0.2, -0.7).GetSafeNormal(), 1.1);
	Limits.Swing1Deg = 25.0;
	Limits.Swing2Deg = 10.0;
	Limits.TwistDeg = 15.0;
	const FQuat Parent = FQuat(FVector(-0.2, 0.5, 1.0).GetSafeNormal(), 0.9);
	// A child bone whose joint frame stands at the given turn from the parent's frame.
	const auto ChildAt = [&](const FQuat& Relative) { return (Parent * Limits.ParentFrame * Relative * Limits.ChildFrame.Inverse()).GetNormalized(); };
	const auto RelativeOf = [&](const FQuat& Child) { return ((Parent * Limits.ParentFrame).Inverse() * (Child * Limits.ChildFrame)).GetNormalized(); };
	const double Deg = UE_DOUBLE_PI / 180.0;

	const FQuat Inside = ChildAt(FQuat(FVector::ZAxisVector, 10.0 * Deg) * FQuat(FVector::XAxisVector, -8.0 * Deg));
	const FQuat Same = FSandboxPoseRig::ClampToLimits(Parent, Inside, Limits);
	TestTrue(TEXT("inside the range the rotation is given back exactly"), Same.X == Inside.X && Same.Y == Inside.Y && Same.Z == Inside.Z && Same.W == Inside.W);

	FQuat Swing, Twist;
	RelativeOf(FSandboxPoseRig::ClampToLimits(Parent, ChildAt(FQuat(FVector::XAxisVector, 40.0 * Deg)), Limits)).ToSwingTwist(FVector::XAxisVector, Swing, Twist);
	TestEqual(TEXT("a twist of 40 ends at 15"), FMath::RadiansToDegrees(Twist.GetTwistAngle(FVector::XAxisVector)), 15.0, 1.0e-6);
	TestEqual(TEXT("and leaves no swing"), FMath::RadiansToDegrees(Swing.GetAngle()), 0.0, 1.0e-6);

	// 30 degrees about an axis halfway between Z and Y: (21.2 / 25)^2 + (21.2 / 10)^2 > 1.
	const FVector Diagonal = FVector(0.0, 1.0, 1.0).GetSafeNormal();
	RelativeOf(FSandboxPoseRig::ClampToLimits(Parent, ChildAt(FQuat(Diagonal, 30.0 * Deg)), Limits)).ToSwingTwist(FVector::XAxisVector, Swing, Twist);
	FVector Axis;
	double Angle;
	Swing.ToAxisAndAngle(Axis, Angle);
	const FVector Vector = Axis * FMath::RadiansToDegrees(Angle);
	TestTrue(TEXT("the swing keeps its direction"), Axis.Equals(Diagonal, 1.0e-6));
	TestEqual(TEXT("and ends on the ellipse"), FMath::Square(Vector.Z / 25.0) + FMath::Square(Vector.Y / 10.0), 1.0, 1.0e-6);
	TestEqual(TEXT("with no twist"), FMath::RadiansToDegrees(Twist.GetTwistAngle(FVector::XAxisVector)), 0.0, 1.0e-6);
	return true;
}

/**
 * A child bone is brought into its joint's range as the solver limits it: inside it the rotation comes back as given,
 * bit for bit; outside, each of the three angles ends at its own limit, a locked one at 0, and the others stay.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPoseRigClampRangeTest, "DictatorSandbox.Body.Pose.ClampToJointRange",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FPoseRigClampRangeTest::RunTest(const FString& Parameters)
{
	FSandboxJointLimits Limits;
	Limits.bValid = true;
	Limits.ParentFrame = FQuat(FVector(0.3, -1.0, 0.4).GetSafeNormal(), 0.5);
	Limits.ChildFrame = FQuat(FVector(1.0, 0.2, -0.7).GetSafeNormal(), 1.1);
	Limits.Swing1Deg = 60.0;
	Limits.Swing2Deg = 25.0;
	Limits.TwistDeg = 70.0;
	const FQuat Parent = FQuat(FVector(-0.2, 0.5, 1.0).GetSafeNormal(), 0.9);
	const auto ChildAt = [&](const FQuat& Relative) { return (Parent * Limits.ParentFrame * Relative * Limits.ChildFrame.Inverse()).GetNormalized(); };
	const double Deg = UE_DOUBLE_PI / 180.0;

	const FQuat Inside = ChildAt(FQuat(FVector::ZAxisVector, 40.0 * Deg) * FQuat(FVector::XAxisVector, -50.0 * Deg));
	const FQuat Same = FSandboxPoseRig::ClampToRange(Parent, Inside, Limits);
	TestTrue(TEXT("inside the range the rotation is given back exactly"), Same.X == Inside.X && Same.Y == Inside.Y && Same.Z == Inside.Z && Same.W == Inside.W);

	// A hand turned over and bent back: a swing of 80 degrees about an axis between Y and Z, on a twist of 90.
	const FQuat Outside = ChildAt(FQuat(FVector(0.0, 0.8, 0.6), 80.0 * Deg) * FQuat(FVector::XAxisVector, 90.0 * Deg));
	FVector Before;
	TestTrue(TEXT("the bone starts outside its range"), FSandboxPoseRig::LimitExcessDeg(Parent, Outside, Limits, &Before) > 20.0);
	FVector After;
	const double Left = FSandboxPoseRig::LimitExcessDeg(Parent, FSandboxPoseRig::ClampToRange(Parent, Outside, Limits), Limits, &After);
	TestEqual(TEXT("and ends inside it"), Left, 0.0, 1.0e-6);
	TestEqual(TEXT("the twist at its limit"), After.X, 70.0, 1.0e-6);
	TestEqual(TEXT("the second swing at its limit"), FMath::Abs(After.Z), 25.0, 1.0e-6);
	TestEqual(TEXT("the first swing, inside its own range, as it was"), After.Y, Before.Y, 1.0e-6);

	Limits.Swing2Deg = 0.0;
	Limits.TwistDeg = 0.0;
	FVector Hinge;
	FSandboxPoseRig::LimitExcessDeg(Parent, FSandboxPoseRig::ClampToRange(Parent, Outside, Limits), Limits, &Hinge);
	TestEqual(TEXT("a locked twist ends at 0"), Hinge.X, 0.0, 1.0e-6);
	TestEqual(TEXT("a locked swing ends at 0"), Hinge.Z, 0.0, 1.0e-6);
	TestEqual(TEXT("and the hinge keeps its angle"), Hinge.Y, Before.Y, 1.0e-6);
	return true;
}

/**
 * A joint whose swings are limited in the child's frame (the wrist): the child turned about its length and then bent about
 * its own axes is judged by its own ranges. The same limits in the parent's frame judge a hand turned over by the wrong one.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPoseRigLimitsInChildTest, "DictatorSandbox.Body.Pose.LimitsInTheChildsFrame",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FPoseRigLimitsInChildTest::RunTest(const FString& Parameters)
{
	FSandboxJointLimits Limits;
	Limits.bValid = true;
	Limits.bInChild = true;
	Limits.ParentFrame = FQuat(FVector(0.3, -1.0, 0.4).GetSafeNormal(), 0.5);
	Limits.ChildFrame = FQuat(FVector(1.0, 0.2, -0.7).GetSafeNormal(), 1.1);
	Limits.Swing1Deg = 72.5;
	Limits.Swing2Deg = 25.0;
	Limits.TwistDeg = 70.0;
	FSandboxJointLimits InParent = Limits;
	InParent.bInChild = false;
	const FQuat Parent = FQuat(FVector(-0.2, 0.5, 1.0).GetSafeNormal(), 0.9);
	const double Deg = UE_DOUBLE_PI / 180.0;
	// The child's frame: the parent's, turned about the length, then bent about the child's own axes.
	const auto ChildAt = [&](double TwistDeg, const FQuat& OwnSwing)
	{
		return (Parent * Limits.ParentFrame * FQuat(FVector::XAxisVector, TwistDeg * Deg) * OwnSwing * Limits.ChildFrame.Inverse()).GetNormalized();
	};

	// A hand turned over by 60 and bent back by 70 about its own axis.
	const FQuat BentBack = ChildAt(60.0, FQuat(FVector::ZAxisVector, -70.0 * Deg));
	FVector Angles;
	TestEqual(TEXT("within the child's own ranges there is no excess"), FSandboxPoseRig::LimitExcessDeg(Parent, BentBack, Limits, &Angles), 0.0, 1.0e-6);
	TestEqual(TEXT("its twist"), FMath::Abs(Angles.X), 60.0, 1.0e-6);
	TestEqual(TEXT("its first swing"), FMath::Abs(Angles.Y), 70.0, 1.0e-6);
	TestEqual(TEXT("and no second swing"), Angles.Z, 0.0, 1.0e-6);
	TestTrue(TEXT("the same turn judged in the parent's frame is far outside the sideways range"), FSandboxPoseRig::LimitExcessDeg(Parent, BentBack, InParent) > 30.0);
	const FQuat Same = FSandboxPoseRig::ClampToRange(Parent, BentBack, Limits);
	TestTrue(TEXT("inside the range the rotation is given back exactly"), Same.X == BentBack.X && Same.Y == BentBack.Y && Same.Z == BentBack.Z && Same.W == BentBack.W);

	// Too far in every one of them.
	const FQuat Outside = ChildAt(95.0, FQuat(FVector::ZAxisVector, -100.0 * Deg) * FQuat(FVector::YAxisVector, 10.0 * Deg));
	FVector Before, After;
	TestTrue(TEXT("a hand bent back by 100 is outside"), FSandboxPoseRig::LimitExcessDeg(Parent, Outside, Limits, &Before) > 20.0);
	TestEqual(TEXT("and ends inside"), FSandboxPoseRig::LimitExcessDeg(Parent, FSandboxPoseRig::ClampToRange(Parent, Outside, Limits), Limits, &After), 0.0, 1.0e-6);
	TestEqual(TEXT("its bend at the end of the range"), FMath::Abs(After.Y), 72.5, 1.0e-6);
	TestEqual(TEXT("its twist at the end of the range"), FMath::Abs(After.X), 70.0, 1.0e-6);
	TestEqual(TEXT("its sideways bend, inside its own range, as it was"), After.Z, Before.Z, 1.0e-6);
	return true;
}

/**
 * A clip's bone carried over to the body: a bone the clip leaves in its reference pose leaves the body's in its own,
 * whatever the two reference poses are; a turn of the clip's bone turns the body's by the same, in the space of the
 * whole body; and a segment that the two skeletons hold differently ends up pointing where the clip's points.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FClipPoseCarryTest, "DictatorSandbox.Body.Pose.ClipTurnsCarryOver",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FClipPoseCarryTest::RunTest(const FString& Parameters)
{
	const double Deg = UE_DOUBLE_PI / 180.0;
	// The two skeletons hold the bone differently: other axes, and 12 degrees apart in where the segment points.
	const FQuat SourceReference = FQuat(FVector(0.2, 0.9, -0.3).GetSafeNormal(), 1.3);
	const FQuat TargetReference = FQuat(FVector(-0.6, 0.1, 0.7).GetSafeNormal(), 0.4);
	const FVector SourceWay = FVector(0.3, 0.1, -1.0).GetSafeNormal();
	const FVector TargetWay = FQuat(FVector::YAxisVector, 12.0 * Deg).RotateVector(SourceWay);
	const FQuat Align = FQuat::FindBetweenNormals(TargetWay, SourceWay);

	TestTrue(TEXT("a bone in its reference pose leaves the body's bone in its own"),
		FSandboxClipPose::Carry(SourceReference, SourceReference, FQuat::Identity, TargetReference).Equals(TargetReference, 1.0e-12));

	const FQuat Turn(FVector(1.0, -2.0, 0.5).GetSafeNormal(), 70.0 * Deg);
	const FQuat Carried = FSandboxClipPose::Carry(Turn * SourceReference, SourceReference, FQuat::Identity, TargetReference);
	TestTrue(TEXT("a turn of the clip's bone turns the body's bone by the same"), (Carried * TargetReference.Inverse()).Equals(Turn, 1.0e-9));

	// The segment's direction in each bone's own space, from the reference poses.
	const FVector InTargetBone = TargetReference.UnrotateVector(TargetWay);
	const FQuat Aligned = FSandboxClipPose::Carry(Turn * SourceReference, SourceReference, Align, TargetReference);
	TestTrue(TEXT("an aligned segment points where the clip's points"), Aligned.RotateVector(InTargetBone).Equals(Turn.RotateVector(SourceWay), 1.0e-9));
	TestFalse(TEXT("which it would not without the alignment"), Carried.RotateVector(InTargetBone).Equals(Turn.RotateVector(SourceWay), 1.0e-3));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
