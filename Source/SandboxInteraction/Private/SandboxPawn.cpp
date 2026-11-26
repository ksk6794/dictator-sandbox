#include "SandboxPawn.h"

#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/OverlapResult.h"
#include "PhysicsEngine/BodyInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/PlayerController.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "SandboxCore.h"
#include "SandboxDesktopTargetSource.h"
#include "SandboxEffectorComponent.h"
#include "SandboxEffectorRig.h"
#include "SandboxParamStore.h"
#include "SandboxTargetRecording.h"
#include "SandboxTrackedTargetSource.h"
#include "SandboxXRControllers.h"

namespace
{
	// How far the line of sight is followed, cm.
	constexpr double AimRangeCm = 2000.0;
	// Sanity bounds for what the wheel changes; the starting values and the step are tuning values.
	constexpr double MinImpulseNs = 0.1;
	constexpr double MaxImpulseNs = 1000.0;
	constexpr double MinDistanceCm = 10.0;
	constexpr double MaxReachCm = 300.0;
	constexpr double MaxDragDistanceCm = 1000.0;
	constexpr double MaxPitchDeg = 89.0;
	// A controller whose tracking comes and goes is not written into the log more often than this.
	constexpr int32 MaxTrackingAnnouncements = 40;
	// A headset that gives no pose of the head within this many frames gets its hands all the same (a second), with
	// the head taken to be where a standing person's is.
	constexpr int32 MaxHeadWaitFrames = 90;
	const FVector StandingHeadCm(0.0, 0.0, 165.0);

	const TCHAR* ToolName(ESandboxTool Tool)
	{
		switch (Tool)
		{
		case ESandboxTool::Impulse: return TEXT("impulse");
		case ESandboxTool::Drag: return TEXT("drag handle");
		default: return TEXT("hands");
		}
	}

	/** What a hit is called on the HUD: the bone of a skeletal body, the actor otherwise. */
	FString HitName(const FHitResult& Hit)
	{
		return !Hit.BoneName.IsNone() ? Hit.BoneName.ToString() : Hit.GetActor() ? Hit.GetActor()->GetName() : FString(TEXT("nothing"));
	}
}

ASandboxPawn::ASandboxPawn()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;
	// The camera flies and the time keys work while the world stands still.
	PrimaryActorTick.bTickEvenWhenPaused = true;

	Origin = CreateDefaultSubobject<USceneComponent>(TEXT("Origin"));
	SetRootComponent(Origin);
	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->bUsePawnControlRotation = false;
	Camera->SetupAttachment(Origin);

	// The pawn turns itself from the mouse; the controller's rotation is not used.
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;
	SetCanBeDamaged(false);
}

FTransform ASandboxPawn::ViewTransform() const
{
	return FTransform(Camera->GetComponentQuat(), Camera->GetComponentLocation());
}

FTransform ASandboxPawn::OriginTransform() const
{
	return FTransform(GetActorQuat(), GetActorLocation());
}

void ASandboxPawn::BeginPlay()
{
	Super::BeginPlay();

	ReadTuning();
	HandAnchor = ViewTransform();
	if (!bTuningRead)
	{
		return;
	}

	// In a headset, or a stand-in for one: the hands and the player's body are made once the head's pose is known
	// (MakeHeadsetRig, from the first stepped tick), so that the head, and not the middle of the play space, stands
	// where the session put the player.
	if (StandIn.IsValid() || FSandboxXRControllers::IsHeadsetOn())
	{
		if (StandIn.IsValid())
		{
			Tracker = StandIn;
		}
		else
		{
			Controllers = MakeShared<FSandboxXRControllers>();
			Controllers->Start();
			Tracker = Controllers;
		}
		TrackedSource = MakeShared<FSandboxTrackedTargetSource>();
		UE_LOG(LogSandbox, Display, TEXT("The pawn is in a headset, %s: its hands follow the grip poses of the controllers, the right stick moves it, the left stick turns it"),
			StandIn.IsValid() ? TEXT("a stand-in for one (no person)") : *FSandboxXRControllers::DescribeSystem());
		return;
	}

	FString Error;
	Rig = GetWorld()->SpawnActor<ASandboxEffectorRig>();
	if (Rig && Rig->Start(FSandboxParamStore::Get(), Error) &&
		Rig->AddEffector(SandboxEffectors::HandLeft, FSandboxDesktopTargetSource::RestPose(HandAnchor, /*bLeft*/ true, Tuning.Tools), Error) &&
		Rig->AddEffector(SandboxEffectors::HandRight, FSandboxDesktopTargetSource::RestPose(HandAnchor, /*bLeft*/ false, Tuning.Tools), Error) &&
		// The kicking foot, on the desktop: in a headset a kick waits for a button that is known to arrive.
		Rig->AddEffector(SandboxEffectors::FootRight, FSandboxDesktopTargetSource::FootRestPose(HandAnchor, Tuning.Tools), Error))
	{
		HandSource = MakeShared<FSandboxDesktopTargetSource>();
		Rig->SetSource(HandSource);
	}
	else
	{
		UE_LOG(LogSandbox, Error, TEXT("The pawn has no hands: %s"), *Error);
		if (Rig)
		{
			Rig->Destroy();
			Rig = nullptr;
		}
	}
}

void ASandboxPawn::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// A session that is closed while it records keeps what it has.
	StopRecording();
	LogTrackingSummary();
	DragHandle.Release();
	if (Rig)
	{
		Rig->Destroy();
		Rig = nullptr;
	}
	Super::EndPlay(EndPlayReason);
}

void ASandboxPawn::ReadTuning()
{
	FSandboxParamStore& Params = FSandboxParamStore::Get();
	FSandboxInteractionTuning NewTuning;
	FString Error;
	if (!Params.EnsureLoaded(Error) || !FSandboxInteractionTuning::FromParams(Params, NewTuning, Error))
	{
		UE_LOG(LogSandbox, Error, TEXT("The pawn keeps its tuning: %s"), *Error);
		TuningRevision = Params.GetRevision();
		return;
	}
	// What the wheel changes starts from the file and follows it again when the file's own value changes.
	if (!bTuningRead || NewTuning.Tools.ImpulseNs != Tuning.Tools.ImpulseNs)
	{
		ImpulseNs = NewTuning.Tools.ImpulseNs;
	}
	if (!bTuningRead || NewTuning.Tools.HandReachCm != Tuning.Tools.HandReachCm)
	{
		ReachCm = NewTuning.Tools.HandReachCm;
	}
	Tuning = NewTuning;
	TuningRevision = Params.GetRevision();
	bTuningRead = true;
	DragHandle.SetTuning(Tuning.Tools.Drag);
}

