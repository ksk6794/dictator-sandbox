#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "PhysicsEngine/ConstraintInstance.h"
#include "SandboxBodyTuning.h"
#include "SandboxBodyController.generated.h"

class FSandboxParamStore;
class UPhysicsControlComponent;
class USkeletalMeshComponent;

/**
 * How hard the balance assist is working at one instant, estimated from the assist springs' settings and
 * the bodies' distance from their targets. Both values are sums over all assist bodies.
 */
struct FSandboxAssistLoad
{
	/** Commanded linear force as a fraction of the character's weight. */
	double ForceShare = 0.0;
	/** Commanded torque as a fraction of weight times standing centre-of-mass height. */
	double TorqueShare = 0.0;
};

/** One change of the body's state, for result files and overlays. */
struct FSandboxStateChange
{
	/** Simulated time since the controller started, at the beginning of the first step in the new state. */
	double TimeS = 0.0;
	ESandboxBodyState State = ESandboxBodyState::Standing;
	/** What caused it, in a few words. */
	FString Reason;
};

/**
 * Turns a fully simulated skeletal mesh into an active ragdoll.
 *
 *   Muscles         one parent-space control per joint of the physics asset: an angular spring that turns
 *                   the child body toward the animation pose (the reference pose while no animation plays).
 *   Balance assist  world-space controls on pelvis, chest, head and both feet, with targets captured from
 *                   the pose the body was started in. Balance level 0: the assist alone keeps it upright.
 *   States          Standing, Flinch, Falling, Lying. Each state reads its own tuning ("states.falling...");
 *                   the thresholds that move the body between them are tuning values too.
 *
 *                     Standing --hit--> Flinch --recovered--> Standing
 *                     Standing or Flinch --pelvis too far, too low or too tilted--> Falling
 *                     Falling --at rest for a while--> Lying
 *
 * All bodies stay dynamic. The controller owns a UPhysicsControlComponent and drives it by hand each tick,
 * after the mesh has evaluated its pose and before physics, so the order of work is the same in every run.
 */
UCLASS()
class SANDBOXBODY_API USandboxBodyController : public UActorComponent
{
	GENERATED_BODY()

public:
	USandboxBodyController();

	/**
	 * Creates the controls on a mesh whose bodies are already simulating. The mesh's current pose becomes
	 * the standing target. The tuning is read from the store, and the controller keeps following it: when a
	 * value changes there (an edited file, an override), the controls get the new values on the next tick.
	 * The store must outlive the controller. Returns false with a reason if the tuning cannot be read or a
	 * rig bone has no body.
	 */
	bool Start(USkeletalMeshComponent* InMesh, const FSandboxParamStore& InParams, FString& OutError);

	/** Replaces strengths and damping of the existing controls. The rig cannot change after Start. */
	bool ApplyTuning(const FSandboxBodyTuning& InTuning, FString& OutError);

	/**
	 * A blow to one body: applies the impulse (world space, N*s) and lets the character feel it. A standing
	 * character flinches if the impulse reaches thresholds.flinch_impulse_ns. This is how a scripted hit
	 * enters; hits by other bodies will arrive through contact events.
	 */
	void ApplyImpulse(FName Bone, const FVector& ImpulseNs);

	bool IsStarted() const { return bStarted; }
	/** The values of the current state, as the tuning files and overrides give them. */
	const FSandboxBodyTuning& GetTuning() const { return Tuning; }
	/** How many times an edited file or an override changed the values of the state the body was in. */
	int32 GetTuningChangeCount() const { return TuningChangeCount; }

	ESandboxBodyState GetState() const { return State; }
	/** Every state the body has been in, the first entry being Standing at time 0. */
	const TArray<FSandboxStateChange>& GetStateLog() const { return StateLog; }
	int32 GetMuscleCount() const { return Muscles.Num(); }
	int32 GetAssistCount() const { return Assists.Num(); }

	/** Assist load at the start of the current frame's physics step. */
	const FSandboxAssistLoad& GetAssistLoad() const { return AssistLoad; }

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void OnUnregister() override;

private:
	struct FMuscle
	{
		FName Control;
		ESandboxMuscleGroup Group = ESandboxMuscleGroup::Spine;
	};

	enum class EAssistSlot : uint8 { Pelvis, Chest, Head, Feet };

	struct FAssist
	{
		FName Control;
		FName Bone;
		EAssistSlot Slot = EAssistSlot::Pelvis;
		/** Where the body's bone should be, in world space. */
		FTransform Target = FTransform::Identity;
	};

	const FSandboxAssistTuning& AssistTuning(EAssistSlot Slot) const;
	bool ReadStateTuning(FSandboxBodyTuning& OutTuning, FString& OutError) const;
	void EnterState(ESandboxBodyState NewState, const FString& Reason);
	void UpdateState(float DeltaTime);
	/** Strength of a muscle group right now, as a fraction of its tuning: below 1 only while flinching. */
	double MuscleScale(ESandboxMuscleGroup Group) const;
	void FollowParams();
	void PushTuningToControls();
	void RefreshSolverIsland();
	void ReleaseIslandRefresh();
	void UpdateAssistLoad();

	/**
	 * A constraint that restricts nothing, alive for one frame after the solver iteration counts change.
	 * See RefreshSolverIsland.
	 */
	TUniquePtr<FConstraintInstance> IslandRefresh;

	/** Where the tuning comes from, and the revision of it the controls were last brought up to. */
	const FSandboxParamStore* Params = nullptr;
	uint32 ParamsRevision = 0;
	int32 TuningChangeCount = 0;

	UPROPERTY()
	TObjectPtr<USkeletalMeshComponent> Mesh;

	UPROPERTY()
	TObjectPtr<UPhysicsControlComponent> Control;

	FSandboxBodyTuning Tuning;

	ESandboxBodyState State = ESandboxBodyState::Standing;
	TArray<FSandboxStateChange> StateLog;
	/** Simulated time since Start: the sum of the tick intervals, never the clock. */
	double ElapsedS = 0.0;
	double StateEnteredS = 0.0;
	/** Flinch: the muscle group that was hit. */
	ESandboxMuscleGroup FlinchGroup = ESandboxMuscleGroup::Spine;
	/** Impulses received since HitStartedS, added up: a push delivered over several frames is one hit. */
	FVector HitImpulseNs = FVector::ZeroVector;
	double HitStartedS = 0.0;
	/** Falling: how long every body has been below the rest speeds. */
	double RestS = 0.0;

	TArray<FMuscle> Muscles;
	TArray<FAssist> Assists;
	FSandboxAssistLoad AssistLoad;
	double WeightN = 0.0;
	double StandingComHeightM = 0.0;
	bool bStarted = false;
};
