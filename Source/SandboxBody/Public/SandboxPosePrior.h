#pragma once

#include "CoreMinimal.h"
#include "SandboxBodyProbe.h"

class FSandboxClipPose;
class USkeletalMeshComponent;

/** One pose of the body: how each joint's body is turned from the body it hangs on, and which way is up for the pelvis. */
struct FSandboxPriorPose
{
	TArray<FQuat4f> Joints;
	/** The vertical in the pelvis's own space: a lying body is not a standing one, whichever way it faces. */
	FVector3f Up = FVector3f::UpVector;
};

/** The nearest pose of the database to one of the body's. */
struct FSandboxPriorMatch
{
	/** The mean over the joints and the pelvis's lean of the angle between the two poses, degrees. */
	double DistanceDeg = 0.0;
	FString Clip;
	double TimeS = 0.0;
	bool bMirrored = false;
};

/**
 * A database of recorded human poses, carried over to the body's joints (FSandboxClipPose) and each also mirrored,
 * and the likeness of a pose to them: the mean angle, joint by joint, to the nearest one. Heading plays no part.
 * Built from clips that are Epic's content, so it is written under Saved and never committed.
 */
class SANDBOXBODY_API FSandboxPosePrior
{
public:
	/** The joints of the mesh's physics asset: child body and the body it hangs on, in the order poses hold them. */
	bool Init(const USkeletalMeshComponent* Mesh, FName InPelvis, TConstArrayView<TPair<FName, FName>> InJoints, FString& OutError);

	/** A pose every StepS of the clip, and its mirror image; one nearer than MinDeg to the last one kept of the clip is left out. */
	void AddClip(const FString& Name, const FSandboxClipPose& Clip, double StepS, double MinDeg);

	bool Save(const FString& File, FString& OutError) const;
	/** Reads a database made for the same joints. */
	bool Load(const FString& File, FString& OutError);

	int32 Num() const { return Poses.Num(); }
	int32 NumClips() const { return Clips.Num(); }
	/** Of the clips' names and every pose: two databases with the same stamp are the same. */
	uint32 GetStamp() const;

	/** The pose the bodies are in. False if one of them is missing. */
	bool PoseOf(const TArray<FSandboxBodySample>& Bodies, FSandboxPriorPose& OutPose) const;
	/** The same pose on the other side. */
	FSandboxPriorPose Mirror(const FSandboxPriorPose& Pose) const;
	FSandboxPriorMatch FindNearest(const FSandboxPriorPose& Pose) const;
	static double DistanceDeg(const FSandboxPriorPose& A, const FSandboxPriorPose& B);

	/**
	 * What has to hold of every database: each of its poses is found again at no distance, the mirror image of a
	 * mirror image is the pose itself, and in a mirror image each joint is turned from the reference pose as far as
	 * its counterpart on the other side is. Looks at every Every-th pose. False with the first that does not.
	 */
	bool Check(int32 Every, FString& OutError) const;

private:
	struct FEntry
	{
		FSandboxPriorPose Pose;
		int32 Clip = 0;
		float TimeS = 0.0f;
		bool bMirrored = false;
	};
	FName Pelvis;
	TArray<TPair<FName, FName>> Joints;
	/** Per joint: the body it hangs on as an index into Joints (INDEX_NONE: the pelvis), and the joint on the other side. */
	TArray<int32> Parents;
	TArray<int32> Mirrored;
	/** Orientation of each joint's body, and of the pelvis, in the mesh's reference pose, component space. */
	TArray<FQuat> References;
	FQuat PelvisReference = FQuat::Identity;
	/** Order to work joints in, so that the body a joint hangs on comes before it. */
	TArray<int32> Order;
	/** From the body a joint hangs on to the bone its bone is a child of, in the reference pose. */
	TArray<FQuat> ParentToBoneParent;
	TArray<int32> MeshBones;

	TArray<FString> Clips;
	TArray<FEntry> Poses;
};
