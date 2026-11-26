#pragma once

#include "CoreMinimal.h"
#include "SandboxInteractionTuning.h"
#include "SandboxTargetSource.h"

/** What the player in a headset gives the play space in one simulated frame. Unreal units: cm, x forward, y right, z up. */
struct FSandboxLocomotionInput
{
	/** The right controller's stick: x to the right, y forward, each -1 to 1. It moves the player. */
	FVector2D MoveStick = FVector2D::ZeroVector;
	/** The left controller's stick, of which x alone is read (left -1, right 1): a snap turn. */
	FVector2D TurnStick = FVector2D::ZeroVector;
	/** The head's pose in tracking space: the space of the play space, its origin on the floor in its middle. */
	FVector HeadLocation = FVector::ZeroVector;
	FQuat HeadRotation = FQuat::Identity;
	/**
	 * The player's own body as the physics world has it now: where its middle is, and the net force its contacts pushed it
	 * with in the step just made, N, in the world. Without a body (bHasBody false) nothing holds the stick back.
	 */
	bool bHasBody = false;
	FVector BodyLocation = FVector::ZeroVector;
	FVector BodyContactN = FVector::ZeroVector;
};

/** What the movement has done since it began. For the log and a session's file. */
struct FSandboxLocomotionStats
{
	/** How far the stick has carried the play space, cm, and in how many frames it moved. */
	double WalkedCm = 0.0;
	int32 MovingFrames = 0;
	/** Snap turns made, to the left and to the right. */
	int32 SnapsLeft = 0;
	int32 SnapsRight = 0;
	/** Frames in which something pressing the body back took some of the stick's motion, and the most it took (0 to 1). */
	int32 HeldBackFrames = 0;
	double MostHeldBack = 0.0;
	/** The furthest the body's target was from the body, horizontally, cm: how far the body was held back or left behind. */
	double MaxBodyGapCm = 0.0;
	/** The fastest the stick moved the play space, cm/s. */
	double MaxSpeedCmS = 0.0;
};

/**
 * The movement of a player in a headset (FR-42; docs/active-ragdoll.md, decisions 24 to 26), as arithmetic only: no world,
 * no engine, no headset. The pawn hands it the sticks, the head's pose and the player's body once per simulated frame and
 * takes the play space's new pose, its velocity and where the body is to be; a unit test hands it made-up ones.
 *
 * The right stick moves the play space: in every direction relative to where the head looks, at a speed in proportion to
 * its tilt past a dead zone, up to tools.headset.move_speed_mps, eased from rest to full and back in move_ease_s. The left
 * stick's sideways tilt turns the play space by snap_turn_deg about the head, which stays where it is in the world; one turn
 * a tilt. Nothing but the stick, the snap and the player's own head ever moves the view.
 *
 * The player's body (the effector "body") has a target of its own: the stick carries it along with the play space, and the
 * head draws it after itself once it is further than body_lean_cm from it, at body_follow_speed_mps at most. When something
 * presses the body back (the NPC, a table), the part of the stick's motion that goes against that push fades with the push,
 * from body_blocked_n to none at body_stopped_n: the player stops at the NPC and slides along a table. The body itself is a
 * capsule on a capped spring (effectors.body), and it pushes whatever stands in its way no harder than that cap.
 */
class SANDBOXINTERACTION_API FSandboxLocomotion
{
public:
	/**
	 * The play space that puts a head with this pose in tracking space at StandPoint (on the floor: the head's own height
	 * is kept), looking along StandYawDeg. The play space is level.
	 */
	static FTransform PlaceHead(const FVector& HeadLocation, const FQuat& HeadRotation, const FVector& StandPoint, double StandYawDeg);

	/** The direction the head looks in the world, level: its forward, or for a head that looks straight down or up, its top. */
	static FVector LevelForward(const FQuat& HeadWorldRotation);

	/** The stick past its dead zone: 0 inside it, growing to 1 at full tilt, in the stick's own direction. */
	static FVector2D PastDeadZone(const FVector2D& Stick, double DeadZone);

	/** Starts with the play space at Origin, at rest, and the body's target under the head (HeadLocation in tracking space). */
	void Reset(const FTransform& Origin, const FVector& HeadLocation);

	/** One simulated frame. */
	void Update(const FSandboxLocomotionInput& Input, const FSandboxToolTuning& Tools, float DeltaTime);

	/** The play space's pose in the world: the pawn's. */
	const FTransform& GetOrigin() const { return Origin; }
	/** How fast the play space moves, cm/s: what a tracked hand's target moves with besides its controller. */
	const FVector& GetOriginVelocity() const { return Velocity; }
	/** True if the last Update turned the play space by a snap: whatever rides on it jumped. */
	bool HasSnapped() const { return bSnapped; }
	/** Where the player's body is to be: its middle, upright, and how fast that place moves. */
	FSandboxEffectorTarget GetBodyTarget(const FSandboxEffectorTuning& Body, const FSandboxToolTuning& Tools) const;
	/** How much of the stick's motion the last Update took away because the body was pressed back, 0 to 1. */
	double GetHeldBack() const { return HeldBack; }
	/** The stick's speed of the last Update as a share of the full speed, 0 to 1 (for darkening the view's edges). */
	double GetSpeedShare(const FSandboxToolTuning& Tools) const;
	const FSandboxLocomotionStats& GetStats() const { return Stats; }

private:
	FTransform Origin = FTransform::Identity;
	FVector Velocity = FVector::ZeroVector;
	/** The body's target on the floor's plane (x and y), and how fast it moved in the last Update. */
	FVector BodyTarget = FVector::ZeroVector;
	FVector BodyTargetVelocity = FVector::ZeroVector;
	bool bSnapArmed = true;
	bool bSnapped = false;
	double HeldBack = 0.0;
	FSandboxLocomotionStats Stats;
};
