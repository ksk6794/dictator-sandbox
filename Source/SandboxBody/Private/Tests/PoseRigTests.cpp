#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
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

#endif // WITH_DEV_AUTOMATION_TESTS
