#include "SandboxClipPose.h"

#include "Animation/AnimData/IAnimationDataModel.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "AnimationRuntime.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"

bool FSandboxClipPose::Load(const UAnimSequence* Sequence, const USkeletalMeshComponent* Mesh, FName PelvisBone, TConstArrayView<FSandboxClipJoint> InJoints, FString& OutError)
{
	Joints.Reset();
	Fits.Reset();
	Pelvis.Reset();
	BodyBones.Reset();
	BodyTracks.Reset();
	ReachedTracks.Reset();
	KeyCount = 0;
	KeyS = 0.0;

	const USkeletalMesh* SkeletalMesh = Mesh ? Mesh->GetSkeletalMeshAsset() : nullptr;
	const USkeleton* ClipSkeleton = Sequence ? Sequence->GetSkeleton() : nullptr;
	if (!SkeletalMesh || !ClipSkeleton)
	{
		OutError = TEXT("a clip pose needs a clip with a skeleton and a mesh");
		return false;
	}
	const double RateHz = Sequence->GetSamplingFrameRate().AsDecimal();
	const int32 Keys = Sequence->GetNumberOfSampledKeys();
	if (RateHz <= 0.0 || Keys < 1)
	{
		OutError = FString::Printf(TEXT("the clip '%s' has no keys"), *Sequence->GetPathName());
		return false;
	}

	const FReferenceSkeleton& Target = SkeletalMesh->GetRefSkeleton();
	const FReferenceSkeleton& Source = ClipSkeleton->GetReferenceSkeleton();
	const auto TargetReference = [&Target](int32 Bone) { return FAnimationRuntime::GetComponentSpaceTransformRefPose(Target, Bone); };
	const auto SourceReference = [&Source](int32 Bone) { return FAnimationRuntime::GetComponentSpaceTransformRefPose(Source, Bone); };

	// The bodies: the pelvis, and one for each joint.
	struct FBody
	{
		FName Bone;
		int32 TargetBone = INDEX_NONE;
		/** The bone of the clip whose turn the body takes, and the clip's bone of the body's own name. */
		int32 SourceBone = INDEX_NONE;
		int32 SourceSelf = INDEX_NONE;
		FQuat SourceReference = FQuat::Identity;
		FQuat TargetReference = FQuat::Identity;
		FQuat Align = FQuat::Identity;
		/** Index of the body it hangs on. */
		int32 Parent = INDEX_NONE;
		/** From the parent body's bone to the bone the joint's bone is a child of, in the mesh's reference pose. */
		FQuat ParentToBoneParent = FQuat::Identity;
		/** Where the body's bone is from its parent body's, in that bone's frame, in the mesh's reference pose: its proportions. */
		FVector OffsetInParent = FVector::ZeroVector;
		FSandboxJointLimits Limits;
	};
	TArray<FBody> Bodies;
	const auto AddBody = [&](FName Bone, FName OrientationBone, FString& Error) -> bool
	{
		FBody& Body = Bodies.AddDefaulted_GetRef();
		Body.Bone = Bone;
		Body.TargetBone = Target.FindBoneIndex(Bone);
		const FName From = OrientationBone.IsNone() ? Bone : OrientationBone;
		Body.SourceBone = Source.FindBoneIndex(From);
		Body.SourceSelf = Source.FindBoneIndex(Bone);
		const int32 TargetFrom = Target.FindBoneIndex(From);
		if (Body.TargetBone == INDEX_NONE || TargetFrom == INDEX_NONE)
		{
			Error = FString::Printf(TEXT("the mesh has no bone '%s'"), *(Body.TargetBone == INDEX_NONE ? Bone : From).ToString());
			return false;
		}
		if (Body.SourceBone == INDEX_NONE || Body.SourceSelf == INDEX_NONE)
		{
			Error = FString::Printf(TEXT("the skeleton of the clip '%s' has no bone '%s'"), *Sequence->GetPathName(), *From.ToString());
			return false;
		}
		Body.SourceReference = SourceReference(Body.SourceBone).GetRotation();
		// The body's own bone, turned as the orientation bone is: the two are rigid with each other.
		Body.TargetReference = TargetReference(Body.TargetBone).GetRotation();
		return true;
	};
	if (!AddBody(PelvisBone, NAME_None, OutError))
	{
		return false;
	}
	for (const FSandboxClipJoint& Joint : InJoints)
	{
		if (!AddBody(Joint.ChildBone, Joint.OrientationBone, OutError))
		{
			return false;
		}
	}
	Fits.SetNum(InJoints.Num());
	for (int32 Index = 0; Index < InJoints.Num(); ++Index)
	{
		const FSandboxClipJoint& Joint = InJoints[Index];
		FBody& Body = Bodies[Index + 1];
		Body.Parent = Bodies.IndexOfByPredicate([&Joint](const FBody& Each) { return Each.Bone == Joint.ParentBone; });
		if (Body.Parent == INDEX_NONE)
		{
			OutError = FString::Printf(TEXT("joint '%s' hangs on '%s', which is neither the pelvis nor a joint of the clip pose"), *Joint.ChildBone.ToString(), *Joint.ParentBone.ToString());
			return false;
		}
		const int32 BoneParent = Target.GetParentIndex(Body.TargetBone);
		Body.ParentToBoneParent = BoneParent != INDEX_NONE
			? Bodies[Body.Parent].TargetReference.Inverse() * TargetReference(BoneParent).GetRotation() : FQuat::Identity;
		Body.OffsetInParent = Bodies[Body.Parent].TargetReference.UnrotateVector(
			TargetReference(Body.TargetBone).GetLocation() - TargetReference(Bodies[Body.Parent].TargetBone).GetLocation());
		FSandboxPoseRig::ReadJointLimits(Mesh, Joint.ChildBone, Body.Limits);

		FSandboxClipJointFit& Fit = Fits[Index];
		Fit.Bone = Joint.ChildBone;
		Fit.ReferenceTurnDeg = FMath::RadiansToDegrees(Body.SourceReference.AngularDistance(TargetReference(Target.FindBoneIndex(Joint.OrientationBone.IsNone() ? Joint.ChildBone : Joint.OrientationBone)).GetRotation()));
		Fit.LimitRangeDeg = FVector(
			Body.Limits.bTwistFree ? -1.0 : Body.Limits.TwistDeg, Body.Limits.bSwing1Free ? -1.0 : Body.Limits.Swing1Deg, Body.Limits.bSwing2Free ? -1.0 : Body.Limits.Swing2Deg);
	}
	// A segment points from its joint to the joint of the one body that hangs on it.
	for (int32 Index = 0; Index < InJoints.Num(); ++Index)
	{
		if (!InJoints[Index].bAlignDirection)
		{
			continue;
		}
		FBody& Body = Bodies[Index + 1];
		int32 Next = INDEX_NONE;
		int32 Count = 0;
		for (int32 Other = 0; Other < InJoints.Num(); ++Other)
		{
			if (InJoints[Other].ParentBone == Body.Bone)
			{
				Next = Other + 1;
				++Count;
			}
		}
		const int32 SourceSelf = Source.FindBoneIndex(Body.Bone);
		const int32 SourceNext = Count == 1 ? Source.FindBoneIndex(Bodies[Next].Bone) : INDEX_NONE;
		if (SourceSelf == INDEX_NONE || SourceNext == INDEX_NONE)
		{
			continue;
		}
		const FVector SourceWay = (SourceReference(SourceNext).GetLocation() - SourceReference(SourceSelf).GetLocation()).GetSafeNormal();
		const FVector TargetWay = (TargetReference(Bodies[Next].TargetBone).GetLocation() - TargetReference(Body.TargetBone).GetLocation()).GetSafeNormal();
		if (SourceWay.IsNearlyZero() || TargetWay.IsNearlyZero())
		{
			continue;
		}
		Body.Align = FQuat::FindBetweenNormals(TargetWay, SourceWay);
		Fits[Index].ReferenceDirectionDeg = FMath::RadiansToDegrees(Body.Align.GetAngle());
	}

	// The clip's bones in the space of the whole body, key by key. A bone the clip has no keys for is where the
	// clip's skeleton has it in its reference pose. The keys as they were made, where the editor has them: the
	// compressed data is built in the background, and what a run reads must not depend on whether that has finished.
	TArray<bool> bHasKeys;
	bHasKeys.Init(true, Source.GetNum());
#if WITH_EDITOR
	if (const IAnimationDataModel* Model = Sequence->GetDataModel())
	{
		for (int32 Bone = 0; Bone < Source.GetNum(); ++Bone)
		{
			bHasKeys[Bone] = Model->IsValidBoneTrackName(Source.GetBoneName(Bone));
		}
	}
#endif
	constexpr bool bRawData = WITH_EDITOR != 0;
	TArray<FTransform> Local;
	TArray<FTransform> Space;
	TArray<bool> bSpaceKnown;
	const auto SampleKey = [&](int32 Key)
	{
		Local.SetNum(Source.GetNum());
		Space.SetNum(Source.GetNum());
		bSpaceKnown.Init(false, Source.GetNum());
		const TFunction<const FTransform&(int32)> InSpace = [&](int32 Bone) -> const FTransform&
		{
			if (!bSpaceKnown[Bone])
			{
				FTransform Own = Source.GetRefBonePose()[Bone];
				if (bHasKeys[Bone])
				{
					Sequence->GetBoneTransform(Own, FSkeletonPoseBoneIndex(Bone), FAnimExtractContext(Key / RateHz), bRawData);
				}
				const int32 Parent = Source.GetParentIndex(Bone);
				Space[Bone] = Parent != INDEX_NONE ? Own * InSpace(Parent) : Own;
				Space[Bone].NormalizeRotation();
				bSpaceKnown[Bone] = true;
			}
			return Space[Bone];
		};
		for (const FBody& Body : Bodies)
		{
			InSpace(Body.SourceBone);
			InSpace(Body.SourceSelf);
		}
	};

	Joints.SetNum(InJoints.Num());
	for (int32 Index = 0; Index < InJoints.Num(); ++Index)
	{
		const FBody& Body = Bodies[Index + 1];
		Joints[Index].MeshBoneIndex = Body.TargetBone;
		Joints[Index].Reference = Target.GetRefBonePose()[Body.TargetBone].GetRotation();
		Joints[Index].Rotations.Reserve(Keys);
	}
	Pelvis.Reserve(Keys);
	BodyTracks.SetNum(Bodies.Num());
	ReachedTracks.SetNum(Bodies.Num());
	TArray<FQuat> InRanges;
	InRanges.SetNum(InJoints.Num());
	TArray<FTransform> Reached;
	Reached.SetNum(Bodies.Num());
	for (const FBody& Body : Bodies)
	{
		BodyBones.Add(Body.Bone);
	}
	const FVector SourcePelvis = SourceReference(Bodies[0].SourceBone).GetLocation();
	const FVector TargetPelvis = TargetReference(Bodies[0].TargetBone).GetLocation();
	const double Scale = SourcePelvis.Z > 1.0 ? TargetPelvis.Z / SourcePelvis.Z : 1.0;
	TArray<FQuat> Orientation;
	Orientation.SetNum(Bodies.Num());
	for (int32 Key = 0; Key < Keys; ++Key)
	{
		SampleKey(Key);
		for (int32 Index = 0; Index < Bodies.Num(); ++Index)
		{
			const FBody& Body = Bodies[Index];
			Orientation[Index] = Carry(Space[Body.SourceBone].GetRotation(), Body.SourceReference, Body.Align, Body.TargetReference);
		}
		Pelvis.Add(FTransform(Orientation[0], Space[Bodies[0].SourceBone].GetLocation() * Scale));
		for (int32 Index = 0; Index < Bodies.Num(); ++Index)
		{
			BodyTracks[Index].Add(FTransform(Orientation[Index], Space[Bodies[Index].SourceSelf].GetLocation() * Scale));
		}
		for (int32 Index = 0; Index < InJoints.Num(); ++Index)
		{
			const FBody& Body = Bodies[Index + 1];
			// The bone the joint's bone is a child of is rigid with the parent body, at its reference turn from it.
			// What the clip asks of the joint beyond its range is not asked of the muscle: it would push into the
			// limit for as long as the pose is held. (How far beyond is kept, below.)
			const FQuat BoneParent = Orientation[Body.Parent] * Body.ParentToBoneParent;
			const FQuat InRange = FSandboxPoseRig::ClampToRange(Orientation[Body.Parent], Orientation[Index + 1], Body.Limits);
			Joints[Index].Rotations.Add((BoneParent.Inverse() * InRange).GetNormalized());
			InRanges[Index] = InRange;

			FVector Angles;
			const double Excess = FSandboxPoseRig::LimitExcessDeg(Orientation[Body.Parent], Orientation[Index + 1], Body.Limits, &Angles);
			FSandboxClipJointFit& Fit = Fits[Index];
			if (Key == 0 || Excess > Fit.LimitExcessDeg)
			{
				Fit.LimitExcessDeg = Excess;
				Fit.LimitExcessAtS = Key / RateHz;
				Fit.LimitAnglesDeg = Angles;
			}
		}
		// Where each body is when its joints are as the muscles are asked for them (each within its range), on the mesh's own
		// proportions (N7). The clip's own feet are where its skeleton put them; on stand_B these are 5 to 10 cm apart, mostly
		// the two skeletons' proportions (a hip turned 25 degrees beyond its range moved the foot less than expected).
		Reached[0] = BodyTracks[0].Last();
		for (int32 Index = 1; Index < Bodies.Num(); ++Index)
		{
			const FBody& Body = Bodies[Index];
			if (Body.Parent >= Index)
			{
				Reached[Index] = BodyTracks[Index].Last();
				continue;
			}
			const FTransform& Up = Reached[Body.Parent];
			const FQuat Own = (Up.GetRotation() * Orientation[Body.Parent].Inverse() * InRanges[Index - 1]).GetNormalized();
			Reached[Index] = FTransform(Own, Up.GetLocation() + Up.GetRotation().RotateVector(Body.OffsetInParent));
		}
		for (int32 Index = 0; Index < Bodies.Num(); ++Index)
		{
			ReachedTracks[Index].Add(Reached[Index]);
		}
	}

	KeyCount = Keys;
	KeyS = 1.0 / RateHz;
	return true;
}

