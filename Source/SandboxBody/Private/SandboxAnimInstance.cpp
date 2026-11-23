#include "SandboxAnimInstance.h"

#include "Animation/AnimNodeBase.h"
#include "BonePose.h"
#include "TwoBoneIK.h"

namespace
{
	FQuat Blend(const FSandboxPosedBone& Bone, double Weight)
	{
		// The ends exactly, so that a finished blend leaves the very rotation it was given.
		if (Weight <= 0.0)
		{
			return Bone.From;
		}
		if (Weight >= 1.0)
		{
			return Bone.To;
		}
		return FQuat::Slerp(Bone.From, Bone.To, Weight).GetNormalized();
	}
}

void USandboxAnimInstance::SetTargetPose(const TArray<FSandboxPoseBone>& Pose, double InBlendS)
{
	bool bSame = true;
	for (const FSandboxPoseBone& Target : Pose)
	{
		const FSandboxPosedBone* Current = Bones.FindByPredicate([&Target](const FSandboxPosedBone& Bone) { return Bone.MeshBoneIndex == Target.MeshBoneIndex; });
		const FQuat CurrentTarget = Current ? Current->To : Target.Reference;
		if (!(CurrentTarget == Target.Rotation))
		{
			bSame = false;
			break;
		}
	}
	if (bSame)
	{
		return;
	}

	const double Weight = GetBlendWeight();
	for (const FSandboxPoseBone& Target : Pose)
	{
		FSandboxPosedBone* Current = Bones.FindByPredicate([&Target](const FSandboxPosedBone& Bone) { return Bone.MeshBoneIndex == Target.MeshBoneIndex; });
		if (!Current)
		{
			// A bone that has never been turned is not posed at all until a pose turns it.
			if (Target.Rotation == Target.Reference)
			{
				continue;
			}
			Current = &Bones.AddDefaulted_GetRef();
			Current->MeshBoneIndex = Target.MeshBoneIndex;
			Current->From = Target.Reference;
			Current->To = Target.Reference;
		}
		Current->From = Blend(*Current, Weight);
		Current->To = Target.Rotation;
	}
	BlendS = InBlendS;
	BlendElapsedS = 0.0;
}

double USandboxAnimInstance::GetBlendWeight() const
{
	if (BlendS <= 0.0 || BlendElapsedS >= BlendS)
	{
		return 1.0;
	}
	return FMath::SmoothStep(0.0, 1.0, BlendElapsedS / BlendS);
}

void USandboxAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);
	if (BlendElapsedS < BlendS)
	{
		BlendElapsedS = FMath::Min(BlendElapsedS + DeltaSeconds, BlendS);
	}
	for (int32 Index = LimbStates.Num() - 1; Index >= 0; --Index)
	{
		FLimbState& Limb = LimbStates[Index];
		Limb.ElapsedS = FMath::Min(Limb.ElapsedS + DeltaSeconds, FMath::Max(Limb.BlendS, 0.0));
		Limb.Target.Weight = Limb.CurrentWeight();
		// A limb let go is dropped once it is back in the pose.
		if (Limb.ToWeight <= 0.0 && Limb.Target.Weight <= 0.0)
		{
			LimbStates.RemoveAt(Index);
		}
	}
}

void FSandboxAnimInstanceProxy::PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds)
{
	FAnimInstanceProxy::PreUpdate(InAnimInstance, DeltaSeconds);
	if (const USandboxAnimInstance* Instance = Cast<USandboxAnimInstance>(InAnimInstance))
	{
		Bones = Instance->Bones;
		Turns = Instance->Turns;
		Limbs.Reset();
		for (const USandboxAnimInstance::FLimbState& Limb : Instance->LimbStates)
		{
			if (Limb.Target.Weight > 0.0)
			{
				Limbs.Add(Limb.Target);
			}
		}
		Weight = Instance->GetBlendWeight();
	}
}

