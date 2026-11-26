#include "SandboxPoseRig.h"

#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "PhysicsEngine/ConstraintInstance.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/PhysicsConstraintTemplate.h"

namespace
{
	bool IsRigid(const FConstraintInstance& Joint)
	{
		return Joint.GetLinearXMotion() == LCM_Locked && Joint.GetLinearYMotion() == LCM_Locked && Joint.GetLinearZMotion() == LCM_Locked;
	}
}

bool FSandboxPoseRig::JointBones(const FReferenceSkeleton& Skeleton, const FConstraintInstance& Joint, FName& OutChild, FName& OutParent)
{
	OutChild = Joint.ConstraintBone1;
	OutParent = Joint.ConstraintBone2;
	const int32 First = Skeleton.FindBoneIndex(OutChild);
	const int32 Second = Skeleton.FindBoneIndex(OutParent);
	if (First == INDEX_NONE || Second == INDEX_NONE)
	{
		return false;
	}
	if (Skeleton.BoneIsChildOf(First, Second))
	{
		return true;
	}
	Swap(OutChild, OutParent);
	return Skeleton.BoneIsChildOf(Second, First);
}

const FSandboxPoseRig::FContinuation* FSandboxPoseRig::FindContinuation(int32 Of) const
{
	return Continuations.FindByPredicate([Of](const FContinuation& Each) { return Each.Of == Of; });
}

FSandboxPoseAngles FSandboxPoseRig::Shared(const FSandboxPoseAngles& Angles, const FSandboxPoseAngles& Share, bool bRest)
{
	FSandboxPoseAngles Result;
	Result.Swing1Deg = Angles.Swing1Deg * (bRest ? 1.0 - Share.Swing1Deg : Share.Swing1Deg);
	Result.Swing2Deg = Angles.Swing2Deg * (bRest ? 1.0 - Share.Swing2Deg : Share.Swing2Deg);
	Result.TwistDeg = Angles.TwistDeg * (bRest ? 1.0 - Share.TwistDeg : Share.TwistDeg);
	return Result;
}

bool FSandboxPoseRig::Init(const USkeletalMeshComponent* Mesh, FString& OutError)
{
	Joints.Reset();
	Continuations.Reset();
	const USkeletalMesh* SkeletalMesh = Mesh ? Mesh->GetSkeletalMeshAsset() : nullptr;
	const UPhysicsAsset* Asset = Mesh ? Mesh->GetPhysicsAsset() : nullptr;
	if (!SkeletalMesh || !Asset)
	{
		OutError = TEXT("A pose needs a skeletal mesh with a physics asset");
		return false;
	}
	const FReferenceSkeleton& Skeleton = SkeletalMesh->GetRefSkeleton();
	const TArray<FTransform>& ReferencePose = Skeleton.GetRefBonePose();

	Joints.SetNum(SandboxPoseJointCount);
	for (int32 Index = 0; Index < SandboxPoseJointCount; ++Index)
	{
		const FName Bone(SandboxPoseJoints[Index]);
		for (const UPhysicsConstraintTemplate* Template : Asset->ConstraintSetup)
		{
			if (Template && Template->DefaultInstance.ConstraintBone1 == Bone && IsRigid(Template->DefaultInstance))
			{
				const int32 MeshBoneIndex = Skeleton.FindBoneIndex(Bone);
				if (MeshBoneIndex == INDEX_NONE)
				{
					OutError = FString::Printf(TEXT("Joint '%s' of the physics asset has no bone in the mesh"), *Bone.ToString());
					return false;
				}
				FJoint& Joint = Joints[Index];
				Joint.MeshBoneIndex = MeshBoneIndex;
				Joint.Reference = ReferencePose[MeshBoneIndex].GetRotation();
				Joint.Frame = Template->DefaultInstance.GetRefFrame(EConstraintFrame::Frame1).GetRotation();
				break;
			}
		}
	}

	// A joint of the trunk that no pose names, on a body that a pose does name: it goes on from that one.
	for (const UPhysicsConstraintTemplate* Template : Asset->ConstraintSetup)
	{
		FName Child, Parent;
		if (!Template || !IsRigid(Template->DefaultInstance) || !JointBones(Skeleton, Template->DefaultInstance, Child, Parent) ||
			FSandboxBodyTuning::GroupOfBone(Child) != ESandboxMuscleGroup::Neck)
		{
			continue;
		}
		const auto IndexOf = [](FName Bone)
		{
			for (int32 Index = 0; Index < SandboxPoseJointCount; ++Index)
			{
				if (Bone == FName(SandboxPoseJoints[Index]))
				{
					return Index;
				}
			}
			return static_cast<int32>(INDEX_NONE);
		};
		const int32 Of = IndexOf(Parent);
		FSandboxJointLimits Own, Before;
		if (IndexOf(Child) != INDEX_NONE || Of == INDEX_NONE || !ReadJointLimits(Mesh, Child, Own) || !ReadJointLimits(Mesh, Parent, Before))
		{
			continue;
		}
		const auto Share = [](double Mine, double Theirs) { return Mine + Theirs > 0.0 ? Mine / (Mine + Theirs) : 0.0; };
		FContinuation& Continuation = Continuations.AddDefaulted_GetRef();
		Continuation.Bone = Child;
		Continuation.Of = Of;
		Continuation.Joint.MeshBoneIndex = Skeleton.FindBoneIndex(Child);
		Continuation.Joint.Reference = ReferencePose[Continuation.Joint.MeshBoneIndex].GetRotation();
		Continuation.Joint.Frame = Template->DefaultInstance.GetRefFrame(EConstraintFrame::Frame1).GetRotation();
		Continuation.Share.Swing1Deg = Share(Own.Swing1Deg, Before.Swing1Deg);
		Continuation.Share.Swing2Deg = Share(Own.Swing2Deg, Before.Swing2Deg);
		Continuation.Share.TwistDeg = Share(Own.TwistDeg, Before.TwistDeg);
	}
	return true;
}

