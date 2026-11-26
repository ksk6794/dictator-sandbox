#pragma once

#include "CoreMinimal.h"
#include "SandboxTrackedTargetSource.h"

/**
 * A headset that is not there: the head and the two controllers where a session without a person puts them
 * (USandboxPlaySubsystem, Tools\play.ps1 -Demo -Walk). The pawn takes it for a headset's runtime
 * (ASandboxPawn::UseStandInHeadset) and does all it does in a headset with it, so that the way a person in the
 * headset takes is run once without one. Poses are in tracking space, as a runtime gives them.
 */
class FSandboxStandInHeadset : public ISandboxControllerTracker
{
public:
	FVector HeadLocation = FVector(0.0, 0.0, 165.0);
	FQuat HeadRotation = FQuat::Identity;
	FSandboxControllerState Left;
	FSandboxControllerState Right;

	/**
	 * Both controllers held still beside the hips and a little behind the head, in the head's own heading, tracked
	 * and at rest, with closed grips (fists that never take hold: a grip that never closes takes nothing).
	 */
	void HoldHandsByTheHips()
	{
		const FQuat Heading(FVector::UpVector, FMath::Atan2(HeadRotation.GetForwardVector().Y, HeadRotation.GetForwardVector().X));
		const auto Hold = [&](FSandboxControllerState& Hand, double SideCm)
		{
			Hand.bTracked = true;
			Hand.bEstimated = false;
			Hand.Location = HeadLocation + Heading.RotateVector(FVector(-15.0, SideCm, -75.0));
			Hand.Rotation = Heading;
			Hand.bHasLinearVelocity = true;
			Hand.LinearVelocity = FVector::ZeroVector;
			Hand.bHasAngularVelocity = true;
			Hand.AngularVelocity = FVector::ZeroVector;
			Hand.Squeeze = 1.0f;
		};
		Hold(Left, -30.0);
		Hold(Right, 30.0);
	}

	virtual bool GetController(FName Effector, FSandboxControllerState& OutState) const override
	{
		if (Effector == SandboxEffectors::HandLeft || Effector == SandboxEffectors::HandRight)
		{
			OutState = Effector == SandboxEffectors::HandLeft ? Left : Right;
			return true;
		}
		return false;
	}

	virtual bool GetHead(FVector& OutLocation, FQuat& OutRotation) const override
	{
		OutLocation = HeadLocation;
		OutRotation = HeadRotation;
		return true;
	}
};
