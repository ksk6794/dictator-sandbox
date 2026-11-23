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
				FSandboxLimbTarget& Added = Limbs.Add_GetRef(Limb.Target);
				if (const FSandboxJointLimits* Limits = Instance->EndLimits.Find(Limb.Target.EndMeshBoneIndex))
				{
					Added.EndLimits = *Limits;
				}
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
		const FVector Target = (Limb.bInFrame ? Limb.Frame : Component).InverseTransformPosition(Limb.WorldLocation);
		FVector Effector = FMath::Lerp(EndTransform.GetLocation(), Target, FMath::Clamp(Limb.Weight, 0.0, 1.0));
		if (Limb.ReachShare > 0.0)
		{
			// Within reach (FSandboxLimbTarget::ReachShare), along the line from the root.
			const double ReachCm = Limb.ReachShare * (FVector::Dist(RootTransform.GetLocation(), MiddleTransform.GetLocation()) +
				FVector::Dist(MiddleTransform.GetLocation(), EndTransform.GetLocation()));
			const FVector FromRoot = Effector - RootTransform.GetLocation();
			if (FromRoot.SizeSquared() > ReachCm * ReachCm)
			{
				Effector = RootTransform.GetLocation() + FromRoot.GetSafeNormal() * ReachCm;
			}
		}
		// The knee bends toward the front of the mesh (+y, where the character faces), not the way the pose
		// happens to bend it: a standing leg is almost straight, and its knee could sit a hair behind the line
		// from hip to ankle and have the solver fold it backward.
		// Forward across the line from the root to where the end is to go, not to where the pose has it: with a pole
		// 50 cm ahead of the pose's knee, a foot sent 50 cm forward put that line almost through the pole, the plane of
		// the bend was all but undefined, and the thigh's twist jumped by up to 150 degrees from one frame to the next
		// (the body's thigh at 163 rad/s).
		const FVector Along = (EndTransform.GetLocation() - RootTransform.GetLocation()).GetSafeNormal();
		const FVector ToEffector = Effector - RootTransform.GetLocation();
		const FVector Line = ToEffector.IsNearlyZero() ? Along : ToEffector.GetSafeNormal();
		FVector Bend = (FVector::YAxisVector - Line * Line.Y).GetSafeNormal();
		if (Bend.IsNearlyZero())
		{
			Bend = (FVector::YAxisVector - Along * Along.Y).GetSafeNormal();
		}
		const FVector Pole = RootTransform.GetLocation() + ToEffector * 0.5 + Bend * 50.0;

		const FTransform RootBefore = RootTransform;
		const FTransform MiddleBefore = MiddleTransform;
		const FVector EndBefore = EndTransform.GetLocation();
		AnimationCore::SolveTwoBoneIK(RootTransform, MiddleTransform, EndTransform, Pole, Effector, /*bAllowStretching*/ false, 1.0, 1.0);

		// The solver turns each bone by the least rotation that points it at its new end, which leaves the
		// thigh's twist as it was: for an end off to the side, the plane the knee then has to bend in is not
		// the one its hinge allows, the joint's limits hold the shin in the hinge's plane, and the foot lands
		// elsewhere (a step 10 cm out from the hip went 14 cm in). So both bones are turned as frames instead:
		// the bone's direction and the hinge's axis (across the leg, the mesh's x in a straight leg, otherwise
		// across the bend) from where they were to where they now are. The knee bends about its hinge alone,
		// and the thigh twists to carry the hinge into the plane of the bend.
		{
			const FVector Hip = RootBefore.GetLocation();
			const FVector KneeBefore = MiddleBefore.GetLocation();
			const FVector KneeNow = MiddleTransform.GetLocation();
			const FVector EndNow = EndTransform.GetLocation();
			const auto HingeOf = [](const FVector& Upper, const FVector& Lower, const FVector& Fallback)
			{
				// Across the bend, signed as the fallback for a leg bent forward at the knee; the fallback for
				// a leg too straight to tell, and a blend of the two in between. (Switched from one to the other
				// at a threshold, the hinge of a leg reaching out to the side jumped by 17 degrees as the leg
				// straightened, and a standing leg near the threshold had its thigh's twist flip every frame:
				// the foot was kicked off the floor with 5000 N.)
				const FVector Across = FVector::CrossProduct(Upper, Lower);
				const FVector Along = Upper.GetSafeNormal();
				const FVector Straight = (Fallback - Along * (Fallback | Along)).GetSafeNormal();
				const double Sine = Across.Size() / FMath::Max(Upper.Size() * Lower.Size(), UE_SMALL_NUMBER);
				constexpr double StraightSine = 0.05;
				constexpr double BentSine = 0.15;
				if (Sine <= StraightSine)
				{
					return Straight;
				}
				FVector Axis = Across.GetSafeNormal();
				Axis = (Axis | Fallback) >= 0.0 ? Axis : -Axis;
				const double Bent = FMath::SmoothStep(StraightSine, BentSine, Sine);
				return Bent >= 1.0 ? Axis : (Straight * (1.0 - Bent) + Axis * Bent).GetSafeNormal();
			};
			const auto Basis = [](const FVector& Along, const FVector& Hinge)
			{
				const FVector X = Along.GetSafeNormal();
				const FVector Y = (Hinge - X * (Hinge | X)).GetSafeNormal();
				return FQuat(FMatrix(X, Y, FVector::CrossProduct(X, Y), FVector::ZeroVector));
			};
			// The knee bends toward +y: thigh down and forward, shin down and back, so the hinge is -x.
			const FVector HingeBefore = HingeOf(KneeBefore - Hip, EndBefore - KneeBefore, -FVector::XAxisVector);
			const FQuat Least = FQuat::FindBetweenVectors(KneeBefore - Hip, KneeNow - Hip);
			const FVector HingeNow = HingeOf(KneeNow - Hip, EndNow - KneeNow, Least.RotateVector(HingeBefore));
			const FQuat RootTurn = Basis(KneeNow - Hip, HingeNow) * Basis(KneeBefore - Hip, HingeBefore).Inverse();
			const FQuat MiddleTurn = Basis(EndNow - KneeNow, HingeNow) * Basis(EndBefore - KneeBefore, HingeBefore).Inverse();
			RootTransform.SetRotation((RootTurn * RootBefore.GetRotation()).GetNormalized());
			MiddleTransform.SetRotation((MiddleTurn * MiddleBefore.GetRotation()).GetNormalized());
		}
		if (Limb.bEndInFrame)
		{
			// The pose's orientation of the end, taken relative to the given frame rather than to the component.
			const FQuat Level = (Component.GetRotation().Inverse() * Limb.EndFrame * EndTransform.GetRotation()).GetNormalized();
			EndTransform.SetRotation(FQuat::Slerp(EndTransform.GetRotation(), Level, FMath::Clamp(Limb.Weight, 0.0, 1.0)).GetNormalized());
		}
		if (Limb.EndLimits.bValid)
		{
			// Not more than the ankle can do (FSandboxLimbTarget::EndLimitWeight): the rest of the turn stays with the
			// leg. Its twist is never asked for beyond its range, on the floor either: the floor holds a foot flat,
			// but only friction holds it from turning about the shin, and a planted foot asked for 30 degrees of
			// twist against 15 was spun round by its own ankle and slid 9 cm out under the body's weight.
			FSandboxJointLimits TwistOnly = Limb.EndLimits;
			TwistOnly.bSwing1Free = true;
			TwistOnly.bSwing2Free = true;
			const FQuat Twisted = FSandboxPoseRig::ClampToLimits(MiddleTransform.GetRotation(), EndTransform.GetRotation(), TwistOnly);
			if (Limb.EndLimitWeight <= 0.0)
			{
				EndTransform.SetRotation(Twisted);
			}
			else
			{
				const FQuat Limited = FSandboxPoseRig::ClampToLimits(MiddleTransform.GetRotation(), EndTransform.GetRotation(), Limb.EndLimits);
				EndTransform.SetRotation(Limb.EndLimitWeight >= 1.0 ? Limited : FQuat::Slerp(Twisted, Limited, Limb.EndLimitWeight).GetNormalized());
			}
		}
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