FQuat FSandboxPoseRig::JointTurn(const FQuat& Frame, const FSandboxPoseAngles& Angles)
{
	// The same order as the generator uses to turn a parent frame to the middle of a joint's ranges:
	// swing1 about Z, swing2 about Y, twist about X, in the joint's frame.
	const FQuat InFrame =
		FQuat(FVector::ZAxisVector, FMath::DegreesToRadians(Angles.Swing1Deg)) *
		FQuat(FVector::YAxisVector, FMath::DegreesToRadians(Angles.Swing2Deg)) *
		FQuat(FVector::XAxisVector, FMath::DegreesToRadians(Angles.TwistDeg));
	return Frame * InFrame * Frame.Inverse();
}

bool FSandboxPoseRig::ReadJointLimits(const USkeletalMeshComponent* Mesh, FName ChildBone, FSandboxJointLimits& OutLimits)
{
	OutLimits = FSandboxJointLimits();
	const UPhysicsAsset* Asset = Mesh ? Mesh->GetPhysicsAsset() : nullptr;
	if (!Asset)
	{
		return false;
	}
	for (const UPhysicsConstraintTemplate* Template : Asset->ConstraintSetup)
	{
		if (!Template || Template->DefaultInstance.ConstraintBone1 != ChildBone || !IsRigid(Template->DefaultInstance))
		{
			continue;
		}
		const FConstraintInstance& Joint = Template->DefaultInstance;
		const FConeConstraint& Cone = Joint.ProfileInstance.ConeLimit;
		const FTwistConstraint& Twist = Joint.ProfileInstance.TwistLimit;
		OutLimits.bValid = true;
		OutLimits.ChildFrame = Joint.GetRefFrame(EConstraintFrame::Frame1).GetRotation();
		OutLimits.ParentFrame = Joint.GetRefFrame(EConstraintFrame::Frame2).GetRotation();
		OutLimits.Swing1Deg = Cone.Swing1Motion == ACM_Locked ? 0.0 : Cone.Swing1LimitDegrees;
		OutLimits.Swing2Deg = Cone.Swing2Motion == ACM_Locked ? 0.0 : Cone.Swing2LimitDegrees;
		OutLimits.TwistDeg = Twist.TwistMotion == ACM_Locked ? 0.0 : Twist.TwistLimitDegrees;
		OutLimits.bSwing1Free = Cone.Swing1Motion == ACM_Free;
		OutLimits.bSwing2Free = Cone.Swing2Motion == ACM_Free;
		OutLimits.bTwistFree = Twist.TwistMotion == ACM_Free;
		// The swings in a joint of their own, limited in the child's frame: that one's swings, this one's twist.
		for (const UPhysicsConstraintTemplate* Other : Asset->ConstraintSetup)
		{
			if (!Other || Other->DefaultInstance.ConstraintBone1 != Joint.ConstraintBone2 || Other->DefaultInstance.ConstraintBone2 != ChildBone)
			{
				continue;
			}
			const FConeConstraint& Swings = Other->DefaultInstance.ProfileInstance.ConeLimit;
			OutLimits.bInChild = true;
			OutLimits.ChildFrame = Other->DefaultInstance.GetRefFrame(EConstraintFrame::Frame2).GetRotation();
			OutLimits.ParentFrame = Other->DefaultInstance.GetRefFrame(EConstraintFrame::Frame1).GetRotation();
			OutLimits.Swing1Deg = Swings.Swing1Motion == ACM_Locked ? 0.0 : Swings.Swing1LimitDegrees;
			OutLimits.Swing2Deg = Swings.Swing2Motion == ACM_Locked ? 0.0 : Swings.Swing2LimitDegrees;
			OutLimits.bSwing1Free = Swings.Swing1Motion == ACM_Free;
			OutLimits.bSwing2Free = Swings.Swing2Motion == ACM_Free;
			break;
		}
		return true;
	}
	return false;
}

