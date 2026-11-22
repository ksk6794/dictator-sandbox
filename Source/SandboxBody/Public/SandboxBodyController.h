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

/**
 * Turns a fully simulated skeletal mesh into an active ragdoll.
 *
 *   Muscles         one parent-space control per joint of the physics asset: an angular spring that turns
 *                   the child body toward the animation pose (the reference pose while no animation plays).
 *   Balance assist  world-space controls on pelvis, chest, head and both feet, with targets captured from
 *                   the pose the body was started in. Balance level 0: the assist alone keeps it upright.
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

	bool IsStarted() const { return bStarted; }
	/** The values the controls have now. */
	const FSandboxBodyTuning& GetTuning() const { return Tuning; }
	/** How many times the tuning has been replaced by different values since Start. */
	int32 GetTuningChangeCount() const { return TuningChangeCount; }
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
	TArray<FMuscle> Muscles;
	TArray<FAssist> Assists;
	FSandboxAssistLoad AssistLoad;
	double WeightN = 0.0;
	double StandingComHeightM = 0.0;
	bool bStarted = false;
};
