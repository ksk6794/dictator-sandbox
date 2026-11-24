#include "SandboxIdleMotion.h"

#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"

bool FSandboxIdleMotion::Load(const UAnimSequence* Sequence, const USkeletalMeshComponent* Mesh, TConstArrayView<int32> MeshBones, FName PelvisBone, FString& OutError)
{
	Tracks.Reset();
	Sway.Reset();
	KeyCount = 0;
	KeyS = 0.0;

	const USkeletalMesh* SkeletalMesh = Mesh ? Mesh->GetSkeletalMeshAsset() : nullptr;
	const USkeleton* ClipSkeleton = Sequence ? Sequence->GetSkeleton() : nullptr;
	if (!SkeletalMesh || !ClipSkeleton)
	{
		OutError = TEXT("the idle motion needs a clip with a skeleton and a mesh");
		return false;
	}
	const double RateHz = Sequence->GetSamplingFrameRate().AsDecimal();
	const int32 Keys = Sequence->GetNumberOfSampledKeys();
	if (RateHz <= 0.0 || Keys < 2)
	{
		OutError = FString::Printf(TEXT("the idle clip '%s' has no keys"), *Sequence->GetPathName());
		return false;
	}

	const FReferenceSkeleton& MeshSkeleton = SkeletalMesh->GetRefSkeleton();
	const FReferenceSkeleton& Clip = ClipSkeleton->GetReferenceSkeleton();
	// The keys as they were made, where the editor has them: the compressed data is built in the background, and
	// what a run reads must not depend on whether that has finished.
	constexpr bool bRawData = WITH_EDITOR != 0;
	const auto Sample = [Sequence, RateHz, bRawData](int32 ClipBone, int32 Key)
	{
		FTransform Local = FTransform::Identity;
		Sequence->GetBoneTransform(Local, FSkeletonPoseBoneIndex(ClipBone), FAnimExtractContext(Key / RateHz), bRawData);
		return Local;
	};

	for (const int32 MeshBone : MeshBones)
	{
		const int32 ClipBone = MeshSkeleton.IsValidIndex(MeshBone) ? Clip.FindBoneIndex(MeshSkeleton.GetBoneName(MeshBone)) : INDEX_NONE;
		if (ClipBone == INDEX_NONE)
		{
			continue;
		}
		FTrack& Track = Tracks.AddDefaulted_GetRef();
		Track.MeshBoneIndex = MeshBone;
		Track.Turns.Reserve(Keys);
		const FQuat First = Sample(ClipBone, 0).GetRotation();
		for (int32 Key = 0; Key < Keys; ++Key)
		{
			FQuat Turn = Key == 0 ? FQuat::Identity : (First.Inverse() * Sample(ClipBone, Key).GetRotation()).GetNormalized();
			// The short way round, so that keys blend through small turns.
			if (Turn.W < 0.0)
			{
				Turn = FQuat(-Turn.X, -Turn.Y, -Turn.Z, -Turn.W);
			}
			Track.Turns.Add(Turn);
		}
	}
	if (Tracks.Num() == 0)
	{
		OutError = FString::Printf(TEXT("the idle clip '%s' has none of the body's bones"), *Sequence->GetPathName());
		return false;
	}

	// The pelvis's travel, in the space of the clip's root: the mesh's own space.
	Sway.Init(FVector2D::ZeroVector, Keys);
	const int32 ClipPelvis = Clip.FindBoneIndex(PelvisBone);
	if (ClipPelvis != INDEX_NONE)
	{
		TArray<int32> Chain;
		for (int32 Bone = ClipPelvis; Bone != INDEX_NONE; Bone = Clip.GetParentIndex(Bone))
		{
			Chain.Add(Bone);
		}
		const auto PelvisAt = [&Chain, &Sample](int32 Key)
		{
			FTransform Pose = FTransform::Identity;
			for (const int32 Bone : Chain)
			{
				Pose = Pose * Sample(Bone, Key);
			}
			return Pose.GetLocation();
		};
		const FVector First = PelvisAt(0);
		for (int32 Key = 1; Key < Keys; ++Key)
		{
			const FVector Offset = PelvisAt(Key) - First;
			Sway[Key] = FVector2D(Offset.X, Offset.Y);
		}
	}

	KeyCount = Keys;
	KeyS = 1.0 / RateHz;
	return true;
}

void FSandboxIdleMotion::Locate(double TimeS, int32& OutFrom, int32& OutTo, double& OutAlpha) const
{
	const double Keys = FMath::Fmod(FMath::Max(TimeS, 0.0), GetPeriodS()) / KeyS;
	OutFrom = FMath::Clamp(FMath::FloorToInt32(Keys), 0, KeyCount - 1);
	OutTo = (OutFrom + 1) % KeyCount;
	OutAlpha = FMath::Clamp(Keys - OutFrom, 0.0, 1.0);
}

void FSandboxIdleMotion::AddTurns(double TimeS, double Weight, TArray<FSandboxPoseTurn>& Turns) const
{
	if (KeyCount == 0 || Weight == 0.0)
	{
		return;
	}
	int32 From, To;
	double Alpha;
	Locate(TimeS, From, To, Alpha);
	for (const FTrack& Track : Tracks)
	{
		FQuat Turn = FQuat::Slerp(Track.Turns[From], Track.Turns[To], Alpha);
		if (Weight != 1.0)
		{
			Turn = FQuat::MakeFromRotationVector(Turn.ToRotationVector() * Weight);
		}
		const int32 Bone = Track.MeshBoneIndex;
		if (FSandboxPoseTurn* Existing = Turns.FindByPredicate([Bone](const FSandboxPoseTurn& Each) { return Each.MeshBoneIndex == Bone; }))
		{
			Existing->Turn = (Existing->Turn * Turn).GetNormalized();
		}
		else
		{
			Turns.Add(FSandboxPoseTurn{ Bone, Turn.GetNormalized() });
		}
	}
}

FVector2D FSandboxIdleMotion::SwayCm(double TimeS) const
{
	if (KeyCount == 0)
	{
		return FVector2D::ZeroVector;
	}
	int32 From, To;
	double Alpha;
	Locate(TimeS, From, To, Alpha);
	return FMath::Lerp(Sway[From], Sway[To], Alpha);
}