void ASandboxPawn::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	const FSandboxParamStore& Params = FSandboxParamStore::Get();
	if (Params.IsLoaded() && Params.GetRevision() != TuningRevision)
	{
		ReadTuning();
	}
	if (!bTuningRead)
	{
		return;
	}

	// The camera: turned by the mouse, moved along its own axes, and straight up and down. In a headset the
	// head is the camera and the play space stays where it was put: a mouse that turned it would turn the
	// world round the player.
	if (!TrackedSource)
	{
		FRotator Rotation = GetActorRotation();
		Rotation.Yaw += LookInput.X * Tuning.Tools.CameraLookDegPerCount;
		Rotation.Pitch = FMath::Clamp(Rotation.Pitch + LookInput.Y * Tuning.Tools.CameraLookDegPerCount, -MaxPitchDeg, MaxPitchDeg);
		Rotation.Roll = 0.0;
		const double StepCm = Tuning.Tools.CameraSpeedMps * 100.0 * (bFast ? Tuning.Tools.CameraFastFactor : 1.0) * DeltaTime;
		const FVector Location = GetActorLocation() + (Rotation.RotateVector(FVector(MoveInput.X, MoveInput.Y, 0.0)) + FVector(0.0, 0.0, MoveInput.Z)) * StepCm;
		SetActorLocationAndRotation(Location, Rotation);
	}
	LookInput = FVector2D::ZeroVector;
	MoveInput = FVector::ZeroVector;

	// Whether the world makes a physics step in this frame was settled by the last tick's time control.
	const bool bStepping = !GetWorld()->IsPaused();
	if (bStepping)
	{
		++SimulatedFrames;
	}

	const FTransform View = ViewTransform();
	if (!bCameraDetached)
	{
		HandAnchor = View;
	}

	// The hands and the handle are physics: they get new targets only for a step that will be made. After
	// a pause the camera may be anywhere, so the targets then start without a velocity.
	// A new take starts here, before the hands get the targets of the coming step: the recording that ran is
	// handed on with the body as it is, and the reset is asked for. BeginTake follows once it is done.
	if (bStepping && Rig && bNewTakePending && !bTakeWaitsForReset)
	{
		bNewTakePending = false;
		StopRecording();
		DragHandle.Release();
		bTakeWaitsForReset = true;
		OnResetRequested.Broadcast();
		if (!OnResetRequested.IsBound())
		{
			// Nobody to put a character back (a pawn without a session): the take begins as things are.
			BeginTake();
		}
	}

	if (bStepping && TrackedSource)
	{
		if (Controllers)
		{
			// The squeeze and the sticks are read from the input of whoever possesses the pawn now.
			Controllers->SetPlayer(Cast<APlayerController>(GetController()));
		}
		// The head as the runtime has it for this frame (a stand-in's is put on the camera here, as a runtime's is by the
		// engine). A head that is not seen keeps the pose it was last seen in; a pose of exactly nothing, which a runtime
		// may give before it tracks, is none (the head is put where the session wants it by its first real pose).
		FVector Head;
		FQuat HeadRotation;
		if (Tracker && Tracker->GetHead(Head, HeadRotation) && !Head.IsZero())
		{
			HeadLocal = Head;
			HeadLocalRotation = HeadRotation.GetNormalized();
			bHeadSeen = true;
		}
		if (StandIn)
		{
			Camera->SetRelativeLocationAndRotation(HeadLocal, HeadLocalRotation);
		}
		// The hands and the body are made once the head is known; a headset that gives no head is not waited for long.
		if (!Rig && (bHeadSeen || ++HeadWaitFrames >= MaxHeadWaitFrames))
		{
			MakeHeadsetRig();
		}
		if (Rig)
		{
			UpdateHeadset(DeltaTime);
		}
	}
	else if (bStepping && Rig && HandSource)
	{
		const bool bHands = Tool == ESandboxTool::Hands;
		if (bPlaceHands)
		{
			// A take begins with this frame: a kick under way does not go on into it, and the foot is made at its rest.
			HandSource->StopKick();
		}
		HandSource->SetHandsOpen(bHands && bHandsOpen);
		HandSource->SetFootRestStrength(Tuning.Foot.MaxForceN > 0.0 ? Tuning.Tools.KickRestForceN / Tuning.Foot.MaxForceN : 1.0);
		HandSource->Update(HandAnchor, bHands && bSecondaryDown, bHands && bPrimaryDown, Tuning.Tools, ReachCm, DeltaTime, bSteppedLastFrame);
		RemakeHandsForTake();
		Rig->UpdateTargets(DeltaTime);
	}
	if (bStepping && DragHandle.IsHolding())
	{
		const FVector Target = View.GetLocation() + View.GetRotation().GetForwardVector() * DragDistanceCm;
		DragHandle.SetTarget(Target, bSteppedLastFrame && DeltaTime > 0.0f ? (Target - LastDragTarget) / DeltaTime : FVector::ZeroVector);
		LastDragTarget = Target;
	}
	bSteppedLastFrame = bStepping;

	UpdateTimeControl();
}

