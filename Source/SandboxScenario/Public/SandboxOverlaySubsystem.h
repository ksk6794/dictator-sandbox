#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Subsystems/WorldSubsystem.h"
#include "SandboxOverlaySubsystem.generated.h"

class AHUD;
class ASandboxEffectorRig;
class ASandboxRagdollActor;
class ULineBatchComponent;
class UPrimitiveComponent;

/** What the debug overlays can show. A set of layers is a combination of these bits. */
namespace SandboxOverlay
{
	enum ELayer : uint32
	{
		/** Centre of mass, its drop to the floor, and whether that lies inside the support polygon. */
		Com = 1 << 0,
		/** Support polygon: the hull of the soles of the feet that stand on something. */
		Support = 1 << 1,
		/** Per joint, where the muscle turns its body to, drawn against where the body is. */
		Joints = 1 << 2,
		/** Balance-assist targets against the bodies they hold. */
		Assist = 1 << 3,
		/** Contacts of the last physics step, as arrows the length of their force. */
		Contacts = 1 << 4,
		/** The controller's state above the head, and a block of figures on the screen. */
		State = 1 << 5,
		/** The hands and the drag handle: target against body, and how hard the spring pulls. */
		Hands = 1 << 6,

		None = 0,
		All = Com | Support | Joints | Assist | Contacts | State | Hands
	};

	/**
	 * Turns the argument of the console command into a set of layers: "all", "off" (or "none"), "toggle"
	 * (off if anything is on, else all), or names joined by commas or spaces, "com,support,joints". A name
	 * with a minus in front takes that layer away from what is on: "-joints". False with a reason for a name
	 * that is not a layer.
	 */
	SANDBOXSCENARIO_API bool ParseLayers(const FString& Text, uint32 Current, uint32& OutLayers, FString& OutError);

	/** "com support joints", or "off". */
	SANDBOXSCENARIO_API FString LayerNames(uint32 Layers);
}

/** A line of text the overlay puts on the screen, at a place in the world or in the corner block. */
struct FSandboxOverlayLabel
{
	FString Text;
	FLinearColor Color = FLinearColor::White;
	/** World-space anchor; unused for lines of the corner block. */
	FVector Location = FVector::ZeroVector;
	bool bInWorld = false;
};

/**
 * What the overlay draws with. It lives in the world only while overlays are on: a line batch of its own,
 * emptied and filled again every frame (the world's debug lines pile up while the game is paused), text
 * through the HUD's actor overlays, which draw on the canvas that screenshots capture, and the listener for
 * the contact events of the bodies being watched.
 */
UCLASS(NotPlaceable, Transient)
class SANDBOXSCENARIO_API ASandboxOverlayActor : public AActor
{
	GENERATED_BODY()

public:
	ASandboxOverlayActor();

	virtual void PostRenderFor(APlayerController* PlayerController, UCanvas* Canvas, FVector CameraPosition, FVector CameraDir) override;

	/** One contact of a watched body, as its hit event reported it. */
	struct FContact
	{
		FVector Location = FVector::ZeroVector;
		/** Unit vector out of the watched body's surface... */
		FVector Normal = FVector::UpVector;
		/** ...and the impulse along it, N*s, over one substep. */
		double ImpulseNs = 0.0;
		/** The other body does not move under physics: the floor, a wall. */
		bool bStatic = false;
		FName Bone;
	};

	UFUNCTION()
	void OnWatchedHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComponent, FVector NormalImpulse, const FHitResult& Hit);

	UPROPERTY()
	TObjectPtr<ULineBatchComponent> Lines;

	/** Contacts reported since the list was last taken by the subsystem. */
	TArray<FContact> PendingContacts;
	/** A hand's contact with the character is reported by both; the character's report is the one kept. */
	TWeakObjectPtr<UPrimitiveComponent> CharacterMesh;

	TArray<FSandboxOverlayLabel> Labels;
};

/**
 * Debug overlays (FR-13): centre of mass, support polygon, joint drive targets against the actual pose,
 * balance-assist targets, contacts, the controller's state, the hands.
 *
 *   Sandbox.Overlay all | off | toggle | com,support,joints,assist,contacts,state,hands | -joints
 *   -Overlay=<the same>     on the command line of a scenario run or a play session
 *
 * The overlay only looks. It finds the character and the hands in the world each frame, reads their state
 * through the same accessors result files use, listens to their contact events, and draws. It writes
 * nothing back, so a run is the same with it and without it, and since there is nothing to look at without
 * a renderer it does nothing at all there: it does not tick, listens to nothing, and the command only says
 * so in the log.
 *
 * It lives in SandboxScenario because it needs both the character (SandboxBody) and the hands
 * (SandboxInteraction), and this is the one module that may see both.
 */
UCLASS()
class SANDBOXSCENARIO_API USandboxOverlaySubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickable() const override { return Layers != 0; }
	virtual bool IsTickableWhenPaused() const override { return true; }
	virtual TStatId GetStatId() const override;

	/** Switches layers on and off. Without a renderer the layers stay off, whatever is asked for. */
	void SetLayers(uint32 InLayers);
	uint32 GetLayers() const { return Layers; }

	/** How much the last frame drew, for the session's self-test: line segments and lines of text. */
	int32 GetDrawnLineCount() const { return DrawnLines; }
	int32 GetDrawnLabelCount() const { return DrawnLabels; }

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	void EnsureActor();
	void ReleaseActor();
	void Watch(UPrimitiveComponent* Component);
	void UnwatchAll();
	void AttachToHud();

	void DrawCharacter(const ASandboxRagdollActor& Character);
	void DrawHands(const ASandboxEffectorRig& Rig);
	void DrawDragHandle();
	void DrawContacts();

	void Line(const FVector& From, const FVector& To, const FLinearColor& Color, float Thickness = 1.0f);
	void Cross(const FVector& At, double SizeCm, const FLinearColor& Color, float Thickness = 1.0f);
	void Ring(const FVector& Center, double RadiusCm, const FLinearColor& Color, float Thickness = 1.0f);
	void Arrow(const FVector& From, const FVector& To, const FLinearColor& Color, float Thickness = 1.0f);
	void WorldLabel(const FVector& At, const FString& Text, const FLinearColor& Color);
	void CornerLabel(const FString& Text, const FLinearColor& Color = FLinearColor::White);

	uint32 Layers = 0;
	int32 DrawnLines = 0;
	int32 DrawnLabels = 0;

	UPROPERTY()
	TObjectPtr<ASandboxOverlayActor> Actor;

	TArray<TWeakObjectPtr<UPrimitiveComponent>> Watched;
	TWeakObjectPtr<AHUD> Hud;
	bool bHudShowedOverlays = false;

	/** Contacts of the last physics step that was made, kept while the world is paused. */
	TArray<ASandboxOverlayActor::FContact> ShownContacts;
	double LastWorldTimeS = -1.0;
	/** The frame interval of the last step that was made: turns a contact's impulse into a force. */
	double StepDtS = 0.0;
};
