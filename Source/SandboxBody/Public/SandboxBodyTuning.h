#pragma once

#include "CoreMinimal.h"

class FSandboxParamStore;

/** Joints are tuned in groups, not one by one. */
enum class ESandboxMuscleGroup : uint8
{
	Spine,
	Neck,
	Arms,
	Legs,
	/** The toes of a body that has them (a body on the ball of the foot). */
	Toes
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
	/**
	 * How much of the target pose's own turning speed the damping works against, 0..1. At 1 (Physics Control's
	 * default) the damping holds the joint to the pose's speed as well as to its angle, so the muscle keeps up with a
	 * moving pose; but a pose that jumps in one frame asks for the jump divided by the frame's time, and the damping
	 * turns that into a blow (a leg's target that jumped as the body fell gave it 600 J in one frame). At 0 the damping
	 * only resists the joint's own turning.
	 */
	double PoseVelocityShare = 0.0;
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
	/**
	 * Largest torque of the turn, N*m. 0 = no cap. (In a stagger the pelvis's upright hold is capped: uncapped, it held
	 * a body whose centre of mass was far outside its feet, and a 200 N*s shove did not fell it.)
	 */
	double MaxTorqueNm = 0.0;
	/**
	 * Largest pull along each axis, N. 0 = no cap. (The pull that helps a body up from the floor is capped, so that
	 * it helps and does not carry: what holds the body down, or throws it off, wins.)
	 */
	double MaxForceN = 0.0;
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

/**
 * What the body's surface is like in a contact: the physical material of its bodies. The engine works a contact's
 * friction and restitution out of the two surfaces that meet, by the combine mode with the higher number of the
 * two (0 the average, 1 the smaller, 2 the product, 3 the larger). Everything else in the world has the engine's
 * default material (friction 0.7, restitution 0.3, average) unless something gives it another.
 */
struct FSandboxSurfaceTuning
{
	/** Friction of every body but the feet: skin and cloth. */
	double Friction = 0.0;
	/** Friction of the feet: the soles. */
	double FeetFriction = 0.0;
	/** Restitution of every body: how much of the speed it meets a surface with it leaves with. */
	double Restitution = 0.0;
	/** Combine modes the body's surface asks for, 0 to 3 as above. */
	double FrictionCombine = 0.0;
	double RestitutionCombine = 0.0;

