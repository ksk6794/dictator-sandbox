#include "ScenarioStage.h"

#include "SandboxPropComponent.h"

#include "Camera/CameraActor.h"
#include "Components/BoxComponent.h"
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
#include "PBDRigidsSolver.h"
#include "Physics/Experimental/PhysScene_Chaos.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "SandboxBodyController.h"
#include "SandboxCore.h"
#include "SandboxEffectorComponent.h"
#include "SandboxEffectorRig.h"
#include "SandboxParamStore.h"
#include "SandboxRagdollActor.h"
#include "ScenarioSpec.h"
#include "StereoRendering.h"

bool FScenarioStage::IsStereo()
{
	return GEngine && GEngine->XRSystem.IsValid() && GEngine->StereoRenderingDevice.IsValid() && GEngine->StereoRenderingDevice->IsStereoEnabled();
}

void FScenarioStage::KeepSolverInStep(UWorld& World)
{
	FPhysScene* Scene = World.GetPhysicsScene();
	Chaos::FPhysicsSolver* Solver = Scene ? Scene->GetSolver() : nullptr;
	if (!Solver)
	{
		return;
	}
	// On the physics thread, before every substep. Each blocking call waits for the rebuild that is under way and
	// takes its result in; the second starts no new one, so the step's own look at the task finds it done, every
	// time, and nothing is left running that the next step could find either done or not.
	Solver->AddPreAdvanceCallback(FSolverPreAdvance::FDelegate::CreateLambda([Solver](Chaos::FReal)
	{
		Chaos::FPBDRigidsEvolution* Evolution = Solver->GetEvolution();
		Evolution->SetCanStartAsyncTasks(true);
		Evolution->ComputeIntermediateSpatialAcceleration(/*bBlock*/ true);
		Evolution->ComputeIntermediateSpatialAcceleration(/*bBlock*/ true);
		Evolution->SetCanStartAsyncTasks(false);
		Evolution->ComputeIntermediateSpatialAcceleration(/*bBlock*/ true);
	}));
}

