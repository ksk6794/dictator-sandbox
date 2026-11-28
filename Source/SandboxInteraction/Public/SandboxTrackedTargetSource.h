#pragma once

#include "CoreMinimal.h"
#include "SandboxInteractionTuning.h"
#include "SandboxTargetSource.h"

/**
 * What is known of one hand-held controller at the start of a frame, in tracking space: the space the headset
 * and the controllers are tracked in, with its origin on the floor in the middle of the play space. Unreal
 * units and axes (cm, x forward, y right, z up).
 *
 * The pose is the controller's grip pose: its origin in the middle of the closed fist, x along the handle
 * from the little finger toward the thumb, y through the palm (toward where the palm of the left hand faces,
 * and toward the back of the right hand), z toward the wrist. That is OpenXR's definition carried into
 * Unreal's axes; the owner's first look in the headset found the hands' spheres on the fists.
 */
struct FSandboxControllerState
{
	/** True while the runtime tracks the controller: the pose is where the controller is. */
	bool bTracked = false;
	/**
	 * True when the runtime gave a pose that it does not mark as tracked: a controller out of the headset's
	 * sight, whose pose is the runtime's guess. Such a pose wandered by a metre and a half in the first
	 * session and is not a place to send a hand to; it is only counted.
	 */
	bool bEstimated = false;
	FVector Location = FVector::ZeroVector;
	FQuat Rotation = FQuat::Identity;
	/** Velocity of the grip pose's origin as the runtime tracks it, cm/s. */
	bool bHasLinearVelocity = false;
	FVector LinearVelocity = FVector::ZeroVector;
	/** Angular velocity as the runtime tracks it: the axis times the speed in rad/s, so that v = w x r. */
	bool bHasAngularVelocity = false;
	FVector AngularVelocity = FVector::ZeroVector;
	/** The squeeze (grip) axis, 0 open to 1 closed. */
	float Squeeze = 0.0f;
	/** The controller's thumbstick: x to the right, y forward, each -1 to 1 (FR-42: the right one moves the player, the left one turns). */
	FVector2D Stick = FVector2D::ZeroVector;
	/**
	 * The controller's two face buttons, held down: the first (A on the right controller, X on the left, as the Touch profile names
	 * them; the PS VR2 controllers arrive as that profile) and the second (B, Y). R10's commands in the headset (decision 42): the
	 * right controller's first tells the body get_up, its second stop (ASandboxPawn).
	 */
	bool bFirstButton = false;
	bool bSecondButton = false;
};

/**
 * Where a controller is. The one question the hands ask of a headset's runtime, and the one place a test puts
 * made-up poses in. FSandboxXRControllers answers it from the engine's XR system.
 */
class ISandboxControllerTracker
{
public:
	virtual ~ISandboxControllerTracker() = default;

	/** The controller that moves an effector ("hand_left", "hand_right"). False for a name it has no controller for. */
	virtual bool GetController(FName Effector, FSandboxControllerState& OutState) const = 0;

	/** The head's pose in tracking space, cm. False when there is none (a tracker of hands alone). */
	virtual bool GetHead(FVector& OutLocation, FQuat& OutRotation) const { return false; }
};

/**
 * How the play space itself moved in a frame (FSandboxLocomotion): its velocity, cm/s, which every tracked hand moves with
 * besides its controller, and whether it jumped (a snap turn), which a hand catches up with instead of following.
 */
struct FSandboxPlaySpaceMotion
{
	FVector VelocityCmS = FVector::ZeroVector;
	bool bJumped = false;
};

/** What the tracked source has seen of one hand since it began. For the log, the HUD and a session's file. */
struct FSandboxTrackedHandStats
{
	/** Frames with a tracked pose, and frames without one after the first (the controller was lost). */
	int32 TrackedFrames = 0;
	int32 LostFrames = 0;
	/** Of the lost frames, those in which the runtime still gave a pose, not marked as tracked. */
	int32 EstimatedFrames = 0;
	/** Frames in which such a pose was followed all the same, the controller having been tracked a moment before (tools.headset.estimated_follow_s). */
	int32 EstimatedFollowedFrames = 0;
	/** Tracked frames in which the runtime gave no linear velocity, and no angular velocity. */
	int32 NoLinearVelocityFrames = 0;
	int32 NoAngularVelocityFrames = 0;
	/**
	 * Tracked frames in which the pose moved faster than 20 cm/s while its velocity read under 3 cm/s: what
	 * an untracked pose looked like in the first session. Counted to learn whether the runtime's own mark
	 * covers it; nothing acts on it.
	 */
	int32 MovedWithoutVelocityFrames = 0;
	/** Times the target had to catch up with a controller (found again, or jumped), and the frames that took. */
	int32 CatchUps = 0;
	int32 CatchUpFrames = 0;
	/** The fastest the target went, cm/s, and the most the grip was closed. */
	double MaxTargetSpeedCmS = 0.0;
	float MaxGrip = 0.0f;
};

