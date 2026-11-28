#include "SandboxHandRig.h"

namespace
{
	const TCHAR* const HandRigFingerNames[5] = { TEXT("thumb"), TEXT("index"), TEXT("middle"), TEXT("ring"), TEXT("pinky") };
}

bool FSandboxHandRig::Build(const TArray<FName>& InNames, const TArray<int32>& InParents, const TArray<FTransform>& InRefLocal, bool bLeft, FString& OutError)
{
	bBuilt = false;
	if (InNames.Num() != InParents.Num() || InNames.Num() != InRefLocal.Num() || InNames.Num() == 0)
	{
		OutError = TEXT("the hand's skeleton has no bones, or its names, parents and poses differ in number");
		return false;
	}
	Names = InNames;
	Parents = InParents;
	RefLocal = InRefLocal;
	bLeftHand = bLeft;
	RefMesh.SetNum(RefLocal.Num());
	for (int32 Index = 0; Index < RefLocal.Num(); ++Index)
	{
		const int32 Parent = Parents[Index];
		if (Parent >= Index)
		{
			OutError = FString::Printf(TEXT("bone %s comes before its parent"), *Names[Index].ToString());
			return false;
		}
		RefMesh[Index] = Parent == INDEX_NONE ? RefLocal[Index] : RefLocal[Index] * RefMesh[Parent];
	}
	const TCHAR* const Side = bLeft ? TEXT("l") : TEXT("r");
	const int32 Hand = Names.IndexOfByKey(FName(*FString::Printf(TEXT("hand_%s"), Side)));
	if (Hand == INDEX_NONE)
	{
		OutError = FString::Printf(TEXT("the skeleton has no bone hand_%s"), Side);
		return false;
	}
	for (int32 Finger = 0; Finger < 5; ++Finger)
	{
		for (int32 Joint = 0; Joint < 3; ++Joint)
		{
			const FName Name(*FString::Printf(TEXT("%s_%02d_%s"), HandRigFingerNames[Finger], Joint + 1, Side));
			Joints[Finger][Joint] = Names.IndexOfByKey(Name);
			if (Joints[Finger][Joint] == INDEX_NONE)
			{
				OutError = FString::Printf(TEXT("the skeleton has no bone %s"), *Name.ToString());
				return false;
			}
		}
	}

	// The hand's frame from its bones: along the fingers from the wrist to the middle finger's knuckle, toward the thumb from the little
	// finger's knuckle to the index finger's.
	const FVector Wrist = RefMesh[Hand].GetLocation();
	const FVector Along = (RefMesh[Joints[2][0]].GetLocation() - Wrist).GetSafeNormal();
	const FVector Across = RefMesh[Joints[1][0]].GetLocation() - RefMesh[Joints[4][0]].GetLocation();
	const FVector Thumbward = (Across - Along * FVector::DotProduct(Across, Along)).GetSafeNormal();
	if (Along.IsNearlyZero() || Thumbward.IsNearlyZero())
	{
		OutError = TEXT("the hand's frame cannot be found from its knuckles");
		return false;
	}
	HandFrame = FTransform(FRotationMatrix::MakeFromXZ(Along, Thumbward).ToQuat(), Wrist);
	const FVector Y = HandFrame.GetRotation().GetAxisY();
	PalmNormal = bLeft ? Y : -Y;

	// Each joint's hinge: across its segment and the palm's normal (a positive turn brings the segment toward the palm), in its bone's frame.
	// The thumb folds across the palm instead, toward a point 3 cm out of the palm in front of the middle finger's knuckle: bent toward the
	// palm's normal alone it pointed at the other hand (the first pictures of 28 November).
	const FVector ThumbGoal = RefMesh[Joints[2][0]].GetLocation() + PalmNormal * 3.0;
	for (int32 Finger = 0; Finger < 5; ++Finger)
	{
		for (int32 Joint = 0; Joint < 3; ++Joint)
		{
			const int32 Bone = Joints[Finger][Joint];
			const FVector From = RefMesh[Joint < 2 ? Bone : Joints[Finger][1]].GetLocation();
			const FVector To = RefMesh[Joint < 2 ? Joints[Finger][Joint + 1] : Bone].GetLocation();
			const FVector Toward = Finger == 0 ? (ThumbGoal - RefMesh[Bone].GetLocation()).GetSafeNormal() : PalmNormal;
			FVector Hinge = FVector::CrossProduct((To - From).GetSafeNormal(), Toward).GetSafeNormal();
			if (Hinge.IsNearlyZero())
			{
				Hinge = Thumbward;
			}
			Hinges[Finger][Joint] = RefMesh[Bone].GetRotation().Inverse().RotateVector(Hinge);
		}
	}
	bBuilt = true;
	return true;
}

int32 FSandboxHandRig::JointBone(int32 Finger, int32 Joint) const
{
	return bBuilt && Finger >= 0 && Finger < 5 && Joint >= 0 && Joint < 3 ? Joints[Finger][Joint] : INDEX_NONE;
}

void FSandboxHandRig::Pose(const FSandboxHandCurls& Curls, TArray<FTransform>& OutLocal) const
{
	OutLocal = RefLocal;
	if (!bBuilt)
	{
		return;
	}
	for (int32 Finger = 0; Finger < 5; ++Finger)
	{
		for (int32 Joint = 0; Joint < 3; ++Joint)
		{
			const double Deg = Curls.Deg[Finger][Joint];
			if (Deg == 0.0)
			{
				// A joint not bent keeps its reference pose bit for bit.
				continue;
			}
			FTransform& Local = OutLocal[Joints[Finger][Joint]];
			Local.SetRotation((Local.GetRotation() * FQuat(Hinges[Finger][Joint], FMath::DegreesToRadians(Deg))).GetNormalized());
		}
	}
}

void FSandboxHandRig::ToMeshSpace(const TArray<FTransform>& Local, TArray<FTransform>& OutMesh) const
{
	OutMesh.SetNum(Local.Num());
	for (int32 Index = 0; Index < Local.Num() && Index < Parents.Num(); ++Index)
	{
		const int32 Parent = Parents[Index];
		OutMesh[Index] = Parent == INDEX_NONE ? Local[Index] : Local[Index] * OutMesh[Parent];
	}
}

FVector FSandboxHandRig::TipInMeshSpace(const TArray<FTransform>& MeshSpace, int32 Finger) const
{
	if (!bBuilt || Finger < 0 || Finger >= 5)
	{
		return FVector::ZeroVector;
	}
	const FVector Last = MeshSpace[Joints[Finger][2]].GetLocation();
	// The last segment is as long as the one before it, along the last bone's own turn of that segment.
	const FVector Before = RefMesh[Joints[Finger][2]].GetLocation() - RefMesh[Joints[Finger][1]].GetLocation();
	const FVector InLast = RefMesh[Joints[Finger][2]].GetRotation().Inverse().RotateVector(Before);
	return Last + MeshSpace[Joints[Finger][2]].GetRotation().RotateVector(InLast);
}