double FSandboxPoseRig::LimitExcessDeg(const FQuat& ParentBone, const FQuat& ChildBone, const FSandboxJointLimits& Limits, FVector* OutAnglesDeg)
{
	if (OutAnglesDeg)
	{
		*OutAnglesDeg = FVector::ZeroVector;
	}
	if (!Limits.bValid)
	{
		return 0.0;
	}
	// The engine's joint solver (the linear one, which every joint uses unless told otherwise) takes the child's
	// frame relative to the parent's apart into a twist about X and a swing, and limits three angles each by
	// itself: the twist, and the swing's two angles 4 atan2(z, 1 + w) and 4 atan2(y, 1 + w)
	// (FPBDJointCachedSolver::InitPyramidSwingConstraint). A box, not an ellipse.
	const FQuat Parent = ParentBone * Limits.ParentFrame;
	const FQuat OfChild = ChildBone * Limits.ChildFrame;
	const FQuat Relative = (Limits.bInChild ? OfChild.Inverse() * Parent : Parent.Inverse() * OfChild).GetNormalized();
	FQuat Swing, Twist;
	Relative.ToSwingTwist(FVector::XAxisVector, Swing, Twist);
	if (Swing.W < 0.0)
	{
		Swing = FQuat(-Swing.X, -Swing.Y, -Swing.Z, -Swing.W);
	}
	const double TwistRad = Twist.GetTwistAngle(FVector::XAxisVector);
	const double Swing1Rad = 4.0 * FMath::Atan2(Swing.Z, 1.0 + Swing.W);
	const double Swing2Rad = 4.0 * FMath::Atan2(Swing.Y, 1.0 + Swing.W);
	if (OutAnglesDeg)
	{
		*OutAnglesDeg = FVector(FMath::RadiansToDegrees(TwistRad), FMath::RadiansToDegrees(Swing1Rad), FMath::RadiansToDegrees(Swing2Rad));
	}
	const auto Excess = [](bool bFree, double AngleRad, double LimitDeg)
	{
		return bFree ? 0.0 : FMath::Max(FMath::Abs(AngleRad) - FMath::DegreesToRadians(LimitDeg), 0.0);
	};
	return FMath::RadiansToDegrees(FMath::Max3(
		Excess(Limits.bTwistFree, TwistRad, Limits.TwistDeg),
		Excess(Limits.bSwing1Free, Swing1Rad, Limits.Swing1Deg),
		Excess(Limits.bSwing2Free, Swing2Rad, Limits.Swing2Deg)));
}

