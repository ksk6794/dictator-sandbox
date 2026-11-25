#include "SandboxTrackedTargetSource.h"

namespace
{
	// What an untracked pose looked like in the first session in the headset: it moved while its velocity read
	// zero. Only counted (FSandboxTrackedHandStats::MovedWithoutVelocityFrames).
	constexpr double MovedCmS = 20.0;
	constexpr double StillCmS = 3.0;
}

FVector FSandboxTrackedTargetSource::BodyOffset(const FSandboxEffectorTuning& Hand, bool bLeft)
{
	// The grip pose's y runs toward where the left palm faces and away from where the right one does, and its
	// z toward the wrist (see FSandboxControllerState).
	return FVector(Hand.OffsetThumbCm, bLeft ? Hand.OffsetPalmCm : -Hand.OffsetPalmCm, -Hand.OffsetKnucklesCm);
}

void FSandboxTrackedTargetSource::Update(const ISandboxControllerTracker& Tracker, const FTransform& Origin, const FSandboxEffectorTuning& Hand, const FSandboxToolTuning& Tools, float DeltaTime, bool bContinuous)
{
	UpdateHand(Left, SandboxEffectors::HandLeft, /*bLeft*/ true, Tracker, Origin, Hand, Tools, DeltaTime, bContinuous);
	UpdateHand(Right, SandboxEffectors::HandRight, /*bLeft*/ false, Tracker, Origin, Hand, Tools, DeltaTime, bContinuous);
}

void FSandboxTrackedTargetSource::SetWaitingPose(FName Effector, const FTransform& Pose)
{
	if (FHand* Hand = Find(Effector))
	{
		Hand->Target = FSandboxEffectorTarget();
		Hand->Target.Location = Pose.GetLocation();
		Hand->Target.Rotation = Pose.GetRotation();
		Hand->bHasTarget = true;
	}
}

