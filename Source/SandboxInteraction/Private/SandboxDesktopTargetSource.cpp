#include "SandboxDesktopTargetSource.h"

namespace
{
	/** A resting hand in the anchor's space: x forward, y to the right, z up. */
	FVector RestOffset(bool bLeft, const FSandboxToolTuning& Tools)
	{
		return FVector(Tools.HandRestForwardCm, bLeft ? -Tools.HandRestSideCm : Tools.HandRestSideCm, -Tools.HandRestDownCm);
	}
}

FTransform FSandboxDesktopTargetSource::RestPose(const FTransform& Anchor, bool bLeft, const FSandboxToolTuning& Tools)
{
	return FTransform(Anchor.GetRotation(), Anchor.TransformPositionNoScale(RestOffset(bLeft, Tools)));
}

void FSandboxDesktopTargetSource::Update(const FTransform& Anchor, bool bReachLeft, bool bReachRight, const FSandboxToolTuning& Tools, double ReachCm, float DeltaTime, bool bContinuous)
{
	UpdateHand(Left, /*bLeft*/ true, bReachLeft, Anchor, Tools, ReachCm, DeltaTime, bContinuous);
	UpdateHand(Right, /*bLeft*/ false, bReachRight, Anchor, Tools, ReachCm, DeltaTime, bContinuous);
}

void FSandboxDesktopTargetSource::UpdateHand(FHand& Hand, bool bLeft, bool bReach, const FTransform& Anchor, const FSandboxToolTuning& Tools, double ReachCm, float DeltaTime, bool bContinuous)
{
	const FVector Rest = RestOffset(bLeft, Tools);
	const FVector Ahead(ReachCm, 0.0, 0.0);

	// The target travels between the two points at the reach speed, whichever way it is going.
	const double TravelCm = FVector::Dist(Rest, Ahead);
	const double Step = TravelCm > UE_KINDA_SMALL_NUMBER ? Tools.HandReachSpeedMps * 100.0 * DeltaTime / TravelCm : 1.0;
	Hand.Reach = FMath::Clamp(Hand.Reach + (bReach ? Step : -Step), 0.0, 1.0);

	const FVector Location = Anchor.TransformPositionNoScale(FMath::Lerp(Rest, Ahead, Hand.Reach));
	// The target's own velocity, which includes the camera's: a hand should keep up with a moving player.
	Hand.Target.Velocity = bContinuous && Hand.bHasTarget && DeltaTime > 0.0f ? (Location - Hand.Target.Location) / DeltaTime : FVector::ZeroVector;
	Hand.Target.Location = Location;
	Hand.Target.Rotation = Anchor.GetRotation();
	Hand.bHasTarget = true;
}

bool FSandboxDesktopTargetSource::GetTarget(FName Effector, FSandboxEffectorTarget& OutTarget) const
{
	const FHand* Hand = Effector == SandboxEffectors::HandLeft ? &Left : Effector == SandboxEffectors::HandRight ? &Right : nullptr;
	if (!Hand || !Hand->bHasTarget)
	{
		return false;
	}
	OutTarget = Hand->Target;
	return true;
}