void FSandboxClipPose::Locate(double TimeS, int32& OutFrom, int32& OutTo, double& OutAlpha) const
{
	const double Keys = FMath::Clamp(TimeS, 0.0, GetLengthS()) / KeyS;
	OutFrom = FMath::Clamp(FMath::FloorToInt32(Keys), 0, KeyCount - 1);
	OutTo = FMath::Min(OutFrom + 1, KeyCount - 1);
	OutAlpha = FMath::Clamp(Keys - OutFrom, 0.0, 1.0);
}

void FSandboxClipPose::Evaluate(double TimeS, TArray<FSandboxPoseBone>& OutBones) const
{
	OutBones.Reset();
	if (KeyCount == 0)
	{
		return;
	}
	int32 From, To;
	double Alpha;
	Locate(TimeS, From, To, Alpha);
	OutBones.Reserve(Joints.Num());
	for (const FJoint& Joint : Joints)
	{
		FSandboxPoseBone& Bone = OutBones.AddDefaulted_GetRef();
		Bone.MeshBoneIndex = Joint.MeshBoneIndex;
		Bone.Reference = Joint.Reference;
		Bone.Rotation = FQuat::Slerp(Joint.Rotations[From], Joint.Rotations[To], Alpha).GetNormalized();
	}
}

bool FSandboxClipPose::BodyAt(FName Bone, double TimeS, FTransform& OutTransform) const
{
	const int32 Index = BodyBones.IndexOfByKey(Bone);
	if (KeyCount == 0 || Index == INDEX_NONE)
	{
		return false;
	}
	int32 From, To;
	double Alpha;
	Locate(TimeS, From, To, Alpha);
	const TArray<FTransform>& Track = BodyTracks[Index];
	OutTransform = FTransform(FQuat::Slerp(Track[From].GetRotation(), Track[To].GetRotation(), Alpha).GetNormalized(),
		FMath::Lerp(Track[From].GetLocation(), Track[To].GetLocation(), Alpha));
	return true;
}

