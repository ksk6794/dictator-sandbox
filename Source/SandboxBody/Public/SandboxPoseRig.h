#pragma once

#include "CoreMinimal.h"
#include "SandboxBodyTuning.h"

class USkeletalMeshComponent;

/** One bone of a built pose: its rotation relative to its parent bone, as an animation pose holds it. */
struct FSandboxPoseBone
{
	/** Index of the bone in the mesh's reference skeleton. */
	int32 MeshBoneIndex = INDEX_NONE;
	/** Local rotation the pose asks for. Exactly the reference pose's own rotation where the pose is 0. */
	FQuat Rotation = FQuat::Identity;
	/** The reference pose's local rotation of the bone. */
	FQuat Reference = FQuat::Identity;
};

/** A turn added on top of a bone's pose: local rotation = pose rotation x Turn, in the bone's own space. */
struct FSandboxPoseTurn
{
	int32 MeshBoneIndex = INDEX_NONE;
	FQuat Turn = FQuat::Identity;
};

/**
 * Turns a pose written as joint angles (FSandboxPose) into bone rotations for one mesh. The axes are the
 * joints' own, read from the physics asset: the generator puts the child-side frame of every joint on the
 * anatomical axes of its spec in the reference pose (X twist, Z swing1, Y swing2; mirrored on the right),
 * so a pose and the joint limits count angles the same way.
 */
class SANDBOXBODY_API FSandboxPoseRig
{
public:
	/** Reads the joints of the mesh's physics asset. False with a reason if the mesh has none. */
	bool Init(const USkeletalMeshComponent* Mesh, FString& OutError);

	/**
	 * Every joint of the pose that the mesh has, with its rotation. A joint the physics asset lacks is left
	 * out if the pose leaves it at 0; turning it is an error.
	 */
	bool Build(const FSandboxPose& Pose, TArray<FSandboxPoseBone>& OutBones, FString& OutError) const;

	/**
	 * The joints a pose turns, scaled by Weight, as turns to add on top of another pose. Joints at 0 are left
	 * out, and so is everything when Weight is 0.
	 */
	void BuildTurns(const FSandboxPose& Pose, double Weight, TArray<FSandboxPoseTurn>& OutTurns) const;

	/** Index of a joint's bone in the mesh, INDEX_NONE if the physics asset has no such joint. */
	int32 MeshBoneIndexOf(FName JointBone) const;

	/** The rotation, relative to the reference pose and in the child bone's space, of one joint's angles. */
	static FQuat JointTurn(const FQuat& Frame, const FSandboxPoseAngles& Angles);

private:
	struct FJoint
	{
		int32 MeshBoneIndex = INDEX_NONE;
		FQuat Reference = FQuat::Identity;
		/** The joint's child-side frame, in the child bone's space. */
		FQuat Frame = FQuat::Identity;
	};
	/** In the order of SandboxPoseJoints. */
	TArray<FJoint> Joints;
};