void USandboxAnimInstance::MoveLimbTarget(int32 EndMeshBoneIndex, const FVector& WorldLocation, const FTransform* Frame, const FQuat* EndFrame,
	double EndLimitWeight, double ReachShare)
{
	FLimbState* State = LimbStates.FindByPredicate([EndMeshBoneIndex](const FLimbState& Limb) { return Limb.Target.EndMeshBoneIndex == EndMeshBoneIndex; });
	if (!State)
	{
		return;
	}
	State->Target.WorldLocation = WorldLocation;
	State->Target.bInFrame = Frame != nullptr;
	State->Target.Frame = Frame ? *Frame : FTransform::Identity;
	State->Target.bEndInFrame = EndFrame != nullptr;
	State->Target.EndFrame = EndFrame ? *EndFrame : FQuat::Identity;
	State->Target.EndLimitWeight = FMath::Clamp(EndLimitWeight, 0.0, 1.0);
	State->Target.ReachShare = FMath::Max(ReachShare, 0.0);
}

double USandboxAnimInstance::GetLimbWeight(int32 EndMeshBoneIndex) const
{
	const FLimbState* State = LimbStates.FindByPredicate([EndMeshBoneIndex](const FLimbState& Limb) { return Limb.Target.EndMeshBoneIndex == EndMeshBoneIndex; });
	return State ? State->CurrentWeight() : 0.0;
}

FAnimInstanceProxy* USandboxAnimInstance::CreateAnimInstanceProxy()
{
	return new FSandboxAnimInstanceProxy(this);
}

void USandboxAnimInstance::DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy)
{
	delete InProxy;
}
