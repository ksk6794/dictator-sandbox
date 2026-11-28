#pragma once

#include "CoreMinimal.h"

/**
 * How far each finger joint is bent from a hand mesh's reference pose, degrees (the player's visible hands, docs/active-ragdoll.md,
 * section 8, item 4). Fingers 0 the thumb, 1 the index, 2 the middle, 3 the ring, 4 the little finger; joints 0 at the knuckle (for the
 * thumb its base, which brings it across the palm), 1 the middle, 2 the tip. A positive angle bends toward the palm.
 */
struct SANDBOXINTERACTION_API FSandboxHandCurls
{
	double Deg[5][3] = {};
};

/**
 * The fingers of a hand mesh as arithmetic (no Animation Blueprint, no engine object): built from the reference skeleton's names,
 * parents and local poses, it gives the local pose of every bone for a set of curls. Each joint bends about its own hinge: the axis
 * across its segment and the palm's normal, found in the reference pose, so that a positive angle brings the segment toward the palm.
 * It also gives the hand's own frame in the mesh's space, the one every effector is moved in (x along the fingers, z toward the thumb,
 * y = z ^ x; the palm faces -y on a right hand and +y on a left one), so that a mesh is laid on its effector by its bones, not by
 * numbers read off the asset.
 */
class SANDBOXINTERACTION_API FSandboxHandRig
{
public:
	/**
	 * From a reference skeleton: Names, Parents (INDEX_NONE for a root; a parent before its child) and RefLocal, each bone's pose in
	 * its parent's space. Side is the bones' suffix ("l" or "r": hand_r, index_01_r, ...). False, with the reason, if a bone the hand
	 * needs is missing or the hand's frame cannot be found.
	 */
	bool Build(const TArray<FName>& Names, const TArray<int32>& Parents, const TArray<FTransform>& RefLocal, bool bLeft, FString& OutError);

	bool IsBuilt() const { return bBuilt; }
	bool IsLeft() const { return bLeftHand; }
	int32 NumBones() const { return RefLocal.Num(); }
	/** The hand's own frame in the mesh's space, with its origin at the wrist (the hand bone). */
	const FTransform& GetHandFrame() const { return HandFrame; }
	/** The palm's normal in the mesh's space (the way the palm faces). */
	const FVector& GetPalmNormal() const { return PalmNormal; }
	/** The bone index of a finger's joint, INDEX_NONE if there is none. */
	int32 JointBone(int32 Finger, int32 Joint) const;
	/** Every bone's local pose for Curls: the reference pose, each finger joint turned about its hinge. */
	void Pose(const FSandboxHandCurls& Curls, TArray<FTransform>& OutLocal) const;
	/** The poses of every bone in the mesh's space for local poses as Pose gives them. */
	void ToMeshSpace(const TArray<FTransform>& Local, TArray<FTransform>& OutMesh) const;
	/** Where a finger's tip is in the mesh's space for those poses: past its last joint by that joint's segment from the joint before. */
	FVector TipInMeshSpace(const TArray<FTransform>& MeshSpace, int32 Finger) const;

private:
	bool bBuilt = false;
	bool bLeftHand = false;
	TArray<FName> Names;
	TArray<int32> Parents;
	TArray<FTransform> RefLocal;
	TArray<FTransform> RefMesh;
	int32 Joints[5][3] = {};
	/** Each joint's hinge in its own bone's reference frame. */
	FVector Hinges[5][3];
	FTransform HandFrame = FTransform::Identity;
	FVector PalmNormal = FVector::ZeroVector;
};