void ASandboxPawn::AnnounceTracking()
{
	const FName Hands[2] = { SandboxEffectors::HandLeft, SandboxEffectors::HandRight };
	for (int32 Index = 0; Index < 2; ++Index)
	{
		const bool bTracked = TrackedSource->IsTracked(Hands[Index]);
		if (bTracked == bWasTracked[Index])
		{
			continue;
		}
		bWasTracked[Index] = bTracked;
		if (++TrackingAnnouncements > MaxTrackingAnnouncements)
		{
			continue;
		}
		const FSandboxTrackedHandStats* Stats = TrackedSource->GetStats(Hands[Index]);
		const USandboxEffectorComponent* Effector = Rig ? Rig->FindEffector(Hands[Index]) : nullptr;
		if (bTracked && Stats && Stats->TrackedFrames == 1)
		{
			// The first pose: whether the runtime gives the velocities with it is what the feel of the hands rests on.
			UE_LOG(LogSandbox, Display, TEXT("Headset: the controller of %s is tracked (velocity from the runtime: linear %s, angular %s); the hand is %.0f cm from it"),
				*Hands[Index].ToString(), Stats->NoLinearVelocityFrames == 0 ? TEXT("yes") : TEXT("NO, the difference of poses is used"),
				Stats->NoAngularVelocityFrames == 0 ? TEXT("yes") : TEXT("NO, the difference of poses is used"), Effector ? Effector->GetGapCm() : 0.0);
		}
		else
		{
			UE_LOG(LogSandbox, Display, TEXT("Headset: the controller of %s is %s (simulated frame %lld)"), *Hands[Index].ToString(), bTracked ? TEXT("tracked again") : TEXT("lost: the hand stays where it was"), SimulatedFrames);
		}
		if (TrackingAnnouncements == MaxTrackingAnnouncements)
		{
			UE_LOG(LogSandbox, Display, TEXT("Headset: tracking has changed %d times; further changes are not logged (the totals come when the session ends)"), MaxTrackingAnnouncements);
		}
	}
}

void ASandboxPawn::LogTrackingSummary() const
{
	if (!TrackedSource)
	{
		return;
	}
	for (const FName Hand : { SandboxEffectors::HandLeft, SandboxEffectors::HandRight })
	{
		if (const FSandboxTrackedHandStats* Stats = TrackedSource->GetStats(Hand))
		{
			UE_LOG(LogSandbox, Display, TEXT("Headset: %s was tracked for %d frames (%d of them on a pose the runtime only guessed, for a moment) and lost for %d (in %d of those the runtime still gave a pose); no velocity from the runtime in %d (linear) and %d (angular) tracked frames; the pose moved while its velocity read zero in %d; the target caught up %d times over %d frames; fastest target %.2f m/s, grip up to %.2f"),
				*Hand.ToString(), Stats->TrackedFrames, Stats->EstimatedFollowedFrames, Stats->LostFrames, Stats->EstimatedFrames, Stats->NoLinearVelocityFrames, Stats->NoAngularVelocityFrames,
				Stats->MovedWithoutVelocityFrames, Stats->CatchUps, Stats->CatchUpFrames, Stats->MaxTargetSpeedCmS * 0.01, Stats->MaxGrip);
		}
	}
	if (Controllers)
	{
		UE_LOG(LogSandbox, Display, TEXT("Headset: controller keys that reached the player's input: %s"), *Controllers->DescribeKeysSeen());
	}
	const FSandboxLocomotionStats& Moved = Locomotion.GetStats();
	UE_LOG(LogSandbox, Display, TEXT("Headset: the stick moved the player %.1f m in %d frames, at %.2f m/s at most; %d snap turns to the left and %d to the right; the body was held back in %d frames (%.0f%% of the motion at most), %.1f cm from its target at most; the view was darkened by %.0f%% at most"),
		Moved.WalkedCm * 0.01, Moved.MovingFrames, Moved.MaxSpeedCmS * 0.01, Moved.SnapsLeft, Moved.SnapsRight, Moved.HeldBackFrames, Moved.MostHeldBack * 100.0, Moved.MaxBodyGapCm, MaxHeadFade * 100.0);
}

bool ASandboxPawn::MakeHeadsetRig()
{
	if (!bHeadSeen)
	{
		HeadLocal = StandingHeadCm;
		HeadLocalRotation = FQuat::Identity;
		UE_LOG(LogSandbox, Warning, TEXT("Headset: no pose of the head in %d frames; the hands are made for a head %.0f cm above the middle of the play space"), MaxHeadWaitFrames, StandingHeadCm.Z);
	}
	// The head, not the middle of the play space, stands where the session put the player, looking the way it said.
	if (bHasStand)
	{
		const FTransform Placed = FSandboxLocomotion::PlaceHead(HeadLocal, HeadLocalRotation, StandPoint, StandYawDeg);
		SetActorLocationAndRotation(Placed.GetLocation(), Placed.GetRotation());
	}
	Locomotion.Reset(OriginTransform(), HeadLocal);

	// The hands wait in front of the head at a set height above the floor until their controllers are tracked, and
	// catch up with them from there; the body is made under the head.
	const FTransform PlaySpace = OriginTransform();
	const FVector Head = PlaySpace.TransformPositionNoScale(HeadLocal);
	const FVector Facing = FSandboxLocomotion::LevelForward(PlaySpace.GetRotation() * HeadLocalRotation);
	HandAnchor = FTransform(Facing.Rotation(), FVector(Head.X, Head.Y, PlaySpace.GetLocation().Z + Tuning.Tools.HeadsetHandsHeightCm + Tuning.Tools.HandRestDownCm));
	const FSandboxEffectorTarget Body = Locomotion.GetBodyTarget(Tuning.Body, Tuning.Tools);

	FString Error;
	Rig = GetWorld()->SpawnActor<ASandboxEffectorRig>();
	if (!Rig || !Rig->Start(FSandboxParamStore::Get(), Error) ||
		!Rig->AddEffector(SandboxEffectors::HandLeft, FSandboxDesktopTargetSource::RestPose(HandAnchor, /*bLeft*/ true, Tuning.Tools), Error) ||
		!Rig->AddEffector(SandboxEffectors::HandRight, FSandboxDesktopTargetSource::RestPose(HandAnchor, /*bLeft*/ false, Tuning.Tools), Error) ||
		!Rig->AddEffector(SandboxEffectors::Body, FTransform(Body.Rotation, Body.Location), Error))
	{
		UE_LOG(LogSandbox, Error, TEXT("The pawn has no hands: %s"), *Error);
		if (Rig)
		{
			Rig->Destroy();
			Rig = nullptr;
		}
		return false;
	}
	// The player is inside the body and does not see it (its mesh is owner-no-see).
	Rig->SetOwner(this);
	TrackedSource->SetWaitingPose(SandboxEffectors::HandLeft, FSandboxDesktopTargetSource::RestPose(HandAnchor, /*bLeft*/ true, Tuning.Tools));
	TrackedSource->SetWaitingPose(SandboxEffectors::HandRight, FSandboxDesktopTargetSource::RestPose(HandAnchor, /*bLeft*/ false, Tuning.Tools));
	TrackedSource->SetBodyTarget(Body);
	Rig->SetSource(TrackedSource);
	UE_LOG(LogSandbox, Display, TEXT("Headset: the head stands at %.0f, %.0f, %.0f cm, looking along %.0f degrees; the hands and the player's body are made (simulated frame %lld)"),
		Head.X, Head.Y, Head.Z, FMath::RadiansToDegrees(FMath::Atan2(Facing.Y, Facing.X)), SimulatedFrames);
	return true;
}

