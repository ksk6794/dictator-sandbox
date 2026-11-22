#include "SandboxPawn.h"

#include "Camera/CameraComponent.h"
#include "Components/PrimitiveComponent.h"
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

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->bUsePawnControlRotation = false;
	SetRootComponent(Camera);

	// The pawn turns itself from the mouse; the controller's rotation is not used.
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;
	SetCanBeDamaged(false);
}

FTransform ASandboxPawn::ViewTransform() const
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

	FString Error;
	Rig = GetWorld()->SpawnActor<ASandboxEffectorRig>();
	if (Rig && Rig->Start(FSandboxParamStore::Get(), Error) &&
		Rig->AddEffector(SandboxEffectors::HandLeft, FSandboxDesktopTargetSource::RestPose(HandAnchor, /*bLeft*/ true, Tuning.Tools), Error) &&
		Rig->AddEffector(SandboxEffectors::HandRight, FSandboxDesktopTargetSource::RestPose(HandAnchor, /*bLeft*/ false, Tuning.Tools), Error))
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

	// The camera: turned by the mouse, moved along its own axes, and straight up and down.
	FRotator Rotation = GetActorRotation();
	Rotation.Yaw += LookInput.X * Tuning.Tools.CameraLookDegPerCount;
	Rotation.Pitch = FMath::Clamp(Rotation.Pitch + LookInput.Y * Tuning.Tools.CameraLookDegPerCount, -MaxPitchDeg, MaxPitchDeg);
	Rotation.Roll = 0.0;
	const double StepCm = Tuning.Tools.CameraSpeedMps * 100.0 * (bFast ? Tuning.Tools.CameraFastFactor : 1.0) * DeltaTime;
	const FVector Location = GetActorLocation() + (Rotation.RotateVector(FVector(MoveInput.X, MoveInput.Y, 0.0)) + FVector(0.0, 0.0, MoveInput.Z)) * StepCm;
	SetActorLocationAndRotation(Location, Rotation);
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
	if (bStepping && Rig && HandSource)
	{
		const bool bHands = Tool == ESandboxTool::Hands;
		HandSource->Update(HandAnchor, bHands && bSecondaryDown, bHands && bPrimaryDown, Tuning.Tools, ReachCm, DeltaTime, bSteppedLastFrame);
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
	Tool = InTool;
	LastAction = FString::Printf(TEXT("tool: %s"), ToolName(Tool));
}

void ASandboxPawn::SetPrimary(bool bDown)
{
	bPrimaryDown = bDown;
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

void ASandboxPawn::SetView(const FVector& Location, const FRotator& Rotation)
{
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
	if (!LastAction.IsEmpty())
	{
		OutLines.Add(FString::Printf(TEXT("Last: %s"), *LastAction));
	}
	if (ExtraHudLines)
	{
		ExtraHudLines(OutLines);
	}
	OutLines.Add(TEXT("1 2 3 tool | buttons use | wheel adjust | P pause | O step | T slow | R reset | C free camera | Esc quit"));
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

	Input->BindAction(MakeKey(TEXT("ToolHands"), EKeys::One), ETriggerEvent::Started, this, &ASandboxPawn::OnToolHands);
	Input->BindAction(MakeKey(TEXT("ToolImpulse"), EKeys::Two), ETriggerEvent::Started, this, &ASandboxPawn::OnToolImpulse);
	Input->BindAction(MakeKey(TEXT("ToolDrag"), EKeys::Three), ETriggerEvent::Started, this, &ASandboxPawn::OnToolDrag);
	Input->BindAction(MakeKey(TEXT("Pause"), EKeys::P), ETriggerEvent::Started, this, &ASandboxPawn::TogglePause);
	Input->BindAction(MakeKey(TEXT("Step"), EKeys::O), ETriggerEvent::Started, this, &ASandboxPawn::StepOnce);
	Input->BindAction(MakeKey(TEXT("Slow"), EKeys::T), ETriggerEvent::Started, this, &ASandboxPawn::ToggleSlowMotion);
	Input->BindAction(MakeKey(TEXT("Reset"), EKeys::R), ETriggerEvent::Started, this, &ASandboxPawn::RequestReset);
	Input->BindAction(MakeKey(TEXT("FreeCamera"), EKeys::C), ETriggerEvent::Started, this, &ASandboxPawn::ToggleCameraDetached);
	Input->BindAction(MakeKey(TEXT("Quit"), EKeys::Escape), ETriggerEvent::Started, this, &ASandboxPawn::OnQuit);

	Subsystem->AddMappingContext(MappingContext, 0);
}