	bool operator==(const FSandboxSurfaceTuning& Other) const
	{
		return Friction == Other.Friction && FeetFriction == Other.FeetFriction && Restitution == Other.Restitution &&
			FrictionCombine == Other.FrictionCombine && RestitutionCombine == Other.RestitutionCombine;
	}
};

/** What the body is doing. The names double as the state names of the tuning files ("states.falling..."). */
enum class ESandboxBodyState : uint8
{
	/** Upright, muscles and balance assist at full strength. */
	Standing,
	/** Just hit: still upright, the muscles of the hit region are weak for a moment. */
	Flinch,
	/** The capture point has left what the ankles and hips can bring back: the body has to step. */
	Stagger,
	/** The posture has given way: the assist is off, the body goes down and takes the pose of its fall (poses.fall_*). */
	Falling,
	/** Down and nearly still: the muscles let go. */
	Lying,
	/**
	 * Getting up from the floor: the muscles are asked for the pose of a recorded get-up, the clip's time waits for
	 * the body, and the assist pulls the pelvis toward where the clip has it, with a capped force (getup.*). Still a
	 * simulated body like any other: a push moves it, and one that throws it off the clip's path fells it again.
	 */
	GetUp
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
	 * A fallen body lies, and lets its muscles go, once it moves and turns no faster than this as a whole (root
	 * mean square over its bodies, weighted by mass)... m/s, rad/s. Not motionless: held until it was, the fall's
	 * pose kept the body curled up on the floor, and it took 3.5 s to come to rest instead of 1.7.
	 */
	double RestSpeedMps = 0.0;
	double RestAngularSpeedRadS = 0.0;
	/** ...for this long. Seconds. */
	double RestTimeS = 0.0;
	/**
	 * A falling body that has touched nothing of the world and has not come down for this long is held up (by a
	 * hand, a hook): it lets go of its fall's pose and hangs. Seconds.
	 */
	double HeldTimeS = 0.0;
	/** ...where coming down is the centre of mass this far below the lowest it has been. cm. */
	double HeldDescentCm = 0.0;
	/**
	 * A falling body lets its muscles go (the lying state's) this long after its trunk or head first met the floor,
	 * whether it has come to rest or not: the fall's pose is for meeting the floor, and held after that it keeps the
	 * body a stiff shape that rolls and balances (a body knocked over by the head stood on it, legs up, for a second).
	 * 0: only once it is at rest. Seconds.
	 */
	double ImpactLetGoS = 0.0;
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
	 * can catch it only if it is now no further out than this divided by that growth over StepTimeS. A body further
	 * out than that steps at once, as far as this, and goes on stepping; it falls when its posture gives way
	 * (thresholds.fall_*). Only a body whose legs are placed from outside is counted as fallen there.
	 */
	double StepReachCm = 0.0;
	/** ...and how long a step takes, from the decision to the foot on the floor. Seconds. */
	double StepTimeS = 0.0;
	/** A step is needed only once the capture point has been past balance.recoverable_cm this long (a capture point beyond one step's reach needs no wait). Seconds. */
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
	/**
	 * Stagger ends, back to standing, once the centre of mass has been over the support and the capture point
	 * within balance.recoverable_cm of it for this long (and no step is under way). Seconds.
	 */
	double RecoverS = 0.0;
	/**
	 * ...and only once the centre of mass moves no faster than this over the floor. A body that came out of its
	 * stagger at 0.7 m/s over a wide stance was caught by the assist's springs at full strength, and they threw it
	 * into the next stagger. m/s.
	 */
	double RecoverSpeedMps = 0.0;
	/**
	 * A staggering body that has come to rest (both feet down, no faster than balance.recover_speed_mps) with its
	 * centre of mass and its capture point outside the part of its soles that touches the floor, for this long,
	 * needs a step: at rest nothing brings the body back over its feet, and what keeps it up is the assist's torque
	 * on the pelvis. Without this rule such a body neither stepped (its capture point was within
	 * balance.recoverable_cm of its whole soles) nor stood (its centre of mass was not over its support), for good.
	 * 0 switches the rule off. Seconds.
	 */
	double RestStepS = 0.0;

