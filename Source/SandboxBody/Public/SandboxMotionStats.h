#pragma once

#include "CoreMinimal.h"

/**
 * Arithmetic on recorded motion: what the measures of naturalness are worked out with. No world and no engine
 * objects, so every function is held by a unit test with a known answer.
 */
struct SANDBOXBODY_API FSandboxMotionStats
{
	static double Mean(TConstArrayView<double> Values);
	/** Root mean square of the values' distance from their mean. */
	static double RmsDeviation(TConstArrayView<double> Values);
	/** The value that the given share (0 to 1) of the values do not exceed. 0 without values. */
	static double Percentile(TArray<double> Values, double Share);

	/** Mean of each point and its neighbours, Half of them to either side (fewer at the ends). */
	static TArray<FVector2D> MovingMean(TConstArrayView<FVector2D> Points, int32 Half);
	/** Length of the path through the points over the time it takes: units per second. */
	static double PathSpeed(TConstArrayView<FVector2D> Points, double StepS);

	/**
	 * The period of a motion in the plane: the lag, of at least MinPeriodS and at most half the record, of the
	 * highest local maximum of its autocorrelation, if that is above MinCorrelation. 0 if it has none: a motion
	 * that does not come round, or a slow drift (which is correlated with itself and has no maximum).
	 */
	static double Period(TConstArrayView<FVector2D> Points, double StepS, double MinPeriodS, double MinCorrelation, double* OutCorrelation = nullptr);

	/**
	 * The torque of a joint drive on its child body, N*m, world space, as the engine's joint solver works a SLERP
	 * drive out (FPBDJointCachedSolver): for each axis of the child body its share of the turn from the target,
	 * times the stiffness, less the damping times the relative turning speed, and each axis capped by itself. The
	 * solver integrates the drive implicitly, so this is an estimate: with the state after a step and the target
	 * of that step it is the one nearest to what the solver applied. Target is the child's orientation relative to
	 * the parent that the drive holds; the speeds are rad/s, world space. bOutAtCap says whether an axis asked for
	 * CapShare of the cap or more. A cap of 0 is no cap.
	 */
	static FVector DriveTorqueNm(const FQuat& Parent, const FQuat& Child, const FQuat& Target, const FVector& ParentSpeed, const FVector& ChildSpeed,
		const FVector& TargetSpeed, double StiffnessNmPerRad, double DampingNmsPerRad, double CapNm, bool& bOutAtCap);

	/** Share of the cap from which a drive counts as being at it. */
	static constexpr double CapShare = 0.98;

	/** Turning speed that takes one orientation to another in StepS by the shorter way, rad/s, in the space the orientations are given in. */
	static FVector TurnSpeed(const FQuat& From, const FQuat& To, double StepS);
};
