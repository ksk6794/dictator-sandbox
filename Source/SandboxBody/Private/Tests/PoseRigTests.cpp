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

#endif // WITH_DEV_AUTOMATION_TESTS
