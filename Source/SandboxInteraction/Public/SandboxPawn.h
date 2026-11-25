#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "SandboxDragHandle.h"
#include "SandboxInteractionTuning.h"
#include "SandboxPawn.generated.h"

class ASandboxEffectorRig;
class FSandboxDesktopTargetSource;
class FSandboxTrackedTargetSource;
class FSandboxXRControllers;
struct FSandboxTargetRecording;
class UCameraComponent;
class USceneComponent;
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
 *   K                record the hands / end    N           a new take: character back, a new recording
 *
 * Input is made in C++ when the pawn is possessed; there are no input assets. Every key calls one of the
 * public functions below, which a test can call just as well.
 *
 * The hands are effectors: bodies with mass on springs with a force cap (ASandboxEffectorRig). The pawn
 * only says where they should be, through a target source that reads the camera.
 *
 * In a headset (FSandboxXRControllers::IsHeadsetOn when play begins) the same pawn is the middle of the
 * player's play space: it stands on the floor, level, and does not fly or turn with the mouse; the camera is
 * the head, moved by the engine; and the hands' target source is the tracked one, which follows the
 * controllers (FSandboxTrackedTargetSource). The hands, the rig, the recording and the time control are the
 * same. Walking is M4 part 5.
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
	/** The desktop's kick (the F key): the foot goes to what the camera aims at, or straight ahead, and back. */
	void Kick();
	/**
	 * The desktop's hands held open (the G key) or closed, as they are otherwise. An open hand that closes
	 * within reach of a body takes hold of it where hands can grab (effectors.hand.grab).
	 */
	void SetHandsOpen(bool bOpen) { bHandsOpen = bOpen; }
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
	/**
	 * A new take: a recording that runs ends, the character is put back as it started (a request, as the reset
	 * is), the hands are made anew on their targets and a new recording begins. Each take's recording so
	 * begins with a fresh body and fresh hands at rest, which is how the scenario that replays it begins. In a
	 * headset: both fists raised above the eyes (tools.headset.new_take_raise_cm) with both grips held closed
	 * for tools.headset.new_take_hold_s; the controllers buzz when the take begins.
	 *
	 * It takes two frames. In the pawn's next stepped tick the recording that runs ends and the reset is asked
	 * for; whoever carries the reset out does so after that frame's physics and then calls BeginTake, so that
	 * the fresh body's first step is the next frame's, the step the new recording's first targets are for.
	 */
	void RequestNewTake();
	/** True from the frame a new take has asked for its reset until BeginTake. */
	bool IsTakeWaiting() const { return bTakeWaitsForReset; }
	/** The character has been put back: the take that waited for it begins with the next stepped frame. */
	void BeginTake();
	int32 GetTakes() const { return Takes; }
	bool IsRecording() const { return Recording.IsValid(); }

	/**
	 * Puts the camera somewhere, looking along a rotation. The hands follow unless they are detached. In a
	 * headset it is the middle of the play space that is put there, level: give it a point of the floor.
	 */
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
	/** The source that follows the headset's controllers; null on the desktop. */
	const FSandboxTrackedTargetSource* GetTrackedSource() const { return TrackedSource.Get(); }
	bool IsInHeadset() const { return TrackedSource.IsValid(); }
	const FSandboxDragHandle& GetDragHandle() const { return DragHandle; }
	const FSandboxInteractionTuning& GetTuning() const { return Tuning; }
	void GetHudLines(TArray<FString>& OutLines) const;

	/** What the line of sight hits first: the world or a simulated body, never the pawn's own hands. */
	bool TraceAim(FHitResult& OutHit) const;

private:
	void ReadTuning();
	void UpdateTimeControl();
	/** The camera's pose in the world: the pawn's own on the desktop, the head's in a headset. */
	FTransform ViewTransform() const;
	/** The pawn's own pose: in a headset the origin of the play space, on the floor. */
	FTransform OriginTransform() const;
	/** Says in the log when a controller is first tracked, lost and found again. */
	void AnnounceTracking();
	/** In the first stepped tick of a take: the hands are made anew where their source wants them. */
	void RemakeHandsForTake();
	void LogTrackingSummary() const;

	void OnMove(const FInputActionValue& Value);
	void OnLook(const FInputActionValue& Value);
	void OnWheel(const FInputActionValue& Value);
	void OnFast(const FInputActionValue& Value);
	void OnOpenHandsDown() { SetHandsOpen(true); }
	void OnOpenHandsUp() { SetHandsOpen(false); }
	void OnPrimaryDown() { SetPrimary(true); }
	void OnPrimaryUp() { SetPrimary(false); }
	void OnSecondaryDown() { SetSecondary(true); }
	void OnSecondaryUp() { SetSecondary(false); }
	void OnToolHands() { SelectTool(ESandboxTool::Hands); }
	void OnToolImpulse() { SelectTool(ESandboxTool::Impulse); }
	void OnToolDrag() { SelectTool(ESandboxTool::Drag); }
	void OnQuit();

	/**
	 * The pawn's root. The camera hangs from it and does not leave it on the desktop; in a headset the engine
	 * moves the camera under it as the head moves, which is why the camera cannot be the root itself.
	 */
	UPROPERTY(VisibleAnywhere, Category = "Sandbox")
	TObjectPtr<USceneComponent> Origin;

	UPROPERTY(VisibleAnywhere, Category = "Sandbox")
	TObjectPtr<UCameraComponent> Camera;

	UPROPERTY()
	TObjectPtr<ASandboxEffectorRig> Rig;

	UPROPERTY()
	TObjectPtr<UInputMappingContext> MappingContext;

	UPROPERTY()
	TArray<TObjectPtr<UInputAction>> Actions;

	/** The hands' source: one of the two. */
	TSharedPtr<FSandboxDesktopTargetSource> HandSource;
	TSharedPtr<FSandboxTrackedTargetSource> TrackedSource;
	TSharedPtr<FSandboxXRControllers> Controllers;
	/** Whether each hand's controller was tracked at the last look, and how often a change has been logged. */
	bool bWasTracked[2] = { false, false };
	int32 TrackingAnnouncements = 0;
	FSandboxDragHandle DragHandle;
	/** The recording being made, if any, and the pawn's count of stepped frames when it began. */
	TSharedPtr<FSandboxTargetRecording> Recording;
	int64 RecordingFirstFrame = 0;
	/** A new take asked for and not yet begun, the takes begun, and the headset's gesture for one. */
	bool bNewTakePending = false;
	bool bTakeWaitsForReset = false;
	bool bPlaceHands = false;
	int32 Takes = 0;
	double GripsHeldS = 0.0;
	bool bGripsArmed = true;
	/** Seconds the take's vibration still has to run. */
	double BuzzLeftS = 0.0;
	/** What each controller was last told to vibrate at, left and right. */
	float LastHaptic[2] = { 0.0f, 0.0f };

	FSandboxInteractionTuning Tuning;
	uint32 TuningRevision = 0;
	bool bTuningRead = false;

	ESandboxTool Tool = ESandboxTool::Hands;
	bool bPrimaryDown = false;
	bool bSecondaryDown = false;
	bool bHandsOpen = false;
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
