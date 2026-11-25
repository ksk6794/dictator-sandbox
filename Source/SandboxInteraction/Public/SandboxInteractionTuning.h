#pragma once

#include "CoreMinimal.h"

class FSandboxParamStore;

/**
 * One kind of effector ("effectors.hand..."): a rigid body with real mass that a spring pulls toward its
 * target pose. The force cap is the player's strength; spring and damper set how quickly the body follows.
 */
struct FSandboxEffectorTuning
{
	/** Mass of the body, kg: the hand and the part of the arm that moves with it. */
	double MassKg = 0.0;
	/** Radius of the collision sphere, cm. Read when the effector is created. */
	double RadiusCm = 0.0;
	/** Pull toward the target position, N/m. */
	double StiffnessNPerM = 0.0;
	/** Damping as a time: damping force = stiffness x this x (velocity error). Seconds. */
	double DampingS = 0.0;
	/** Largest force the spring can apply, N, in any direction. */
	double MaxForceN = 0.0;
	/** Turn toward the target orientation, N*m/rad. */
	double AngularStiffnessNmPerRad = 0.0;
	double AngularDampingS = 0.0;
	/** Largest torque about each axis, N*m. */
	double MaxTorqueNm = 0.0;
	/**
	 * A body further than this from its target is put on the target at rest, cm: the way back for a hand
	 * left behind a wall. It is not physics, so every use is counted and reported.
	 */
	double SnapDistanceCm = 0.0;
	/**
	 * Where the body is when a tracked controller moves it, off the controller's grip pose, cm: toward the
	 * thumb along the handle, the way the palm faces, and toward the knuckles. The same numbers serve both
	 * hands (FSandboxTrackedTargetSource::BodyOffset mirrors them). Desktop and scripted hands do not read them.
	 */
	double OffsetThumbCm = 0.0;
	double OffsetPalmCm = 0.0;
	double OffsetKnucklesCm = 0.0;
	/**
	 * The turn from a tracked controller's grip pose to the hand's own frame, degrees. The hand's own frame is
	 * the same whatever moves it: x along the forearm toward the knuckles and the fingers, z toward the thumb,
	 * y through the palm. A fist strikes along x; an open hand's palm lies in the plane of x and z.
	 */
	double GripPitchDeg = 0.0;
	double GripYawDeg = 0.0;
	double GripRollDeg = 0.0;
	/**
	 * The grip above which the hand is a fist and below which it is open, and how far the grip has to pass it
	 * to either side before the hand changes: a grip that hovers does not flicker.
	 */
	double FistGrip = 0.0;
	double FistGripSlack = 0.0;
	/**
	 * The open hand as a flat plate in the hand's own frame, cm: its length along the fingers, its width
	 * across the palm, its thickness, and how far ahead of the body's centre its middle is. Always shown
	 * while the hand is open. With PalmCollides (1 or 0) it is also the body that collides then, in place of
	 * the ball, with a mass of its own.
	 */
	double PalmLengthCm = 0.0;
	double PalmWidthCm = 0.0;
	double PalmThicknessCm = 0.0;
	double PalmForwardCm = 0.0;
	double PalmCollides = 0.0;
	/**
	 * A grab. With Grab (1 or 0), a grip that closes while a simulated body is within GrabReachCm of the hand's
	 * shape takes hold of the nearest such body. The grip slips when the hand has been further than
	 * GrabSlipCm from its target for GrabSlipS (0 cm: it never slips), and is torn open when the joint has
	 * carried more than GrabMaxForceN for GrabOverloadS (0 N: never). A hand that holds something collides
	 * with no simulated body; having let go, it collides again once it lies no deeper than GrabClearCm in one
	 * (at most 1.5 cm, half the thickness of the open hand: deeper, its middle could be inside the body).
	 */
	double Grab = 0.0;
	double GrabReachCm = 0.0;
	double GrabSlipCm = 0.0;
	double GrabSlipS = 0.0;
	double GrabMaxForceN = 0.0;
	double GrabOverloadS = 0.0;
	double GrabClearCm = 0.0;
	/**
	 * A thing that is held (not a limb of a character) keeps the direction it was taken in: a spring of
	 * GrabHoldNmPerRad with damping GrabHoldDampingS and a torque cap of GrabHoldMaxTorqueNm between the hand
	 * and the thing. 0 N*m/rad: it only hangs from the hand's point.
	 */
	double GrabHoldNmPerRad = 0.0;
	double GrabHoldDampingS = 0.0;
	double GrabHoldMaxTorqueNm = 0.0;
	/** A thing that names a handle is taken by it when the hand closes within this distance of the handle, cm (0: never). */
	double GrabHandleCm = 0.0;
	/** For how long after that the two held points may be apart without it counting as the joint's stretch, s: the handle is being drawn in. */
	double GrabDrawS = 0.0;
	/**
	 * A blow. A hand that meets something while it moves faster than StrikeSpeedMps has, for StrikeS, a higher
	 * force cap and a stiffer spring: the arm's push behind the fist. How much higher grows with the speed, up
	 * to StrikeForceN and StrikeStiffnessNPerM at StrikeFullSpeedMps. Pushing, holding and dragging keep the cap
	 * and the spring above. StrikeForceN at or below that cap: no blows.
	 */
	double StrikeForceN = 0.0;
	double StrikeStiffnessNPerM = 0.0;
	/** A blow is carried through: while it lasts the fist is asked to be at least this far beyond where it landed, the way it came (as much of it as the blow has of its strength), though its target stops short or goes back. cm. */
	double StrikeThroughCm = 0.0;
	double StrikeS = 0.0;
	double StrikeSpeedMps = 0.0;
	double StrikeFullSpeedMps = 0.0;
	double PalmMassKg = 0.0;
};