FQuat FSandboxPoseRig::ClampToRange(const FQuat& ParentBone, const FQuat& ChildBone, const FSandboxJointLimits& Limits)
{
	if (!Limits.bValid)
	{
		return ChildBone;
	}
	// The same three angles as LimitExcessDeg measures.
	const FQuat Parent = ParentBone * Limits.ParentFrame;
	const FQuat OfChild = ChildBone * Limits.ChildFrame;
	const FQuat Relative = (Limits.bInChild ? OfChild.Inverse() * Parent : Parent.Inverse() * OfChild).GetNormalized();
	FQuat Swing, Twist;
	Relative.ToSwingTwist(FVector::XAxisVector, Swing, Twist);
	if (Swing.W < 0.0)
	{
		Swing = FQuat(-Swing.X, -Swing.Y, -Swing.Z, -Swing.W);
	}
	double TwistRad = Twist.GetTwistAngle(FVector::XAxisVector);
	double Swing1Rad = 4.0 * FMath::Atan2(Swing.Z, 1.0 + Swing.W);
	double Swing2Rad = 4.0 * FMath::Atan2(Swing.Y, 1.0 + Swing.W);
	bool bClamped = false;
	const auto Hold = [&bClamped](bool bFree, double& AngleRad, double LimitDeg)
	{
		const double Limit = FMath::DegreesToRadians(LimitDeg);
		if (!bFree && FMath::Abs(AngleRad) > Limit)
		{
			AngleRad = FMath::Sign(AngleRad) * Limit;
			bClamped = true;
		}
	};
	Hold(Limits.bTwistFree, TwistRad, Limits.TwistDeg);
	Hold(Limits.bSwing1Free, Swing1Rad, Limits.Swing1Deg);
	Hold(Limits.bSwing2Free, Swing2Rad, Limits.Swing2Deg);
	if (!bClamped)
	{
		return ChildBone;
	}
	// The swing with those two angles: z = a (1 + w) and y = b (1 + w) with a and b the tangents of a quarter of
	// each, and a unit quaternion with no x.
	const double A = FMath::Tan(Swing1Rad * 0.25);
	const double B = FMath::Tan(Swing2Rad * 0.25);
	const double K = A * A + B * B;
	const FQuat NewSwing(0.0, 2.0 * B / (1.0 + K), 2.0 * A / (1.0 + K), (1.0 - K) / (1.0 + K));
	const FQuat NewRelative = NewSwing * FQuat(FVector::XAxisVector, TwistRad);
	return ((Parent * (Limits.bInChild ? NewRelative.Inverse() : NewRelative)) * Limits.ChildFrame.Inverse()).GetNormalized();
}

FQuat FSandboxPoseRig::ClampToLimits(const FQuat& ParentBone, const FQuat& ChildBone, const FSandboxJointLimits& Limits)
{
	if (!Limits.bValid)
	{
		return ChildBone;
	}
	// The child's frame relative to the parent's: twist about X, then a swing whose axis lies in Y and Z.
	const FQuat Parent = ParentBone * Limits.ParentFrame;
	const FQuat OfChild = ChildBone * Limits.ChildFrame;
	const FQuat Relative = (Limits.bInChild ? OfChild.Inverse() * Parent : Parent.Inverse() * OfChild).GetNormalized();
	FQuat Swing, Twist;
	Relative.ToSwingTwist(FVector::XAxisVector, Swing, Twist);

	bool bClamped = false;
	double TwistRad = Twist.GetTwistAngle(FVector::XAxisVector);
	const double TwistMax = FMath::DegreesToRadians(Limits.TwistDeg);
	if (!Limits.bTwistFree && FMath::Abs(TwistRad) > TwistMax)
	{
		TwistRad = FMath::Sign(TwistRad) * TwistMax;
		bClamped = true;
	}

	// The swing as a rotation vector: its Z part is swing1, its Y part swing2.
	FVector SwingAxis;
	double SwingRad;
	Swing.ToAxisAndAngle(SwingAxis, SwingRad);
	SwingRad = FMath::UnwindRadians(SwingRad);
	FVector SwingVector = SwingAxis * SwingRad;
	SwingVector.X = 0.0;
	const double Limit1 = FMath::DegreesToRadians(Limits.Swing1Deg);
	const double Limit2 = FMath::DegreesToRadians(Limits.Swing2Deg);
	if (!Limits.bSwing1Free && Limit1 <= 0.0 && SwingVector.Z != 0.0)
	{
		SwingVector.Z = 0.0;
		bClamped = true;
	}
	if (!Limits.bSwing2Free && Limit2 <= 0.0 && SwingVector.Y != 0.0)
	{
		SwingVector.Y = 0.0;
		bClamped = true;
	}
	const double Part1 = !Limits.bSwing1Free && Limit1 > 0.0 ? FMath::Square(SwingVector.Z / Limit1) : 0.0;
	const double Part2 = !Limits.bSwing2Free && Limit2 > 0.0 ? FMath::Square(SwingVector.Y / Limit2) : 0.0;
	const double Ellipse = Part1 + Part2;
	if (Ellipse > 1.0)
	{
		// Back along the same direction onto the ellipse; a free axis keeps its part.
		const double Scale = 1.0 / FMath::Sqrt(Ellipse);
		if (!Limits.bSwing1Free)
		{
			SwingVector.Z *= Scale;
		}
		if (!Limits.bSwing2Free)
		{
			SwingVector.Y *= Scale;
		}
		bClamped = true;
	}
	if (!bClamped)
	{
		return ChildBone;
	}
	const double Angle = SwingVector.Size();
	const FQuat NewSwing = Angle > UE_SMALL_NUMBER ? FQuat(SwingVector / Angle, Angle) : FQuat::Identity;
	const FQuat NewRelative = NewSwing * FQuat(FVector::XAxisVector, TwistRad);
	return ((Parent * (Limits.bInChild ? NewRelative.Inverse() : NewRelative)) * Limits.ChildFrame.Inverse()).GetNormalized();
}