void ASandboxPawn::UpdateHeadset(float DeltaTime)
{
	// The sticks, the head and the body as they are at the start of this step; the play space moves, and the hands and
	// the body are told where to be in it.
	FSandboxLocomotionInput Input;
	FSandboxControllerState Left, Right;
	if (Tracker)
	{
		Tracker->GetController(SandboxEffectors::HandLeft, Left);
		Tracker->GetController(SandboxEffectors::HandRight, Right);
	}
	Input.MoveStick = Right.Stick;
	Input.TurnStick = Left.Stick;
	Input.HeadLocation = HeadLocal;
	Input.HeadRotation = HeadLocalRotation;
	if (const USandboxEffectorComponent* Body = Rig->FindEffector(SandboxEffectors::Body))
	{
		Input.bHasBody = true;
		Input.BodyLocation = Body->GetHandTransform().GetLocation();
		Input.BodyContactN = Body->GetContactPushN();
	}
	Locomotion.Update(Input, Tuning.Tools, DeltaTime);
	SetActorLocationAndRotation(Locomotion.GetOrigin().GetLocation(), Locomotion.GetOrigin().GetRotation());
	TrackedSource->SetBodyTarget(Locomotion.GetBodyTarget(Tuning.Body, Tuning.Tools));
	FSandboxPlaySpaceMotion Motion;
	Motion.VelocityCmS = Locomotion.GetOriginVelocity();
	Motion.bJumped = Locomotion.HasSnapped();
	TrackedSource->Update(*Tracker, OriginTransform(), Tuning.Hand, Tuning.Tools, DeltaTime, bSteppedLastFrame, Motion);
	RemakeHandsForTake();
	Rig->UpdateTargets(DeltaTime);
	AnnounceTracking();

	// Both grips held closed with both hands above the eyes: a new take. Closed grips alone will not do: a
	// person who punches holds both closed all the time. It fires once a hold, and the controllers say that
	// it has.
	const FVector Eyes = GetHeadLocation();
	FSandboxEffectorTarget LeftTarget, RightTarget;
	const double RaisedZ = Eyes.Z + Tuning.Tools.HeadsetNewTakeRaiseCm;
	const bool bBothClosed = TrackedSource->IsTracked(SandboxEffectors::HandLeft) && TrackedSource->IsTracked(SandboxEffectors::HandRight) &&
		TrackedSource->GetTarget(SandboxEffectors::HandLeft, LeftTarget) && TrackedSource->GetTarget(SandboxEffectors::HandRight, RightTarget) &&
		LeftTarget.Grip > Tuning.Hand.FistGrip && RightTarget.Grip > Tuning.Hand.FistGrip && LeftTarget.Location.Z > RaisedZ && RightTarget.Location.Z > RaisedZ;
	GripsHeldS = bBothClosed ? GripsHeldS + DeltaTime : 0.0;
	if (!bBothClosed)
	{
		bGripsArmed = true;
	}
	else if (bGripsArmed && Tuning.Tools.HeadsetNewTakeHoldS > 0.0 && GripsHeldS >= Tuning.Tools.HeadsetNewTakeHoldS)
	{
		bGripsArmed = false;
		RequestNewTake();
	}
	// The controllers vibrate with what their hands press or strike with (the hands' own contacts: the pawn
	// knows nothing of what they touch), and for a moment when a take begins. A stand-in has nothing to vibrate.
	const float TakeBuzz = BuzzLeftS > 0.0 ? static_cast<float>(Tuning.Tools.HeadsetTakeBuzzStrength) : 0.0f;
	BuzzLeftS = FMath::Max(BuzzLeftS - DeltaTime, 0.0);
	const FName Hands[2] = { SandboxEffectors::HandLeft, SandboxEffectors::HandRight };
	for (int32 Index = 0; Controllers && Index < 2; ++Index)
	{
		const USandboxEffectorComponent* Effector = Rig->FindEffector(Hands[Index]);
		// What the hand presses with, or what it pulls with on a thing it holds, whichever is more.
		const double FeltN = Effector ? FMath::Max(Effector->GetContactForceN(), Effector->GetGrabForceN()) : 0.0;
		const float Amplitude = FMath::Max(TakeBuzz, FSandboxXRControllers::HapticAmplitude(FeltN, Tuning.Tools.HeadsetHapticFullN));
		// One call is one frame of vibration: asked for in every frame it is to go on, and once more to stop it.
		if (Amplitude > 0.0f || LastHaptic[Index] > 0.0f)
		{
			Controllers->Buzz(Index == 0, Amplitude);
		}
		LastHaptic[Index] = Amplitude;
	}

	// The view darkens as the eyes come near the NPC or the furniture (decision 25: the head may go in, the view goes
	// dark), and at its edges while the stick moves it, if the tuning asks for that. Both only look at the world.
	HeadFade = MeasureHeadFade(Eyes);
	MaxHeadFade = FMath::Max(MaxHeadFade, HeadFade);
	APlayerController* PlayerController = Cast<APlayerController>(GetController());
	if (PlayerController && PlayerController->PlayerCameraManager && (HeadFade > 0.0 || AppliedFade > 0.0))
	{
		PlayerController->PlayerCameraManager->SetManualCameraFade(static_cast<float>(HeadFade), FLinearColor::Black, /*bFadeAudio*/ false);
		AppliedFade = HeadFade;
	}
	if (Tuning.Tools.HeadsetVignette > 0.0 || Camera->PostProcessSettings.bOverride_VignetteIntensity)
	{
		Camera->PostProcessSettings.bOverride_VignetteIntensity = Tuning.Tools.HeadsetVignette > 0.0;
		Camera->PostProcessSettings.VignetteIntensity = static_cast<float>(Tuning.Tools.HeadsetVignette * Locomotion.GetSpeedShare(Tuning.Tools));
	}
}

