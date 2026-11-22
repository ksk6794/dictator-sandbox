#pragma once

#include "CoreMinimal.h"
#include "SandboxInteractionTuning.h"
#include "SandboxTargetSource.h"

/**
 * The desktop's hands: two targets held in front of the camera. A hand rests below and beside the line of
 * sight; while its button is held its target travels to a point straight ahead, at a set speed, and comes
 * back when the button is let go. Strike verbs with a wind-up come in M4.
 *
 * The pawn calls Update once per simulated frame with the camera's pose; the rig then asks for the targets.
 */
class SANDBOXINTERACTION_API FSandboxDesktopTargetSource : public ISandboxTargetSource
{
public:
	/**
	 * Anchor is the pose the hands hang from (the camera). ReachCm is how far ahead a reaching hand goes.
	 * bContinuous is false when the anchor may have jumped since the last call (the first frame, or after
	 * a pause): the targets then carry no velocity.
	 */
	void Update(const FTransform& Anchor, bool bReachLeft, bool bReachRight, const FSandboxToolTuning& Tools, double ReachCm, float DeltaTime, bool bContinuous);

	virtual bool GetTarget(FName Effector, FSandboxEffectorTarget& OutTarget) const override;

	/** Where a hand rests for an anchor, in the world: where the pawn creates its hands. */
	static FTransform RestPose(const FTransform& Anchor, bool bLeft, const FSandboxToolTuning& Tools);

private:
	struct FHand
	{
		/** 0 at rest, 1 fully reached. */
		double Reach = 0.0;
		FSandboxEffectorTarget Target;
		bool bHasTarget = false;
	};

	void UpdateHand(FHand& Hand, bool bLeft, bool bReach, const FTransform& Anchor, const FSandboxToolTuning& Tools, double ReachCm, float DeltaTime, bool bContinuous);

	FHand Left;
	FHand Right;
};