/**
 * The hands of a player in a headset: each target is the controller's grip pose, carried from tracking space
 * into the world through the origin of the play space (the pawn), with the hand's body put at a tunable offset
 * from the grip pose (effectors.hand.offset_*) and turned from the grip pose's axes into the hand's own
 * (effectors.hand.grip_*_deg: x along the fingers, z toward the thumb). The target's velocity is the controller's velocity as the
 * runtime tracks it, plus what its turning gives a body that is off the grip pose; the grip is the squeeze
 * axis.
 *
 * Nothing here knows of a headset or its runtime: the poses come through ISandboxControllerTracker, and all
 * this class does is arithmetic. The pawn calls Update once per simulated frame; the rig then asks for the
 * targets.
 *
 * A controller that is lost leaves its hand where it was last seen, at rest. A pose that the runtime gives
 * without tracking the controller is followed for a moment (tools.headset.estimated_follow_s) and counts as a
 * loss after that. When a controller is found again, and when
 * a tracked pose jumps further in one frame than a hand moves (tools.headset.max_hand_speed_mps), the target
 * does not jump after it: it catches up at a set speed (tools.headset.catch_up_speed_mps) on top of the
 * controller's own, and follows again once it is there. A hand sent after a jump of 116 cm flew at 13 m/s with
 * nobody moving.
 *
 * A runtime that gives no velocity gets the difference of two poses instead, and the frames are counted: a
 * hand that follows a differenced velocity lags and jitters more, and whoever reads a recording should know.
 */
class SANDBOXINTERACTION_API FSandboxTrackedTargetSource : public ISandboxTargetSource
{
public:
	/**
	 * Origin is the play space's origin in the world, without scale. bContinuous is false when the world did
	 * not step in the frame before (the first frame, or after a pause): a missing velocity is then not made
	 * up from two poses that are more than a step apart. Motion is how the play space moves (FSandboxLocomotion): its
	 * velocity is added to every hand's, and a jump of it is caught up with as a jump of the tracking is.
	 */
	void Update(const ISandboxControllerTracker& Tracker, const FTransform& Origin, const FSandboxEffectorTuning& Hand, const FSandboxToolTuning& Tools, float DeltaTime, bool bContinuous,
		const FSandboxPlaySpaceMotion& Motion = FSandboxPlaySpaceMotion());

	/** Where the player's own body is to be (the effector "body"), as FSandboxLocomotion says; GetTarget answers for it from then on. */
	void SetBodyTarget(const FSandboxEffectorTarget& Target)
	{
		BodyTarget = Target;
		bHasBodyTarget = true;
	}

	/**
	 * Where a hand waits before its controller has a pose (where its body was made). The target then catches
	 * up from there instead of jumping to the controller. Without it a hand has no target until the first pose.
	 */
	void SetWaitingPose(FName Effector, const FTransform& Pose);

	virtual bool GetTarget(FName Effector, FSandboxEffectorTarget& OutTarget) const override;

	/** Null for a name that is not a hand. */
	const FSandboxTrackedHandStats* GetStats(FName Effector) const;
	/** True once the controller of this hand has had a tracked pose in the last Update. */
	bool IsTracked(FName Effector) const;
	/** True while the hand's target is on its way to a controller that was found again or jumped. */
	bool IsCatchingUp(FName Effector) const;

	/**
	 * The hand's body in the grip pose's own axes, from the offsets of the tuning, which are written for
	 * both hands in terms of the hand (toward the thumb, the way the palm faces, toward the knuckles).
	 */
	static FVector BodyOffset(const FSandboxEffectorTuning& Hand, bool bLeft);

private:
	struct FHand
	{
		FSandboxEffectorTarget Target;
		bool bHasTarget = false;
		bool bCatchingUp = false;
		/** For how long the runtime has given only a pose it does not track, s. */
		double EstimatedS = 0.0;
		/** The controller's state of the last Update, for a velocity the runtime does not give. */
		FSandboxControllerState Last;
		FSandboxTrackedHandStats Stats;
	};

	void UpdateHand(FHand& Hand, FName Effector, bool bLeft, const ISandboxControllerTracker& Tracker, const FTransform& Origin, const FSandboxEffectorTuning& Tuning, const FSandboxToolTuning& Tools, float DeltaTime, bool bContinuous,
		const FSandboxPlaySpaceMotion& Motion);
	const FHand* Find(FName Effector) const;
	FHand* Find(FName Effector);

	FHand Left;
	FHand Right;
	FSandboxEffectorTarget BodyTarget;
	bool bHasBodyTarget = false;
};
