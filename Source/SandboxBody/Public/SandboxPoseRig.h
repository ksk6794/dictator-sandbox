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
 * The range of one joint, as the physics asset sets it: the frames on either side (each in its own bone's
 * space; the parent-side frame turned to the middle of the range) and the half-ranges. A locked axis has 0,
 * a free one is not limited.
 */
struct FSandboxJointLimits
{
	bool bValid = false;
	FQuat ParentFrame = FQuat::Identity;
	FQuat ChildFrame = FQuat::Identity;
	/** Half-ranges, degrees: swing1 (about the frame's Z), swing2 (about its Y), twist (about its X). */
	double Swing1Deg = 0.0;
	double Swing2Deg = 0.0;
	double TwistDeg = 0.0;
	bool bSwing1Free = false;
	bool bSwing2Free = false;
	bool bTwistFree = false;
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

	/** The range of the joint whose child is ChildBone in the mesh's physics asset. False if there is no such joint. */
	static bool ReadJointLimits(const USkeletalMeshComponent* Mesh, FName ChildBone, FSandboxJointLimits& OutLimits);

	/**
	 * A child bone's rotation (component space) brought into its joint's range, the parent's rotation given: the
	 * twist clamped to its half-range, the swing scaled back onto the ellipse of the two swing half-ranges. Inside
	 * that the rotation comes back exactly as it was given. The ellipse lies inside what the solver allows, which
	 * is each swing up to its own half-range (LimitExcessDeg): a target clamped here is never asked beyond the
	 * joint's range, and is kept out of the corners of it. (It was written when the solver was believed to limit by
	 * the ellipse, and left as it is: the feet it is used for step and stand as they were tuned with it.)
	 */
	static FQuat ClampToLimits(const FQuat& ParentBone, const FQuat& ChildBone, const FSandboxJointLimits& Limits);

	/**
	 * How far a joint is outside its range, degrees: the largest of what the twist and each of the two swings is
	 * beyond its own half-range. Measured as the engine's joint solver limits a joint, each angle by itself (a box;
	 * see the function), so 0 inside the range. The engine's own FConstraintInstance::GetCurrentSwing1 is not the
	 * solver's angle: it is the twist of the whole turn about the swing axis, and reads a joint that swings and
	 * twists at once as far outside a range it is inside of. OutAnglesDeg takes the joint's angles as measured
	 * here: twist (X), swing1 (Y), swing2 (Z).
	 */
	static double LimitExcessDeg(const FQuat& ParentBone, const FQuat& ChildBone, const FSandboxJointLimits& Limits, FVector* OutAnglesDeg = nullptr);

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
