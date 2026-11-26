#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AnimationRuntime.h"
#include "Engine/SkeletalMesh.h"
#include "Misc/AutomationTest.h"
#include "SandboxFingerRig.h"

/**
 * The fingers of both hands: a curl brings the fingertips toward the wrist, a spread parts them, the two hands do the
 * mirror image of each other, and a pose of zeros turns nothing.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFingerRigTest, "DictatorSandbox.Body.Pose.FingersCurlAndSpread",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFingerRigTest::RunTest(const FString& Parameters)
{
	const USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/MetaHumans/SandboxNPC/Body/SKM_SandboxNPC_BodyMesh.SKM_SandboxNPC_BodyMesh"));
	if (!TestNotNull(TEXT("the NPC's mesh"), Mesh))
	{
		return false;
	}
	const FReferenceSkeleton& Skeleton = Mesh->GetRefSkeleton();
	FSandboxFingerRig Rig;
	if (!TestTrue(TEXT("the mesh has both hands"), Rig.Init(Skeleton)))
	{
		return false;
	}

	// Where a bone is, in the mesh's space, with a set of turns on the reference pose.
	const auto Place = [&Skeleton](const TArray<FSandboxPoseTurn>& Turns, const TCHAR* Bone)
	{
		TArray<FTransform> Local = Skeleton.GetRefBonePose();
		for (const FSandboxPoseTurn& Turn : Turns)
		{
			Local[Turn.MeshBoneIndex].SetRotation((Local[Turn.MeshBoneIndex].GetRotation() * Turn.Turn).GetNormalized());
		}
		TArray<FTransform> Space;
		FAnimationRuntime::FillUpComponentSpaceTransforms(Skeleton, Local, Space);
		return Space[FMath::Max(Skeleton.FindBoneIndex(Bone), 0)].GetLocation();
	};
	const auto Mirror = [](const FVector& V) { return FVector(-V.X, V.Y, V.Z); };
	const TArray<FSandboxPoseTurn> None;

	FSandboxFingerPose Curl;
	for (double& Deg : Curl.CurlDeg)
	{
		Deg = 150.0;
	}
	TArray<FSandboxPoseTurn> Left, Right;
	Rig.BuildTurns(0, Curl, Left);
	Rig.BuildTurns(1, Curl, Right);
	TestEqual(TEXT("a curl of every finger turns fifteen bones"), Left.Num(), 15);
	for (const TCHAR* Finger : { TEXT("index"), TEXT("middle"), TEXT("ring"), TEXT("pinky") })
	{
		const FString Tip = FString::Printf(TEXT("%s_03"), Finger);
		const FString LeftTip = Tip + TEXT("_l");
		const FString RightTip = Tip + TEXT("_r");
		const double Open = FVector::Dist(Place(None, *LeftTip), Place(None, TEXT("hand_l")));
		const double Closed = FVector::Dist(Place(Left, *LeftTip), Place(Left, TEXT("hand_l")));
		TestTrue(FString::Printf(TEXT("%s: the tip comes at least 4 cm nearer the wrist (%.1f to %.1f)"), Finger, Open, Closed), Closed < Open - 4.0);
		// Toward the palm and not the back of the hand: the thumb's side of the hand is in front, the palm faces the thigh, which is toward the midline.
		TestTrue(FString::Printf(TEXT("%s: it bends toward the palm, which faces the body"), Finger), FMath::Abs(Place(Left, *LeftTip).X) < FMath::Abs(Place(None, *LeftTip).X) - 1.0);
		TestTrue(FString::Printf(TEXT("%s: the right hand does the mirror image"), Finger), FVector::Dist(Place(Right, *RightTip), Mirror(Place(Left, *LeftTip))) < 0.1);
	}
	const double ThumbOpen = FVector::Dist(Place(None, TEXT("thumb_03_l")), Place(None, TEXT("pinky_01_l")));
	TestTrue(TEXT("the thumb comes nearer the root of the little finger"), FVector::Dist(Place(Left, TEXT("thumb_03_l")), Place(Left, TEXT("pinky_01_l"))) < ThumbOpen - 1.0);
	TestTrue(TEXT("the thumbs mirror each other"), FVector::Dist(Place(Right, TEXT("thumb_03_r")), Mirror(Place(Left, TEXT("thumb_03_l")))) < 0.1);

	FSandboxFingerPose Spread;
	Spread.SpreadDeg = 10.0;
	Left.Reset();
	Right.Reset();
	Rig.BuildTurns(0, Spread, Left);
	Rig.BuildTurns(1, Spread, Right);
	const auto Apart = [&](const TArray<FSandboxPoseTurn>& Turns, const TCHAR* Suffix)
	{
		return FVector::Dist(Place(Turns, *FString::Printf(TEXT("index_03_%s"), Suffix)), Place(Turns, *FString::Printf(TEXT("pinky_03_%s"), Suffix)));
	};
	TestTrue(TEXT("a spread parts the fingers of the left hand"), Apart(Left, TEXT("l")) > Apart(None, TEXT("l")) + 1.5);
	TestTrue(TEXT("and of the right"), Apart(Right, TEXT("r")) > Apart(None, TEXT("r")) + 1.5);

	Left.Reset();
	Rig.BuildTurns(0, FSandboxFingerPose(), Left);
	TestEqual(TEXT("a pose of zeros turns nothing"), Left.Num(), 0);

	// Half of one pose and half of another is the mean of the two.
	FSandboxFingerPose A, B;
	A.CurlDeg[1] = 100.0;
	B.SpreadDeg = 8.0;
	const TPair<const FSandboxFingerPose*, double> Shares[] = { TPair<const FSandboxFingerPose*, double>(&A, 0.5), TPair<const FSandboxFingerPose*, double>(&B, 0.5) };
	const FSandboxFingerPose Mean = FSandboxFingerRig::Blend(Shares);
	TestEqual(TEXT("half a curl"), Mean.CurlDeg[1], 50.0);
	TestEqual(TEXT("and half a spread"), Mean.SpreadDeg, 4.0);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
