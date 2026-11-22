#pragma once

#include "CoreMinimal.h"
#include "SandboxInteractionTuning.h"
#include "SandboxWorldSpring.h"

class UPrimitiveComponent;

/**
 * The drag handle: a spring with a force cap from a point the player moves to the point of a body that was
 * clicked. It holds whatever simulated body it is given, a limb of the character or a prop, and knows
 * nothing more about it: the body is pulled through the physics world like by any other force.
 */
class SANDBOXINTERACTION_API FSandboxDragHandle
{
public:
	/**
	 * Takes hold of the body of a component (of the bone, for a skeletal mesh) at a world point. False if
	 * there is no simulated body there.
	 */
	bool Grab(UPrimitiveComponent* InComponent, FName InBone, const FVector& WorldPoint, const FSandboxSpringTuning& Tuning, UObject* DebugOwner);
	void Release();

	/** False once released, and once the held component is gone. */
	bool IsHolding() const { return Component.IsValid() && Spring.IsAttached(); }

	void SetTuning(const FSandboxSpringTuning& Tuning);

	/** Once per frame, before the physics step: where the held point should be, and how fast that place moves. */
	void SetTarget(const FVector& WorldPoint, const FVector& VelocityCmS);

	UPrimitiveComponent* GetComponent() const { return Component.Get(); }
	FName GetBone() const { return Bone; }
	/** Where the held point of the body is now, and how far that is from where it is pulled to. */
	FVector GetGripLocation() const { return Spring.GetGripLocation(); }
	double GetGapCm() const { return Spring.GetGapCm(); }
	/** The force the spring asked for at the last SetTarget, before the cap, N. */
	double GetDemandN() const { return Spring.GetDemandN(); }

private:
	FSandboxWorldSpring Spring;
	TWeakObjectPtr<UPrimitiveComponent> Component;
	FName Bone;
};
