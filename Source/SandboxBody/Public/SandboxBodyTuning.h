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
	/**
	 * How much of the torque that carries the weight of what hangs beyond the joint the muscle gives on top of its spring
	 * (FSandboxWeightTorques): 0 none, the spring carries it by its error; 1 all of it, the spring is there for the pose.
	 */
	double WeightShare = 0.0;
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
	 * the turn that gives it follows from the body's weight and the legs' stiffness. 0 switches it off. Forward and back only:
	 * measured on this body, a sideways turn of the ankles after a blow took the load off the far foot and made the capture
	 * point leave the support, whichever way it turned (balance.ankle_side_gain, at 0 since M3, taken out in the audit of
	 * 28 November 2026).
	 */
	double AnkleGain = 0.0;
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
	 * The jumps of a step put right, each by itself; 0 leaves it as it was, 1 (above 0.5) puts it right.
	 * LiftSmooth: at a lift the leg's frame goes over from the one the leg was last solved in (and not from the whole of
	 * the one it was on its way to), and the foot's level and its ankle's limit come in over StepEaseS.
	 * SettleSmooth: a foot that has landed settles from the height its leg was solved for, not from where its bone is.
	 * ReleaseSmooth: a leg let go as a fall begins goes back to the pose from the target it was solved for.
	 */
	double LiftSmooth = 0.0;
	double SettleSmooth = 0.0;
	double ReleaseSmooth = 0.0;
	/** Time over which the swinging leg's own strength goes once it has landed; 0: at once. Seconds. */
	double LandEaseS = 0.0;
	/** Time over which the muscles go from the strengths of the state before to those of a fall as it begins; 0: at once. Seconds. */
	double FallEaseS = 0.0;
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
	 * After a hit felt while standing, the body moves by mechanics alone for this long (N4): the flinch and its weakening,
	 * a doubling over, the balance strategies' new goals and a decision to step wait for it; a fall by the posture does not.
	 * 0: all at once, as before. Seconds.
	 */
	double ReflexDelayS = 0.0;
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

/** A pose of one hand's fingers, counted from the reference pose, in which the hand is open ("poses.fingers.<pose>.*"). Both hands alike. */
struct FSandboxFingerPose
{
	/** How far each finger is bent toward the palm, over its three joints together: thumb, index, middle, ring, pinky. Degrees. */
	double CurlDeg[5] = { 0.0, 0.0, 0.0, 0.0, 0.0 };
	/** How far the fingers part from the middle one (the index by this, the others in proportion); negative closes them. Degrees. */
	double SpreadDeg = 0.0;

	bool IsZero() const { return CurlDeg[0] == 0.0 && CurlDeg[1] == 0.0 && CurlDeg[2] == 0.0 && CurlDeg[3] == 0.0 && CurlDeg[4] == 0.0 && SpreadDeg == 0.0; }
};

/**
 * Poses of the fingers ("poses.fingers.*"): which is asked for follows what the body does. The fingers have no bodies:
 * a pose of them moves nothing and is for the eye.
 */
struct FSandboxFingerTuning
{
	/** How much of the poses the fingers take, 0 (they stay as modelled) to 1. */
	double Weight = 0.0;
	/** Time in which the fingers go from one pose to another, eased. Seconds. */
	double BlendS = 0.0;
	/** Relaxed: standing, lying. */
	FSandboxFingerPose Rest;
	/** A closed hand; half of it while the body flinches or staggers. */
	FSandboxFingerPose Fist;
	/** Falling: spread, for the floor. */
	FSandboxFingerPose Splayed;
	/** A hand that the world pushes on (the floor under it, as the body gets up): flat. */
	FSandboxFingerPose Flat;
};

