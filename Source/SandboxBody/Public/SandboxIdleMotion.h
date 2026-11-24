#pragma once

#include "CoreMinimal.h"
#include "SandboxPoseRig.h"

class UAnimSequence;
class USkeletalMeshComponent;

/**
 * The motion of a recorded idle: breathing, the shoulders' rise and fall, a slow sway. It is taken off the clip as
 * what each bone does relative to the clip's own first frame, in the bone's own space, and added as turns
 * (FSandboxPoseTurn) to whatever pose the body holds. The clip's pose itself is not used: the body keeps its own
 * stance, and its legs stay with its balance.
 *
 * The clip may be made for another skeleton, as long as the bones have the same names and the same axes (the
 * engine's mannequin and a MetaHuman body do: their reference poses agree bone by bone). Nothing is retargeted.
 *
 * Sampled once, when loaded, at the clip's own keys; evaluated by simulated time, never by the clock. At time 0
 * every turn is exactly none, so a body that starts with it starts in its pose.
 */
class SANDBOXBODY_API FSandboxIdleMotion
{
public:
	/**
	 * Samples the clip for the given bones of the mesh (indices in its reference skeleton), and the pelvis's travel
	 * for the sway. A bone the clip does not have is left out. False with a reason if the clip has none of them.
	 */
	bool Load(const UAnimSequence* Sequence, const USkeletalMeshComponent* Mesh, TConstArrayView<int32> MeshBones, FName PelvisBone, FString& OutError);

	bool IsLoaded() const { return KeyCount > 0; }
	/** Length of the loop, seconds. */
	double GetPeriodS() const { return KeyCount * KeyS; }
	int32 GetBoneCount() const { return Tracks.Num(); }

	/** Adds the motion at a time, scaled by Weight, to a set of pose turns, merged per bone. Weight 0 adds nothing. */
	void AddTurns(double TimeS, double Weight, TArray<FSandboxPoseTurn>& Turns) const;

	/** Where the pelvis is from where it was in the clip's first frame, cm, in the mesh's own space (x and y). */
	FVector2D SwayCm(double TimeS) const;

private:
	struct FTrack
	{
		int32 MeshBoneIndex = INDEX_NONE;
		/** The bone's turn from the first frame, one per key. */
		TArray<FQuat> Turns;
	};

	/** The two keys around a time and how far between them it lies. The last key leads back to the first. */
	void Locate(double TimeS, int32& OutFrom, int32& OutTo, double& OutAlpha) const;

	TArray<FTrack> Tracks;
	TArray<FVector2D> Sway;
	int32 KeyCount = 0;
	double KeyS = 0.0;
};