	/**
	 * The recovery step. The foot is put where the capture point will be when it lands (it runs away from the
	 * other foot's sole by e^(omega t) over balance.step_time_s, which is also how long the swing takes), this
	 * much further along the way it runs, so that the body comes to rest over the new support. cm.
	 */
	double StepMarginCm = 0.0;
	/**
	 * Where that point would leave the capture point less than this far inside the new support (the hull of both
	 * soles), the foot goes to a point nearby that does (up to 15 cm along the capture point's way and across it),
	 * the nearest of them; 0 aims at the point alone. A capture point on the edge of the support cannot be brought
	 * back without another step. cm.
	 */
	double StepMarginGoalCm = 0.0;
	/** How high the swinging foot is lifted at the top of its arc. cm. */
	double StepLiftCm = 0.0;
	/**
	 * Share of the swing (0 to 1) by which the foot's point is over its target. The leg lags behind its point, and a
	 * point that came down along the whole way set the foot on the floor short of the target (40% short at 0.25 s for
	 * 30 to 50 cm).
	 */
	double StepOverShare = 0.0;
	/**
	 * Share of the swing (0 to 1) from which the foot's point comes down; it may be less than step_over_share, so that
	 * the foot comes down on a slant while it still travels. Coming down in the last fifth of a 0.25 s swing only, the
	 * foot met the floor at 1.7 m/s. A touch of the floor ends the swing from the earlier of the two.
	 */
	double StepDescentShare = 0.0;
	/**
	 * The swinging leg's muscles (hip, knee, ankle of that side) are this many times as stiff as the legs' group
	 * while the foot is in the air: at the group's stiffness the leg lagged its point by 10 to 20 cm. The swing
	 * starts from where the leg is, so the stronger muscles start with nothing to pull. Factor, at least 1.
	 */
	double SwingStiffnessScale = 0.0;
	/**
	 * The swinging leg's muscles' damping, as a factor on the legs' group (the damping follows the muscle's
	 * target, so it helps the leg keep up with its point rather than holding it back): at the group's 0.06 s the
	 * foot overshot its point by 4 to 5 cm. Factor.
	 */
	double SwingDampingScale = 0.0;
	/**
	 * The swinging leg's muscles' pose_velocity_share (see FSandboxMuscleTuning), eased in with the stiffness: its
	 * target is the swing's own smooth path, which the leg has to keep up with. 0..1.
	 */
	double SwingPoseVelocityShare = 0.0;
	/**
	 * The balance assist's spring on the swinging foot (horizontal and vertical), pulling it along its swing,
	 * the target moving with it: the "foot-planting control" of the assist (docs/PRD.md, "Control stack"),
	 * which puts the foot where the step is aimed while the leg's muscles lag behind. Counted in the assist's
	 * load like every assist spring. N/m; 0 leaves the swing to the muscles alone.
	 */
	double SwingFootNPerM = 0.0;
	/**
	 * The swing ends this far below the height the foot was planted at, so that the leg puts the foot down on the
	 * floor rather than next to it in the air; it goes on until the foot touches (twice its time at most). cm.
	 */
	double StepPressCm = 0.0;
	/**
	 * A foot that touches the floor once it is over its target (in the swing's way down) has landed. It may touch
	 * with its toe or an edge (the ankle cannot keep it level when the leg reaches back or out): its point is then
	 * let down from where the bone was at the touch to the planted height over this long, so that the sole comes
	 * down onto the floor and the leg does not push the body up. Seconds.
	 */
	double StepSettleS = 0.0;
	/** A step that keeps to its own side puts the foot's bone at least this far to that side of the other foot's. cm. */
	double StepWidthMinCm = 0.0;
	/**
	 * A step whose target lies past the other foot's line (owner's decision in part 4: the legs may cross) lands at
	 * least this far ahead of the other foot's bone, or behind it for a step that goes back, and its swing passes the
	 * other foot that far in front or behind: calves and feet collide. cm.
	 */
	double CrossOffsetCm = 0.0;
	/** Time over which a step's placement of the legs eases in, and a swinging leg's frame changes. Seconds. */
	double StepEaseS = 0.0;
	/**
	 * While a foot swings, the leg that carries the body is solved with the pelvis upright and at its standing
	 * height, and this far (0 to 1) from its anchor toward where it is over the floor: 0 holds the body back
	 * toward the old support, 1 lets it travel over the stance foot toward the step.
	 */
	double StanceFollow = 0.0;
	/**
	 * The legs of planted feet are solved with the pelvis no higher than lets each reach its foot with the leg
	 * at most this share of its full length (hip to ankle): in a wide stance the pelvis is held lower than
	 * standing, so that no leg is asked for more than it has and lifts its foot off the floor. 0 to 1.
	 */
	double LegExtensionMax = 0.0;
	/**
	 * A step asked for while the body stands (not a recovery step) first moves the weight onto the other foot:
	 * the pelvis's goal in the legs' upright frames goes this share (0 to 1) of the way sideways from the middle
	 * of the feet to the stance foot (sideways and, from a stance with one foot ahead, forward or back too), and
	 * stays there until the swinging foot is down; then it goes over to the new support with the anchors.
	 */
	double StepShiftShare = 0.0;
	/** The foot lifts once the capture point is within this distance of the stance sole (or inside it). cm. */
	double StepShiftReadyCm = 0.0;
	/** Lag of that shift, in and out (out after the step has landed). Seconds. */
	double StepShiftS = 0.0;
	/** The foot lifts after this long of shifting even if the capture point is not where it should be yet. Seconds. */
	double StepShiftMaxS = 0.0;
	/**
	 * The anchors glide to a new support no faster than keeps the capture point of a body pulled along (its speed
	 * over omega ahead of it) within this share (0 to 1) of the way from the glide's end to the edge of the
	 * support, along the glide; such a glide takes longer than balance.anchor_follow_s. After a step of 25 cm
	 * back, a half-second glide put the capture point outside the new support. 0: no limit.
	 */
	double AnchorRoomShare = 0.0;
	/** How long the swing of a step asked for takes (a recovery step's takes balance.step_time_s). Seconds. */
	double StepAskedTimeS = 0.0;
	/**
	 * Lag of the pelvis's height in the legs' upright frames as it follows what the planted legs (and the swinging
	 * one, as it travels over) need to reach their feet; down and up alike. Seconds.
	 */
	double LowerS = 0.0;
	/**
	 * The legs' frames are lowered no further than this, and only this much of a pelvis below its standing height is
	 * the posture's own and not a fall. A leg of 81 cm reaches 46 cm to the side of its hip with the pelvis 15 cm
	 * down; a stance that needs more is no stance. (Unbounded, a body that had sprawled with its feet 80 cm apart was
	 * lowered by 42 cm, never counted as fallen, and its legs went on swinging until the solver blew up.) cm.
	 */
	double LowerMaxCm = 0.0;
	/**
	 * A step across the other foot lands no further past it (bone to bone, sideways) than this: the first of two
	 * steps, after which the other foot steps out. cm.
	 */
	double CrossPastCm = 0.0;
	/**
	 * In a stagger the planted legs are solved for a pelvis behind where it would be (balance.stance_follow of the way
	 * from its anchor to where it is) by the body's speed times this: they brake it in proportion to its speed. s.
	 */
	double StaggerDampingS = 0.0;
	/**
	 * After recovery steps, once the body has stood still this long, a foot steps back to its place beside the other
	 * (0: the feet stay where the steps put them). Seconds.
	 */
	double RegroupWaitS = 0.0;
	/** Feet within this of their stance are left alone. cm. */
	double RegroupToleranceCm = 0.0;
	/** At most this many regrouping steps after one loss of balance. */
	double RegroupMaxSteps = 0.0;
	/**
	 * Before a regrouping step the body goes to the stance foot: to the middle of its sole, less this share of the way
	 * from there to the middle of the sole the other foot will stand on (0 the middle of the stance sole, 0.5 the
	 * middle of the stance to come).
	 */
	double RegroupInsideShare = 0.0;
	/**
	 * The pelvis's goal is moved beyond that point by the distance the capture point still has to go times this...
	 */
	double RegroupLeadGain = 0.0;
	/** ...plus that distance times this for every second it stays, per second... */
	double RegroupShiftGainPerS = 0.0;
	/** ...by no more than this. cm. */
	double RegroupLeadMaxCm = 0.0;
	/**
	 * The foot of a regrouping step lifts when the capture point, where it will be as the foot lands, is this far
	 * inside the support the step gives. cm.
	 */
	double RegroupMarginCm = 0.0;
	/** A regrouping step whose weight shift has not got that far by then is given up. Seconds. */
	double RegroupShiftMaxS = 0.0;
	/**
	 * The arms join the fight for balance (poses.balance_*) once the capture point is further than ArmDeadCm from where
	 * the body stands, in full at ArmFullCm; what is asked of them follows with a lag of ArmResponseS. Quiet standing
	 * and the idle's sway stay inside the dead zone. ArmFullCm at or below ArmDeadCm: the arms take no part.
	 */
	double ArmDeadCm = 0.0;
	double ArmFullCm = 0.0;
	double ArmResponseS = 0.0;
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
	 * Doubling over. A hit from the front into the belly or the solar plexus that brings this much, N*s, beyond
	 * what was pressing already, makes an upright body double over: poses.double_over comes in over
	 * DoubleOverInS, is held until DoubleOverS after the hit and goes out over DoubleOverOutS, by its own clock
	 * whatever the body's state. 0: the body does not double over.
	 */
	double DoubleOverNs = 0.0;
	double DoubleOverS = 0.0;
	double DoubleOverInS = 0.0;
	double DoubleOverOutS = 0.0;
	/**
	 * The belly, as a stretch of the line from the pelvis's joint to the neck's: a hit whose point lies between
	 * these two shares of it (0 the pelvis, 1 the base of the neck) is one into the belly or the solar plexus.
	 */
	double DoubleOverFrom = 0.0;
	double DoubleOverTo = 0.0;
	/**
	 * A knockdown. A hit on the head that brings this much, N*s, beyond what was pressing already, summed over
	 * one contact, takes the legs from under an upright body: it falls, as it falls when its posture gives
	 * way, and gets up again as after any fall. A kick does it, a punch does not. 0: never.
	 */
	double KnockdownHeadNs = 0.0;
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
	/**
	 * A thing with mass as support (M4 part 4; 1 or 0). With it, a simulated body that pushes up on a part of
	 * the character is what the character stands or lies on, as the floor is: it goes to the sense of balance
	 * and is not felt as a hit or a press: one that has a weight of its own (a hand has none) and does not itself come
	 * up faster than ThingSupportMaxUpMps where it touches. Anything from the side or from above is felt as before.
	 */
	double ThingSupport = 0.0;
	/** Above 0.5 a hold is felt as a held push is: the body leans into it, steps for it and, while getting up, is pressed by it. */
	double HoldFelt = 0.0;
	double ThingSupportMaxUpMps = 0.0;
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
	/**
	 * Added on top of the pose while the body is doubled over (reactions.double_over_*): the trunk bends
	 * forward, the head goes down, the forearms come in front of the belly.
	 */
	FSandboxPose DoubleOver;
	/**
	 * What a body that falls does with itself, by the way it goes down: backward it sits down and curls up with the
	 * chin on the chest and the hands behind it; forward the hands go out in front and the knees bend; sideways
	 * (written for a fall to the left; a fall to the right uses the mirror image) the arm of that side reaches for
	 * the floor. Held with the falling state's muscles, and kept, with the lying state's, once the body lies.
	 * (Without them the falling body kept the standing pose and went over like a plank.)
	 */
	FSandboxPose FallBack;
	FSandboxPose FallForward;
	FSandboxPose FallSide;
	/** Time over which the target goes from the pose held to the pose of the fall. Seconds. */
	double FallBlendS = 0.0;
	/**
	 * What the arms do while the body fights for its balance, added on top of the pose in proportion to how far the
	 * capture point is from where the body stands (balance.arm_*): going over backward the arms come forward and up,
	 * going over forward they go back and out, going over to the left (written for the left; to the right uses the
	 * mirror image) the far arm goes up and out. An arm thrown the way the body tips turns the trunk back the other
	 * way, and its weight ends up on the side the body has to come back to.
	 */
	FSandboxPose BalanceBack;
	FSandboxPose BalanceForward;
	FSandboxPose BalanceSide;
	/**
	 * The clip whose motion a standing body carries on top of its stance: breathing, the shoulders, a slow sway
	 * (FSandboxIdleMotion; "poses.idle.animation", an asset path, empty for none). Read when the body starts; a
	 * change while it runs is not followed.
	 */
	FString IdleAnimation;
	/** How much of the clip's turns of trunk, neck and arms the body carries: 1 as recorded, 0 none ("poses.idle.weight"). */
	double IdleWeight = 0.0;
	/** How much of the clip's sway of the pelvis the whole body leans by, about its ankles ("poses.idle.sway_weight"). */
	double IdleSwayWeight = 0.0;

