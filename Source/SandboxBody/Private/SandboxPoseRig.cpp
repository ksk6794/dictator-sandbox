#include "SandboxPoseRig.h"

#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "PhysicsEngine/ConstraintInstance.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/PhysicsConstraintTemplate.h"

bool FSandboxPoseRig::Init(const USkeletalMeshComponent* Mesh, FString& OutError)
{
	Joints.Reset();
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
			if (Template && Template->DefaultInstance.ConstraintBone1 == Bone)
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
		if (!Template || Template->DefaultInstance.ConstraintBone1 != ChildBone)
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
		return true;
	}
	return false;
}

FQuat FSandboxPoseRig::ClampToLimits(const FQuat& ParentBone, const FQuat& ChildBone, const FSandboxJointLimits& Limits)
{
	if (!Limits.bValid)
	{
		return ChildBone;
	}
	// The child's frame relative to the parent's: twist about X, then a swing whose axis lies in Y and Z.
	const FQuat Parent = ParentBone * Limits.ParentFrame;
	const FQuat Relative = (Parent.Inverse() * (ChildBone * Limits.ChildFrame)).GetNormalized();
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
	return ((Parent * NewRelative) * Limits.ChildFrame.Inverse()).GetNormalized();
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
		Bone.Rotation = Angles.IsZero() ? Joint.Reference : (Joint.Reference * JointTurn(Joint.Frame, Angles)).GetNormalized();
	}
	return true;
}
