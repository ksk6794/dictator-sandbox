#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SandboxRagdollActor.generated.h"

class UPhysicsAsset;
class USandboxBodyController;
class USkeletalMesh;
class USkeletalMeshComponent;
class FSandboxParamStore;

/**
 * A skeletal mesh whose every body is simulated in the Chaos world.
 * Passive by default (no drives): a plain ragdoll. StartActiveControl adds muscles and balance assist and
 * makes it the NPC body.
 */
UCLASS()
class SANDBOXBODY_API ASandboxRagdollActor : public AActor
{
	GENERATED_BODY()

public:
	ASandboxRagdollActor();

	/**
	 * Assigns the mesh and an optional physics asset override. With bSimulate every body is switched to
	 * simulation (a passive ragdoll); without it the mesh stays kinematic in its reference pose.
	 * Returns false with a reason if the mesh has no usable physics bodies.
	 */
	bool InitBody(USkeletalMesh* Mesh, UPhysicsAsset* PhysicsAssetOverride, bool bSimulate, FString& OutError);

	/**
	 * Adds a skeletal mesh that copies the body's pose bone by bone (leader pose), such as a MetaHuman
	 * face. It has no collision and no physics of its own.
	 */
	USkeletalMeshComponent* AddAttachedMesh(USkeletalMesh* Mesh);

	/**
	 * Makes the simulated body an active ragdoll that holds the pose it is in now. Call after InitBody with
	 * bSimulate, before the first physics step. The tuning comes from the store, which the body keeps
	 * following (see USandboxBodyController::Start). Returns false with a reason on failure.
	 */
	bool StartActiveControl(const FSandboxParamStore& Params, FString& OutError);

	USkeletalMeshComponent* GetBodyMesh() const { return BodyMesh; }

	/** Null while the body is passive. */
	USandboxBodyController* GetController() const { return Controller; }

private:
	UPROPERTY(VisibleAnywhere, Category = "Sandbox")
	TObjectPtr<USandboxBodyController> Controller;

	UPROPERTY(VisibleAnywhere, Category = "Sandbox")
	TObjectPtr<USkeletalMeshComponent> BodyMesh;

	UPROPERTY(VisibleAnywhere, Category = "Sandbox")
	TArray<TObjectPtr<USkeletalMeshComponent>> AttachedMeshes;
};