/** A spring with a force cap, as the drag handle uses it ("tools.drag..."). */
struct FSandboxSpringTuning
{
	double StiffnessNPerM = 0.0;
	double DampingS = 0.0;
	double MaxForceN = 0.0;
};

/** The desktop pawn's hands and tools ("tools..."). */
struct FSandboxToolTuning
{
	/** Where a resting hand is held, relative to the camera: forward, to its own side and down. cm. */
	double HandRestForwardCm = 0.0;
	double HandRestSideCm = 0.0;
	double HandRestDownCm = 0.0;
	/** How far in front of the camera a reaching hand goes, cm, and how fast its target travels, m/s. */
	double HandReachCm = 0.0;
	double HandReachSpeedMps = 0.0;
	/** How fast an open desktop hand travels, m/s: a reach, not a punch. */
	double HandOpenReachSpeedMps = 0.0;
	/** A desktop fist that is sent at something the camera aims at goes this far through it, cm (an open hand reaches as far as the wheel has set). */
	double HandThroughCm = 0.0;
	/**
	 * The desktop's kick. The foot rests below the camera (forward, to the right and down from it, the camera
	 * taken as level), and a kick takes it in a straight line to KickThroughCm beyond the point that is aimed
	 * at, at KickSpeedMps, and back. KickReachCm is as far as a kick goes from where the foot rests: a point
	 * further away is kicked toward, that far, and with nothing aimed at the kick goes that far ahead.
	 */
	double KickRestForwardCm = 0.0;
	double KickRestRightCm = 0.0;
	double KickRestDownCm = 0.0;
	double KickSpeedMps = 0.0;
	double KickThroughCm = 0.0;
	/** A kick's thrust begins this far before the point that is aimed at, at that point's height: the foot comes up to there first. cm. */
	double KickChamberCm = 0.0;
	double KickReachCm = 0.0;
	/** The force the foot is held with while it rests, N: it follows the player and pushes nothing. */
	double KickRestForceN = 0.0;
	/** For how long after a kick the foot keeps its whole strength, s: it comes home at speed and has to be stopped there. */
	double KickSettleS = 0.0;

	/** Impulse tool: the impulse a click applies, N*s, and the factor one notch of the wheel changes it by. */
	double ImpulseNs = 0.0;
	double ImpulseWheelFactor = 0.0;