/** The poses the body is asked to hold, and how it goes from one to the next. */
struct FSandboxPoseTuning
{
	/** Time over which the target goes from one pose to the next, eased in and out. Seconds. */
	double BlendS = 0.0;
	/** The fingers. */
	FSandboxFingerTuning Fingers;
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
	 * R2, step 9 (getup.roll): what a body lying on its left side does to roll onto its back or its front, the upper arm and leg
	 * thrown the way it rolls and the trunk turned after them; mirrored for a body on its right side. The pose itself, not added on top.
	 */
	FSandboxPose RollToBack;
	FSandboxPose RollToFront;
	/**
	 * R3 (docs/recon/r3_plan.md): the curl of a body that has no time to catch its fall (item d: the pose itself, the knees and the
	 * arms in, the back rounded, the chin in); and, laid on top of the fall's pose, sitting down going backward and turning toward a
	 * side (item e; the turn written for the left, mirrored for the right).
	 */
	FSandboxPose FallCurl;
	FSandboxPose FallSit;
	FSandboxPose FallTurn;
	/**
	 * R10, step 5 (behaviour.stay_down): a body that lies and stays down while the player stands over it covers its head: both forearms
	 * over the face and the head, the knees drawn up, the chin in. The pose itself, not added on top.
	 */
	FSandboxPose ProtectHead;
	/**
	 * R10, step 6 (behaviour.protect): the covers after a hit on the trunk (the forearms across the chest and the belly, the knees up),
	 * the groin (the hands at the groin, the hips and knees flexed) and a leg (the knees drawn in, the hands to the knees). Poses themselves.
	 */
	FSandboxPose ProtectTrunk;
	FSandboxPose ProtectGroin;
	FSandboxPose ProtectLeg;
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

/** One kind of joint's own muscle, as factors on its group's ("muscles.joints.<kind>.*"); 1, 1, 1 is the group's muscle. */
struct FSandboxJointScale
{
	double StiffnessScale = 0.0;
	double DampingScale = 0.0;
	/** On the group's cap: a person's joints are not as strong as each other within a group (an ankle against a hip). */
	double MaxTorqueScale = 0.0;
};

/** The kinds of joint that have values of their own; both sides alike. */
constexpr int32 SandboxJointKindCount = 12;
extern SANDBOXBODY_API const TCHAR* const SandboxJointKinds[SandboxJointKindCount];

/** The levels of the body's tone. */
enum class ESandboxTone : uint8
{
	Limp,
	Relaxed,
	Ready,
	Braced
};
constexpr int32 SandboxToneCount = 4;

/**
 * The body's tone ("tone.*"): how tense each muscle group is now, as factors on its stiffness and damping, for four named
 * levels; it follows the level asked for, faster up than down, so that no strength jumps. Ready is 1 everywhere.
 */
struct FSandboxToneTuning
{
	/** Factors on stiffness and on damping, by level and by muscle group (spine, neck, arms, legs, toes). */
	double Stiffness[SandboxToneCount][5] = {};
	double Damping[SandboxToneCount][5] = {};
	/** Time constants of the way up (bracing) and down (letting go). Seconds. */
	double RiseS = 0.0;
	double FallS = 0.0;
	/** The level each group stands at with nothing happening: 0 limp, 1 relaxed, 2 ready, 3 braced. */
	double Standing[5] = {};
	/**
	 * A hit of at least BraceFromNs braces the whole body, BraceDelayS after it (a reflex, not the frame of the hit), for
	 * BraceHoldS; 0 never. N*s, s.
	 */
	double BraceFromNs = 0.0;
	double BraceDelayS = 0.0;
	double BraceHoldS = 0.0;
};

/** The fall with an aim (N6, "fall.*"): the arms reach for the floor while the body goes down. */
struct FSandboxFallAimTuning
{
	/** 1 reaches, 0 does not (today's fall, bit for bit). */
	double Aim = 0.0;
	/** How far along the fall's way from the shoulder the hand is sent, and how high above the floor its wrist. Centimetres. */
	double ReachAheadCm = 0.0;
	double HandHeightCm = 0.0;
	/** How fast the reach comes in, and how fast the fall's way follows the body. Seconds. */
	double ReachBlendS = 0.0;
	double WayFollowS = 0.0;
	/** The arms' tone while they reach (before a hand touches), and once a hand has touched (they give way): factors. */
	double ReachStiffness = 0.0;
	double ReachDamping = 0.0;
	/** 1: the hands reach in a fall backward too (behind the hips); 0: only forward and sideways. */
	double ReachBack = 0.0;
	double YieldStiffness = 0.0;
	double YieldDamping = 0.0;
	/**
	 * R3, step 2 (docs/recon/r3_step2_plan.md): 1 lays a blend of the sample's reaching poses over the fall's written pose while the
	 * body falls, weighted every tick by the sensed way in the body's frame (forward, back, left, right); 0 does not (today's fall, bit
	 * for bit).
	 */
	double Blend = 0.0;
	/** How much of the blend lies over the written fall pose: 1 all of it. */
	double BlendShare = 0.0;
	/** How fast the weights follow the way, and the time in each reaching clip its pose is read at. Seconds. */
	double BlendFollowS = 0.0;
	double BlendClipS = 0.0;
	/** The reaching clips, for a fall forward, back, to the left and to the right of the body. */
	FString ClipReachForward;
	FString ClipReachBack;
	FString ClipReachLeft;
	FString ClipReachRight;