double ASandboxPawn::MeasureHeadFade(const FVector& Eyes) const
{
	const double RadiusCm = Tuning.Tools.HeadsetHeadFadeCm;
	const UWorld* World = GetWorld();
	if (RadiusCm <= 0.0 || !World)
	{
		return 0.0;
	}
	// A ball at the eyes: how deep it lies in the NPC, a thing or the room is how near the eyes are to its surface. By
	// object type, as the aim is (a character's bodies do not answer the visibility channel); the player's own hands
	// and body are left out.
	const FCollisionShape Ball = FCollisionShape::MakeSphere(static_cast<float>(RadiusCm));
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(ECC_WorldStatic);
	Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
	Objects.AddObjectTypesToQuery(ECC_PhysicsBody);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(SandboxHeadFade), /*bTraceComplex*/ false, this);
	if (Rig)
	{
		Params.AddIgnoredActor(Rig);
	}
	TArray<FOverlapResult> Overlaps;
	World->OverlapMultiByObjectType(Overlaps, Eyes, FQuat::Identity, Objects, Ball, Params);
	double DepthCm = 0.0;
	TArray<const UPrimitiveComponent*, TInlineAllocator<4>> Seen;
	for (const FOverlapResult& Overlap : Overlaps)
	{
		UPrimitiveComponent* Component = Overlap.GetComponent();
		if (!Component || Seen.Contains(Component))
		{
			continue;
		}
		Seen.Add(Component);
		FMTDResult Separation;
		if (const USkeletalMeshComponent* Skeletal = Cast<USkeletalMeshComponent>(Component))
		{
			// A skeletal mesh answers an overlap as one thing: its bodies are asked one by one.
			for (const FBodyInstance* Part : Skeletal->Bodies)
			{
				if (Part && Part->IsValidBodyInstance() && Part->OverlapTest(Eyes, FQuat::Identity, Ball, &Separation))
				{
					DepthCm = FMath::Max(DepthCm, static_cast<double>(Separation.Distance));
				}
			}
		}
		else if (Component->ComputePenetration(Separation, Ball, Eyes, FQuat::Identity))
		{
			DepthCm = FMath::Max(DepthCm, static_cast<double>(Separation.Distance));
		}
	}
	// The ball's radius deep: the eyes are at the surface, and the view is black.
	return FMath::Clamp(DepthCm / RadiusCm, 0.0, 1.0);
}

FVector ASandboxPawn::GetHeadLocation() const
{
	return TrackedSource ? OriginTransform().TransformPositionNoScale(HeadLocal) : ViewTransform().GetLocation();
}

void ASandboxPawn::UpdateTimeControl()
{
	// Decides whether the world steps in the next frame. A step is always the full fixed step: time is
	// slowed by leaving frames out, never by making steps shorter.
	bool bRunNext = true;
	if (StepsPending > 0)
	{
		--StepsPending;
	}
	else if (bUserPaused)
	{
		bRunNext = false;
	}
	else if (bSlowMotion)
	{
		SlowCounter = (SlowCounter + 1) % FMath::Max(FMath::RoundToInt32(Tuning.Tools.SlowMotionDivisor), 1);
		bRunNext = SlowCounter == 0;
	}

	APlayerController* PlayerController = Cast<APlayerController>(GetController());
	if (PlayerController && GetWorld()->IsPaused() == bRunNext)
	{
		PlayerController->SetPause(!bRunNext);
	}
}

void ASandboxPawn::SelectTool(ESandboxTool InTool)
{
	if (Tool == InTool)
	{
		return;
	}
	DragHandle.Release();
	bPrimaryDown = false;
	bSecondaryDown = false;
	bHandsOpen = false;
	Tool = InTool;
	LastAction = FString::Printf(TEXT("tool: %s"), ToolName(Tool));
}

void ASandboxPawn::Kick()
{
	// The desktop's kick: at what the camera aims at, or straight ahead at the height of a shin.
	if (!HandSource || HandSource->IsKicking())
	{
		return;
	}
	// From where the foot is, which is under the hands' anchor: with the camera detached that is not under the camera.
	FHitResult Hit;
	FVector Point;
	if (!bCameraDetached && TraceAim(Hit))
	{
		Point = Hit.ImpactPoint;
		LastAction = FString::Printf(TEXT("kick at %s"), !Hit.BoneName.IsNone() ? *Hit.BoneName.ToString() : TEXT("what is aimed at"));
	}
	else
	{
		const FTransform Rest = FSandboxDesktopTargetSource::FootRestPose(HandAnchor, Tuning.Tools);
		Point = Rest.GetLocation() + Rest.GetRotation().RotateVector(FVector(Tuning.Tools.KickReachCm, 0.0, 0.0));
		LastAction = TEXT("kick ahead");
	}
	HandSource->Kick(Point);
}

void ASandboxPawn::AimReach()
{
	// A fist that is sent at something goes there and through it, as a punch does. An open hand reaches as far
	// as the wheel has set, and so does a fist with nothing under the crosshair. (An open hand that stopped at
	// the surface was tried: the body gives way by more than a grab's reach, and half a centimetre of the
	// body's stance was a sixth of the hand's push.)
	FHitResult Hit;
	if (Tool == ESandboxTool::Hands && !bCameraDetached && !bHandsOpen && TraceAim(Hit))
	{
		ReachCm = FMath::Clamp(Hit.Distance + Tuning.Tools.HandThroughCm, MinDistanceCm, MaxReachCm);
	}
}

