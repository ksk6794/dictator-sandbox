#pragma once

#include "CoreMinimal.h"
#include "Components/SphereComponent.h"
#include "SandboxInteractionTuning.h"
#include "SandboxTargetSource.h"
#include "SandboxWorldSpring.h"
#include "SandboxEffectorComponent.generated.h"

class UStaticMeshComponent;

/** What one effector has done since it was started. For result files and the HUD. */
struct FSandboxEffectorStats
{
	/** Sum of the sizes of all contact impulses on the effector, N*s. */
	double ContactImpulseNs = 0.0;
	/**
	 * Largest net contact impulse received within one frame, N*s. Divided by the frame time it is a force,
	 * which may well exceed the spring's cap: a moving hand that is stopped gives up its momentum too.
	 */
	double MaxFrameImpulseNs = 0.0;
	/** Net contact impulse of the last completed physics step, N*s: what the hand is pressing with now. */
	double LastFrameImpulseNs = 0.0;
	/** Frames in which the effector touched something. */
	int32 ContactFrames = 0;
	/** Contact impulse by what was touched: the bone of a skeletal body, the actor's name otherwise. N*s. */
	TMap<FName, double> ImpulseByBody;
	/** Furthest the body was from its target, cm, and the fastest it moved, cm/s. */
	double MaxGapCm = 0.0;
	double MaxSpeedCmS = 0.0;
	/** Largest force the spring law asked for before the cap, N. */
	double MaxDemandN = 0.0;
	/** Times the body was put back on its target because it was further away than the snap distance. */
	int32 Snaps = 0;
	/**
	 * The first thing the effector ever touched (named as in ImpulseByBody), and how fast the effector was
	 * moving at the start of the physics step in which that happened, cm/s: the speed a strike arrived with.
	 */
	FName FirstTouched;
	double FirstContactSpeedCmS = 0.0;
};

/**
 * One of the player's hands as the physics world sees it: a sphere with real mass, pulled toward a target
 * pose by a spring with a force cap (FSandboxWorldSpring). It is never kinematic: a kinematic hand has
 * infinite mass and launches whatever it touches. What it touches it knows nothing about; the other body
 * feels the contact through its own contact events.
 *
 * No gravity acts on it: the arm that would carry the hand is not simulated, and its strength, the force
 * cap, should not be spent on the hand's own weight. Continuous collision detection is on.
 *
 * The collision shape is the sphere itself; a mesh of the same size is attached so that it can be seen.
 *
 * The owner gives it a target and calls Update once per frame, before the physics step.
 */
UCLASS()
class SANDBOXINTERACTION_API USandboxEffectorComponent : public USphereComponent
{
	GENERATED_BODY()

public:
	USandboxEffectorComponent();

	/**
	 * Makes the body at Pose and ties it to its spring. Call on a registered component. False with a reason
	 * if the sphere mesh cannot be loaded or no physics body was created.
	 */
	bool Start(FName InId, const FSandboxEffectorTuning& InTuning, const FTransform& Pose, FString& OutError);

	/** New mass, spring and caps. The radius cannot change after Start. */
	void ApplyTuning(const FSandboxEffectorTuning& InTuning);

	/** The pose to pull toward from the next Update on. */
	void SetTarget(const FSandboxEffectorTarget& InTarget) { Target = InTarget; }
	const FSandboxEffectorTarget& GetTarget() const { return Target; }

	/** Once per frame, before the physics step: hands the target to the spring and keeps the record. */
	void Update(float DeltaTime);

	FName GetId() const { return Id; }
	bool IsStarted() const { return bStarted; }
	const FSandboxEffectorTuning& GetTuning() const { return Tuning; }
	const FSandboxEffectorStats& GetStats() const { return Stats; }
	/** Largest net contact force over one frame so far, N. */
	double GetMaxContactForceN() const { return FrameDt > 0.0 ? Stats.MaxFrameImpulseNs / FrameDt : 0.0; }
	/** Net contact force over the last completed physics step, N. A hand held against something reads its force cap. */
	double GetContactForceN() const { return FrameDt > 0.0 ? Stats.LastFrameImpulseNs / FrameDt : 0.0; }
	/** Distance from the body to its target now, cm. */
	double GetGapCm() const { return Spring.GetGapCm(); }
	/** The force the spring law asked for at the last Update, before its cap, N. */
	double GetDemandN() const { return Spring.GetDemandN(); }

	/** The spring lets go here, while the body it holds still exists. */
	virtual void OnDestroyPhysicsState() override;

private:
	UFUNCTION()
	void OnEffectorHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComponent, FVector NormalImpulse, const FHitResult& Hit);

	/** What is seen of the hand: a sphere mesh without collision that rides on the body. */
	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Visual;

	FName Id;
	FSandboxEffectorTuning Tuning;
	FSandboxEffectorTarget Target;
	FSandboxWorldSpring Spring;
	FSandboxEffectorStats Stats;
	bool bStarted = false;

	/** Frames counted by Update, and the frame the contact impulses in FrameImpulseNs belong to. */
	int64 Frame = 0;
	int64 ImpulseFrame = -1;
	FVector FrameImpulseNs = FVector::ZeroVector;
	double FrameDt = 0.0;
	/** Speed of the body at the last Update: at the start of the step whose contacts are reported next. */
	double StepStartSpeedCmS = 0.0;
};