	/**
	 * R3, item a (docs/recon/r3_plan.md): 1 makes the arms' tone while the body falls a curve of the sensed time to impact (as the
	 * sample's strengths from curves): loose far from the floor, braced as the floor comes, giving way once a hand or a forearm touches
	 * the world; 0 does not (today's fall, bit for bit). Every change goes through the tone's follow (tone.rise_s, tone.fall_s).
	 */
	double ArmCurve = 0.0;
	/** The sensed time to impact under which the arms brace. Seconds. */
	double ArmBraceFromS = 0.0;
	/** The arms' tone far from the floor, braced, and once a hand or a forearm has touched: factors of the falling arms' stiffness and damping. */
	double ArmFarStiffness = 0.0;
	double ArmFarDamping = 0.0;
	double ArmBraceStiffness = 0.0;
	double ArmBraceDamping = 0.0;
	double ArmTouchStiffness = 0.0;
	double ArmTouchDamping = 0.0;
	/**
	 * R3, item b: 1 places the arms in the trunk's frame toward where the floor will be (the catch fall); 0 does not. The trunk's
	 * orientation at impact is foreseen as the chest's now tipped along the sensed way until it leans CatchTiltDeg from upright; the
	 * floor's direction from each shoulder then (down, and CatchAhead along the way) is held in that foreseen chest's frame, so the
	 * hand's target moves with the trunk, never with the world.
	 */
	double Catch = 0.0;
	/** How far the trunk is foreseen to lean from upright as it meets the floor. Degrees. */
	double CatchTiltDeg = 0.0;
	/** How far along the way the floor's direction from the shoulder goes, for each unit down: 0 straight down. */
	double CatchAhead = 0.0;
	/** The hand's distance from its shoulder, a share of the arm's length (1: straight). */
	double CatchReach = 0.0;
	/** How fast the way the catch is aimed by follows the sensed way; and the time to impact under which it is held. Seconds. */
	double CatchFollowS = 0.0;
	double CatchHoldS = 0.0;
	/** How fast the placement comes in. Seconds. */
	double CatchBlendS = 0.0;
	/** 1: the arms catch a fall backward too (behind the hips); 0: a fall backward keeps the written pose's arms. */
	double CatchBack = 0.0;
	/** The arms' torque caps while they catch: a factor of the arms' max_torque_nm. */
	double CatchMaxTorqueScale = 0.0;
	/** A catching arm is let go, for the rest of that fall, once the way turns more than this from the way it was aimed by. Degrees. */
	double CatchTurnDeg = 0.0;
	/**
	 * 1: a catching arm is blended in and out by its joints' turns (as the guard's arm, FSandboxLimbTarget::bJointBlend); 0: by where its
	 * hand is (the hand target's way), which turned the elbow's target over in some falls (docs/testing.md, "R3"; decision 56).
	 */
	double CatchJointBlend = 0.0;
	/** 1: the catch is let go once the trunk has met the world (the arms' work is done); 0: when the body lies (impact_let_go_s later). */
	double CatchLetGoAtTrunk = 0.0;
	/**
	 * R3, item c: 1 keeps the head off the floor (the neck's tone braced while falling, and the head's own joint turned away from the
	 * floor by the sensed way: the chin in going backward, the head back going forward, the ear to the upper shoulder going sideways);
	 * 0 does not.
	 */
	double Head = 0.0;
	/** The neck's tone while falling: factors of the falling neck's stiffness and damping. */
	double HeadStiffness = 0.0;
	double HeadDamping = 0.0;
	/** How fast the head's turn follows the way. Seconds. */
	double HeadFollowS = 0.0;
	/** The head's turn, going backward (the chin in), forward (the head back) and sideways (away from the floor). Degrees. */
	double HeadBackDeg = 0.0;
	double HeadForwardDeg = 0.0;
	double HeadSideDeg = 0.0;
	/**
	 * R3, item d: 1 curls the body up when there is no time to catch (the sensed time to impact at the fall's first tick under
	 * CurlBelowS): the fall's pose is poses.fall_curl for that whole fall and the arms do not catch; 0 does not.
	 */
	double Curl = 0.0;
	double CurlBelowS = 0.0;
	/** Or by the hit that felled it: a fall begun within a second of a hit of at least this (the struck part's sum) curls; 0 not by a hit. N*s. */
	double CurlFromNs = 0.0;
	/**
	 * R3, item e: 1 sits a fall backward down (poses.fall_sit laid on top of the fall's pose, over SitBlendS, once the backward share of
	 * the way is over SitFrom); 0 does not.
	 */
	double Sit = 0.0;
	double SitFrom = 0.0;
	double SitBlendS = 0.0;
	/** While it sits down, the legs' tone: a factor of the falling legs' stiffness (below 1 the knees give under the weight). */
	double SitLegStiffness = 0.0;
	/**
	 * R3, item e: 1 turns a fall backward or forward toward a side when there is time (the sensed time to impact at its first tick at
	 * least TurnMinS): poses.fall_turn, written for a turn to the left, mirrored for the right, laid on top over TurnBlendS; 0 does not.
	 */
	double Turn = 0.0;
	double TurnMinS = 0.0;
	double TurnBlendS = 0.0;
	/** The least difference of the way's left and right weights that picks a side to turn to (a way straight back picks none). */
	double TurnSideMin = 0.0;
	/**
	 * R3, item f: 1 turns each catching hand so that its palm faces the floor's direction at impact (PalmShare of the turn, within
	 * the wrist's range); 0 leaves the hand as the arm's placement has it.
	 */
	double Palms = 0.0;
	double PalmShare = 0.0;
};

/** What the body makes of what moves near it (N5, "perception.*"). Step 1 judges and does nothing with it. */
struct FSandboxPerceptionTuning
{
	/** 1 judges what it is told (a scenario's body only with "perception": true); 0 not at all. */
	double Enabled = 0.0;
	/** In view: within this angle of where the head faces. Degrees. */
	double ViewHalfAngleDeg = 0.0;
	/** Noticed: this long in view. Seconds. */
	double NoticeS = 0.0;
	/** A threat: closing on the head or the trunk at least this fast with at most this long to the contact; or this near the face. */
	double ThreatSpeedMps = 0.0;
	double ThreatTimeS = 0.0;
	double ThreatNearCm = 0.0;
	/** The sizes contact is reckoned with: the head's and the trunk's, and a thing's that is told with none. Centimetres. */
	double HeadRadiusCm = 0.0;
	double TrunkRadiusCm = 0.0;
	double ThingRadiusCm = 0.0;
	/** Gaze (N5 step 2): 1 turns the head toward what the body attends to; 0 not. */
	double Gaze = 0.0;
	/** How much of the angle the head takes (the eyes the rest, in M5), how fast it turns, how far. Share, deg/s, deg. */
	double GazeHeadShare = 0.0;
	double GazeSpeedDps = 0.0;
	double GazeMaxYawDeg = 0.0;
	double GazeMaxPitchDeg = 0.0;
	/** How long it stays on a thing before another may take its place (a threat always may), and how fast a thing must move to be looked at. */
	double GazeDwellS = 0.0;
	double GazeMoveMps = 0.0;
	/** How long the head takes to come up to its turning speed: the neck's target never starts or stops turning in one frame. Seconds. */
	double GazeRiseS = 0.0;
	/**
	 * Anticipation (N5 step 4): 1 braces the body and draws the head in before the contact of a threat it noticed; 0 not. It begins react_s
	 * after the threat (a person's reaction to what is seen), holds anticipate_hold_s, and its pose comes in over anticipate_pose_s.
	 */
	double Anticipate = 0.0;
	double ReactS = 0.0;
	double AnticipateHoldS = 0.0;
	double AnticipatePoseS = 0.0;
	/**
	 * The guarding arm (N5 step 5): 1 raises the near arm against a threat to the head that has at least guard_time_s to the contact, or is near the
	 * face; its hand goes guard_reach_cm from the face toward the threat, for guard_hold_s, eased over guard_blend_s. 0 not.
	 */
	double Guard = 0.0;
	double GuardTimeS = 0.0;
	double GuardReachCm = 0.0;
	double GuardHoldS = 0.0;
	double GuardBlendS = 0.0;
};

/** R10's behaviours ("behaviour.*", docs/recon/r10_plan.md): fear (step 4), staying down and lowering (step 5). Every switch 0: none. */
struct FSandboxBehaviourTuning
{
	/**
	 * Fear (step 4; decision 41): 1 keeps one number from 0 to 1 that rises by fear_per_ns for each N*s of a hit from something that moves
	 * (a contact's whole hit, as the body counts it; the static world is no such hit) and by fear_per_threat for each threat N5 sees, and
	 * falls to 0 over fear_fade_s from the last of them, whether or not the player is near. Read only by the behaviours below. 0 none.
	 */
	double Fear = 0.0;
	double FearPerNs = 0.0;
	double FearPerThreat = 0.0;
	double FearFadeS = 0.0;
	/**
	 * Staying down (step 5; FR-48, FR-49, FR-51; C33): 1 holds back a get-up while the body's fear is at least fear_stay_down and a viewer
	 * (the player, ESandboxSeenKind::Viewer) is within near_cm of it over the floor; it may begin again once the viewer is beyond near_cm
	 * and near_margin_cm, or its fear is below the line, and getup.rest_s has gone since. A get-up under way when that becomes true is
	 * turned back after perception.react_s: its clip runs back to where the attempt began at lower_rate of its pace, led as it rose
	 * (lowered, not dropped), and the attempt is then given up (ESandboxGiveUp::Afraid); lower_rate 0 gives it up at once, the world's
	 * pull let go over getup.carry_release_s. Lying so, the body holds poses.protect_head, its muscles at the protect_* stiffnesses (the
	 * lying state's are too weak to hold a pose), eased in and out over protect_blend_s. 0 none.
	 */
	double StayDown = 0.0;
	double FearStayDown = 0.0;
	double NearCm = 0.0;
	double NearMarginCm = 0.0;
	double LowerRate = 0.0;
	/** The clip's pace turns from forward to back over this long (s): turned in one frame, the reference's speed reverses at once. */
	double LowerTurnS = 0.0;
	/**
	 * The fastest the clip's pelvis may come down as it is run back, m/s: its pace back is lowered where the clip rose faster (0: no bound).
	 * At 0.7 of the pace alone a body that began sitting against a wall came down at 1.02 m/s (C33's limit 1.0).
	 */
	double LowerMaxMps = 0.0;
	/** After an attempt given up afraid, the world's pull goes over this long (s) instead of getup.carry_release_s; 0: that. */
	double LowerReleaseS = 0.0;
	/**
	 * Commands (step 8 in part; FR-51, FR-52; C39): 1 answers get_up (up at once, the disposition set aside: the player may be near; a
	 * cover after a hit or an attempt given up interrupts it and it goes on, at most command_tries times) and stop (ends the command in
	 * force); 0 refuses every command ("off"). Told by a session or a run (USandboxBodyController::TellCommand, the console's Sandbox.Command).
	 */
	double Commands = 0.0;
	double CommandTries = 0.0;
	/** From a get_up to the first attempt for it, at least, s (C39: a person answers a spoken word in 0.2 to 1.0 s; a light in 0.15). */
	double CommandReactS = 0.0;
	/**
	 * R10's gate (C39): 1 lets the protective pose go command_react_s after a get_up or a give_hand is told, not in its tick, and counts
	 * that as the command's first change of a target. 0: let go in the command's tick, the first motion the get-up begun or the arm reaching.
	 */
	double CommandCoverReact = 0.0;
	/**
	 * The cover after a hit (step 6, in part: the head; C34; decision 45): 1 makes a hit on the head that sums protect_from_head_ns or more
	 * in one contact, while the body lies or gets up, give an attempt up (ESandboxGiveUp::Protect) and cover the head (poses.protect_head)
	 * for protect_hold_s; no get-up begins meanwhile. The body's own landing is no such hit (the static world is not felt as one). 0 none.
	 */
	double Protect = 0.0;
	double ProtectFromHeadNs = 0.0;
	/** The same for the trunk, the groin and the legs (decision 45: 40 N*s, the doubling over's line); each covered by its pose. */
	double ProtectFromNs = 0.0;
	double ProtectHoldS = 0.0;
	double ProtectBlendS = 0.0;
	/**
	 * A leg taken hold of is not covered (R10's gate, C35): 1 begins a leg's cover protect_leg_wait_s after the hit, and not at all if a
	 * hand takes hold of a leg meanwhile; a leg's cover is let go when a hand takes hold of a leg. Two hands that close on the ankles press
	 * 40 N*s into the feet before their grips close (28 November). 0 covers a struck leg at once, as before.
	 */
	double ProtectHeldLeg = 0.0;
	double ProtectLegWaitS = 0.0;
	/** N*m/rad of the spine's, the neck's, the arms' and the legs' muscles while the body protects itself lying; their damping, s. */
	double ProtectSpineNmPerRad = 0.0;
	double ProtectNeckNmPerRad = 0.0;
	double ProtectArmsNmPerRad = 0.0;
	double ProtectLegsNmPerRad = 0.0;
	double ProtectDampingS = 0.0;
	/**
	 * The offered hand (step 8's rest; FR-52, decision 47; C39): 1 answers give_hand: a body that lies reaches the arm of its nearer
	 * shoulder toward the player's nearest open hand (grip under offer_open_grip) within offer_reach_cm of that shoulder, for offer_s;
	 * once a hand takes hold of that arm, it gets up on it as for get_up, and while that get-up runs a hand that pulls within
	 * offer_along_deg of straight up is a help, not a hold: it does not slow the clip (decision 47; the world's pull still goes in
	 * proportion to the contact, as for any hold). 0 refuses give_hand ("off").
	 */
	double GiveHand = 0.0;
	double OfferS = 0.0;
	double OfferReachCm = 0.0;
	double OfferOpenGrip = 0.0;
	/** The arm comes and goes over this long, s, its muscles (the lying arm's are too weak to lift it) with it: N*m/rad, damping s. */
	double OfferBlendS = 0.0;
	double OfferArmsNmPerRad = 0.0;
	double OfferDampingS = 0.0;
	double OfferAlongDeg = 0.0;
	/**
	 * The struggle (step 7; FR-50, decision 44; C35): 1: a body that lies and is dragged (the drag of decision 18) struggles in a share
	 * struggle_chance of drags (drawn once a drag, FSandboxChance), in attempts every struggle_every_s that last struggle_attempt_s, each
	 * with a vigour drawn from struggle_vigour_min to 1: every held arm is pulled in (its hand toward its shoulder, struggle_pull_in_cm
	 * from it, along the line to where it is held) and every held leg drawn up (its foot toward its hip, struggle_leg_in_cm from it), by
	 * muscles of struggle_arms_nm_per_rad and struggle_legs_nm_per_rad times the vigour. Whether a grip lets go is the grip's own rule;
	 * the body never opens one. 0 none.
	 */
	double Struggle = 0.0;
	double StruggleChance = 0.0;
	double StruggleEveryS = 0.0;
	double StruggleAttemptS = 0.0;
	double StruggleVigourMin = 0.0;
	double StrugglePullInCm = 0.0;
	double StruggleLegInCm = 0.0;
	double StruggleArmsNmPerRad = 0.0;
	double StruggleLegsNmPerRad = 0.0;
	double StruggleBlendS = 0.0;
	/** A drag, for the struggle: hands that pull the lying body with this much in all (N) for perception.react_s. */
	double StruggleFromN = 0.0;
	/** The pulled-in limbs' muscles' cap on torque during an attempt, N*m, times the vigour (a yank: what can bring a grip to its 450 N). */
	double StruggleMaxTorqueNm = 0.0;
};

/** How the weight torques are worked out ("muscles.weight_*"). */
struct FSandboxWeightTuning
{
	/** Smoothing of what the world pushes the bodies with, and of how much of the weight the world holds up. Seconds. */
	double FollowS = 0.0;
	/**
	 * Where the push under a standing foot comes from: 0 what the floor pushed it with; 1 a share of the weight by where the
	 * centre of mass lies between the soles, only while the body is upright. (Measured, the torque follows the push it makes:
	 * an integrator of the spring's error.)
	 */
	double LegsBy = 0.0;
	/**
	 * The ankle strategy as torques (0 or 1): the shift of the centre of pressure it asks for is a shift of where the floor
	 * pushes the feet that the leg torques are worked out from, bounded by the soles, and not a turn of the ankles' targets.
	 * With LegsBy 1.
	 */
	double AnkleAsTorque = 0.0;
	/**
	 * How much of what the assist wants of the pelvis, the chest and the head of an upright body is given by the legs and the
	 * trunk (0 to 1) and not by the world: a horizontal force and an upright torque are a push of the floor at a shifted
	 * place, bounded by the soles and by friction. What is cut off is not given back to the world. With LegsBy 1.
	 */
	double InternalShare = 0.0;
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
	 * ...and so is one whose clip has waited this long in all, counting only the time in which nothing pressed on the body. Seconds;
	 * 0: no such bound. A clip that moves on by a frame now and then never stands still for stall_s (under holds that slow it to a
	 * crawl and let go, the clip stands and goes in turns), and the body held one pose for up to 13 s once.
	 */
	double WaitS = 0.0;
	/** A pelvis this far from where the clip has it has been thrown off: the get-up is given up at once. cm. */
	double LostCm = 0.0;
	/** The body stands again once the clip is through and its pelvis is within this of upright, with both feet on the floor. Degrees. */
	double UprightDeg = 0.0;
	/**
	 * N7: how long the body lies depends on how it fell. The rest is RestS plus this per newton-second of the hardest hit felt
	 * from a second before the fall to the get-up, and at most RestMaxS (0: no bound). 0: RestS for every fall. s per N*s, s.
	 */
	double RestPerNsS = 0.0;
	double RestMaxS = 0.0;
	/**
	 * The led get-up (docs/active-ragdoll.md, part R1; the only get-up since R2's rest, the clip-following path and its switch
	 * getup.carry gone): every body of the character is pulled by the world toward where the clip has it (FSandboxCarry; the clip's
	 * pose chained on the body's own proportions), the clip runs at its rate without waiting for the pelvis or a foot and is not laid
	 * over the feet, and the old assist on pelvis and feet is off while getting up. The springs per kg of each body (CarryNPerMPerKg,
	 * CarryNmPerRadPerKg), their damping as a time (CarryDampingS), their caps in weights of the body and in N*m per kg
	 * (CarryMaxWeights over the floor, CarryMaxUpWeights upward: what the world may carry of each body's weight; it never pushes a body
	 * down; CarryMaxNmPerKg). They come up with the get-up's ease and go over CarryReleaseS, by their own clock, once the get-up has
	 * ended. A hit felt of CarryYieldNs or more takes all of the pull (below, HitBackS).
	 */
	double CarryNPerMPerKg = 0.0;
	double CarryNmPerRadPerKg = 0.0;
	double CarryDampingS = 0.0;
	double CarryMaxWeights = 0.0;
	double CarryMaxUpWeights = 0.0;
	double CarryMaxNmPerKg = 0.0;
	double CarryReleaseS = 0.0;
	double CarryYieldNs = 0.0;
	/**
	 * R2, decision 14 of docs/active-ragdoll.md: the hold of a led get-up taken body by body, and the clip slowed by the force
	 * (getup.hold_by_body, which chose R1's hold of the whole body instead, went in the audit of 28 November 2026).
	 * Each hold (a hand's grip, told by TellHolds) takes the carry's springs off its chain: the held body and, by
	 * HoldChain (0 the held body alone, 1 its chain to the pelvis without the pelvis, 2 with it, 3 with it and everything that hangs
	 * from the held body), the bodies toward the pelvis, each further one counting HoldChainFalloff less (0: alike). A chain's hold
	 * is the pull over HoldChainFullN (at most 1); a body keeps 1 - (1 - HoldChainShare) * hold of its springs. Every body, in a
	 * chain or not, also keeps no more than 1 - (the holds' pulls added) / HoldRestFullN (0: the rest is led whole). A press from
	 * contacts takes the springs off the whole body as before, up to
	 * HoldForceN. The clip runs at Rate times a pace that is 1 below HoldSlowFromN of force and 0 at HoldForceN, in proportion
	 * between; the force is the press, counted at HoldPressPace of itself (1: as a hold), and the holds together, as vectors added
	 * (HoldSum 0) or by their sizes (1). Shares and the
	 * pace go down at once and back by CarryReleaseS. N, shares, modes.
	 */
	double HoldChain = 0.0;
	double HoldChainFalloff = 0.0;
	double HoldChainFullN = 0.0;
	double HoldChainShare = 0.0;
	double HoldRestFullN = 0.0;
	double HoldSlowFromN = 0.0;
	double HoldSum = 0.0;
	double HoldPressPace = 0.0;
	/**
	 * R2, decision 20 of docs/active-ragdoll.md: while led every hit the body feels takes impulse / CarryYieldNs of the carry's share
	 * away (all of it at CarryYieldNs; hits that follow each other add up) and it comes back over HitBackS (a whole share in that
	 * time); no attempt is given up for the size of a hit: the blow moves the body and the get-up's own rules decide (lost_cm,
	 * stall_s). (getup.hit_by_impulse, which chose giving the attempt up at CarryYieldNs instead, went in the audit of 28 November
	 * 2026.) s.
	 */
	double HitBackS = 0.0;
	/**
	 * R2, decision 22: above 0, a get-up starts from where the body is: the muscles' targets cross from the body's own pose to the
	 * clip's first pose over this time, and the carry's targets from where each body lay to where the clip has it, by the same share.
	 * 0: the clip's first pose taken whole, as before, bit for bit. s.
	 */
	double StartBlendS = 0.0;
	/** 1: what a hit takes beyond the whole share is owed, so the share stays at 0 until HitBackS has given that much back; 0: it stops at 0. */
	double HitOverflow = 0.0;
	/**
	 * The authority rule after an attempt (the audit of 28 November 2026; docs/recon/window3_plan.md, stage 1). 1: (a) a hit that gives
	 * an attempt up to cover the struck place takes the world's pull by its impulse first, as a hit does while led (decision 20); (b) a
	 * hit takes the pull in the same way while it fades after an attempt, whatever the state; and the marks of an attempt given up afraid
	 * or to cover a place are cleared as the next attempt begins. 2: also (c), a body that lies lets what is left go by the get-up's own
	 * release (CarryReleaseS), not by behaviour.lower_release_s. 0: as before, bit for bit (a covering hit gave the attempt up before it
	 * was judged, and the pull after an afraid attempt faded over lower_release_s into Lying, where no hit took it). 0, 1 or 2.
	 */
	double StrictRelease = 0.0;
	/**
	 * R2, step 8 (docs/recon/r2_step8_plan.md; the only choice since R2's rest, the rule by the pelvis's lean, getup.clip_* and the
	 * switch getup.match gone): a get-up's clip, the time it starts at and where it is laid come from the motion library and its
	 * matcher (FSandboxMotionMatcher): the lying body against the first MatchWindowS of every get-up the library lets the matcher
	 * choose, and their mirror images, by the places of its pelvis, trunk, hands, elbows, feet and knees (weights MatchPelvis to
	 * MatchKnees), turned about the vertical and moved over the floor to lie on the body's, and by how the pelvis and the chest lean
	 * (MatchUpCmPerDeg: a degree counted as that many cm); the variant whose attempt was just given up, and its mirror image, cost
	 * MatchGivenUpCm more. The weights also weigh the start's measure (FSandboxGetUpStart::OffCm). s, weights, cm per degree, cm.
	 */
	double MatchWindowS = 0.0;
	double MatchPelvis = 0.0;
	double MatchTrunk = 0.0;
	double MatchHands = 0.0;
	double MatchElbows = 0.0;
	double MatchFeet = 0.0;
	double MatchKnees = 0.0;
	double MatchUpCmPerDeg = 0.0;
	double MatchGivenUpCm = 0.0;
	/**
	 * Decision 51: a second of a get-up that a start in the clip leaves out costs this many cm in the matcher's choice, so that a clip
	 * begun late (and so shorter than C21's 2.6 s) has to lie that much nearer the body. 0: no such price, bit for bit as before. cm/s.
	 */
	double MatchLateCmPerS = 0.0;
	/**
	 * R2, step 9 (the owner's decision 50; docs/recon/r2_rest_plan.md): Roll 1, a body that begins an attempt lying on its side (its
	 * hips' line RollSideDeg or more from level) first rolls onto its back or its front by its own muscles, asked for poses.roll_to_back
	 * or poses.roll_to_front (written for a body on its left side, mirrored for the right), with no pull from the world; the roll is
	 * done once the hips' line is within RollDoneDeg of level, or after RollS whatever it reached, and the matcher then starts the clip
	 * from the body as it lies. RollWay: 0 by where the front looks (down: onto the front), 1 always onto the front, 2 always onto the
	 * back. With Roll 1 the matcher leaves out the get-ups that begin on a side (decision 38: C27 from the back and the front). Needs
	 * Match 1. 0: no roll, bit for bit as before. Degrees, s.
	 */
	double Roll = 0.0;
	double RollSideDeg = 0.0;
	double RollDoneDeg = 0.0;
	double RollS = 0.0;
	double RollWay = 0.0;
	/**
	 * The time over which the roll's pose comes in (eased); from then to RollS it goes on to RollPush times its angles, so that the
	 * target keeps moving through the roll (C19: no pose held still while getting up) and the roll is pressed home. s, a factor.
	 */
	double RollBlendS = 0.0;
	double RollPush = 0.0;
	/**
	 * R2, step 10 (docs/recon/r2_rest_plan.md), each with the matcher (Match 1) and each 0 bit for bit as before:
	 * FitSurface 1: the clip's floor is what the pelvis lies on as the clip begins (a sweep down, read only), not the floor last stood on;
	 * FitFloor, 0 to 1: that share of the motion library's fit of each key (FSandboxMotionVariant::FitCm: the bones the recording has on
	 * its floor laid on it with this body's shapes) raises or lowers the reference, followed over FitEaseS so that it never jumps;
	 * FitFreeZ 1: a body the recording has on its floor that touched the world in the last step gets no vertical spring or damping from
	 * the carry (the floor carries it; decision 30's damping downward goes where it pressed). s.
	 */
	double FitSurface = 0.0;
	double FitFloor = 0.0;
	double FitEaseS = 0.0;
	double FitFreeZ = 0.0;
	/**
	 * R2, step 11 (the owner's decision 18): Drag 1, holds that together pull with DragForceN or more, or that move the pelvis along
	 * their pull at DragMps or faster, for DragS, give a get-up up at once, the world letting go of every body at once, and keep the
	 * next from beginning until the body has not been dragged for DragFreeS. 0: none of it, bit for bit as before. N, s, m/s, s.
	 */
	double Drag = 0.0;
	double DragForceN = 0.0;
	double DragS = 0.0;
	double DragMps = 0.0;
	double DragFreeS = 0.0;
	/**
	 * R2, step 12 (FR-43; the owner's decisions 19 and 48): what an arm held by a hand does while the body gets up by its clip.
	 * ArmAnswer: 0 nothing (bit for bit as before); 1 by the body's state (braced: pulls the arm in; otherwise yields); 2 always yields;
	 * 3 always pulls in. Yielding, the held arm's muscles go to ArmYieldScale of their strength and its hand is placed ArmReachCm beyond
	 * the holding point along the pull (it reaches after the hand instead of being drawn straight); pulling in, they go to ArmPullScale
	 * and the hand is placed ArmPullInCm from its shoulder toward the holding point (the elbow bends, the body draws the arm). cm.
	 */
	double ArmAnswer = 0.0;
	double ArmYieldScale = 0.0;
	double ArmReachCm = 0.0;
	double ArmPullScale = 0.0;
	double ArmPullInCm = 0.0;
	/** The time over which a held arm's placed hand follows its answer's point (no step at the answer's start or change). s. */
	double ArmFollowS = 0.0;
};

/**
 * The motion library (R2, step 8; FSandboxMotionLibrary): the list of recorded clips it reads ("motion.library", a path from the
 * project's folder) and when a body of a clip counts as touching the clip's floor: its lowest point within ContactCm of it, moving
 * slower than ContactMps.
 */
struct FSandboxMotionTuning
{
	FString Library;
	double ContactCm = 0.0;
	double ContactMps = 0.0;
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
	FSandboxWeightTuning Weight;
	FSandboxJointScale Joints[SandboxJointKindCount];
	FSandboxToneTuning Tone;
	FSandboxFallAimTuning FallAim;
	FSandboxPerceptionTuning Perception;
	FSandboxBehaviourTuning Behaviour;
	/** The values of the joint whose child is this bone (its kind with the side taken off); null for a bone of no kind. */
	const FSandboxJointScale* JointScale(FName ChildBone) const;

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
	FSandboxMotionTuning Motion;

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