void FSandboxPoseRig::BuildTurns(const FSandboxPose& Pose, double Weight, TArray<FSandboxPoseTurn>& OutTurns) const
{
	OutTurns.Reset();
	if (Weight == 0.0)
	{
		return;
	}
	for (int32 Index = 0; Index < Joints.Num(); ++Index)
	{
		const FJoint& Joint = Joints[Index];
		const FSandboxPoseAngles& Angles = Pose.Joints[Index];
		if (Joint.MeshBoneIndex == INDEX_NONE || Angles.IsZero())
		{
			continue;
		}
		FSandboxPoseAngles Scaled;
		Scaled.Swing1Deg = Angles.Swing1Deg * Weight;
		Scaled.Swing2Deg = Angles.Swing2Deg * Weight;
		Scaled.TwistDeg = Angles.TwistDeg * Weight;
		if (const FContinuation* Continuation = FindContinuation(Index))
		{
			OutTurns.Add(FSandboxPoseTurn{ Joint.MeshBoneIndex, JointTurn(Joint.Frame, Shared(Scaled, Continuation->Share, true)) });
			OutTurns.Add(FSandboxPoseTurn{ Continuation->Joint.MeshBoneIndex, JointTurn(Continuation->Joint.Frame, Shared(Scaled, Continuation->Share, false)) });
			continue;
		}
		OutTurns.Add(FSandboxPoseTurn{ Joint.MeshBoneIndex, JointTurn(Joint.Frame, Scaled) });
	}
}

int32 FSandboxPoseRig::MeshBoneIndexOf(FName JointBone) const
{
	for (int32 Index = 0; Index < Joints.Num(); ++Index)
	{
		if (JointBone == FName(SandboxPoseJoints[Index]))
		{
			return Joints[Index].MeshBoneIndex;
		}
	}
	for (const FContinuation& Continuation : Continuations)
	{
		if (JointBone == Continuation.Bone)
		{
			return Continuation.Joint.MeshBoneIndex;
		}
	}
	return INDEX_NONE;
}

bool FSandboxPoseRig::Build(const FSandboxPose& Pose, TArray<FSandboxPoseBone>& OutBones, FString& OutError) const
{
	OutBones.Reset();
	for (int32 Index = 0; Index < Joints.Num(); ++Index)
	{
		const FJoint& Joint = Joints[Index];
		const FSandboxPoseAngles& Angles = Pose.Joints[Index];
		if (Joint.MeshBoneIndex == INDEX_NONE)
		{
			if (!Angles.IsZero())
			{
				OutError = FString::Printf(TEXT("The pose turns joint '%s', which this physics asset does not have"), SandboxPoseJoints[Index]);
				return false;
			}
			continue;
		}
		FSandboxPoseBone& Bone = OutBones.AddDefaulted_GetRef();
		Bone.MeshBoneIndex = Joint.MeshBoneIndex;
		Bone.Reference = Joint.Reference;
		// Where the pose leaves a joint alone, the bone keeps the reference rotation exactly, not a rotation
		// that only rounds to it: a pose of zeros must change nothing in a run.
		const FContinuation* Continuation = FindContinuation(Index);
		const FSandboxPoseAngles Own = Continuation ? Shared(Angles, Continuation->Share, true) : Angles;
		Bone.Rotation = Angles.IsZero() ? Joint.Reference : (Joint.Reference * JointTurn(Joint.Frame, Own)).GetNormalized();
		if (Continuation)
		{
			FSandboxPoseBone& Next = OutBones.AddDefaulted_GetRef();
			Next.MeshBoneIndex = Continuation->Joint.MeshBoneIndex;
			Next.Reference = Continuation->Joint.Reference;
			Next.Rotation = Angles.IsZero() ? Next.Reference : (Next.Reference * JointTurn(Continuation->Joint.Frame, Shared(Angles, Continuation->Share, false))).GetNormalized();
		}
	}
	return true;
}
