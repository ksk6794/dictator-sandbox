#pragma once

#include "CoreMinimal.h"

class FSandboxParamStore;

/** Joints are tuned in groups, not one by one. */
enum class ESandboxMuscleGroup : uint8
{
	Spine,
	Neck,
	Arms,
	Legs
};

/**
 * Drive of one muscle group: a rotational spring-damper on every joint of the group that turns the child
 * body toward its target pose, relative to the parent body. Values are physical, so they can be held
 * against measured human joints (an ankle in quiet standing is a few hundred N*m/rad).
 */
struct FSandboxMuscleTuning
{
	/** Spring stiffness in N*m per radian of error. 0 switches the muscle off. */
	double StiffnessNmPerRad = 0.0;
	/** Damping as a time: damping torque = stiffness x this x angular velocity. Seconds. */
	double DampingS = 0.0;
	/** Largest torque the muscle can produce, N*m. 0 = no cap. */
	double MaxTorqueNm = 0.0;
};

/**
 * Balance assist on one body: springs between the body and a fixed target in the world. This is the one
 * non-physical force acting on the character, so every value here should be as low as standing allows.
 */
struct FSandboxAssistTuning
{
	/** Pull toward the target position in the horizontal plane, N/m. 0 leaves the body free to move sideways. */
	double HorizontalNPerM = 0.0;
	/** Pull toward the target height, N/m. 0 leaves the weight to the legs. */
	double VerticalNPerM = 0.0;
	/** Turn toward the target orientation, N*m/rad. */
	double AngularNmPerRad = 0.0;
	/** Damping of all three springs as a time: damping = stiffness x this x velocity. Seconds. */
	double DampingS = 0.0;
};

/**
 * Solver effort spent on the character's bodies. The engine runs every simulation island with the highest
 * counts any of its bodies asks for, so these apply to the character and whatever it touches, not to the
 * rest of the world.
 */
struct FSandboxSolverTuning
{
	/**
	 * Position iterations per substep. The engine default of 8 does not converge for a heavy body standing
	 * on light feet through stiff joints: the feet rock and creep across the floor.
	 */
	double PositionIterations = 0.0;
	double VelocityIterations = 0.0;
	double ProjectionIterations = 0.0;
};

/** What the body is doing. The names double as the state names of the tuning files ("states.falling..."). */
enum class ESandboxBodyState : uint8
{
	/** Upright, muscles and balance assist at full strength. */
	Standing,
	/** Just hit: still upright, the muscles of the hit region are weak for a moment. */
	Flinch,
	/** Balance is lost: the assist is off and the body goes down. */
	Falling,
	/** Down and at rest. */
	Lying
};

/** When the body changes state. */
struct FSandboxThresholdTuning
{
	/** A hit of at least this impulse makes a standing body flinch. N*s. */
	double FlinchImpulseNs = 0.0;
	/** Impulses that arrive within this time of each other count as one hit. Seconds. */
	double FlinchWindowS = 0.0;
	/**
	 * The posture is lost, whatever the capture point says, once the pelvis is this far below its standing
	 * height (the legs gave way)... cm.
	 */
	double FallPelvisDropCm = 0.0;
	/** ...or tilted this far from its standing orientation. Degrees. */
	double FallTiltDeg = 0.0;
	/**
	 * A fallen body is at rest when it moves and turns no faster than this as a whole (root mean square
	 * over its bodies, weighted by mass)... m/s, rad/s.
	 */
	double RestSpeedMps = 0.0;
	double RestAngularSpeedRadS = 0.0;
	/** ...for this long. Seconds. */
	double RestTimeS = 0.0;
};

/** How the body senses its own balance: what counts as a foot that stands, and what part of it is its sole. */
struct FSandboxBalanceTuning
{
	/**
	 * A foot stands when the floor pushes up on it with at least this force, averaged over a frame. N. In
	 * quiet standing each foot carries half the body's weight, about 390 N.
	 */
	double FootContactMinN = 0.0;
	/** The sole of a standing foot is the part of its shapes within this of its lowest point. cm. */
	double SoleToleranceCm = 0.0;
	/**
	 * How far outside the support polygon the capture point may be and still be brought back without a step,
	 * by the ankles and the hips. Beyond it a step is needed. cm.
	 */
	double RecoverableCm = 0.0;
	/**
	 * How far beyond the edge of the support a step can put a foot... cm. The capture point runs away from the
	 * support while the step is under way (by e^(omega t), omega = sqrt(g / centre-of-mass height)), so a step
	 * can catch it only if it is now no further out than this divided by that growth over StepTimeS.
	 */
	double StepReachCm = 0.0;
	/** ...and how long a step takes, from the decision to the foot on the floor. Seconds. */
	double StepTimeS = 0.0;
	/** A step is needed only once the capture point has been past balance.recoverable_cm this long (losing balance needs no wait). Seconds. */
	double DecisionS = 0.0;
	/** Time over which the assist's anchors of pelvis, chest and head follow a new support, eased in and out. Seconds. */
	double AnchorFollowS = 0.0;
	/** A foot that has been off the floor at least this long is planted anew where it lands: its assist anchor moves there. Seconds. */
	double ReplantAirS = 0.0;
	/**
	 * A foot knocked off the floor (a blow to the leg) still counts as support, where it is, for this long:
	 * it comes back down by itself, which is not a step. Only after that is a capture point outside the other
	 * foot a step needed. Seconds.
	 */
	double LiftGraceS = 0.0;

