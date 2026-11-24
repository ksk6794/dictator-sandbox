#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "SandboxDragHandle.h"
#include "SandboxInteractionTuning.h"
#include "SandboxPawn.generated.h"

class ASandboxEffectorRig;
class FSandboxDesktopTargetSource;
struct FSandboxTargetRecording;
class UCameraComponent;
class UInputAction;
class UInputMappingContext;
struct FInputActionValue;

/** What the left mouse button does. */
UENUM()
enum class ESandboxTool : uint8
{
	/** The buttons reach out with the hands: left button the right hand, right button the left. */
	Hands,
	/** A click applies a calibrated impulse to the body under the crosshair, along the line of sight. */
	Impulse,
	/** Holding the button pulls the body under the crosshair with a spring; the wheel changes its length. */
	Drag
};

/**
 * The desktop player: a free-flying camera with two physical hands in front of it and a set of test tools.
 *
 *   Mouse            look                      1 / 2 / 3   tool: hands, impulse, drag handle
 *   W A S D, E / Q   fly, up / down            Left button, right button   use the tool
 *   Left Shift       fly fast                  Wheel       reach of the hands, size of the impulse, length of the handle
 *   C                leave the hands where they are and fly the camera alone; again to take them back
 *   P                pause / resume            O           one step while paused
 *   T                slow motion on / off      R           put the character back on its feet
 *   V                debug overlays on / off   Escape      quit
 *
 * Input is made in C++ when the pawn is possessed; there are no input assets. Every key calls one of the
 * public functions below, which a test can call just as well.
 *
 * The hands are effectors: bodies with mass on springs with a force cap (ASandboxEffectorRig). The pawn
 * only says where they should be, through a target source that reads the camera; VR will replace that
 * source and nothing else.
 *
 * The pawn knows nothing about the character. The drag handle pulls whatever body the crosshair is on. The
 * two tools that need the character itself, the calibrated impulse and the reset, are requests: the pawn
 * aims and announces them, and whoever set the session up carries them out.
 *
 * Time control never changes the physics step. The world either makes its full step or stands still:
 * paused, it stands; in slow motion it steps on one frame in several; a single step is one frame. What
 * slow motion shows is exactly what full speed computes.
 */
UCLASS()
class SANDBOXINTERACTION_API ASandboxPawn : public APawn
{
	GENERATED_BODY()

public:
	ASandboxPawn();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaTime) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

	// What the keys do.

	void SelectTool(ESandboxTool InTool);
	/** The tool's main button, down or up. */
	void SetPrimary(bool bDown);
	/** The second button: the left hand, with the hands tool. */
	void SetSecondary(bool bDown);
	/** One notch of the wheel is 1 or -1. */
	void AdjustTool(float Notches);
	void TogglePause();
	/** Makes the paused world advance by one frame. */
	void StepOnce();
	void ToggleSlowMotion();
	void RequestReset();
	void ToggleCameraDetached();
	/**
	 * Switches the debug overlays on or off. The pawn does not know what draws them: it types the console
	 * command "Sandbox.Overlay toggle", as a person could.
	 */
	void ToggleOverlays();
	/**
	 * Begins to keep what the hands are asked for in every stepped frame, or ends it. A recording that ends is
	 * handed on through OnRecordingStopped; the pawn writes no file. Quitting ends a recording too.
	 */
	void ToggleRecording();
	bool StartRecording();
	void StopRecording();
	bool IsRecording() const { return Recording.IsValid(); }

	/** Puts the camera somewhere, looking along a rotation. The hands follow unless they are detached. */
	void SetView(const FVector& Location, const FRotator& Rotation);

	// Requests the pawn cannot carry out itself.

	/** An impulse of ImpulseNs (world space, N*s) was asked for on what the aim ray hit. */
	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnImpulseRequested, const FHitResult& /*Hit*/, const FVector& /*ImpulseNs*/);
	FOnImpulseRequested OnImpulseRequested;

	/** The character should be put back as it started. The drag handle has let go by the time this is called. */
	FSimpleMulticastDelegate OnResetRequested;

	/**
	 * A recording of the hands has ended. FirstFrame is how many frames the world had stepped, by the pawn's
	 * count (GetSimulatedFrames), before the step the recording's first targets were for.
	 */
	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnRecordingStopped, const TSharedRef<FSandboxTargetRecording>& /*Recording*/, int64 /*FirstFrame*/);
	FOnRecordingStopped OnRecordingStopped;

	/** Lines the session wants on the HUD below the pawn's own (the character's state, for one). */
	TFunction<void(TArray<FString>& /*OutLines*/)> ExtraHudLines;

	// State, for the HUD and for tests.

	ESandboxTool GetTool() const { return Tool; }
	bool IsUserPaused() const { return bUserPaused; }
	bool IsSlowMotion() const { return bSlowMotion; }
	bool IsCameraDetached() const { return bCameraDetached; }
	/** Frames on which the world made a physics step since the pawn began to play. */
	int64 GetSimulatedFrames() const { return SimulatedFrames; }
	double GetImpulseNs() const { return ImpulseNs; }
	double GetReachCm() const { return ReachCm; }
	ASandboxEffectorRig* GetRig() const { return Rig; }
	const FSandboxDragHandle& GetDragHandle() const { return DragHandle; }
	const FSandboxInteractionTuning& GetTuning() const { return Tuning; }
	void GetHudLines(TArray<FString>& OutLines) const;

	/** What the line of sight hits first: the world or a simulated body, never the pawn's own hands. */
	bool TraceAim(FHitResult& OutHit) const;

