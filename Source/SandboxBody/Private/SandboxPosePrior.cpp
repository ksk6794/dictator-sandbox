#include "SandboxPosePrior.h"

#include "AnimationRuntime.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "HAL/FileManager.h"
#include "Misc/Crc.h"
#include "Misc/FileHelper.h"
#include "SandboxClipPose.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

namespace
{
	constexpr uint32 PriorMagic = 0x53424e50;
	constexpr int32 PriorVersion = 1;

	double AngleDeg(const FQuat4f& A, const FQuat4f& B)
	{
		return FMath::RadiansToDegrees(2.0 * FMath::Acos(FMath::Min(FMath::Abs(static_cast<double>(A | B)), 1.0)));
	}

	double AngleDeg(const FVector3f& A, const FVector3f& B)
	{
		return FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(static_cast<double>(A | B), -1.0, 1.0)));
	}

	/** The bone on the other side of the body; itself for one in the middle. */
	FName OtherSide(FName Bone)
	{
		FString Name = Bone.ToString();
		if (Name.EndsWith(TEXT("_l")))
		{
			return FName(*(Name.LeftChop(1) + TEXT("r")));
		}
		if (Name.EndsWith(TEXT("_r")))
		{
			return FName(*(Name.LeftChop(1) + TEXT("l")));
		}
		return Bone;
	}

	/** A turn seen in a mirror across the body's middle (x to -x in component space). */
	FQuat MirrorTurn(const FQuat& Turn)
	{
		return FQuat(Turn.X, -Turn.Y, -Turn.Z, Turn.W);
	}
}

bool FSandboxPosePrior::Init(const USkeletalMeshComponent* Mesh, FName InPelvis, TConstArrayView<TPair<FName, FName>> InJoints, FString& OutError)
{
	const USkeletalMesh* Asset = Mesh ? Mesh->GetSkeletalMeshAsset() : nullptr;
	if (!Asset)
	{
		OutError = TEXT("a pose database needs a mesh");
		return false;
	}
	const FReferenceSkeleton& Skeleton = Asset->GetRefSkeleton();
	const auto Reference = [&Skeleton](int32 Bone) { return FAnimationRuntime::GetComponentSpaceTransformRefPose(Skeleton, Bone).GetRotation(); };
	Pelvis = InPelvis;
	Joints = InJoints;
	const int32 PelvisBone = Skeleton.FindBoneIndex(Pelvis);
	if (PelvisBone == INDEX_NONE)
	{
		OutError = FString::Printf(TEXT("the mesh has no bone '%s'"), *Pelvis.ToString());
		return false;
	}
	PelvisReference = Reference(PelvisBone);
	for (const TPair<FName, FName>& Joint : Joints)
	{
		const int32 Bone = Skeleton.FindBoneIndex(Joint.Key);
		const int32 Parent = Joints.IndexOfByPredicate([&Joint](const TPair<FName, FName>& Each) { return Each.Key == Joint.Value; });
		const int32 Other = Joints.IndexOfByPredicate([&Joint](const TPair<FName, FName>& Each) { return Each.Key == OtherSide(Joint.Key); });
		if (Bone == INDEX_NONE || Other == INDEX_NONE || (Parent == INDEX_NONE && Joint.Value != Pelvis))
		{
			OutError = FString::Printf(TEXT("joint '%s' has no bone, no counterpart on the other side or no body to hang on"), *Joint.Key.ToString());
			return false;
		}
		const FQuat ParentReference = Parent == INDEX_NONE ? PelvisReference : Reference(Skeleton.FindBoneIndex(Joint.Value));
		Parents.Add(Parent);
		Mirrored.Add(Other);
		References.Add(Reference(Bone));
		ParentToBoneParent.Add(ParentReference.Inverse() * Reference(Skeleton.GetParentIndex(Bone)));
		MeshBones.Add(Bone);
	}
	TArray<bool> bDone;
	bDone.Init(false, Joints.Num());
	for (int32 Round = 0; Round < Joints.Num(); ++Round)
	{
		for (int32 Index = 0; Index < Joints.Num(); ++Index)
		{
			if (!bDone[Index] && (Parents[Index] == INDEX_NONE || bDone[Parents[Index]]))
			{
				Order.Add(Index);
				bDone[Index] = true;
			}
		}
	}
	return true;
}