	/**
	 * Ankle strategy. The body brings its capture point back over the place it stands on (where its centre of
	 * mass was at Start, in the frame of the feet) by shifting its centre of pressure: the ankles' targets
	 * turn, so the muscles press the toes or the heels down, or, sideways, the legs lean like a parallelogram.
	 * AnkleGain is the extra shift of the centre of pressure per cm of capture-point error (dimensionless);
	 * the turn that gives it follows from the body's weight and the legs' stiffness. 0 switches it off.
	 */
	double AnkleGain = 0.0;
	/**
	 * The same sideways. Measured on this body, a sideways turn of the ankles after a blow took the load off the
	 * far foot and made the capture point leave the support, whichever way it turned; so it stands at 0.
	 */
	double AnkleSideGain = 0.0;
	/**
	 * Time over which the strategy's slow part takes up a steady offset of the pelvis from its assist anchor
	 * (what the assist would otherwise hold): the centre of pressure moves by the offset once per this time. 0 = none. Seconds.
	 */
	double AnkleIntegralS = 0.0;
	/** The slow part's largest shift of the centre of pressure. cm. */
	double AnkleIntegralMaxCm = 0.0;
	/** Largest turn of the ankles' targets, in each direction. Degrees. */
	double AnkleMaxDeg = 0.0;
	/**
	 * Hip strategy, for a capture point the ankles cannot bring back: the trunk bends toward the fall at the
	 * hips, which drives the hips the other way. Degrees of bend per cm of capture point beyond
	 * balance.recoverable_cm... 0 switches it off.
	 */
	double HipDegPerCm = 0.0;
	/** ...up to this. Degrees. */
	double HipMaxDeg = 0.0;
	/** Time constant with which the strategies' turns follow what they are asked for: no target jumps. Seconds. */
	double ResponseS = 0.0;
	/**
	 * Leaning into a held push: under a steady force F at height h the body stands with its centre of mass
	 * F h / (m g) toward the force, so that its centre of pressure stays in the middle of its feet. At most
	 * this far. cm.
	 */
	double LeanMaxCm = 0.0;
};

/** How the body reacts to a hit. */
struct FSandboxReactionTuning
{
	/** Stiffness of the hit region's muscles at the moment of a hit, as a fraction of normal. 0..1. */
	double FlinchWeakening = 0.0;
	/** Time over which those muscles come back to full strength, which is also how long a flinch lasts. Seconds. */
	double FlinchRecoveryS = 0.0;
	/**
	 * A hit of this impulse puts the flinch's pose offset on at full weight (poses.flinch, and the struck
	 * body giving way); a smaller one in proportion. The offset fades out over the flinch. N*s.
	 */
	double FlinchPoseFullNs = 0.0;
	/** How far the struck body's joint gives way along the hit at full weight. Degrees. */
	double FlinchGiveDeg = 0.0;
	/**
	 * What the body feels is split in two: pressure, the felt force smoothed over this time, and the hit, the
	 * impulse beyond that pressure. Only a hit makes the body flinch, so a hand held against it is felt once,
	 * as it lands, and not again for as long as it presses. Seconds.
	 */
	double PressureS = 0.0;
	/**
	 * A force felt in every frame for this long is a held push, which the body leans into. A shove over in a
	 * tenth of a second is not one: its momentum is in the capture point already. Seconds.
	 */
	double HeldPushS = 0.0;
};

/**
 * Joints a pose can turn, by the name of their child bone (MetaHuman and UE5 mannequin naming). The order is
 * the order of FSandboxPose::Joints.
 */
constexpr int32 SandboxPoseJointCount = 17;
extern SANDBOXBODY_API const TCHAR* const SandboxPoseJoints[SandboxPoseJointCount];

