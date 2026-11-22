#pragma once

#include "CoreMinimal.h"

/** Names of the effectors a source may be asked about. The part before the underscore is the kind ("hand"). */
namespace SandboxEffectors
{
	inline const FName HandLeft(TEXT("hand_left"));
	inline const FName HandRight(TEXT("hand_right"));

	/** "hand" for "hand_left": the name of the effector's group in the tuning files. */
	SANDBOXINTERACTION_API FName KindOf(FName Effector);
	/** True for the names above. */
	SANDBOXINTERACTION_API bool IsKnown(FName Effector);
}

/** Where one effector should be at the start of the coming physics step. World space, Unreal units. */
struct FSandboxEffectorTarget
{
	FVector Location = FVector::ZeroVector;
	FQuat Rotation = FQuat::Identity;
	/** Velocity of the target itself, cm/s, so that a hand following a moving target does not lag behind it. */
	FVector Velocity = FVector::ZeroVector;
	/** 0 open hand to 1 closed. Nothing reads it before grabbing exists (M4). */
	float Grip = 0.0f;
};

/**
 * Where the player's hands should be. This is the one seam between desktop, VR and tests: a desktop pawn
 * answers from the camera and the mouse, a scenario from the trajectories in its file, VR from the tracked
 * controllers. Everything behind it (the effector bodies, their springs, contact) is the same.
 *
 * A source is asked once per effector and frame, before the physics step, and answers for the start of
 * that step. Whoever owns the source keeps it up to date; the question itself changes nothing.
 */
class ISandboxTargetSource
{
public:
	virtual ~ISandboxTargetSource() = default;

	/** False if the source has nothing to say about this effector; its target then stays where it was. */
	virtual bool GetTarget(FName Effector, FSandboxEffectorTarget& OutTarget) const = 0;
};