ACameraActor* FScenarioStage::SpawnEnvironment(UWorld& World, const FScenarioSpec& Spec, bool bSpawnCamera)
{
	KeepSolverInStep(World);

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

void FScenarioStage::TellBodyOfHolds(const ASandboxEffectorRig* Rig, ASandboxRagdollActor* Body)
{
	USandboxBodyController* Controller = Body ? Body->GetController() : nullptr;
	if (!Controller)
	{
		return;
	}
	TArray<FSandboxBodyHold, TInlineAllocator<2>> Holds;
	if (Rig)
	{
		for (const USandboxEffectorComponent* Effector : Rig->GetEffectors())
		{
			if (Effector && Effector->IsGrabbing() && Effector->GetGrabbedComponent() == Body->GetBodyMesh())
			{
				FSandboxBodyHold& Hold = Holds.AddDefaulted_GetRef();
				Hold.Holder = Effector->GetId();
				Hold.Bone = Effector->GetGrabbedBone();
				Hold.ForceN = Effector->GetGrabForceN();
				Hold.PullN = Effector->GetGrabPullN();
				Hold.PointCm = Effector->GetHandTransform().GetLocation();
			}
		}
	}
	// Told also when nothing is held: that is how the body learns that a hold has ended.
	if (Holds.Num() > 0 || Controller->GetHolds().Num() > 0)
	{
		Controller->TellHolds(Holds);
	}
}

TArray<AActor*> FScenarioStage::SpawnProps(UWorld& World, const FScenarioSpec& Spec)
{
	TArray<AActor*> Props;
	UStaticMesh* Cube = Spec.Props.Num() > 0 ? LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")) : nullptr;
	for (const FScenarioProp& Prop : Spec.Props)
	{
		// A box component is the body, as a sphere component is a hand's: a static mesh component with one
		// of the engine's basic shapes did not become a simulating body here. The mesh is only looked at.
		FActorSpawnParameters Params;
		// "Prop_<name>", which is what a hand that touches or holds it reports; another name only if that one is taken.
		const FName Wanted(*FString::Printf(TEXT("Prop_%s"), *Prop.Name.ToString()));
		Params.Name = FindObjectFast<UObject>(World.GetCurrentLevel(), Wanted) ? MakeUniqueObjectName(World.GetCurrentLevel(), AActor::StaticClass(), Wanted) : Wanted;
		AActor* Actor = World.SpawnActor<AActor>(AActor::StaticClass(), FTransform(Prop.Rotation, Prop.LocationCm), Params);
		// A thing made of boxes is one body with several shapes (USandboxPropComponent); a thing of one box is a box component.
		const bool bMany = Prop.Boxes.Num() > 0;
		UPrimitiveComponent* Box = nullptr;
		UBoxComponent* Single = nullptr;
		USandboxPropComponent* Several = nullptr;
		if (Actor)
		{
			if (bMany)
			{
				Several = NewObject<USandboxPropComponent>(Actor, TEXT("Body"));
				Box = Several;
			}
			else
			{
				Single = NewObject<UBoxComponent>(Actor, TEXT("Body"));
				Box = Single;
			}
		}
		if (!Box)
		{
			UE_LOG(LogSandbox, Error, TEXT("Scenario: the prop '%s' could not be made"), *Prop.Name.ToString());
			Props.Add(nullptr);
			continue;
		}
		Box->SetMobility(EComponentMobility::Movable);
		if (Single)
		{
			Single->SetBoxExtent(Prop.SizeCm * 0.5, /*bUpdateOverlaps*/ false);
		}
		else
		{
			TArray<TPair<FVector, FVector>> Boxes;
			for (const FScenarioPropBox& Each : Prop.Boxes)
			{
				Boxes.Emplace(Each.CenterCm, Each.SizeCm);
			}
			Several->SetBoxes(Boxes);
		}
		Box->SetCollisionProfileName(TEXT("PhysicsActor"));
		Box->SetGenerateOverlapEvents(false);
		Box->BodyInstance.bNotifyRigidBodyCollision = true;
		// Swept, as the hands are: a stick that is swung is thin and fast.
		Box->BodyInstance.bUseCCD = true;
		Actor->SetRootComponent(Box);
		Box->SetWorldTransform(FTransform(Prop.Rotation, Prop.LocationCm));
		Box->RegisterComponent();
		Box->SetSimulatePhysics(true);
		Box->SetMassOverrideInKg(NAME_None, static_cast<float>(Prop.MassKg), /*bOverrideMass*/ true);
		const auto MakeVisual = [&](const FName& VisualName, const FVector& Offset, const FVector& Size)
		{
			if (!Cube)
			{
				return;
			}
			UStaticMeshComponent* Visual = NewObject<UStaticMeshComponent>(Actor, VisualName);
			Visual->SetupAttachment(Box);
			Visual->SetRelativeLocation(Offset);
			Visual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Visual->SetGenerateOverlapEvents(false);
			Visual->SetStaticMesh(Cube);
			Visual->SetRelativeScale3D(Size / 100.0);
			Visual->RegisterComponent();
		};
		if (bMany)
		{
			for (int32 Part = 0; Part < Prop.Boxes.Num(); ++Part)
			{
				MakeVisual(FName(*FString::Printf(TEXT("Visual%d"), Part)), Prop.Boxes[Part].CenterCm, Prop.Boxes[Part].SizeCm);
			}
		}
		else
		{
			MakeVisual(TEXT("Visual"), FVector::ZeroVector, Prop.SizeCm);
		}
		if (Prop.HandleCm.IsSet())
		{
			// The place the thing is to be held by: a component with a tag, which is all a hand looks for.
			USceneComponent* Handle = NewObject<USceneComponent>(Actor, TEXT("Handle"));
			Handle->SetupAttachment(Box);
			Handle->SetRelativeLocation(Prop.HandleCm.GetValue());
			Handle->ComponentTags.Add(TEXT("SandboxHandle"));
			Handle->RegisterComponent();
		}
		const FBodyInstance* Body = Box->GetBodyInstance();
		if (!Body || !Body->IsValidBodyInstance() || !Body->IsInstanceSimulatingPhysics())
		{
			UE_LOG(LogSandbox, Error, TEXT("Scenario: the prop '%s' got no simulating body"), *Prop.Name.ToString());
		}
		Props.Add(Actor);
	}
	return Props;
}