void FSandboxPosePrior::AddClip(const FString& Name, const FSandboxClipPose& Clip, double StepS, double MinDeg)
{
	const int32 ClipIndex = Clips.Add(Name);
	const int32 Steps = FMath::FloorToInt32(Clip.GetLengthS() / StepS);
	FSandboxPriorPose Last;
	for (int32 Step = 0; Step <= Steps; ++Step)
	{
		const double TimeS = Step * StepS;
		TArray<FSandboxPoseBone> Bones;
		Clip.Evaluate(TimeS, Bones);
		FSandboxPriorPose Pose;
		for (int32 Index = 0; Index < Joints.Num(); ++Index)
		{
			const FSandboxPoseBone* Bone = Bones.FindByPredicate([this, Index](const FSandboxPoseBone& Each) { return Each.MeshBoneIndex == MeshBones[Index]; });
			const FQuat Local = Bone ? Bone->Rotation : FQuat::Identity;
			Pose.Joints.Add(FQuat4f((ParentToBoneParent[Index] * Local).GetNormalized()));
		}
		Pose.Up = FVector3f(Clip.PelvisAt(TimeS).GetRotation().UnrotateVector(FVector::UpVector));
		if (Last.Joints.Num() > 0 && DistanceDeg(Last, Pose) < MinDeg)
		{
			continue;
		}
		Last = Pose;
		Poses.Add(FEntry{ Pose, ClipIndex, static_cast<float>(TimeS), false });
		Poses.Add(FEntry{ Mirror(Pose), ClipIndex, static_cast<float>(TimeS), true });
	}
}

FSandboxPriorPose FSandboxPosePrior::Mirror(const FSandboxPriorPose& Pose) const
{
	// The whole body in component space with its pelvis leaning as it does (any heading will do), each body's turn
	// from the reference pose seen in the mirror, and given to the body on the other side.
	const FQuat PelvisTurn = FQuat::FindBetweenNormals(FVector(Pose.Up).GetSafeNormal(), PelvisReference.UnrotateVector(FVector::UpVector));
	const FQuat PelvisNow = (PelvisReference * PelvisTurn).GetNormalized();
	TArray<FQuat> Space;
	Space.SetNum(Joints.Num());
	for (const int32 Index : Order)
	{
		Space[Index] = ((Parents[Index] == INDEX_NONE ? PelvisNow : Space[Parents[Index]]) * FQuat(Pose.Joints[Index])).GetNormalized();
	}
	const FQuat PelvisMirrored = (MirrorTurn(PelvisNow * PelvisReference.Inverse()) * PelvisReference).GetNormalized();
	TArray<FQuat> SpaceMirrored;
	SpaceMirrored.SetNum(Joints.Num());
	for (int32 Index = 0; Index < Joints.Num(); ++Index)
	{
		const int32 Other = Mirrored[Index];
		SpaceMirrored[Other] = (MirrorTurn(Space[Index] * References[Index].Inverse()) * References[Other]).GetNormalized();
	}
	FSandboxPriorPose Result;
	Result.Joints.SetNum(Joints.Num());
	for (int32 Index = 0; Index < Joints.Num(); ++Index)
	{
		const FQuat& Parent = Parents[Index] == INDEX_NONE ? PelvisMirrored : SpaceMirrored[Parents[Index]];
		Result.Joints[Index] = FQuat4f((Parent.Inverse() * SpaceMirrored[Index]).GetNormalized());
	}
	Result.Up = FVector3f(PelvisMirrored.UnrotateVector(FVector::UpVector));
	return Result;
}