void ASandboxPawn::SetPrimary(bool bDown)
{
	bPrimaryDown = bDown;
	if (bDown)
	{
		AimReach();
	}
	if (Tool == ESandboxTool::Impulse && bDown)
	{
		FHitResult Hit;
		if (TraceAim(Hit))
		{
			LastAction = FString::Printf(TEXT("impulse of %.1f N*s on %s"), ImpulseNs, *HitName(Hit));
			OnImpulseRequested.Broadcast(Hit, ViewTransform().GetRotation().GetForwardVector() * ImpulseNs);
		}
		else
		{
			LastAction = TEXT("impulse: nothing under the crosshair");
		}
	}
	else if (Tool == ESandboxTool::Drag)
	{
		FHitResult Hit;
		if (!bDown)
		{
			if (DragHandle.IsHolding())
			{
				LastAction = TEXT("drag handle: let go");
			}
			DragHandle.Release();
		}
		else if (TraceAim(Hit) && DragHandle.Grab(Hit.GetComponent(), Hit.BoneName, Hit.ImpactPoint, Tuning.Tools.Drag, this))
		{
			DragDistanceCm = Hit.Distance;
			LastDragTarget = Hit.ImpactPoint;
			LastAction = FString::Printf(TEXT("drag handle: holding %s"), *HitName(Hit));
		}
		else
		{
			LastAction = TEXT("drag handle: no simulated body under the crosshair");
		}
	}
}

void ASandboxPawn::SetSecondary(bool bDown)
{
	bSecondaryDown = bDown;
	if (bDown)
	{
		AimReach();
	}
}

void ASandboxPawn::AdjustTool(float Notches)
{
	if (!bTuningRead || Notches == 0.0f)
	{
		return;
	}
	const double DistanceFactor = FMath::Pow(Tuning.Tools.WheelDistanceFactor, static_cast<double>(Notches));
	switch (Tool)
	{
	case ESandboxTool::Impulse:
		ImpulseNs = FMath::Clamp(ImpulseNs * FMath::Pow(Tuning.Tools.ImpulseWheelFactor, static_cast<double>(Notches)), MinImpulseNs, MaxImpulseNs);
		break;
	case ESandboxTool::Drag:
		DragDistanceCm = FMath::Clamp(DragDistanceCm * DistanceFactor, MinDistanceCm, MaxDragDistanceCm);
		break;
	default:
		ReachCm = FMath::Clamp(ReachCm * DistanceFactor, MinDistanceCm, MaxReachCm);
		break;
	}
}

void ASandboxPawn::TogglePause()
{
	bUserPaused = !bUserPaused;
	StepsPending = 0;
	LastAction = bUserPaused ? TEXT("paused") : TEXT("resumed");
}

void ASandboxPawn::StepOnce()
{
	if (bUserPaused)
	{
		++StepsPending;
		LastAction = TEXT("one step");
	}
}

void ASandboxPawn::ToggleSlowMotion()
{
	bSlowMotion = !bSlowMotion;
	SlowCounter = 0;
	LastAction = bSlowMotion ? TEXT("slow motion on") : TEXT("slow motion off");
}

void ASandboxPawn::RequestReset()
{
	// Whatever the handle holds is about to be destroyed.
	DragHandle.Release();
	LastAction = TEXT("reset");
	OnResetRequested.Broadcast();
}

void ASandboxPawn::ToggleCameraDetached()
{
	bCameraDetached = !bCameraDetached;
	LastAction = bCameraDetached ? TEXT("camera free: the hands stay where they are") : TEXT("hands back in front of the camera");
}

void ASandboxPawn::ToggleOverlays()
{
	if (APlayerController* PlayerController = Cast<APlayerController>(GetController()))
	{
		PlayerController->ConsoleCommand(TEXT("Sandbox.Overlay toggle"));
		LastAction = TEXT("overlays toggled (Sandbox.Overlay in the console chooses layers)");
	}
}

void ASandboxPawn::ToggleRecording()
{
	if (Recording)
	{
		StopRecording();
	}
	else
	{
		StartRecording();
	}
}

bool ASandboxPawn::StartRecording()
{
	if (!Rig || Recording)
	{
		return false;
	}
	const TSharedRef<FSandboxTargetRecording> Fresh = MakeShared<FSandboxTargetRecording>();
	FString Error;
	if (!Rig->SetRecording(Fresh, Error))
	{
		LastAction = FString::Printf(TEXT("no recording: %s"), *Error);
		return false;
	}
	Recording = Fresh;
	RecordingFirstFrame = SimulatedFrames;
	LastAction = TEXT("recording the hands (K ends it)");
	return true;
}

void ASandboxPawn::RemakeHandsForTake()
{
	if (!bPlaceHands)
	{
		return;
	}
	// The hands start the take as the hands of the scenario that replays it start: made after the character,
	// on the targets of the take's first frame, at rest and with nothing to their name.
	bPlaceHands = false;
	if (HandSource)
	{
		// A kick under way does not go on into the take: the foot is made anew at its rest.
		HandSource->StopKick();
	}
	FString Error;
	if (!Rig->RemakeEffectors(Error))
	{
		UE_LOG(LogSandbox, Error, TEXT("The pawn lost its hands at the start of a take: %s"), *Error);
	}
}

void ASandboxPawn::RequestNewTake()
{
	bNewTakePending = true;
}

void ASandboxPawn::BeginTake()
{
	if (!bTakeWaitsForReset)
	{
		return;
	}
	bTakeWaitsForReset = false;
	StartRecording();
	bPlaceHands = true;
	++Takes;
	BuzzLeftS = Tuning.Tools.HeadsetTakeBuzzS;
	LastAction = FString::Printf(TEXT("take %d: the character is back, the hands are recorded"), Takes);
	UE_LOG(LogSandbox, Display, TEXT("The pawn begins take %d after simulated frame %lld"), Takes, SimulatedFrames);
}

void ASandboxPawn::StopRecording()
{
	if (!Recording)
	{
		return;
	}
	FString Error;
	if (Rig)
	{
		Rig->SetRecording(nullptr, Error);
	}
	const TSharedRef<FSandboxTargetRecording> Done = Recording.ToSharedRef();
	Recording.Reset();
	LastAction = FString::Printf(TEXT("recorded %d frames of the hands"), Done->NumFrames());
	OnRecordingStopped.Broadcast(Done, RecordingFirstFrame);
}

