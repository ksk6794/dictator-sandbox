#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SandboxRagdollActor.generated.h"

class UPhysicsAsset;
class USkeletalMesh;
class USkeletalMeshComponent;

/**
 * A skeletal mesh whose every body is simulated in the Chaos world.
 * M0: fully passive (no drives). It becomes the NPC body once muscles and balance are added in M2.
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

	USkeletalMeshComponent* GetBodyMesh() const { return BodyMesh; }

private:
	UPROPERTY(VisibleAnywhere, Category = "Sandbox")
	TObjectPtr<USkeletalMeshComponent> BodyMesh;

	UPROPERTY(VisibleAnywhere, Category = "Sandbox")
	TArray<TObjectPtr<USkeletalMeshComponent>> AttachedMeshes;
};