double FSandboxPosePrior::DistanceDeg(const FSandboxPriorPose& A, const FSandboxPriorPose& B)
{
	double Sum = AngleDeg(A.Up, B.Up);
	const int32 Count = FMath::Min(A.Joints.Num(), B.Joints.Num());
	for (int32 Index = 0; Index < Count; ++Index)
	{
		Sum += AngleDeg(A.Joints[Index], B.Joints[Index]);
	}
	return Sum / (Count + 1);
}

bool FSandboxPosePrior::PoseOf(const TArray<FSandboxBodySample>& Bodies, FSandboxPriorPose& OutPose) const
{
	const auto Rotation = [&Bodies](FName Bone, FQuat& OutRotation)
	{
		const FSandboxBodySample* Body = Bodies.FindByPredicate([Bone](const FSandboxBodySample& Each) { return Each.Bone == Bone; });
		OutRotation = Body ? Body->WorldTransform.GetRotation() : FQuat::Identity;
		return Body != nullptr;
	};
	OutPose.Joints.Reset();
	FQuat Now;
	if (!Rotation(Pelvis, Now))
	{
		return false;
	}
	OutPose.Up = FVector3f(Now.UnrotateVector(FVector::UpVector));
	for (const TPair<FName, FName>& Joint : Joints)
	{
		FQuat Child;
		FQuat Parent;
		if (!Rotation(Joint.Key, Child) || !Rotation(Joint.Value, Parent))
		{
			return false;
		}
		OutPose.Joints.Add(FQuat4f((Parent.Inverse() * Child).GetNormalized()));
	}
	return true;
}

FSandboxPriorMatch FSandboxPosePrior::FindNearest(const FSandboxPriorPose& Pose) const
{
	FSandboxPriorMatch Match;
	double Best = TNumericLimits<double>::Max();
	const FEntry* Nearest = nullptr;
	for (const FEntry& Entry : Poses)
	{
		// The sum so far is only ever added to: a pose that is already further than the best is left.
		double Sum = AngleDeg(Entry.Pose.Up, Pose.Up);
		for (int32 Index = 0; Index < Pose.Joints.Num() && Sum < Best; ++Index)
		{
			Sum += AngleDeg(Entry.Pose.Joints[Index], Pose.Joints[Index]);
		}
		if (Sum < Best)
		{
			Best = Sum;
			Nearest = &Entry;
		}
	}
	if (Nearest)
	{
		Match.DistanceDeg = Best / (Pose.Joints.Num() + 1);
		Match.Clip = Clips[Nearest->Clip];
		Match.TimeS = Nearest->TimeS;
		Match.bMirrored = Nearest->bMirrored;
	}
	return Match;
}

bool FSandboxPosePrior::Check(int32 Every, FString& OutError) const
{
	for (int32 Index = 0; Index < Poses.Num(); Index += FMath::Max(Every, 1))
	{
		const FSandboxPriorPose& Pose = Poses[Index].Pose;
		const double FoundDeg = FindNearest(Pose).DistanceDeg;
		const double TwiceDeg = DistanceDeg(Mirror(Mirror(Pose)), Pose);
		const FSandboxPriorPose Image = Mirror(Pose);
		double SidesDeg = 0.0;
		for (int32 Joint = 0; Joint < Joints.Num(); ++Joint)
		{
			const auto FromReference = [this](const FSandboxPriorPose& Of, int32 Which)
			{
				const FQuat& Parent = Parents[Which] == INDEX_NONE ? PelvisReference : References[Parents[Which]];
				return AngleDeg(FQuat4f((Parent.Inverse() * References[Which]).GetNormalized()), Of.Joints[Which]);
			};
			SidesDeg = FMath::Max(SidesDeg, FMath::Abs(FromReference(Pose, Joint) - FromReference(Image, Mirrored[Joint])));
		}
		if (FoundDeg > 0.2 || TwiceDeg > 0.2 || SidesDeg > 1.0)
		{
			OutError = FString::Printf(TEXT("pose %d of the database (%s at %.1f s) is found at %.2f degrees, twice mirrored it is %.2f degrees from itself, and a joint of its mirror image is turned %.2f degrees otherwise than its counterpart"),
				Index, *Clips[Poses[Index].Clip], Poses[Index].TimeS, FoundDeg, TwiceDeg, SidesDeg);
			return false;
		}
	}
	return Poses.Num() > 0;
}

