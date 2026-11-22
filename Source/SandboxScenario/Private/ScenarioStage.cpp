#include "ScenarioStage.h"

#include "Camera/CameraActor.h"
#include "Components/LightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "IXRTrackingSystem.h"
#include "Misc/App.h"
#include "Misc/PackageName.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "SandboxCore.h"
#include "SandboxParamStore.h"
#include "SandboxRagdollActor.h"
#include "ScenarioSpec.h"
#include "StereoRendering.h"

bool FScenarioStage::IsStereo()
{
	return GEngine && GEngine->XRSystem.IsValid() && GEngine->StereoRenderingDevice.IsValid() && GEngine->StereoRenderingDevice->IsStereoEnabled();
}

ACameraActor* FScenarioStage::SpawnEnvironment(UWorld& World, const FScenarioSpec& Spec, bool bSpawnCamera)
{
	// A scenario map brings its own floor and lighting; only the camera is added.
	const bool bOwnMap = !Spec.MapPath.IsEmpty();
	if (bOwnMap && !World.GetOutermost()->GetName().EndsWith(FPackageName::GetShortName(Spec.MapPath)))
	{
		UE_LOG(LogSandbox, Warning, TEXT("Scenario asks for map '%s' but the loaded world is '%s'"), *Spec.MapPath, *World.GetOutermost()->GetName());
	}

	if (!bOwnMap)
	{
		// Floor: a 20 m x 20 m slab whose top face is at z = 0.
		if (UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")))
		{
			AStaticMeshActor* Floor = World.SpawnActor<AStaticMeshActor>(FVector(0.0, 0.0, -50.0), FRotator::ZeroRotator);
			Floor->SetMobility(EComponentMobility::Movable);
			Floor->GetStaticMeshComponent()->SetStaticMesh(Cube);
			Floor->SetActorScale3D(FVector(20.0, 20.0, 1.0));
		}
		else
		{
			UE_LOG(LogSandbox, Warning, TEXT("Scenario: floor mesh /Engine/BasicShapes/Cube not found; the body will fall forever"));
		}
	}

	if (!FApp::CanEverRender())
	{
		return nullptr;
	}

	ACameraActor* Camera = nullptr;
	if (bSpawnCamera)
	{
		FVector CameraLocation = Spec.bHasCamera ? Spec.CameraLocationCm : FVector(-420.0, 260.0, 170.0);
		const FVector LookAt = Spec.bHasCamera ? Spec.CameraLookAtCm : FVector(0.0, 0.0, 50.0);
		FRotator CameraRotation = (LookAt - CameraLocation).Rotation();
		if (IsStereo())
		{
			// In the headset the camera actor is the tracking origin: on the floor, level, facing the target.
			// A pitched or raised origin would tilt or lift the whole world for the wearer.
			GEngine->XRSystem->SetTrackingOrigin(EHMDTrackingOrigin::LocalFloor);
			CameraLocation.Z = 0.0;
			CameraRotation = FRotator(0.0, CameraRotation.Yaw, 0.0);
		}
		Camera = World.SpawnActor<ACameraActor>(CameraLocation, CameraRotation);
	}
	if (bOwnMap)
	{
		return Camera;
	}

	// Rendered runs without a map: two lights, so screenshots show the result.
	ADirectionalLight* Key = World.SpawnActor<ADirectionalLight>(FVector(0.0, 0.0, 500.0), FRotator(-50.0, -30.0, 0.0));
	Key->GetLightComponent()->SetMobility(EComponentMobility::Movable);
	Key->GetLightComponent()->SetIntensity(6.0f);

	ADirectionalLight* Fill = World.SpawnActor<ADirectionalLight>(FVector(0.0, 0.0, 500.0), FRotator(-30.0, 150.0, 0.0));
	Fill->GetLightComponent()->SetMobility(EComponentMobility::Movable);
	Fill->GetLightComponent()->SetIntensity(2.0f);
	Fill->GetLightComponent()->SetCastShadows(false);
	return Camera;
}

ASandboxRagdollActor* FScenarioStage::SpawnBody(UWorld& World, const FScenarioSpec& Spec, FString& OutError)
{
	USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, *Spec.MeshPath);
	if (!Mesh)
	{
		OutError = FString::Printf(TEXT("Cannot load skeletal mesh '%s'"), *Spec.MeshPath);
		return nullptr;
	}

	UPhysicsAsset* PhysicsAssetOverride = nullptr;
	if (!Spec.PhysicsAssetPath.IsEmpty())
	{
		PhysicsAssetOverride = LoadObject<UPhysicsAsset>(nullptr, *Spec.PhysicsAssetPath);
		if (!PhysicsAssetOverride)
		{
			OutError = FString::Printf(TEXT("Cannot load physics asset '%s'"), *Spec.PhysicsAssetPath);
			return nullptr;
		}
	}

	ASandboxRagdollActor* Ragdoll = World.SpawnActor<ASandboxRagdollActor>(Spec.SpawnLocationCm, Spec.SpawnRotation);
	if (!Ragdoll)
	{
		OutError = TEXT("Failed to spawn the ragdoll actor");
		return nullptr;
	}
	const auto Fail = [Ragdoll]() -> ASandboxRagdollActor*
	{
		Ragdoll->Destroy();
		return nullptr;
	};
	if (!Ragdoll->InitBody(Mesh, PhysicsAssetOverride, Spec.bSimulate, OutError))
	{
		return Fail();
	}

	for (const FString& Path : Spec.AttachedMeshPaths)
	{
		USkeletalMesh* Attached = LoadObject<USkeletalMesh>(nullptr, *Path);
		if (!Attached)
		{
			OutError = FString::Printf(TEXT("Cannot load attached mesh '%s'"), *Path);
			return Fail();
		}
		Ragdoll->AddAttachedMesh(Attached);
	}

	if (Spec.Control == EScenarioBodyControl::Active)
	{
		// The body's tuning: the files of Config/Tuning (or -TuningDir=), with -Tune="name=value,..." on
		// top for this run only. Both are read by the store.
		FSandboxParamStore& Params = FSandboxParamStore::Get();
		if (!Params.EnsureLoaded(OutError) || !Ragdoll->StartActiveControl(Params, OutError))
		{
			return Fail();
		}
	}
	return Ragdoll;
}