	FSandboxSpringTuning Drag;

	/** The factor one notch of the wheel changes a distance by: a hand's reach, the length of the drag handle. */
	double WheelDistanceFactor = 0.0;

	/** Free camera: speed in m/s, the factor of the "fast" key and degrees turned per count of the mouse. */
	double CameraSpeedMps = 0.0;
	double CameraFastFactor = 0.0;
	double CameraLookDegPerCount = 0.0;

	/** Slow motion: the world makes one step in this many frames. A whole number. */
	double SlowMotionDivisor = 0.0;

	/**
	 * In a headset: how far from what the session looks at the middle of the play space is put, cm, and how
	 * high above the floor the hands wait until their controllers are tracked, cm.
	 */
	double HeadsetStandDistanceCm = 0.0;
	double HeadsetHandsHeightCm = 0.0;
	/**
	 * In a headset: a tracked pose that moves faster than this in one frame, m/s, is a jump of the tracking
	 * and not a motion of the hand; and the speed, m/s, on top of the controller's own, at which a hand's
	 * target catches up with a controller that jumped or was found again.
	 */
	double HeadsetMaxHandSpeedMps = 0.0;
	double HeadsetCatchUpSpeedMps = 0.0;
	/**
	 * In a headset: for how long, s, a pose that the runtime gives without tracking the controller is still
	 * followed. A controller that leaves the cameras' sight for a moment in a swing is carried on by its
	 * gyroscope well enough; one that stays out of sight wanders, and its hand is then left where it was.
	 */
	double HeadsetEstimatedFollowS = 0.0;
	/**
	 * In a headset: how much of the target's linear velocity is the runtime's (1) and how much the difference
	 * of the last two poses (0). The runtime's trails its own poses; see docs/testing.md, M4 part 1, step 3.
	 */
	double HeadsetRuntimeVelocityShare = 0.0;
	/**
	 * In a headset: both grips held closed for this long, s, with both hands this far above the eyes, cm, begin
	 * a new take (ASandboxPawn::RequestNewTake). A hold of 0: never.
	 */
	double HeadsetNewTakeHoldS = 0.0;
	double HeadsetNewTakeRaiseCm = 0.0;
	/** In a headset: the contact force of a hand, N, at which its controller vibrates at full strength. */
	double HeadsetHapticFullN = 0.0;
	/** In a headset: how long, s, and how strongly, 0 to 1, both controllers vibrate when a take begins. */
	double HeadsetTakeBuzzS = 0.0;
	double HeadsetTakeBuzzStrength = 0.0;
};

/**
 * Everything tunable on the player's side. The values live in Config/Tuning (effectors.json, tools.json)
 * and reach the code through FromParams; a default-constructed tuning is empty and does not validate.
 */
struct SANDBOXINTERACTION_API FSandboxInteractionTuning
{
	FSandboxEffectorTuning Hand;
	/**
	 * The kicking foot: the same body on the same kind of spring, with a leg's mass and strength. Of an
	 * effector's values it has those of the body and its spring; what makes a hand a hand (the grip, the open
	 * hand, the grab, the place on a controller) it has not, and those stay 0: a foot is always a fist.
	 */
	FSandboxEffectorTuning Foot;
	FSandboxToolTuning Tools;

	/** Tells a parameter store the names of this module and adds the range checks of Validate to it. */
	static void DeclareParams(FSandboxParamStore& Params);

	/** False with a reason if a value is missing or does not validate. */
	static bool FromParams(const FSandboxParamStore& Params, FSandboxInteractionTuning& OutTuning, FString& OutError);

	bool Validate(FString& OutError) const;

	/** Every number by name ("effectors.hand.mass_kg"), in a fixed order. */
	void ListValues(TArray<TPair<FString, double>>& OutValues) const;
	bool HasSameValues(const FSandboxInteractionTuning& Other) const;

	/** The tuning of one kind of effector ("hand"); null for a kind that does not exist. */
	const FSandboxEffectorTuning* Effector(FName Kind) const;
};
