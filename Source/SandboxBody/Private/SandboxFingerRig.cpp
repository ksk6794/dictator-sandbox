#include "SandboxFingerRig.h"

#include "AnimationRuntime.h"
#include "ReferenceSkeleton.h"

namespace
{
	const TCHAR* const FingerNames[5] = { TEXT("thumb"), TEXT("index"), TEXT("middle"), TEXT("ring"), TEXT("pinky") };

	// How a curl is shared out over a finger's three joints (a relaxed hand and a fist both bend most in the middle), and over the thumb's.
	constexpr double FingerShares[3] = { 0.36, 0.40, 0.24 };
	constexpr double ThumbShares[3] = { 0.25, 0.40, 0.35 };

	// What the spread of 1 degree is for each finger, toward the thumb's side: they part from the middle finger.
	constexpr double SpreadShares[5] = { 0.5, 1.0, 0.0, -0.8, -1.7 };
}

bool FSandboxFingerRig::Init(const FReferenceSkeleton& Skeleton)
{
	Hands[0].Reset();
	Hands[1].Reset();
	for (int32 Side = 0; Side < 2; ++Side)
	{
		const TCHAR* Suffix = Side == 0 ? TEXT("_l") : TEXT("_r");
		const auto BoneAt = [&Skeleton, Suffix](int32 Finger, int32 Segment)
		{
			return Skeleton.FindBoneIndex(FName(*FString::Printf(TEXT("%s_%02d%s"), FingerNames[Finger], Segment + 1, Suffix)));
		};
		int32 Bones[5][3];
		bool bAll = true;
		for (int32 Finger = 0; Finger < 5; ++Finger)
		{
			for (int32 Segment = 0; Segment < 3; ++Segment)
			{
				Bones[Finger][Segment] = BoneAt(Finger, Segment);
				bAll = bAll && Bones[Finger][Segment] != INDEX_NONE;
			}
		}
		const int32 Hand = Skeleton.FindBoneIndex(FName(Side == 0 ? TEXT("hand_l") : TEXT("hand_r")));
		if (!bAll || Hand == INDEX_NONE)
		{
			Hands[0].Reset();
			Hands[1].Reset();
			return false;
		}
		const auto Space = [&Skeleton](int32 Bone) { return FAnimationRuntime::GetComponentSpaceTransformRefPose(Skeleton, Bone); };
		const auto At = [&Space](int32 Bone) { return Space(Bone).GetLocation(); };

		// The hand: its length toward the middle finger, the line across the knuckles toward the thumb's side, and from
		// the two the axis about which a finger bends toward the palm. On the left that is the line from the little
		// finger to the first; a reflection reverses a rotation's axis, so on the right it is the other way round.
		const FVector Along = (At(Bones[2][0]) - At(Hand)).GetSafeNormal();
		const FVector Across = (At(Bones[1][0]) - At(Bones[4][0])).GetSafeNormal();
		const double Sign = Side == 0 ? 1.0 : -1.0;
		const FVector Flexion = Across * Sign;
		// Away from the palm: turning about it by Sign times an angle takes a finger toward the thumb's side.
		const FVector Back = FVector::CrossProduct(Along, Flexion).GetSafeNormal();

		for (int32 Finger = 0; Finger < 5; ++Finger)
		{
			for (int32 Segment = 0; Segment < 3; ++Segment)
			{
				FJoint& Joint = Hands[Side].AddDefaulted_GetRef();
				Joint.MeshBoneIndex = Bones[Finger][Segment];
				Joint.Finger = Finger;
				Joint.Segment = Segment;
				const FVector From = At(Bones[Finger][Segment]);
				const FVector Way = (Segment < 2 ? At(Bones[Finger][Segment + 1]) - From : From - At(Bones[Finger][Segment - 1])).GetSafeNormal();
				FVector Curl;
				if (Finger == 0)
				{
					// The thumb bends toward the root of the little finger.
					Curl = FVector::CrossProduct(Way, (At(Bones[4][0]) - From).GetSafeNormal()).GetSafeNormal();
				}
				else
				{
					Curl = (Flexion - Way * (Flexion | Way)).GetSafeNormal();
				}
				const FQuat Bone = Space(Bones[Finger][Segment]).GetRotation();
				Joint.CurlAxis = Bone.UnrotateVector(Curl);
				if (Segment == 0)
				{
					Joint.SideAxis = Bone.UnrotateVector(Back * Sign);
				}
			}
		}
	}
	return true;
}

void FSandboxFingerRig::BuildTurns(int32 Side, const FSandboxFingerPose& Pose, TArray<FSandboxPoseTurn>& OutTurns) const
{
	if (Side < 0 || Side > 1 || Pose.IsZero())
	{
		return;
	}
	for (const FJoint& Joint : Hands[Side])
	{
		const double CurlDeg = Pose.CurlDeg[Joint.Finger] * (Joint.Finger == 0 ? ThumbShares : FingerShares)[Joint.Segment];
		const double SideDeg = Joint.Segment == 0 ? Pose.SpreadDeg * SpreadShares[Joint.Finger] : 0.0;
		if (CurlDeg == 0.0 && SideDeg == 0.0)
		{
			continue;
		}
		FQuat Turn(Joint.CurlAxis, FMath::DegreesToRadians(CurlDeg));
		if (SideDeg != 0.0)
		{
			Turn = FQuat(Joint.SideAxis, FMath::DegreesToRadians(SideDeg)) * Turn;
		}
		OutTurns.Add(FSandboxPoseTurn{ Joint.MeshBoneIndex, Turn.GetNormalized() });
	}
}

FSandboxFingerPose FSandboxFingerRig::Blend(TConstArrayView<TPair<const FSandboxFingerPose*, double>> Poses)
{
	FSandboxFingerPose Result;
	for (const TPair<const FSandboxFingerPose*, double>& Each : Poses)
	{
		for (int32 Finger = 0; Finger < 5; ++Finger)
		{
			Result.CurlDeg[Finger] += Each.Key->CurlDeg[Finger] * Each.Value;
		}
		Result.SpreadDeg += Each.Key->SpreadDeg * Each.Value;
	}
	return Result;
}
