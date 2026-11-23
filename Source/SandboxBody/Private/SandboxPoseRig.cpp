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
