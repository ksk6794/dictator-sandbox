#include "SandboxRagdollActor.h"

#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "PhysicsEngine/PhysicsAsset.h"

ASandboxRagdollActor::ASandboxRagdollActor()
{
	PrimaryActorTick.bCanEverTick = false;

	BodyMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("BodyMesh"));
	SetRootComponent(BodyMesh);
	BodyMesh->SetCollisionProfileName(TEXT("Ragdoll"));
	BodyMesh->SetGenerateOverlapEvents(false);

	// Headless runs (-nullrhi) never mark the mesh as recently rendered. Without these two settings the
	// engine would skip pose updates there and bone transforms would differ from a rendered run.
	BodyMesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	BodyMesh->bEnableUpdateRateOptimizations = false;
}

bool ASandboxRagdollActor::InitBody(USkeletalMesh* Mesh, UPhysicsAsset* PhysicsAssetOverride, bool bSimulate, FString& OutError)
{
	if (!Mesh)
	{
		OutError = TEXT("No skeletal mesh given");
		return false;
	}

	BodyMesh->SetSkeletalMeshAsset(Mesh);
	if (PhysicsAssetOverride)
	{
		BodyMesh->SetPhysicsAsset(PhysicsAssetOverride, /*bForceReInit*/ true);
	}

	if (!BodyMesh->GetPhysicsAsset())
	{
		OutError = FString::Printf(TEXT("Skeletal mesh '%s' has no physics asset"), *Mesh->GetPathName());
		return false;
	}
	if (BodyMesh->Bodies.Num() == 0)
	{
		OutError = FString::Printf(TEXT("Physics asset '%s' created no bodies"), *BodyMesh->GetPhysicsAsset()->GetPathName());
		return false;
	}

	if (bSimulate)
	{
		BodyMesh->SetAllBodiesSimulatePhysics(true);
		BodyMesh->WakeAllRigidBodies();
	}
	return true;
}

USkeletalMeshComponent* ASandboxRagdollActor::AddAttachedMesh(USkeletalMesh* Mesh)
{
	if (!Mesh)
	{
		return nullptr;
	}

	USkeletalMeshComponent* Component = NewObject<USkeletalMeshComponent>(this);
	Component->SetupAttachment(BodyMesh);
	Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Component->SetGenerateOverlapEvents(false);
	Component->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	Component->bEnableUpdateRateOptimizations = false;
	Component->RegisterComponent();
	Component->SetSkeletalMeshAsset(Mesh);
	// Bones the two skeletons share by name follow the body; the rest keep their reference pose.
	Component->SetLeaderPoseComponent(BodyMesh);
	AttachedMeshes.Add(Component);
	return Component;
}
