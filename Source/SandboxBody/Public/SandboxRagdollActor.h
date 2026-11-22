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
	 * Assigns the mesh (and an optional physics asset override) and switches all bodies to simulation.
	 * Returns false with a reason if the mesh ends up with no simulated bodies.
	 */
	bool InitPassiveRagdoll(USkeletalMesh* Mesh, UPhysicsAsset* PhysicsAssetOverride, FString& OutError);

	USkeletalMeshComponent* GetBodyMesh() const { return BodyMesh; }

private:
	UPROPERTY(VisibleAnywhere, Category = "Sandbox")
	TObjectPtr<USkeletalMeshComponent> BodyMesh;
};
