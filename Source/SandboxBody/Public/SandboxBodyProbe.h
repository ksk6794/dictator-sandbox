#pragma once

#include "CoreMinimal.h"

class USkeletalMeshComponent;
struct FConstraintInstance;

/** State of one simulated body at one instant. Unreal units: cm, cm/s, rad/s. */
struct FSandboxBodySample
{
	FName Bone;
	FTransform WorldTransform = FTransform::Identity;
	/** World position of the body's centre of mass. */
	FVector CenterOfMass = FVector::ZeroVector;
	FVector LinearVelocity = FVector::ZeroVector;
	FVector AngularVelocityRad = FVector::ZeroVector;
	double MassKg = 0.0;
};

/** Read-only measurements of a simulated skeletal mesh. Used by scenarios and debug overlays. */
struct SANDBOXBODY_API FSandboxBodyProbe
{
	/** One sample per valid body, in physics-asset order (stable between runs). */
	static void SampleBodies(const USkeletalMeshComponent* Mesh, TArray<FSandboxBodySample>& OutSamples);

	/**
	 * Largest distance (cm) between the two anchor frames of any joint whose linear axes are all locked.
	 * A perfectly solved joint reads 0; growth means the solver is letting the skeleton pull apart.
	 * Constraints with free or limited linear motion are skipped.
	 */
	static double MaxJointStretchCm(const USkeletalMeshComponent* Mesh, FName* OutWorstJoint = nullptr);

	/** The same separation for one joint; 0 if either body is missing. */
	static double JointStretchCm(const USkeletalMeshComponent* Mesh, const FConstraintInstance& Constraint);

	/**
	 * Fastest relative rotation (rad/s) between the two bodies of any rigid joint: how hard the fastest
	 * joint is being whipped around. Free and limited-linear constraints are skipped, as for stretch.
	 */
	static double MaxJointAngularSpeedRadS(const USkeletalMeshComponent* Mesh, FName* OutFastestJoint = nullptr);

	/** Mass-weighted centre of all bodies, world space, cm. */
	static FVector CenterOfMass(const TArray<FSandboxBodySample>& Samples);

	/** Translational kinetic energy in joules (rotational energy is not included). */
	static double TranslationalKineticEnergyJ(const TArray<FSandboxBodySample>& Samples);

	static double TotalMassKg(const TArray<FSandboxBodySample>& Samples);
};
