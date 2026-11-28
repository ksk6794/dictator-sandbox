#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/SkeletalMesh.h"
#include "Misc/AutomationTest.h"
#include "SandboxHandRig.h"

/**
 * The player's visible hands (effectors.hand.mesh): on both of the XR mannequin's hands the rig finds the hand's own frame by the
 * bones (the fingers along x, the thumb toward z, the right palm toward -y and the left toward +y), a pose of zeros is the reference
 * pose bit for bit, a fist brings every fingertip toward the palm and nearer the wrist, a straighter open hand takes them away from
 * the palm, and the left hand does the mirror image of the right.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHandRigTest, "DictatorSandbox.Interaction.HandRig.FistAndOpen",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FHandRigTest::RunTest(const FString& Parameters)
{
	struct FSide
	{
		const TCHAR* Path;
		bool bLeft;
		FSandboxHandRig Rig;
		TArray<FVector> RefTips, FistTips, OpenTips;
	};
	FSide Sides[2] = {
		{ TEXT("/Game/XRMannequins/Meshes/SKM_MannyXR_right.SKM_MannyXR_right"), false },
		{ TEXT("/Game/XRMannequins/Meshes/SKM_MannyXR_left.SKM_MannyXR_left"), true } };
	FSandboxHandCurls Fist;
	FSandboxHandCurls Open;
	for (int32 Finger = 0; Finger < 5; ++Finger)
	{
		for (int32 Joint = 0; Joint < 3; ++Joint)
		{
			Fist.Deg[Finger][Joint] = Finger == 0 ? 30.0 : 70.0;
			Open.Deg[Finger][Joint] = -15.0;
		}
	}
	for (FSide& Side : Sides)
	{
		const USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, Side.Path);
		if (!TestNotNull(FString::Printf(TEXT("the hand mesh %s (Tools\\fetch-stock-content.ps1)"), Side.Path), Mesh))
		{
			return false;
		}
		const FReferenceSkeleton& Skeleton = Mesh->GetRefSkeleton();
		TArray<FName> Names;
		TArray<int32> Parents;
		for (int32 Index = 0; Index < Skeleton.GetNum(); ++Index)
		{
			Names.Add(Skeleton.GetBoneName(Index));
			Parents.Add(Skeleton.GetParentIndex(Index));
		}
		FString Error;
		const bool bBuilt = Side.Rig.Build(Names, Parents, Skeleton.GetRefBonePose(), Side.bLeft, Error);
		if (!TestTrue(FString::Printf(TEXT("the rig of %s builds (%s)"), Side.Path, *Error), bBuilt))
		{
			return false;
		}
		const FTransform& Frame = Side.Rig.GetHandFrame();
		TArray<FTransform> Local;
		TArray<FTransform> Space;
		Side.Rig.Pose(FSandboxHandCurls(), Local);
		bool bSame = Local.Num() == Skeleton.GetNum();
		for (int32 Index = 0; bSame && Index < Local.Num(); ++Index)
		{
			bSame = Local[Index].Equals(Skeleton.GetRefBonePose()[Index], 0.0);
		}
		TestTrue(TEXT("a pose of zeros is the reference pose bit for bit"), bSame);
		Side.Rig.ToMeshSpace(Local, Space);
		const FVector Thumb = Frame.InverseTransformPosition(Space[Side.Rig.JointBone(0, 0)].GetLocation());
		const FVector Middle = Frame.InverseTransformPosition(Space[Side.Rig.JointBone(2, 2)].GetLocation());
		TestTrue(TEXT("the fingers lie along the hand's x"), Middle.X > 10.0);
		TestTrue(TEXT("the thumb is toward the hand's z"), Thumb.Z > 1.0);
		const FVector PalmInFrame = Frame.InverseTransformVector(Side.Rig.GetPalmNormal());
		TestTrue(TEXT("the palm faces -y on the right hand and +y on the left"), Side.bLeft ? PalmInFrame.Y > 0.99 : PalmInFrame.Y < -0.99);
		const auto Tips = [&Side](const FSandboxHandCurls& Curls, TArray<FVector>& Out)
		{
			TArray<FTransform> L;
			TArray<FTransform> S;
			Side.Rig.Pose(Curls, L);
			Side.Rig.ToMeshSpace(L, S);
			for (int32 Finger = 0; Finger < 5; ++Finger)
			{
				Out.Add(Side.Rig.GetHandFrame().InverseTransformPosition(Side.Rig.TipInMeshSpace(S, Finger)));
			}
		};
		Tips(FSandboxHandCurls(), Side.RefTips);
		Tips(Fist, Side.FistTips);
		Tips(Open, Side.OpenTips);
		const double PalmSign = Side.bLeft ? 1.0 : -1.0;
		for (int32 Finger = 1; Finger < 5; ++Finger)
		{
			// The middle of the palm, 2 cm out of the hand on the palm's side, in the finger's own line across the hand.
			const FVector Knuckle = Frame.InverseTransformPosition(Space[Side.Rig.JointBone(Finger, 0)].GetLocation());
			const FVector PalmMiddle(6.0, PalmSign * 2.0, Knuckle.Z);
			TestTrue(FString::Printf(TEXT("finger %d: a fist brings its tip to the middle of the palm"), Finger),
				FVector::Dist(Side.FistTips[Finger], PalmMiddle) < FVector::Dist(Side.RefTips[Finger], PalmMiddle) - 3.0);
			TestTrue(FString::Printf(TEXT("finger %d: a fist keeps its tip on the palm's side"), Finger), PalmSign * Side.FistTips[Finger].Y > 0.0);
			TestTrue(FString::Printf(TEXT("finger %d: a fist brings its tip nearer the wrist"), Finger), Side.FistTips[Finger].X < Side.RefTips[Finger].X - 3.0);
			TestTrue(FString::Printf(TEXT("finger %d: the open hand takes its tip away from the palm"), Finger),
				PalmSign * Side.OpenTips[Finger].Y < PalmSign * Side.RefTips[Finger].Y - 0.5);
		}
		// The thumb folds across, in front of the middle finger's knuckle.
		const FVector MiddleKnuckle = Frame.InverseTransformPosition(Space[Side.Rig.JointBone(2, 0)].GetLocation());
		TestTrue(TEXT("the thumb folds toward the middle finger's knuckle in a fist"),
			FVector::Dist(Side.FistTips[0], MiddleKnuckle) < FVector::Dist(Side.RefTips[0], MiddleKnuckle) - 2.0);
	}
	// The left hand is the mirror image of the right through the plane of the fingers and the thumb (its y turned over).
	for (int32 Finger = 0; Finger < 5; ++Finger)
	{
		const FVector& Right = Sides[0].FistTips[Finger];
		const FVector& Left = Sides[1].FistTips[Finger];
		TestTrue(FString::Printf(TEXT("finger %d: the left fist is the mirror image of the right (%s against %s)"), Finger, *Left.ToString(), *Right.ToString()),
			FVector(Left.X, -Left.Y, Left.Z).Equals(Right, 0.05));
	}
	return true;
}

#endif