bool FSandboxAnimInstanceProxy::Evaluate(FPoseContext& Output)
{
	Output.ResetToRefPose();
	const FBoneContainer& Container = Output.Pose.GetBoneContainer();
	for (const FSandboxPosedBone& Bone : Bones)
	{
		const FCompactPoseBoneIndex Index = Container.MakeCompactPoseIndex(FMeshPoseBoneIndex(Bone.MeshBoneIndex));
		if (Index != INDEX_NONE)
		{
			Output.Pose[Index].SetRotation(Blend(Bone, Weight));
		}
	}
	for (const FSandboxPoseTurn& Turn : Turns)
	{
		const FCompactPoseBoneIndex Index = Container.MakeCompactPoseIndex(FMeshPoseBoneIndex(Turn.MeshBoneIndex));
		if (Index != INDEX_NONE)
		{
			Output.Pose[Index].SetRotation((Output.Pose[Index].GetRotation() * Turn.Turn).GetNormalized());
		}
	}
	if (Limbs.Num() > 0)
	{
		PlaceLimbs(Output);
	}
	return true;
}

void FSandboxAnimInstanceProxy::PlaceLimbs(FPoseContext& Output) const
{
	const FBoneContainer& Container = Output.Pose.GetBoneContainer();
	FCSPose<FCompactPose> ComponentPose;
	ComponentPose.InitPose(Output.Pose);
	// The pose is in the component's space, and the component follows the simulated body.
	const FTransform& Component = GetComponentTransform();

	for (const FSandboxLimbTarget& Limb : Limbs)
	{
		const FCompactPoseBoneIndex End = Container.MakeCompactPoseIndex(FMeshPoseBoneIndex(Limb.EndMeshBoneIndex));
		if (End == INDEX_NONE)
		{
			continue;
		}
		const FCompactPoseBoneIndex Middle = Container.GetParentBoneIndex(End);
		const FCompactPoseBoneIndex Root = Middle != INDEX_NONE ? Container.GetParentBoneIndex(Middle) : FCompactPoseBoneIndex(INDEX_NONE);
		if (Root == INDEX_NONE)
		{
			continue;
		}

		FTransform RootTransform = ComponentPose.GetComponentSpaceTransform(Root);
		FTransform MiddleTransform = ComponentPose.GetComponentSpaceTransform(Middle);
		FTransform EndTransform = ComponentPose.GetComponentSpaceTransform(End);
		const FVector Target = Component.InverseTransformPosition(Limb.WorldLocation);
		const FVector Effector = FMath::Lerp(EndTransform.GetLocation(), Target, FMath::Clamp(Limb.Weight, 0.0, 1.0));
		// The knee bends toward the front of the mesh (+y, where the character faces), not the way the pose
		// happens to bend it: a standing leg is almost straight, and its knee could sit a hair behind the line
		// from hip to ankle and have the solver fold it backward.
		const FVector Along = (EndTransform.GetLocation() - RootTransform.GetLocation()).GetSafeNormal();
		const FVector Bend = (FVector::YAxisVector - Along * Along.Y).GetSafeNormal();
		const FVector Pole = MiddleTransform.GetLocation() + Bend * 50.0;

		AnimationCore::SolveTwoBoneIK(RootTransform, MiddleTransform, EndTransform, Pole, Effector, /*bAllowStretching*/ false, 1.0, 1.0);
		const FBoneTransform Placed[] = { FBoneTransform(Root, RootTransform), FBoneTransform(Middle, MiddleTransform), FBoneTransform(End, EndTransform) };
		ComponentPose.SafeSetCSBoneTransforms(Placed);
	}
	FCSPose<FCompactPose>::ConvertComponentPosesToLocalPoses(MoveTemp(ComponentPose), Output.Pose);
}

void USandboxAnimInstance::SetLimbTarget(int32 EndMeshBoneIndex, const FVector& WorldLocation, double Weight, double InBlendS)
{
	FLimbState* State = LimbStates.FindByPredicate([EndMeshBoneIndex](const FLimbState& Limb) { return Limb.Target.EndMeshBoneIndex == EndMeshBoneIndex; });
	if (!State)
	{
		if (Weight <= 0.0)
		{
			return;
		}
		State = &LimbStates.AddDefaulted_GetRef();
		State->Target.EndMeshBoneIndex = EndMeshBoneIndex;
	}
	State->FromWeight = State->CurrentWeight();
	State->ToWeight = FMath::Clamp(Weight, 0.0, 1.0);
	State->BlendS = InBlendS;
	State->ElapsedS = 0.0;
	State->Target.WorldLocation = WorldLocation;
}

FAnimInstanceProxy* USandboxAnimInstance::CreateAnimInstanceProxy()
{
	return new FSandboxAnimInstanceProxy(this);
}

void USandboxAnimInstance::DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy)
{
	delete InProxy;
}
