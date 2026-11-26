#pragma once

#include "CoreMinimal.h"
#include "SandboxBodyTuning.h"
#include "SandboxPoseRig.h"

struct FReferenceSkeleton;

/**
 * Poses of the fingers as turns of their bones. Fingers have no bodies: they are bones the animation instance sets,
 * relative to the hand, so that a hand looks like one; nothing collides with them and they move nothing. A pose is a
 * curl for each finger and the thumb and one spread (FSandboxFingerPose), counted from the reference pose, in which
 * the hand is open; the axes are worked out from the reference pose, so that both hands do the mirror image of each other.
 */
class SANDBOXBODY_API FSandboxFingerRig
{
public:
	/** Reads the finger bones of both hands (thumb, index, middle, ring, pinky; 01 to 03). False if the skeleton has no such hand. */
	bool Init(const FReferenceSkeleton& Skeleton);
	bool IsValid() const { return Hands[0].Num() > 0 && Hands[1].Num() > 0; }

	/** Adds the turns of one hand (0 left, 1 right) for a pose to OutTurns. A pose of zeros adds none. */
	void BuildTurns(int32 Side, const FSandboxFingerPose& Pose, TArray<FSandboxPoseTurn>& OutTurns) const;

	/** The poses of one hand, each by its weight, as one: the sum of what they ask for. */
	static FSandboxFingerPose Blend(TConstArrayView<TPair<const FSandboxFingerPose*, double>> Poses);

private:
	struct FJoint
	{
		int32 MeshBoneIndex = INDEX_NONE;
		/** Thumb 0, index 1, middle 2, ring 3, pinky 4. */
		int32 Finger = 0;
		/** 0 the knuckle, 1 and 2 the joints beyond it. */
		int32 Segment = 0;
		/** Turning by a positive angle about it bends the finger toward the palm; in the bone's own space. */
		FVector CurlAxis = FVector::ZeroVector;
		/** Turning by a positive angle about it takes the finger toward the thumb's side; in the bone's own space. The knuckles only. */
		FVector SideAxis = FVector::ZeroVector;
	};
	TArray<FJoint> Hands[2];
};