void FSandboxTrackedTargetSource::UpdateHand(FHand& Hand, FName Effector, bool bLeft, const ISandboxControllerTracker& Tracker, const FTransform& Origin, const FSandboxEffectorTuning& Tuning, const FSandboxToolTuning& Tools, float DeltaTime, bool bContinuous)
{
	FSandboxControllerState State;
	const bool bAnswered = Tracker.GetController(Effector, State);
	// A pose the runtime does not track is its guess. For a moment after the controller was tracked the guess
	// is good (a hand that leaves the cameras' sight in the middle of a swing), and it is followed; a
	// controller that stays out of sight wanders, and is lost.
	if (bAnswered && !State.bTracked && State.bEstimated && Hand.Last.bTracked && bContinuous && Hand.EstimatedS + DeltaTime <= Tools.HeadsetEstimatedFollowS)
	{
		Hand.EstimatedS += DeltaTime;
		State.bTracked = true;
		++Hand.Stats.EstimatedFollowedFrames;
	}
	else if (bAnswered && State.bTracked)
	{
		Hand.EstimatedS = 0.0;
	}
	if (!bAnswered || !State.bTracked)
	{
		// Lost: the hand stays where it was last seen, and its target no longer moves. Left with the velocity
		// of the last frame, the spring's damper would go on pushing the hand along it. A pose the runtime
		// gives without tracking the controller is its guess, and is not followed.
		if (Hand.Stats.TrackedFrames > 0)
		{
			Hand.Target.Velocity = FVector::ZeroVector;
			++Hand.Stats.LostFrames;
			Hand.Stats.EstimatedFrames += State.bEstimated ? 1 : 0;
		}
		Hand.bCatchingUp = false;
		Hand.Last = FSandboxControllerState();
		return;
	}
	State.Rotation.Normalize();

	// A velocity the runtime does not give is the difference of two poses, when there are two a step apart.
	// Not across a jump of the tracking, though: that difference is no velocity of anything.
	const bool bHasLast = bContinuous && Hand.Last.bTracked && DeltaTime > 0.0f &&
		FVector::Dist(State.Location, Hand.Last.Location) <= Tools.HeadsetMaxHandSpeedMps * 100.0 * DeltaTime;
	FVector Linear = State.LinearVelocity;
	if (!State.bHasLinearVelocity)
	{
		Linear = bHasLast ? (State.Location - Hand.Last.Location) / DeltaTime : FVector::ZeroVector;
		++Hand.Stats.NoLinearVelocityFrames;
	}
	else if (bHasLast && FVector::Dist(State.Location, Hand.Last.Location) > MovedCmS * DeltaTime && State.LinearVelocity.Size() < StillCmS)
	{
		++Hand.Stats.MovedWithoutVelocityFrames;
	}
	if (State.bHasLinearVelocity && bHasLast && Tools.HeadsetRuntimeVelocityShare < 1.0)
	{
		// The runtime's velocity trails the motion of its own poses; the difference of the poses does not.
		Linear = FMath::Lerp((State.Location - Hand.Last.Location) / DeltaTime, State.LinearVelocity, Tools.HeadsetRuntimeVelocityShare);
	}
	FVector Angular = State.AngularVelocity;
	if (!State.bHasAngularVelocity)
	{
		Angular = FVector::ZeroVector;
		if (bHasLast)
		{
			FQuat Turn = State.Rotation * Hand.Last.Rotation.Inverse();
			Turn.EnforceShortestArcWith(FQuat::Identity);
			Angular = Turn.ToRotationVector() / DeltaTime;
		}
		++Hand.Stats.NoAngularVelocityFrames;
	}

	// The hand's body rides on the grip pose at an offset: it is where the pose puts it, and it moves as the
	// pose moves plus what the pose's turning gives a point that far from its origin.
	const FVector Arm = State.Rotation.RotateVector(BodyOffset(Tuning, bLeft));
	FSandboxEffectorTarget Wanted;
	Wanted.Location = Origin.TransformPositionNoScale(State.Location + Arm);
	// The hand's own frame, off the grip pose's by a fixed turn (effectors.hand.grip_*_deg).
	const FQuat GripToHand = FRotator(Tuning.GripPitchDeg, Tuning.GripYawDeg, Tuning.GripRollDeg).Quaternion();
	Wanted.Rotation = (Origin.GetRotation() * State.Rotation * GripToHand).GetNormalized();
	Wanted.Velocity = Origin.GetRotation().RotateVector(Linear + FVector::CrossProduct(Angular, Arm));
	Wanted.Grip = FMath::Clamp(State.Squeeze, 0.0f, 1.0f);

	// The target follows the controller, but it does not jump: found again after it was lost (or tracked for
	// the first time, away from where its hand waits), or moved further in one frame than a hand moves, the
	// controller is caught up with at a set speed on top of its own.
	const double StepS = DeltaTime > 0.0f ? DeltaTime : 0.0;
	const double GapCm = Hand.bHasTarget ? FVector::Dist(Wanted.Location, Hand.Target.Location) : 0.0;
	if (Hand.bHasTarget && !Hand.bCatchingUp && (!Hand.Last.bTracked || GapCm > Tools.HeadsetMaxHandSpeedMps * 100.0 * StepS) && GapCm > UE_KINDA_SMALL_NUMBER && StepS > 0.0)
	{
		// One step at the catch-up speed is no jump: a controller found again where it was lost is simply followed.
		if (GapCm > (Tools.HeadsetCatchUpSpeedMps * 100.0 + Wanted.Velocity.Size()) * StepS)
		{
			Hand.bCatchingUp = true;
			++Hand.Stats.CatchUps;
		}
	}
	if (Hand.bCatchingUp)
	{
		const double SpeedCmS = Tools.HeadsetCatchUpSpeedMps * 100.0 + Wanted.Velocity.Size();
		if (GapCm <= SpeedCmS * StepS)
		{
			Hand.bCatchingUp = false;
		}
		else
		{
			const FVector Toward = (Wanted.Location - Hand.Target.Location) / GapCm;
			// The turn goes the same share of its way as the place does, so that both arrive together.
			Wanted.Rotation = FQuat::Slerp(Hand.Target.Rotation, Wanted.Rotation, SpeedCmS * StepS / GapCm).GetNormalized();
			Wanted.Location = Hand.Target.Location + Toward * SpeedCmS * StepS;
			Wanted.Velocity = Toward * SpeedCmS;
			++Hand.Stats.CatchUpFrames;
		}
	}

	Hand.Target = Wanted;
	Hand.bHasTarget = true;
	Hand.Last = State;

	++Hand.Stats.TrackedFrames;
	Hand.Stats.MaxTargetSpeedCmS = FMath::Max(Hand.Stats.MaxTargetSpeedCmS, Hand.Target.Velocity.Size());
	Hand.Stats.MaxGrip = FMath::Max(Hand.Stats.MaxGrip, Hand.Target.Grip);
}

const FSandboxTrackedTargetSource::FHand* FSandboxTrackedTargetSource::Find(FName Effector) const
{
	return Effector == SandboxEffectors::HandLeft ? &Left : Effector == SandboxEffectors::HandRight ? &Right : nullptr;
}

FSandboxTrackedTargetSource::FHand* FSandboxTrackedTargetSource::Find(FName Effector)
{
	return Effector == SandboxEffectors::HandLeft ? &Left : Effector == SandboxEffectors::HandRight ? &Right : nullptr;
}

bool FSandboxTrackedTargetSource::GetTarget(FName Effector, FSandboxEffectorTarget& OutTarget) const
{
	// Before a controller's first pose, and with no waiting pose given, there is nothing to say.
	const FHand* Hand = Find(Effector);
	if (!Hand || !Hand->bHasTarget)
	{
		return false;
	}
	OutTarget = Hand->Target;
	return true;
}

const FSandboxTrackedHandStats* FSandboxTrackedTargetSource::GetStats(FName Effector) const
{
	const FHand* Hand = Find(Effector);
	return Hand ? &Hand->Stats : nullptr;
}

bool FSandboxTrackedTargetSource::IsTracked(FName Effector) const
{
	const FHand* Hand = Find(Effector);
	return Hand && Hand->Last.bTracked;
}

bool FSandboxTrackedTargetSource::IsCatchingUp(FName Effector) const
{
	const FHand* Hand = Find(Effector);
	return Hand && Hand->bCatchingUp;
}