uint32 FSandboxPosePrior::GetStamp() const
{
	uint32 Stamp = FCrc::MemCrc32(&PriorVersion, sizeof(PriorVersion));
	for (const FString& Clip : Clips)
	{
		Stamp = FCrc::StrCrc32(*Clip, Stamp);
	}
	for (const FEntry& Entry : Poses)
	{
		Stamp = FCrc::MemCrc32(Entry.Pose.Joints.GetData(), Entry.Pose.Joints.Num() * sizeof(FQuat4f), Stamp);
		Stamp = FCrc::MemCrc32(&Entry.Pose.Up, sizeof(Entry.Pose.Up), Stamp);
	}
	return Stamp;
}

bool FSandboxPosePrior::Save(const FString& File, FString& OutError) const
{
	TArray<uint8> Bytes;
	FMemoryWriter Writer(Bytes);
	uint32 Magic = PriorMagic;
	int32 Version = PriorVersion;
	Writer << Magic << Version;
	int32 JointCount = Joints.Num();
	Writer << JointCount;
	for (const TPair<FName, FName>& Joint : Joints)
	{
		FString Name = Joint.Key.ToString();
		Writer << Name;
	}
	TArray<FString> Names = Clips;
	Writer << Names;
	int32 Count = Poses.Num();
	Writer << Count;
	for (const FEntry& Entry : Poses)
	{
		FEntry Copy = Entry;
		Writer << Copy.Clip << Copy.TimeS << Copy.bMirrored << Copy.Pose.Up;
		for (FQuat4f& Joint : Copy.Pose.Joints)
		{
			Writer << Joint;
		}
	}
	if (!FFileHelper::SaveArrayToFile(Bytes, *File))
	{
		OutError = FString::Printf(TEXT("cannot write '%s'"), *File);
		return false;
	}
	return true;
}

bool FSandboxPosePrior::Load(const FString& File, FString& OutError)
{
	TArray<uint8> Bytes;
	if (!FFileHelper::LoadFileToArray(Bytes, *File))
	{
		OutError = FString::Printf(TEXT("cannot read the pose database '%s' (Tools\\pose-prior.ps1 -Build makes it)"), *File);
		return false;
	}
	FMemoryReader Reader(Bytes);
	uint32 Magic = 0;
	int32 Version = 0;
	int32 JointCount = 0;
	Reader << Magic << Version << JointCount;
	bool bSame = Magic == PriorMagic && Version == PriorVersion && JointCount == Joints.Num();
	for (int32 Index = 0; bSame && Index < JointCount; ++Index)
	{
		FString Name;
		Reader << Name;
		bSame = Name == Joints[Index].Key.ToString();
	}
	if (!bSame)
	{
		OutError = FString::Printf(TEXT("the pose database '%s' was made for another body or by another version; build it again"), *File);
		return false;
	}
	Reader << Clips;
	int32 Count = 0;
	Reader << Count;
	Poses.SetNum(Count);
	for (FEntry& Entry : Poses)
	{
		Reader << Entry.Clip << Entry.TimeS << Entry.bMirrored << Entry.Pose.Up;
		Entry.Pose.Joints.SetNum(JointCount);
		for (FQuat4f& Joint : Entry.Pose.Joints)
		{
			Reader << Joint;
		}
	}
	if (Reader.IsError() || Count == 0)
	{
		OutError = FString::Printf(TEXT("the pose database '%s' is broken or empty; build it again"), *File);
		return false;
	}
	return true;
}