bool FSandboxClipPose::ReachedBodyAt(FName Bone, double TimeS, FTransform& OutTransform) const
{
	const int32 Index = BodyBones.IndexOfByKey(Bone);
	if (KeyCount == 0 || Index == INDEX_NONE || !ReachedTracks.IsValidIndex(Index))
	{
		return false;
	}
	int32 From, To;
	double Alpha;
	Locate(TimeS, From, To, Alpha);
	const TArray<FTransform>& Track = ReachedTracks[Index];
	OutTransform = FTransform(FQuat::Slerp(Track[From].GetRotation(), Track[To].GetRotation(), Alpha).GetNormalized(),
		FMath::Lerp(Track[From].GetLocation(), Track[To].GetLocation(), Alpha));
	return true;
}

FTransform FSandboxClipPose::PelvisAt(double TimeS) const
{
	if (KeyCount == 0)
	{
		return FTransform::Identity;
	}
	int32 From, To;
	double Alpha;
	Locate(TimeS, From, To, Alpha);
	return FTransform(FQuat::Slerp(Pelvis[From].GetRotation(), Pelvis[To].GetRotation(), Alpha).GetNormalized(),
		FMath::Lerp(Pelvis[From].GetLocation(), Pelvis[To].GetLocation(), Alpha));
}