private:
	void ReadTuning();
	void UpdateTimeControl();
	FTransform ViewTransform() const;

	void OnMove(const FInputActionValue& Value);
	void OnLook(const FInputActionValue& Value);
	void OnWheel(const FInputActionValue& Value);
	void OnFast(const FInputActionValue& Value);
	void OnPrimaryDown() { SetPrimary(true); }
	void OnPrimaryUp() { SetPrimary(false); }
	void OnSecondaryDown() { SetSecondary(true); }
	void OnSecondaryUp() { SetSecondary(false); }
	void OnToolHands() { SelectTool(ESandboxTool::Hands); }
	void OnToolImpulse() { SelectTool(ESandboxTool::Impulse); }
	void OnToolDrag() { SelectTool(ESandboxTool::Drag); }
	void OnQuit();

	UPROPERTY(VisibleAnywhere, Category = "Sandbox")
	TObjectPtr<UCameraComponent> Camera;

	UPROPERTY()
	TObjectPtr<ASandboxEffectorRig> Rig;

	UPROPERTY()
	TObjectPtr<UInputMappingContext> MappingContext;

	UPROPERTY()
	TArray<TObjectPtr<UInputAction>> Actions;

	TSharedPtr<FSandboxDesktopTargetSource> HandSource;
	FSandboxDragHandle DragHandle;
	/** The recording being made, if any, and the pawn's count of stepped frames when it began. */
	TSharedPtr<FSandboxTargetRecording> Recording;
	int64 RecordingFirstFrame = 0;

	FSandboxInteractionTuning Tuning;
	uint32 TuningRevision = 0;
	bool bTuningRead = false;

	ESandboxTool Tool = ESandboxTool::Hands;
	bool bPrimaryDown = false;
	bool bSecondaryDown = false;
	double ImpulseNs = 0.0;
	double ReachCm = 0.0;
	/** Drag handle: how far along the line of sight the held point is pulled to, and where that was last frame. */
	double DragDistanceCm = 0.0;
	FVector LastDragTarget = FVector::ZeroVector;
	FString LastAction;

	// The camera. The hands hang from HandAnchor, which is the camera's pose unless they are detached.
	FVector MoveInput = FVector::ZeroVector;
	FVector2D LookInput = FVector2D::ZeroVector;
	bool bFast = false;
	bool bCameraDetached = false;
	FTransform HandAnchor = FTransform::Identity;

	// Time control.
	bool bUserPaused = false;
	bool bSlowMotion = false;
	int32 StepsPending = 0;
	int32 SlowCounter = 0;
	int64 SimulatedFrames = 0;
	bool bSteppedLastFrame = false;
};
