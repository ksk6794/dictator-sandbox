#include "SandboxDesktopTargetSource.h"

namespace
{
	/** A resting hand in the anchor's space: x forward, y to the right, z up. */
	FVector RestOffset(bool bLeft, const FSandboxToolTuning& Tools)
	{
		return FVector(Tools.HandRestForwardCm, bLeft ? -Tools.HandRestSideCm : Tools.HandRestSideCm, -Tools.HandRestDownCm);
	}
}

FTransform FSandboxDesktopTargetSource::FootRestPose(const FTransform& Anchor, const FSandboxToolTuning& Tools)
{
	// Level, whatever way the camera looks: a foot is under the player, not under the line of sight.
	const FQuat Level(FRotator(0.0, Anchor.GetRotation().Rotator().Yaw, 0.0));
	return FTransform(Level, Anchor.GetLocation() + Level.RotateVector(FVector(Tools.KickRestForwardCm, Tools.KickRestRightCm, -Tools.KickRestDownCm)));
}

void FSandboxDesktopTargetSource::Kick(const FVector& WorldPoint)
{
	if (!Foot.bKicking && !Foot.bAsked)
	{
		Foot.bAsked = true;
		Foot.AskedPoint = WorldPoint;
	}
}

void FSandboxDesktopTargetSource::UpdateFoot(const FTransform& Anchor, const FSandboxToolTuning& Tools, float DeltaTime, bool bContinuous)
{
	const FTransform Rest = FootRestPose(Anchor, Tools);
	if (Foot.bAsked)
	{
		// To the point and a little way through it, along the line from where the foot rests now, and no
		// further than a leg reaches: aimed at a wall across the room, the foot goes that way for the reach.
		const FVector Toward = (Foot.AskedPoint - Rest.GetLocation()).GetSafeNormal();
		const double FarCm = FMath::Min(FVector::Dist(Rest.GetLocation(), Foot.AskedPoint) + Tools.KickThroughCm, Tools.KickReachCm);
		Foot.Strike = Rest.GetLocation() + Toward * FarCm;
		Foot.Phase = 0.0;
		Foot.bKicking = true;
		Foot.bAsked = false;
	}
	FVector Location = Rest.GetLocation();
	if (Foot.bKicking)
	{
		// Out and back at the kick's speed. The line is from the rest as it is now, so a player who moves
		// during the kick takes it along.
		const double TravelCm = FVector::Dist(Rest.GetLocation(), Foot.Strike);
		Foot.Phase += TravelCm > UE_KINDA_SMALL_NUMBER ? Tools.KickSpeedMps * 100.0 * DeltaTime / TravelCm : 2.0;
		if (Foot.Phase >= 2.0)
		{
			Foot.bKicking = false;
			Foot.Phase = 0.0;
			// The target is home; the foot is still on its way, at the kick's speed, and needs its strength to stop there.
			Foot.SettleLeftS = Tools.KickSettleS;
		}
		else
		{
			Location = FMath::Lerp(Rest.GetLocation(), Foot.Strike, Foot.Phase <= 1.0 ? Foot.Phase : 2.0 - Foot.Phase);
		}
	}
	Foot.Target.Velocity = bContinuous && Foot.bHasTarget && DeltaTime > 0.0f ? (Location - Foot.Target.Location) / DeltaTime : FVector::ZeroVector;
	Foot.Target.Location = Location;
	Foot.Target.Rotation = Rest.GetRotation();
	Foot.Target.Grip = 1.0f;
	// A leg's strength while it kicks; at rest the foot is a body out of sight under the camera, held weakly.
	// A rest that has moved further in one step than a kick goes has jumped (the first frame, a view that was
	// set): the foot goes to its new rest with its whole strength, as the hands go to theirs. Held weakly it
	// would drift there through whatever stands between. (Not every frame after a pause: in slow motion that
	// is every frame, and the foot would never be weak.)
	if (!Foot.bHasTarget || FVector::Dist(Rest.GetLocation(), Foot.LastRest) > Tools.KickSpeedMps * 100.0 * DeltaTime)
	{
		Foot.SettleLeftS = FMath::Max(Foot.SettleLeftS, Tools.KickSettleS);
	}
	Foot.LastRest = Rest.GetLocation();
	const bool bStrong = Foot.bKicking || Foot.SettleLeftS > 0.0;
	if (!Foot.bKicking)
	{
		Foot.SettleLeftS = FMath::Max(Foot.SettleLeftS - DeltaTime, 0.0);
	}
	Foot.Target.Strength = bStrong ? 1.0f : static_cast<float>(FootRestStrength);
	Foot.bHasTarget = true;
}

FTransform FSandboxDesktopTargetSource::RestPose(const FTransform& Anchor, bool bLeft, const FSandboxToolTuning& Tools)
{
	return FTransform(Anchor.GetRotation(), Anchor.TransformPositionNoScale(RestOffset(bLeft, Tools)));
}

void FSandboxDesktopTargetSource::Update(const FTransform& Anchor, bool bReachLeft, bool bReachRight, const FSandboxToolTuning& Tools, double ReachCm, float DeltaTime, bool bContinuous)
{
	UpdateHand(Left, /*bLeft*/ true, bReachLeft, Anchor, Tools, ReachCm, DeltaTime, bContinuous);
	UpdateHand(Right, /*bLeft*/ false, bReachRight, Anchor, Tools, ReachCm, DeltaTime, bContinuous);
	UpdateFoot(Anchor, Tools, DeltaTime, bContinuous);
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
	// A desktop hand is a fist, and goes straight ahead as a punch does, unless the player holds the hands open.
	Hand.Target.Grip = bHandsOpen ? 0.0f : 1.0f;
	Hand.bHasTarget = true;
}

bool FSandboxDesktopTargetSource::GetTarget(FName Effector, FSandboxEffectorTarget& OutTarget) const
{
	if (Effector == SandboxEffectors::FootRight)
	{
		if (Foot.bHasTarget)
		{
			OutTarget = Foot.Target;
		}
		return Foot.bHasTarget;
	}
	const FHand* Hand = Effector == SandboxEffectors::HandLeft ? &Left : Effector == SandboxEffectors::HandRight ? &Right : nullptr;
	if (!Hand || !Hand->bHasTarget)
	{
		return false;
	}
	OutTarget = Hand->Target;
	return true;
}
