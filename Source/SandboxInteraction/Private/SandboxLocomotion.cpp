#include "SandboxLocomotion.h"

namespace
{
	// A head that looks this close to straight down or up has no level forward of its own worth following: the top of the
	// head stands in for it (a sanity bound on a direction, not a tuning value).
	constexpr double MinLevelForward = 0.2;
}

FVector FSandboxLocomotion::LevelForward(const FQuat& HeadWorldRotation)
{
	const FVector Forward = HeadWorldRotation.GetForwardVector();
	FVector Level(Forward.X, Forward.Y, 0.0);
	if (Level.Size() < MinLevelForward)
	{
		// Looking down, the top of the head points the way the face is turned; looking up, the other way.
		const FVector Up = HeadWorldRotation.GetUpVector();
		Level = Forward.Z < 0.0 ? FVector(Up.X, Up.Y, 0.0) : FVector(-Up.X, -Up.Y, 0.0);
	}
	return Level.GetSafeNormal(UE_SMALL_NUMBER, FVector::ForwardVector);
}

FVector2D FSandboxLocomotion::PastDeadZone(const FVector2D& Stick, double DeadZone)
{
	const double Tilt = Stick.Size();
	if (Tilt <= DeadZone || Tilt <= UE_SMALL_NUMBER || DeadZone >= 1.0)
	{
		return FVector2D::ZeroVector;
	}
	const double Share = FMath::Min((Tilt - DeadZone) / (1.0 - DeadZone), 1.0);
	return Stick / Tilt * Share;
}

FTransform FSandboxLocomotion::PlaceHead(const FVector& HeadLocation, const FQuat& HeadRotation, const FVector& StandPoint, double StandYawDeg)
{
	// The play space's yaw turns the head's own level heading to the one wanted; its place puts the head over the point.
	const FVector Heading = LevelForward(HeadRotation);
	const double HeadYawDeg = FMath::RadiansToDegrees(FMath::Atan2(Heading.Y, Heading.X));
	const FQuat Turn(FVector::UpVector, FMath::DegreesToRadians(StandYawDeg - HeadYawDeg));
	return FTransform(Turn, StandPoint - Turn.RotateVector(FVector(HeadLocation.X, HeadLocation.Y, 0.0)));
}

void FSandboxLocomotion::Reset(const FTransform& InOrigin, const FVector& HeadLocation)
{
	Origin = FTransform(InOrigin.GetRotation(), InOrigin.GetLocation());
	Velocity = FVector::ZeroVector;
	const FVector Head = Origin.TransformPositionNoScale(HeadLocation);
	BodyTarget = FVector(Head.X, Head.Y, 0.0);
	BodyTargetVelocity = FVector::ZeroVector;
	bSnapArmed = true;
	bSnapped = false;
	HeldBack = 0.0;
	Stats = FSandboxLocomotionStats();
}

