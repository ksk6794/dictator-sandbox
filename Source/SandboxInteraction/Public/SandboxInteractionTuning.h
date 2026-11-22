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
};

/**
 * Everything tunable on the player's side. The values live in Config/Tuning (effectors.json, tools.json)
 * and reach the code through FromParams; a default-constructed tuning is empty and does not validate.
 */
struct SANDBOXINTERACTION_API FSandboxInteractionTuning
{
	FSandboxEffectorTuning Hand;
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
