#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SandboxInteractionTuning.h"
#include "SandboxTargetSource.h"
#include "SandboxEffectorRig.generated.h"

class FSandboxParamStore;
class USandboxEffectorComponent;
struct FSandboxTargetRecording;

/**
 * The player's physical presence: a set of effectors and the source that says where they should be. The
 * desktop pawn owns one and gives it a source that reads the camera and the mouse; a scenario owns one and
 * gives it the trajectories of its file, or a recording; in a headset the pawn gives it the tracked controllers.
 *
 * The rig does not tick. Its owner calls UpdateTargets once per frame before the physics step, which keeps
 * the order of work the owner's decision and the same in every run.
 */
UCLASS()
class SANDBOXINTERACTION_API ASandboxEffectorRig : public AActor
{
	GENERATED_BODY()

public:
	ASandboxEffectorRig();

	/**
	 * Reads the effectors' tuning from the store and keeps following it: an edited file or an override
	 * reaches the effectors with the next UpdateTargets. The store must outlive the rig.
	 */
	bool Start(const FSandboxParamStore& InParams, FString& OutError);

	/** Adds one effector ("hand_left", "hand_right") at a pose. Null with a reason if the name is unknown. */
	USandboxEffectorComponent* AddEffector(FName Id, const FTransform& Pose, FString& OutError);

	/**
	 * Destroys the effectors and makes them again, in their order, each where its source now wants it (or on
	 * the target it had), at rest and with nothing to its name. A take begins so: the hands of a scenario are
	 * made after its character, and bodies made in another order are solved in another order, which showed as
	 * a hand's impulse 2% off between a take and its replay.
	 */
	bool RemakeEffectors(FString& OutError);

	void SetSource(TSharedPtr<const ISandboxTargetSource> InSource) { Source = MoveTemp(InSource); }

	/** Asks the source where each effector should be and updates the effectors. */
	void UpdateTargets(float DeltaTime);

	/**
	 * From the next UpdateTargets on, the target each effector is given is added to the recording, a frame a
	 * call; null ends it. A recording that names no effectors yet gets the rig's. False with a reason if it
	 * names others than the rig has, in another order.
	 */
	bool SetRecording(TSharedPtr<FSandboxTargetRecording> InRecording, FString& OutError);
	bool IsRecording() const { return Recording.IsValid(); }

	const TArray<TObjectPtr<USandboxEffectorComponent>>& GetEffectors() const { return Effectors; }
	USandboxEffectorComponent* FindEffector(FName Id) const;
	const FSandboxInteractionTuning& GetTuning() const { return Tuning; }
	/** How many times an edited file or an override changed the values. */
	int32 GetTuningChangeCount() const { return TuningChangeCount; }

private:
	UPROPERTY()
	TArray<TObjectPtr<USandboxEffectorComponent>> Effectors;

	TSharedPtr<const ISandboxTargetSource> Source;
	TSharedPtr<FSandboxTargetRecording> Recording;
	FSandboxInteractionTuning Tuning;
	const FSandboxParamStore* Params = nullptr;
	uint32 ParamsRevision = 0;
	int32 TuningChangeCount = 0;
};