void FSandboxLocomotion::Update(const FSandboxLocomotionInput& Input, const FSandboxToolTuning& Tools, float DeltaTime)
{
	bSnapped = false;
	HeldBack = 0.0;
	const double Dt = FMath::Max(static_cast<double>(DeltaTime), 0.0);

	// A snap turn: the play space turns about the head, which stays where it is in the world. One turn a tilt: the next
	// waits until the stick has come back toward the middle.
	const double Turn = Input.TurnStick.X;
	if (FMath::Abs(Turn) <= Tools.HeadsetSnapRelease)
	{
		bSnapArmed = true;
	}
	else if (bSnapArmed && FMath::Abs(Turn) >= Tools.HeadsetSnapPress && Tools.HeadsetSnapTurnDeg > 0.0)
	{
		bSnapArmed = false;
		const bool bRight = Turn > 0.0;
		// A positive yaw turns x toward y, which is to the right.
		const FQuat Snap(FVector::UpVector, FMath::DegreesToRadians(bRight ? Tools.HeadsetSnapTurnDeg : -Tools.HeadsetSnapTurnDeg));
		const FVector Head = Origin.TransformPositionNoScale(Input.HeadLocation);
		Origin = FTransform((Snap * Origin.GetRotation()).GetNormalized(), Head + Snap.RotateVector(Origin.GetLocation() - Head));
		// A motion under way goes on the way the player now faces.
		Velocity = Snap.RotateVector(Velocity);
		bSnapped = true;
		++(bRight ? Stats.SnapsRight : Stats.SnapsLeft);
	}

	// The right stick: forward is where the head looks, level; right is to its right.
	const FVector Forward = LevelForward(Origin.GetRotation() * Input.HeadRotation);
	const FVector Right(-Forward.Y, Forward.X, 0.0);
	const FVector2D Tilt = PastDeadZone(Input.MoveStick, Tools.HeadsetMoveDeadZone);
	const double FullCmS = Tools.HeadsetMoveSpeedMps * 100.0;
	const FVector Wanted = (Forward * Tilt.Y + Right * Tilt.X) * FullCmS;
	// Eased: the velocity changes by no more than the full speed in move_ease_s.
	FVector Change = Wanted - Velocity;
	const double MaxChange = Tools.HeadsetMoveEaseS > 0.0 ? FullCmS / Tools.HeadsetMoveEaseS * Dt : TNumericLimits<double>::Max();
	if (Change.Size() > MaxChange)
	{
		Change *= MaxChange / Change.Size();
	}
	Velocity += Change;
	Velocity.Z = 0.0;

	// Something presses the body back (the NPC, a table): the part of the motion that goes against that push fades with
	// it. The velocity itself is what is cut, so that the motion comes back eased once the push is gone. A body that is
	// pressed back while the player does not walk into the push rests against what it touches instead of leaning on it.
	const FVector Push(Input.BodyContactN.X, Input.BodyContactN.Y, 0.0);
	const double PushN = Push.Size();
	if (Input.bHasBody && PushN > Tools.HeadsetBodyBlockedN && PushN > UE_SMALL_NUMBER)
	{
		const FVector Away = Push / PushN;
		const double Into = -FVector::DotProduct(Velocity, Away);
		if (Into > 0.0)
		{
			HeldBack = FMath::Clamp((PushN - Tools.HeadsetBodyBlockedN) / (Tools.HeadsetBodyStoppedN - Tools.HeadsetBodyBlockedN), 0.0, 1.0);
			Velocity += Away * Into * HeldBack;
			++Stats.HeldBackFrames;
			Stats.MostHeldBack = FMath::Max(Stats.MostHeldBack, HeldBack);
		}
		else
		{
			const FVector Lead(BodyTarget.X - Input.BodyLocation.X, BodyTarget.Y - Input.BodyLocation.Y, 0.0);
			const double LeadInto = -FVector::DotProduct(Lead, Away);
			if (LeadInto > 0.0)
			{
				BodyTarget += Away * LeadInto;
			}
		}
	}

	// The stick carries the play space, and the body's target with it.
	const FVector Step = Velocity * Dt;
	Origin.AddToTranslation(Step);
	BodyTarget += Step;
	if (!Step.IsNearlyZero())
	{
		Stats.WalkedCm += Step.Size();
		++Stats.MovingFrames;
	}
	Stats.MaxSpeedCmS = FMath::Max(Stats.MaxSpeedCmS, Velocity.Size());

	// The head draws the body after itself once it is further from it than a lean, and no faster than a walk: a head
	// whose tracking jumps does not throw the body.
	const FVector Head = Origin.TransformPositionNoScale(Input.HeadLocation);
	const FVector ToHead(Head.X - BodyTarget.X, Head.Y - BodyTarget.Y, 0.0);
	const double FarCm = ToHead.Size();
	FVector Pull = FVector::ZeroVector;
	if (FarCm > Tools.HeadsetBodyLeanCm && FarCm > UE_SMALL_NUMBER)
	{
		Pull = ToHead / FarCm * FMath::Min(FarCm - Tools.HeadsetBodyLeanCm, Tools.HeadsetBodyFollowSpeedMps * 100.0 * Dt);
		BodyTarget += Pull;
	}
	BodyTargetVelocity = Velocity + (Dt > 0.0 ? Pull / Dt : FVector::ZeroVector);
	if (Input.bHasBody)
	{
		Stats.MaxBodyGapCm = FMath::Max(Stats.MaxBodyGapCm, FVector::Dist2D(BodyTarget, Input.BodyLocation));
	}
}

FSandboxEffectorTarget FSandboxLocomotion::GetBodyTarget(const FSandboxEffectorTuning& Body, const FSandboxToolTuning& Tools) const
{
	// Upright, its bottom a little above the floor the play space stands on.
	FSandboxEffectorTarget Target;
	Target.Location = FVector(BodyTarget.X, BodyTarget.Y, Origin.GetLocation().Z + Body.HalfHeightCm + Tools.HeadsetBodyFloorGapCm);
	Target.Rotation = FQuat::Identity;
	Target.Velocity = BodyTargetVelocity;
	return Target;
}

double FSandboxLocomotion::GetSpeedShare(const FSandboxToolTuning& Tools) const
{
	const double FullCmS = Tools.HeadsetMoveSpeedMps * 100.0;
	return FullCmS > 0.0 ? FMath::Clamp(Velocity.Size() / FullCmS, 0.0, 1.0) : 0.0;
}
