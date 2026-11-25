#pragma once

#include "CoreMinimal.h"

class UPrimitiveComponent;
struct FBodyInstance;
struct FConstraintInstance;

/**
 * A grab: a joint that keeps one point of a hand's body and one point of another simulated body together,
 * with both free to turn about it (a hand without fingers holds a place, not a direction). It holds whatever
 * body it is given, a limb of the character or a prop, and knows nothing more about it.
 *
 * The joint itself has no force cap. What makes a held body heavy is the hand's own spring: the hand follows
 * its target with a capped force, and whatever it holds follows the hand.
 *
 * A thing can also be held in its direction (HoldNmPerRad above 0): a spring with a torque cap then turns it
 * toward the way it lay in the hand when it was taken, so that a stick follows the hand's turning and can be
 * struck with. A limb of the character is held at a point only.
 *
 * With ThingPoint the thing is held at that point of the world (its handle) and not where the hand is: the
 * joint then draws the two points together, and the thing comes to the hand.
 */
class SANDBOXINTERACTION_API FSandboxGrabJoint
{
public:
	FSandboxGrabJoint();
	~FSandboxGrabJoint();
	FSandboxGrabJoint(const FSandboxGrabJoint&) = delete;
	FSandboxGrabJoint& operator=(const FSandboxGrabJoint&) = delete;

	/**
	 * Ties the hand's body to the body of a component (of the bone, for a skeletal mesh) at a world point.
	 * False if there is no simulated body there.
	 */
	bool Attach(FBodyInstance* InHand, const FVector& WorldPoint, UPrimitiveComponent* InComponent, FName InBone, UObject* DebugOwner,
		double HoldNmPerRad = 0.0, double HoldDampingS = 0.0, double HoldMaxTorqueNm = 0.0, const FVector* ThingPoint = nullptr);
	void Detach();

	/** True from Attach to Detach, whether or not the held body still exists. */
	bool IsAttached() const { return Constraint.IsValid(); }
	/** False once released, and once the held body is gone. */
	bool IsHolding() const;

	UPrimitiveComponent* GetComponent() const { return Component.Get(); }
	FName GetBone() const { return Bone; }
	/** The hand's body the joint is on. */
	const FBodyInstance* GetHand() const { return Hand; }
	/** How far apart the two held points are now, cm: what the joint has let through. */
	double GetStretchCm() const;
	/**
	 * The force the joint held the two bodies together with in the last physics substep, N. SubstepS is that
	 * substep's length: what the engine reports as a joint's force is its impulse over one substep (measured:
	 * a hand that drags a body at a steady 231 N of its spring reads 124 kg*cm/s at 180 Hz, 223 N).
	 */
	double GetForceN(double SubstepS) const;

private:
	TUniquePtr<FConstraintInstance> Constraint;
	FBodyInstance* Hand = nullptr;
	TWeakObjectPtr<UPrimitiveComponent> Component;
	FName Bone;
	/** The held point in each body's own frame. */
	FVector HandPoint = FVector::ZeroVector;
	FVector OtherPoint = FVector::ZeroVector;
};