/**
 * How far one joint is turned from the reference pose, in the joint's own anatomical axes as the physics
 * asset's spec defines them (Specs/README.md, "Joints"): swing1 about swing1_axis, swing2 about the third
 * axis, twist about the limb. A positive angle turns the child body the way the spec's limits count it, and
 * the same angles on a mirrored joint give the mirror-image motion. Degrees. All three 0 is the reference pose.
 */
struct FSandboxPoseAngles
{
	double Swing1Deg = 0.0;
	double Swing2Deg = 0.0;
	double TwistDeg = 0.0;

	bool IsZero() const { return Swing1Deg == 0.0 && Swing2Deg == 0.0 && TwistDeg == 0.0; }
};

/** A whole-body target pose, joint by joint ("poses.stance.thigh_l.swing1_deg"). */
struct FSandboxPose
{
	FSandboxPoseAngles Joints[SandboxPoseJointCount];
};

/** The poses the body is asked to hold, and how it goes from one to the next. */
struct FSandboxPoseTuning
{
	/** Time over which the target goes from one pose to the next, eased in and out. Seconds. */
	double BlendS = 0.0;
	/** Standing. All zero is the reference pose the body held before it had poses. */
	FSandboxPose Stance;
	/**
	 * Added on top of the pose while the body flinches, at full weight for a hit of
	 * reactions.flinch_pose_full_ns: the shoulders rise and the head draws in.
	 */
	FSandboxPose Flinch;
};

/** Which bones the balance assist holds ("rig.pelvis" and so on). Each must have a body in the physics asset. */
struct FSandboxBodyRig
{
	FName Pelvis;
	FName Chest;
	/** The body that carries the head; in the NPC's physics asset head and neck are one body on neck_01. */
	FName Head;
	FName FootLeft;
	FName FootRight;

	bool operator==(const FSandboxBodyRig& Other) const
	{
		return Pelvis == Other.Pelvis && Chest == Other.Chest && Head == Other.Head && FootLeft == Other.FootLeft && FootRight == Other.FootRight;
	}
};

/**
 * Everything that shapes how the active ragdoll behaves. The values live in Config/Tuning/body.json and
 * reach the code through FromParams; a default-constructed tuning is empty and does not validate.
 */
struct SANDBOXBODY_API FSandboxBodyTuning
{
	FSandboxBodyRig Rig;

	FSandboxMuscleTuning Spine;
	FSandboxMuscleTuning Neck;
	FSandboxMuscleTuning Arms;
	FSandboxMuscleTuning Legs;

	FSandboxAssistTuning Pelvis;
	FSandboxAssistTuning Chest;
	FSandboxAssistTuning Head;
	FSandboxAssistTuning Feet;

	FSandboxSolverTuning Solver;
	FSandboxThresholdTuning Thresholds;
	FSandboxBalanceTuning Balance;
	FSandboxReactionTuning Reactions;
	FSandboxPoseTuning Poses;

	/**
	 * Tells a parameter store which names the body understands and which states may carry values of their
	 * own, and adds the range checks of Validate to it, so the store refuses a file or an override the body
	 * could not use.
	 */
	static void DeclareParams(FSandboxParamStore& Params);

	/**
	 * Reads the tuning for one state of the body ("falling"). An empty state reads the base values, which
	 * are also what a state uses for everything it does not set itself. False with a reason if a value is
	 * missing or does not validate.
	 */
	static bool FromParams(const FSandboxParamStore& Params, const FString& State, FSandboxBodyTuning& OutTuning, FString& OutError);

	/** True if every number is the same. The rig is not compared. */
	bool HasSameValues(const FSandboxBodyTuning& Other) const;

	const FSandboxMuscleTuning& Muscle(ESandboxMuscleGroup Group) const;

	/** False with a reason if a value is negative, not finite or absurdly large. */
	bool Validate(FString& OutError) const;

	/**
	 * Every tunable number by name, "muscles.legs.stiffness_nm_per_rad", "assist.pelvis.horizontal_n_per_m"
	 * or "solver.position_iterations", in a fixed order. The names are the ones used in parameter files, overrides and result files.
	 */
	void ListValues(TArray<TPair<FString, double>>& OutValues) const;

	/** Sets one number by name. False with a reason if the name is unknown. Does not validate the range. */
	bool SetValue(const FString& Key, double Value, FString& OutError);

	/** Group a joint belongs to, from the name of its child bone (UE5 mannequin and MetaHuman naming). */
	static ESandboxMuscleGroup GroupOfBone(FName ChildBone);
	static const TCHAR* GroupName(ESandboxMuscleGroup Group);
	/** "standing", "flinch", "falling" or "lying": the state's name in tuning files and result files. */
	static const TCHAR* StateName(ESandboxBodyState State);
};