	/** The mirror image of a pose: left and right joints change places, the trunk's sideways bend and twist change sign. */
	static FSandboxPose Mirrored(const FSandboxPose& Pose);
};

/** How a body that lies gets up ("getup.*"). */
struct FSandboxGetUpTuning
{
	/** A body that has lain still this long gets up. 0: it stays down. Seconds. */
	double RestS = 0.0;
	/** Time over which the muscles and the assist's pull come up from nothing as the get-up begins. Seconds. */
	double EaseS = 0.0;
	/** How fast the clip's time runs while the body keeps up: 1 as recorded. */
	double Rate = 0.0;
	/** The time in a clip by which its body stands; the rest of the clip is standing still. Seconds. */
	double EndS = 0.0;
	/** The clip's time waits while the pelvis is further than this below or above where the clip has it... cm. */
	double LagCm = 0.0;
	/**
	 * ...or further than this from it over the floor. cm. Wider than the other: rising is what takes strength and
	 * has to be waited for, while a get-up in place may as well happen a hand's width to the side, which is where a
	 * table's leg or a chair leaves it room.
	 */
	double SideLagCm = 0.0;
	/** ...or a foot that is off the floor is further than this from where the clip has it. cm. */
	double FootLagCm = 0.0;
	/**
	 * ...but for a foot no longer than this at a time: a foot that does not come (a leg that lies across the other,
	 * or against a chair) is left where it is and the clip goes on. Seconds. (Waited for without end, such a foot
	 * kept the clip at its first key for six attempts.)
	 */
	double FootWaitS = 0.0;
	/**
	 * The clip is laid over the feet the body stands on: its root follows the feet that carry weight, to where they
	 * are from where the clip has them, with this lag when they carry all of the body's weight and more slowly in
	 * proportion when they carry less. 0: the clip stays where it was put. Seconds. (A foot that the body stands on
	 * cannot go where the clip steps it to, and a pelvis pulled over the clip's feet instead of the body's own left
	 * the body leaning or waiting for good.)
	 */
	double FrameFollowS = 0.0;
	/**
	 * Where no foot carries the body (it lies, rolls over, is on its hands and knees), the clip is laid over the body
	 * itself: its root follows the pelvis over the floor with this lag, less in proportion to the weight the feet
	 * carry. 0: it does not. Seconds. (A body that turns into the clip's first pose moves 20 to 35 cm over the floor.
	 * A clip that stayed where it was put waited for the assist to drag the pelvis back, which is what half the
	 * assist's force was for; with a weaker assist it waited for good. A blow moves the pelvis faster than this
	 * follows, and still counts as one.)
	 */
	double PelvisFollowS = 0.0;
	/**
	 * While something presses on the body with less than hold_force_n, how much of that following is left when the
	 * feet carry all of the body's weight: 0 none (the clip is laid over the feet alone, as when nothing presses),
	 * 1 all of it (feet and pelvis each pull the clip their way and meet in between). It lets a body that one hand
	 * presses on get up where the hand leaves it room.
	 */
	double PressedFollow = 0.0;
	/**
	 * ...and how far below the clip's the pelvis may then be before the clip waits, in place of lag_cm. cm. A body
	 * that pushes on under a hand sags under it, and the clip has to run further ahead for the muscles to be asked
	 * for more.
	 */
	double PressedLagCm = 0.0;
	/**
	 * A body pressed down with this much or more does not push on: its clip waits, and the attempt is given up after
	 * stall_s. N; 0: it always pushes on. What presses is the force other simulated bodies (hands) put on the body in
	 * every frame of reactions.held_push_s. Between what one hand and two hands can press with, so that one hand
	 * slows a body that is getting up and two keep it down (owner's rule).
	 */
	double HoldForceN = 0.0;
	/** A get-up whose clip has waited this long without moving on is given up: the body lets go and lies, and tries again. Seconds. */
	double StallS = 0.0;
	/**
	 * ...and so is one whose clip has waited this long in all, counting only the time in which nothing pressed on the
	 * body. Seconds; 0: no such bound. A clip that moves on by a frame now and then never stands still for stall_s,
	 * and the body held one pose for up to 13 s (under the table's edge, on the stand of 50 falls).
	 */
	double WaitS = 0.0;
	/** A pelvis this far from where the clip has it has been thrown off: the get-up is given up at once. cm. */
	double LostCm = 0.0;
	/** The body stands again once the clip is through and its pelvis is within this of upright, with both feet on the floor. Degrees. */
	double UprightDeg = 0.0;
	/**
	 * The clips, by how the body lies at the clip's start: on its back, on its front, on its left side, on its right
	 * ("getup.clip_back" and so on; animation sequence paths, read when a body first gets up). The one whose pelvis
	 * lies most like the body's is taken, whatever its name.
	 */
	FString ClipBack;
	FString ClipFront;
	FString ClipLeft;
	FString ClipRight;
};

/** Which bones the balance assist holds ("rig.pelvis" and so on). Each must have a body in the physics asset. */
struct FSandboxBodyRig
{
	FName Pelvis;
	FName Chest;
	/** The body that carries the head; in the NPC's physics asset head and neck are one body on neck_01. */
	FName Head;
	/**
	 * The bone whose body carries the head in an asset that has a head of its own on top of the one Head names (the neck);
	 * none: Head is the head, whatever the asset has.
	 */
	FName HeadIfOwn;
	FName FootLeft;
	FName FootRight;

	bool operator==(const FSandboxBodyRig& Other) const
	{
		return Pelvis == Other.Pelvis && Chest == Other.Chest && Head == Other.Head && HeadIfOwn == Other.HeadIfOwn && FootLeft == Other.FootLeft && FootRight == Other.FootRight;
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
	FSandboxMuscleTuning Toes;

	FSandboxAssistTuning Pelvis;
	FSandboxAssistTuning Chest;
	FSandboxAssistTuning Head;
	FSandboxAssistTuning Feet;

	FSandboxSolverTuning Solver;
	FSandboxSurfaceTuning Surface;
	FSandboxThresholdTuning Thresholds;
	FSandboxBalanceTuning Balance;
	FSandboxReactionTuning Reactions;
	FSandboxPoseTuning Poses;
	FSandboxGetUpTuning GetUp;

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
	/** "standing", "flinch", "stagger", "falling", "lying" or "get_up": the state's name in tuning files and result files. */
	static const TCHAR* StateName(ESandboxBodyState State);
};
