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
		// A straight kick. The foot comes up to the height of the point a little way before it, as the knee
		// comes up before a front kick, and is thrust from there level, the way the camera looks, through the
		// point. (In one line from where it rests, a metre under the camera, it struck a belly at 54 degrees
		// upward and a head at 65: a glancing blow from below, which lifts a head and does not throw it back.)
		// Neither place is further than a leg reaches from where the foot rests: aimed at a wall across the
		// room, the foot goes that way for the reach.
		const FVector Home = Rest.GetLocation();
		const FVector Sight = Foot.AskedPoint - Anchor.GetLocation();
		FVector Along = FVector(Sight.X, Sight.Y, 0.0);
		// Looking almost straight down or up there is no level way to look: along the line of sight then.
		Along = Along.Size() > 0.2 * Sight.Size() ? Along.GetSafeNormal() : Sight.GetSafeNormal();
		if (Along.IsNearlyZero())
		{
			Along = (Foot.AskedPoint - Home).GetSafeNormal();
		}
		const double BackCm = FMath::Min(Tools.KickChamberCm, 0.8 * FVector::DotProduct(Sight, Along));
		Foot.Chamber = Home + (Foot.AskedPoint - Along * FMath::Max(BackCm, 0.0) - Home).GetClampedToMaxSize(Tools.KickReachCm);
		Foot.Strike = Home + (Foot.AskedPoint + Along * Tools.KickThroughCm - Home).GetClampedToMaxSize(Tools.KickReachCm);
		Foot.Phase = 0.0;
		Foot.bKicking = true;
		Foot.bAsked = false;
	}
	FVector Location = Rest.GetLocation();
	if (Foot.bKicking)
	{
		// Out and back at the kick's speed, by the same way: up to where the thrust begins, and along it. The
		// way up is from the rest as it is now, so a player who moves during the kick takes it along.
		const double OutCm = FVector::Dist(Rest.GetLocation(), Foot.Chamber);
		const double ThrustCm = FVector::Dist(Foot.Chamber, Foot.Strike);
		const double TravelCm = OutCm + ThrustCm;
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
			const double AtCm = (Foot.Phase <= 1.0 ? Foot.Phase : 2.0 - Foot.Phase) * TravelCm;
			Location = AtCm <= OutCm || ThrustCm <= UE_KINDA_SMALL_NUMBER
				? FMath::Lerp(Rest.GetLocation(), Foot.Chamber, OutCm > UE_KINDA_SMALL_NUMBER ? FMath::Min(AtCm / OutCm, 1.0) : 1.0)
				: FMath::Lerp(Foot.Chamber, Foot.Strike, FMath::Min((AtCm - OutCm) / ThrustCm, 1.0));
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
	// A fist goes out as a punch does; an open hand reaches, slowly enough to be no blow when it arrives.
	const double SpeedMps = bHandsOpen ? Tools.HandOpenReachSpeedMps : Tools.HandReachSpeedMps;
	const double Step = TravelCm > UE_KINDA_SMALL_NUMBER ? SpeedMps * 100.0 * DeltaTime / TravelCm : 1.0;
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
