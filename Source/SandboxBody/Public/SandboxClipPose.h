#pragma once

#include "CoreMinimal.h"
#include "SandboxPoseRig.h"

class UAnimSequence;
class USkeletalMeshComponent;

/** One joint of the body, as a clip is read for it. */
struct FSandboxClipJoint
{
	/** The joint's own bone: the body it turns. */
	FName ChildBone;
	/** The bone of the body it hangs on. */
	FName ParentBone;
	/**
	 * The bone whose orientation in the clip the body takes; none: its own. A body that stands for several bones
	 * takes the last of them (the body of the head is on the first bone of the neck: with the head's own bone here,
	 * the head looks where the clip's head looks, and the whole neck's turn is in the one joint the body has).
	 */
	FName OrientationBone;
	/**
	 * Whether the body is turned so that it points the way the clip's bone points (from its joint to the next one),
	 * and not only by as much as the clip's bone is turned from its own reference pose. For the segments of arms and
	 * legs: where a hand or a foot ends up is a matter of where the segments point, and the two skeletons do not
	 * hold their limbs alike in their reference poses.
	 */
	bool bAlignDirection = false;
};

/** How one joint of a clip fits the body: what was measured when the clip was read. Degrees. */
struct FSandboxClipJointFit
{
	FName Bone;
	/** Angle between the bone's orientation in the reference pose of the clip's skeleton and of the body's mesh, in the space of the body. */
	double ReferenceTurnDeg = 0.0;
	/** Angle between the ways the segment points in the two reference poses; below 0 for a joint that is not aligned. */
	double ReferenceDirectionDeg = -1.0;
	/** The furthest the clip asks the joint beyond its range (FSandboxPoseRig::LimitExcessDeg), when, and the joint's angles then: twist, swing1, swing2. */
	double LimitExcessDeg = 0.0;
	double LimitExcessAtS = 0.0;
	FVector LimitAnglesDeg = FVector::ZeroVector;
	/** The joint's half-ranges, in the same order; below 0 for a free axis. */
	FVector LimitRangeDeg = FVector::ZeroVector;
};

/**
 * A recorded clip as a source of target poses for the body's muscles. The clip may be made for another skeleton
 * with the same bone names, and its reference pose may differ from the body's, in how the bones' axes lie and in
 * how the limbs are held. So nothing is copied bone by bone. For each body of the physics asset the clip is asked
 * how its bone is turned, in the space of the whole body, from that skeleton's own reference pose; the body's bone
 * is turned by the same from its own (and a segment of an arm or a leg is first turned to point the way the clip's
 * skeleton holds it). What comes out is one local rotation for each joint's bone, as a pose written in joint
 * angles gives (FSandboxPoseRig::Build): the bones between two bodies stay as the reference pose has them, since
 * the bodies are rigid, and what the clip does with them is in the joint. A joint the clip turns beyond its range
 * is asked for the end of its range (FSandboxPoseRig::ClampToRange); how far beyond the clip goes is reported.
 *
 * Sampled once, when loaded, at the clip's own keys, from the keys as they were made (see FSandboxIdleMotion).
 * A clip is a pose for the muscles to reach for and nothing else: the body is never moved by it.
 */
class SANDBOXBODY_API FSandboxClipPose
{
public:
	/**
	 * Reads the clip for the joints given. False with a reason if the clip or the mesh lacks one of their bones.
	 * PelvisBone is the body nothing turns: the one all the others hang on.
	 */
	bool Load(const UAnimSequence* Sequence, const USkeletalMeshComponent* Mesh, FName PelvisBone, TConstArrayView<FSandboxClipJoint> InJoints, FString& OutError);

	bool IsLoaded() const { return KeyCount > 0; }
	/** From the first key to the last, seconds. */
	double GetLengthS() const { return KeyCount > 1 ? (KeyCount - 1) * KeyS : 0.0; }
	int32 GetKeyCount() const { return KeyCount; }

	/** The pose at a time (held at the clip's ends outside them): every joint's bone with its local rotation. */
	void Evaluate(double TimeS, TArray<FSandboxPoseBone>& OutBones) const;

	/**
	 * Where the pelvis is at a time, in the space of the clip's root: its orientation as the body's pelvis bone
	 * would have it, its place scaled by how high the two pelvises stand in their reference poses.
	 */
	FTransform PelvisAt(double TimeS) const;

	/**
	 * Where the clip has the bone of one of the bodies (a joint's bone, or the pelvis) at a time, in the space of the
	 * clip's root: oriented as the body's bone would be, placed where the clip's bone is, scaled as the pelvis is.
	 * False for a bone that is not one of the bodies.
	 */
	bool BodyAt(FName Bone, double TimeS, FTransform& OutTransform) const;
	/**
	 * The same, but where the body's bone is when its joints are as the muscles are asked for them (each held to its range) on
	 * the mesh's own proportions, chained from the pelvis: a place the body can reach (N7, getup.reached_feet).
	 */
	bool ReachedBodyAt(FName Bone, double TimeS, FTransform& OutTransform) const;

	/** How each joint fits, in the order the joints were given. */
	const TArray<FSandboxClipJointFit>& GetFits() const { return Fits; }

	/**
	 * A body's orientation, in the space of the whole body, for a bone of the clip turned from its reference
	 * orientation SourceReference to Source: the body's own reference orientation TargetReference, first turned by
	 * Align (to point the way the clip's skeleton holds the segment; identity for none), then by what the clip's
	 * bone has turned.
	 */
	static FQuat Carry(const FQuat& Source, const FQuat& SourceReference, const FQuat& Align, const FQuat& TargetReference)
	{
		return (Source * SourceReference.Inverse() * Align * TargetReference).GetNormalized();
	}

private:
	struct FJoint
	{
		int32 MeshBoneIndex = INDEX_NONE;
		/** The bone's local rotation in the mesh's reference pose. */
		FQuat Reference = FQuat::Identity;
		/** The bone's local rotation, one per key. */
		TArray<FQuat> Rotations;
	};

	/** The two keys around a time and how far between them it lies. Clamped to the clip. */
	void Locate(double TimeS, int32& OutFrom, int32& OutTo, double& OutAlpha) const;

	TArray<FJoint> Joints;
	TArray<FSandboxClipJointFit> Fits;
	TArray<FTransform> Pelvis;
	/** Every body's bone, key by key: the pelvis first, then the joints in their order. */
	TArray<FName> BodyBones;
	TArray<TArray<FTransform>> BodyTracks;
	TArray<TArray<FTransform>> ReachedTracks;
	int32 KeyCount = 0;
	double KeyS = 0.0;
};
