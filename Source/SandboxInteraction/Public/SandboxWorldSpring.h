#pragma once

#include "CoreMinimal.h"

struct FBodyInstance;
struct FConstraintInstance;

/**
 * A spring with a force cap between one rigid body and a point that moves through the world: what holds an
 * effector to its target, and what the drag handle pulls a clicked body with.
 *
 * It is a joint to the world with every axis free and a position drive, so the solver integrates the spring
 * together with the contacts, implicitly; forces computed here and added from outside would be a step late
 * and unstable at this stiffness. The spring is in physical units and in force mode: the same spring moves
 * a light body quickly and a heavy one slowly.
 *
 * The force cap. The engine caps a drive axis by axis, which alone would make the spring 1.7 times stronger
 * along a diagonal than along an axis. SetTarget therefore sets the three caps every frame, in proportion
 * to the direction of the force the spring asks for at that moment, so the size of the force stays within
 * the cap in every direction.
 */
class SANDBOXINTERACTION_API FSandboxWorldSpring
{
public:
	FSandboxWorldSpring();
	~FSandboxWorldSpring();
	FSandboxWorldSpring(const FSandboxWorldSpring&) = delete;
	FSandboxWorldSpring& operator=(const FSandboxWorldSpring&) = delete;

	/**
	 * Ties the spring to a body. GripFrame is the point (and orientation) of the body the spring holds, in
	 * the body's own space. With bAngular the spring also turns the body toward the target's orientation.
	 * The body must outlive the spring, or Detach must be called first.
	 */
	void Attach(FBodyInstance* InBody, const FTransform& GripFrame, UObject* DebugOwner, bool bAngular);
	void Detach();
	bool IsAttached() const { return Body != nullptr; }

	/** Spring in N/m, damping as a time in seconds (damping = stiffness x time), cap in N. */
	void SetLinear(double StiffnessNPerM, double DampingS, double MaxForceN);
	/** Spring in N*m/rad, damping as a time, cap in N*m per axis. */
	void SetAngular(double StiffnessNmPerRad, double DampingS, double MaxTorqueNm);

	/** Once per frame, before the physics step. Location in cm, velocity of the target in cm/s. */
	void SetTarget(const FVector& Location, const FQuat& Rotation, const FVector& VelocityCmS);

	/**
	 * The cap of each world axis for a spring that asks for the force Demand (N) and may apply MaxForceN in
	 * all: in proportion to the demand's direction, so that the box of the three caps reaches the sphere of
	 * the cap where the force points. Every axis keeps a small share, so the size of the force can exceed
	 * MaxForceN by a quarter of a percent.
	 */
	static FVector AxisCapsN(const FVector& DemandN, double MaxForceN);

	/** World position of the held point of the body, now. */
	FVector GetGripLocation() const;
	/** Distance from the held point to the target given last, cm. */
	double GetGapCm() const;
	/** The target given last, world space. */
	const FVector& GetTargetLocation() const { return TargetLocation; }
	/** The force the spring law asked for at the last SetTarget, before the cap, N. */
	double GetDemandN() const { return DemandN; }
	double GetMaxForceN() const { return MaxForceN; }

private:
	TUniquePtr<FConstraintInstance> Constraint;
	FBodyInstance* Body = nullptr;
	FTransform Grip = FTransform::Identity;
	bool bAngularDrive = false;

	double StiffnessNPerM = 0.0;
	double DampingS = 0.0;
	double MaxForceN = 0.0;
	FVector TargetLocation = FVector::ZeroVector;
	double DemandN = 0.0;
};
