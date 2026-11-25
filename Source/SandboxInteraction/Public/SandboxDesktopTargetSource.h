#pragma once

#include "CoreMinimal.h"
#include "SandboxInteractionTuning.h"
#include "SandboxTargetSource.h"

/**
 * The desktop's hands: two targets held in front of the camera. A hand rests below and beside the line of
 * sight; while its button is held its target travels to a point straight ahead, at a set speed, and comes
 * back when the button is let go. The hands are fists unless they are opened (SetHandsOpen). Strike verbs with
 * a wind-up come in M4.
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

	/**
	 * Opens both hands (a grip of 0) or closes them (1, as they are unless told otherwise). An open hand that
	 * closes within reach of a body takes hold of it where hands can grab (effectors.hand.grab).
	 */
	void SetHandsOpen(bool bOpen) { bHandsOpen = bOpen; }

	/** Where a hand rests for an anchor, in the world: where the pawn creates its hands. */
	static FTransform RestPose(const FTransform& Anchor, bool bLeft, const FSandboxToolTuning& Tools);

	/**
	 * The kicking foot (foot_right). It rests below the anchor, which is taken as level whatever way the
	 * camera looks; Kick sends it to a point of the world and a little way through it, and back, unless a kick
	 * is under way. Its target comes with the hands', from Update.
	 */
	void Kick(const FVector& WorldPoint);
	bool IsKicking() const { return Foot.bKicking || Foot.bAsked; }
	/** The share of its strength the foot has while it rests, 0 to 1 (it has all of it while it kicks). */
	void SetFootRestStrength(double Share) { FootRestStrength = FMath::Clamp(Share, 0.0, 1.0); }
	/** Ends a kick where it is: the foot's target is at its rest from the next Update on (a new take). */
	void StopKick()
	{
		// The target jumps home with this, and no velocity is made of a jump: the next Update starts afresh.
		Foot.bHasTarget = Foot.bHasTarget && !Foot.bKicking;
		Foot.bKicking = false;
		Foot.bAsked = false;
		Foot.Phase = 0.0;
		Foot.SettleLeftS = 0.0;
	}
	static FTransform FootRestPose(const FTransform& Anchor, const FSandboxToolTuning& Tools);

private:
	struct FHand
	{
		/** 0 at rest, 1 fully reached. */
		double Reach = 0.0;
		FSandboxEffectorTarget Target;
		bool bHasTarget = false;
	};

	void UpdateHand(FHand& Hand, bool bLeft, bool bReach, const FTransform& Anchor, const FSandboxToolTuning& Tools, double ReachCm, float DeltaTime, bool bContinuous);

	struct FFoot
	{
		/** A kick that was asked for and begins with the next Update, and where it is to go. */
		bool bAsked = false;
		FVector AskedPoint = FVector::ZeroVector;
		/** A kick under way: where it turns back, and how far along it is, 0 to 1 out and 1 to 2 back. */
		bool bKicking = false;
		FVector Strike = FVector::ZeroVector;
		double Phase = 0.0;
		/** After a kick: for how long the foot still has its whole strength, s. */
		double SettleLeftS = 0.0;
		/** Where the foot rested at the last Update. */
		FVector LastRest = FVector::ZeroVector;
		FSandboxEffectorTarget Target;
		bool bHasTarget = false;
	};

	void UpdateFoot(const FTransform& Anchor, const FSandboxToolTuning& Tools, float DeltaTime, bool bContinuous);

	FHand Left;
	FHand Right;
	FFoot Foot;
	double FootRestStrength = 1.0;
	bool bHandsOpen = false;
};