void ASandboxPawn::SetView(const FVector& Location, const FRotator& Rotation)
{
	if (StandIn.IsValid() || FSandboxXRControllers::IsHeadsetOn())
	{
		// In a headset the pawn is the play space: a pitched one would tilt the whole world for the wearer. The head is
		// put over the point once its pose is known (MakeHeadsetRig); after that, at once, with the play space's
		// movement begun again from there (the hands and the body catch up).
		StandPoint = Location;
		StandYawDeg = Rotation.Yaw;
		bHasStand = true;
		if (Rig)
		{
			const FTransform Placed = FSandboxLocomotion::PlaceHead(HeadLocal, HeadLocalRotation, StandPoint, StandYawDeg);
			SetActorLocationAndRotation(Placed.GetLocation(), Placed.GetRotation());
			Locomotion.Reset(OriginTransform(), HeadLocal);
		}
		else
		{
			SetActorLocationAndRotation(Location, FRotator(0.0, Rotation.Yaw, 0.0));
		}
		return;
	}
	SetActorLocationAndRotation(Location, FRotator(FMath::Clamp(Rotation.Pitch, -MaxPitchDeg, MaxPitchDeg), Rotation.Yaw, 0.0));
}

bool ASandboxPawn::TraceAim(FHitResult& OutHit) const
{
	// By object type: the character's bodies do not answer the visibility channel.
	const FTransform View = ViewTransform();
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(ECC_WorldStatic);
	Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
	Objects.AddObjectTypesToQuery(ECC_PhysicsBody);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(SandboxAim), /*bTraceComplex*/ false, this);
	if (Rig)
	{
		Params.AddIgnoredActor(Rig);
	}
	const FVector Start = View.GetLocation();
	return GetWorld()->LineTraceSingleByObjectType(OutHit, Start, Start + View.GetRotation().GetForwardVector() * AimRangeCm, Objects, Params);
}

void ASandboxPawn::GetHudLines(TArray<FString>& OutLines) const
{
	switch (Tool)
	{
	case ESandboxTool::Impulse:
		OutLines.Add(FString::Printf(TEXT("Tool: impulse, %.1f N*s (wheel changes it)"), ImpulseNs));
		break;
	case ESandboxTool::Drag:
		OutLines.Add(DragHandle.IsHolding()
			? FString::Printf(TEXT("Tool: drag handle, holding %s, %.0f cm from the camera, %.1f cm behind its target, asks for %.0f N of %.0f"),
				*(DragHandle.GetBone().IsNone() ? FString(TEXT("a body")) : DragHandle.GetBone().ToString()), DragDistanceCm, DragHandle.GetGapCm(), DragHandle.GetDemandN(), Tuning.Tools.Drag.MaxForceN)
			: FString(TEXT("Tool: drag handle (hold the left button on a body)")));
		break;
	default:
		OutLines.Add(FString::Printf(TEXT("Tool: hands, reach %.0f cm (left button: right hand, right button: left hand)"), ReachCm));
		break;
	}

	OutLines.Add(FString::Printf(TEXT("Time: %s, simulated frame %lld"),
		bUserPaused ? TEXT("PAUSED (O makes one step)") : bSlowMotion ? *FString::Printf(TEXT("slow motion 1/%d"), FMath::RoundToInt32(Tuning.Tools.SlowMotionDivisor)) : TEXT("running"),
		SimulatedFrames));

	if (TrackedSource)
	{
		FString Tracking = TEXT("Headset:");
		for (const FName Hand : { SandboxEffectors::HandLeft, SandboxEffectors::HandRight })
		{
			FSandboxEffectorTarget Target;
			const bool bHasTarget = TrackedSource->GetTarget(Hand, Target);
			Tracking += FString::Printf(TEXT("  %s %s, grip %.2f"), *Hand.ToString(),
				TrackedSource->IsTracked(Hand) ? TEXT("tracked") : bHasTarget ? TEXT("LOST") : TEXT("not tracked yet"), bHasTarget ? Target.Grip : 0.0f);
		}
		OutLines.Add(Tracking);
	}
	if (Rig)
	{
		FString Hands = bCameraDetached ? TEXT("Hands (left behind):") : TEXT("Hands:");
		for (const USandboxEffectorComponent* Effector : Rig->GetEffectors())
		{
			Hands += FString::Printf(TEXT("  %s %.1f cm from target, pressing with %.0f N"), *Effector->GetId().ToString(), Effector->GetGapCm(), Effector->GetContactForceN());
		}
		OutLines.Add(Hands);
	}
	else
	{
		OutLines.Add(TEXT("Hands: none (see the log)"));
	}
	if (Recording)
	{
		OutLines.Add(FString::Printf(TEXT("RECORDING the hands: %d frames (K ends it)"), Recording->NumFrames()));
	}
	if (!LastAction.IsEmpty())
	{
		OutLines.Add(FString::Printf(TEXT("Last: %s"), *LastAction));
	}
	if (ExtraHudLines)
	{
		ExtraHudLines(OutLines);
	}
	OutLines.Add(TEXT("1 2 3 tool | buttons use | G open hands | F kick | wheel adjust | P pause | O step | T slow | R reset | C free camera | V overlays | K record | N new take | Esc quit"));
}

void ASandboxPawn::OnMove(const FInputActionValue& Value)
{
	MoveInput = Value.Get<FVector>();
}

void ASandboxPawn::OnLook(const FInputActionValue& Value)
{
	LookInput += Value.Get<FVector2D>();
}

void ASandboxPawn::OnWheel(const FInputActionValue& Value)
{
	AdjustTool(Value.Get<float>());
}

void ASandboxPawn::OnFast(const FInputActionValue& Value)
{
	bFast = Value.Get<bool>();
}

void ASandboxPawn::OnQuit()
{
	// A recording ends here, while the world is whole, and not as it is taken apart.
	StopRecording();
	if (APlayerController* PlayerController = Cast<APlayerController>(GetController()))
	{
		PlayerController->ConsoleCommand(TEXT("quit"));
	}
}

void ASandboxPawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	UEnhancedInputComponent* Input = Cast<UEnhancedInputComponent>(PlayerInputComponent);
	const APlayerController* PlayerController = Cast<APlayerController>(GetController());
	UEnhancedInputLocalPlayerSubsystem* Subsystem = PlayerController ? ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PlayerController->GetLocalPlayer()) : nullptr;
	if (!Input || !Subsystem)
	{
		UE_LOG(LogSandbox, Error, TEXT("The pawn has no input: the project must use the Enhanced Input component (Config/DefaultInput.ini)"));
		return;
	}

	// Actions and their keys are made here, in code: the project has no input assets.
	MappingContext = NewObject<UInputMappingContext>(this, TEXT("SandboxDesktopInput"));
	Actions.Reset();
	const auto MakeAction = [this](const TCHAR* Name, EInputActionValueType Type)
	{
		UInputAction* Action = NewObject<UInputAction>(this, Name);
		Action->ValueType = Type;
		// The camera flies and the tools work while the world is paused.
		Action->bTriggerWhenPaused = true;
		Actions.Add(Action);
		return Action;
	};
	// A key's value arrives on the X axis; a swizzle moves it to another axis and a negation turns it round.
	const auto Map = [this](UInputAction* Action, const FKey& Key, TOptional<EInputAxisSwizzle> Swizzle = TOptional<EInputAxisSwizzle>(), bool bNegate = false)
	{
		FEnhancedActionKeyMapping& Mapping = MappingContext->MapKey(Action, Key);
		if (Swizzle.IsSet())
		{
			UInputModifierSwizzleAxis* Modifier = NewObject<UInputModifierSwizzleAxis>(MappingContext);
			Modifier->Order = Swizzle.GetValue();
			Mapping.Modifiers.Add(Modifier);
		}
		if (bNegate)
		{
			Mapping.Modifiers.Add(NewObject<UInputModifierNegate>(MappingContext));
		}
	};
	const auto MakeKey = [&](const TCHAR* Name, const FKey& Key)
	{
		UInputAction* Action = MakeAction(Name, EInputActionValueType::Boolean);
		Map(Action, Key);
		return Action;
	};

	UInputAction* Move = MakeAction(TEXT("Move"), EInputActionValueType::Axis3D);
	Map(Move, EKeys::W);
	Map(Move, EKeys::S, TOptional<EInputAxisSwizzle>(), /*bNegate*/ true);
	Map(Move, EKeys::D, EInputAxisSwizzle::YXZ);
	Map(Move, EKeys::A, EInputAxisSwizzle::YXZ, /*bNegate*/ true);
	Map(Move, EKeys::E, EInputAxisSwizzle::ZYX);
	Map(Move, EKeys::Q, EInputAxisSwizzle::ZYX, /*bNegate*/ true);
	Input->BindAction(Move, ETriggerEvent::Triggered, this, &ASandboxPawn::OnMove);

	UInputAction* Look = MakeAction(TEXT("Look"), EInputActionValueType::Axis2D);
	Map(Look, EKeys::Mouse2D);
	Input->BindAction(Look, ETriggerEvent::Triggered, this, &ASandboxPawn::OnLook);

	UInputAction* Wheel = MakeAction(TEXT("Wheel"), EInputActionValueType::Axis1D);
	Map(Wheel, EKeys::MouseWheelAxis);
	Input->BindAction(Wheel, ETriggerEvent::Triggered, this, &ASandboxPawn::OnWheel);

	UInputAction* Fast = MakeKey(TEXT("Fast"), EKeys::LeftShift);
	Input->BindAction(Fast, ETriggerEvent::Started, this, &ASandboxPawn::OnFast);
	Input->BindAction(Fast, ETriggerEvent::Completed, this, &ASandboxPawn::OnFast);

	UInputAction* Primary = MakeKey(TEXT("Primary"), EKeys::LeftMouseButton);
	Input->BindAction(Primary, ETriggerEvent::Started, this, &ASandboxPawn::OnPrimaryDown);
	Input->BindAction(Primary, ETriggerEvent::Completed, this, &ASandboxPawn::OnPrimaryUp);
	UInputAction* Secondary = MakeKey(TEXT("Secondary"), EKeys::RightMouseButton);
	Input->BindAction(Secondary, ETriggerEvent::Started, this, &ASandboxPawn::OnSecondaryDown);
	Input->BindAction(Secondary, ETriggerEvent::Completed, this, &ASandboxPawn::OnSecondaryUp);
	Input->BindAction(MakeKey(TEXT("Kick"), EKeys::F), ETriggerEvent::Started, this, &ASandboxPawn::Kick);
	UInputAction* OpenHands = MakeKey(TEXT("OpenHands"), EKeys::G);
	Input->BindAction(OpenHands, ETriggerEvent::Started, this, &ASandboxPawn::OnOpenHandsDown);
	Input->BindAction(OpenHands, ETriggerEvent::Completed, this, &ASandboxPawn::OnOpenHandsUp);

	Input->BindAction(MakeKey(TEXT("ToolHands"), EKeys::One), ETriggerEvent::Started, this, &ASandboxPawn::OnToolHands);
	Input->BindAction(MakeKey(TEXT("ToolImpulse"), EKeys::Two), ETriggerEvent::Started, this, &ASandboxPawn::OnToolImpulse);
	Input->BindAction(MakeKey(TEXT("ToolDrag"), EKeys::Three), ETriggerEvent::Started, this, &ASandboxPawn::OnToolDrag);
	Input->BindAction(MakeKey(TEXT("Pause"), EKeys::P), ETriggerEvent::Started, this, &ASandboxPawn::TogglePause);
	Input->BindAction(MakeKey(TEXT("Step"), EKeys::O), ETriggerEvent::Started, this, &ASandboxPawn::StepOnce);
	Input->BindAction(MakeKey(TEXT("Slow"), EKeys::T), ETriggerEvent::Started, this, &ASandboxPawn::ToggleSlowMotion);
	Input->BindAction(MakeKey(TEXT("Reset"), EKeys::R), ETriggerEvent::Started, this, &ASandboxPawn::RequestReset);
	Input->BindAction(MakeKey(TEXT("FreeCamera"), EKeys::C), ETriggerEvent::Started, this, &ASandboxPawn::ToggleCameraDetached);
	Input->BindAction(MakeKey(TEXT("Overlays"), EKeys::V), ETriggerEvent::Started, this, &ASandboxPawn::ToggleOverlays);
	Input->BindAction(MakeKey(TEXT("Record"), EKeys::K), ETriggerEvent::Started, this, &ASandboxPawn::ToggleRecording);
	Input->BindAction(MakeKey(TEXT("NewTake"), EKeys::N), ETriggerEvent::Started, this, &ASandboxPawn::RequestNewTake);
	Input->BindAction(MakeKey(TEXT("Quit"), EKeys::Escape), ETriggerEvent::Started, this, &ASandboxPawn::OnQuit);

	Subsystem->AddMappingContext(MappingContext, 0);
}
