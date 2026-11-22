#include "ScenarioRunnerSubsystem.h"

#include "Camera/CameraActor.h"
#include "Components/LightComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformMisc.h"
#include "DynamicRHI.h"
#include "Engine/Engine.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "IHeadMountedDisplay.h"
#include "IXRTrackingSystem.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/Crc.h"
#include "Misc/EngineVersion.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "PhysicsEngine/BodyInstance.h"
#include "PhysicsEngine/ConstraintInstance.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/PhysicsSettings.h"
#include "RenderTimer.h"
#include "StereoRendering.h"
#include "SandboxBodyProbe.h"
#include "SandboxCore.h"
#include "SandboxJson.h"
#include "SandboxRagdollActor.h"
#include "UnrealClient.h"

namespace
{
	// A body counts as at rest below both thresholds.
	constexpr double RestLinearSpeedCmS = 5.0;
	constexpr double RestAngularSpeedRadS = 0.2;
	// Anything further than this from the origin has escaped the scene.
	constexpr double BoundsRadiusCm = 5000.0;
	constexpr double DtToleranceS = 1.0e-6;
	// Frames at the start of a run during which authored joint offsets are still being closed.
	constexpr int32 JointSettleFrames = 5;
	// Frames rendered after the camera is re-aimed and before the screenshot is requested.
	constexpr int32 CameraSettleFrames = 30;
	// Rendered frames skipped before frame timings are collected (shader and streaming warm-up).
	constexpr int32 TimingWarmupFrames = 90;
	// Console variables written to metrics.json: the ones that decide what a frame costs.
	const TCHAR* const RendererCVars[] = {
		TEXT("r.ForwardShading"), TEXT("vr.InstancedStereo"), TEXT("xr.SecondaryScreenPercentage.HMDRenderTarget"), TEXT("r.ScreenPercentage"),
		TEXT("r.AntiAliasingMethod"), TEXT("r.DynamicGlobalIlluminationMethod"), TEXT("r.ReflectionMethod"),
		TEXT("r.Shadow.Virtual.Enable"), TEXT("r.MotionBlurQuality"), TEXT("r.AmbientOcclusionLevels"),
		TEXT("r.SkeletalMeshLODBias"), TEXT("r.RayTracing") };
	// Frames to keep rendering after the request so it is serviced before the process exits.
	constexpr int32 ScreenshotFrames = 30;

	uint32 HashDoubles(const double* Values, int32 Count, uint32 Previous)
	{
		return FCrc::MemCrc32(Values, Count * sizeof(double), Previous);
	}

	uint32 HashTransform(const FTransform& Transform, uint32 Previous)
	{
		const FVector P = Transform.GetLocation();
		const FQuat Q = Transform.GetRotation();
		const double Values[7] = { P.X, P.Y, P.Z, Q.X, Q.Y, Q.Z, Q.W };
		return HashDoubles(Values, 7, Previous);
	}

	TSharedRef<FJsonObject> MakeCheck(const FString& Name, bool bPass, const TSharedPtr<FJsonValue>& Value, const TSharedPtr<FJsonValue>& Limit)
	{
		const TSharedRef<FJsonObject> Check = MakeShared<FJsonObject>();
		Check->SetStringField(TEXT("name"), Name);
		Check->SetBoolField(TEXT("pass"), bPass);
		Check->SetField(TEXT("value"), Value);
		Check->SetField(TEXT("limit"), Limit);
		return Check;
	}

	TSharedPtr<FJsonValue> Number(double Value) { return MakeShared<FJsonValueNumber>(Value); }

	/** Mean, median, 99th percentile and maximum of a list of millisecond samples. */
	TSharedRef<FJsonObject> Summarise(TArray<float> Samples)
	{
		const TSharedRef<FJsonObject> Summary = MakeShared<FJsonObject>();
		if (Samples.Num() == 0)
		{
			return Summary;
		}
		Samples.Sort();
		double Sum = 0.0;
		for (const float Sample : Samples)
		{
			Sum += Sample;
		}
		const auto Percentile = [&Samples](double Fraction)
		{
			return Samples[FMath::Clamp(FMath::CeilToInt32(Fraction * Samples.Num()) - 1, 0, Samples.Num() - 1)];
		};
		Summary->SetNumberField(TEXT("mean"), Sum / Samples.Num());
		Summary->SetNumberField(TEXT("p50"), Percentile(0.50));
		Summary->SetNumberField(TEXT("p99"), Percentile(0.99));
		Summary->SetNumberField(TEXT("max"), Samples.Last());
		return Summary;
	}
	TSharedPtr<FJsonValue> Boolean(bool bValue) { return MakeShared<FJsonValueBoolean>(bValue); }
	TSharedPtr<FJsonValue> Null() { return MakeShared<FJsonValueNull>(); }
}

bool UScenarioRunnerSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	FString Unused;
	return FParse::Value(FCommandLine::Get(), TEXT("Scenario="), Unused) && Super::ShouldCreateSubsystem(Outer);
}

bool UScenarioRunnerSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId UScenarioRunnerSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UScenarioRunnerSubsystem, STATGROUP_Tickables);
}

bool UScenarioRunnerSubsystem::IsStereo() const
{
	return GEngine && GEngine->XRSystem.IsValid() && GEngine->StereoRenderingDevice.IsValid() && GEngine->StereoRenderingDevice->IsStereoEnabled();
}

void UScenarioRunnerSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	FString ScenarioArg;
	FParse::Value(FCommandLine::Get(), TEXT("Scenario="), ScenarioArg);

	// Accept a bare name ("drop_passive"), a project-relative path or an absolute path.
	ScenarioFile = ScenarioArg;
	if (FPaths::GetExtension(ScenarioFile).IsEmpty())
	{
		ScenarioFile = FPaths::Combine(TEXT("Scenarios"), ScenarioFile + TEXT(".json"));
	}
	if (FPaths::IsRelative(ScenarioFile))
	{
		ScenarioFile = FPaths::Combine(FPaths::ProjectDir(), ScenarioFile);
	}
	ScenarioFile = FPaths::ConvertRelativePathToFull(ScenarioFile);

	if (!FParse::Value(FCommandLine::Get(), TEXT("Out="), OutDir))
	{
		OutDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Scenarios"), FPaths::GetBaseFilename(ScenarioFile));
	}
	OutDir = FPaths::ConvertRelativePathToFull(OutDir);

	if (FParse::Value(FCommandLine::Get(), TEXT("Shot="), ShotPath))
	{
		ShotPath = FPaths::ConvertRelativePathToFull(ShotPath);
	}

	FString Error;
	if (!FScenarioSpec::LoadFile(ScenarioFile, Spec, Error))
	{
		FailAndExit(Error);
		return;
	}

	FixedDt = Spec.FixedDt();
	TotalFrames = Spec.TotalFrames();
	NextCheckpointTimeS = Spec.CheckpointIntervalS;

	// Decouple simulation time from wall-clock time. The tool scripts also pass -UseFixedTimeStep -FPS=
	// so that even the first frame is fixed; DtViolations reports any frame that was not.
	FApp::SetFixedDeltaTime(FixedDt);
	FApp::SetUseFixedTimeStep(true);

	SpawnEnvironment(InWorld);
	if (!SpawnBody(InWorld, Error))
	{
		FailAndExit(Error);
		return;
	}

	WallStartS = FPlatformTime::Seconds();
	Phase = EPhase::Running;
	UE_LOG(LogSandbox, Display, TEXT("Scenario '%s': %d frames at %.6f s, output %s"), *Spec.Name, TotalFrames, FixedDt, *OutDir);
}

void UScenarioRunnerSubsystem::SpawnEnvironment(UWorld& World)
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
		return;
	}

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
	if (bOwnMap)
	{
		return;
	}

	// Rendered runs without a map: two lights, so screenshots show the result.
	ADirectionalLight* Key = World.SpawnActor<ADirectionalLight>(FVector(0.0, 0.0, 500.0), FRotator(-50.0, -30.0, 0.0));
	Key->GetLightComponent()->SetMobility(EComponentMobility::Movable);
	Key->GetLightComponent()->SetIntensity(6.0f);

	ADirectionalLight* Fill = World.SpawnActor<ADirectionalLight>(FVector(0.0, 0.0, 500.0), FRotator(-30.0, 150.0, 0.0));
	Fill->GetLightComponent()->SetMobility(EComponentMobility::Movable);
	Fill->GetLightComponent()->SetIntensity(2.0f);
	Fill->GetLightComponent()->SetCastShadows(false);

}

bool UScenarioRunnerSubsystem::SpawnBody(UWorld& World, FString& OutError)
{
	USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, *Spec.MeshPath);
	if (!Mesh)
	{
		OutError = FString::Printf(TEXT("Cannot load skeletal mesh '%s'"), *Spec.MeshPath);
		return false;
	}

	UPhysicsAsset* PhysicsAssetOverride = nullptr;
	if (!Spec.PhysicsAssetPath.IsEmpty())
	{
		PhysicsAssetOverride = LoadObject<UPhysicsAsset>(nullptr, *Spec.PhysicsAssetPath);
		if (!PhysicsAssetOverride)
		{
			OutError = FString::Printf(TEXT("Cannot load physics asset '%s'"), *Spec.PhysicsAssetPath);
			return false;
		}
	}

	Ragdoll = World.SpawnActor<ASandboxRagdollActor>(Spec.SpawnLocationCm, Spec.SpawnRotation);
	if (!Ragdoll)
	{
		OutError = TEXT("Failed to spawn the ragdoll actor");
		return false;
	}
	if (!Ragdoll->InitBody(Mesh, PhysicsAssetOverride, Spec.bSimulate, OutError))
	{
		return false;
	}

	for (const FString& Path : Spec.AttachedMeshPaths)
	{
		USkeletalMesh* Attached = LoadObject<USkeletalMesh>(nullptr, *Path);
		if (!Attached)
		{
			OutError = FString::Printf(TEXT("Cannot load attached mesh '%s'"), *Path);
			return false;
		}
		Ragdoll->AddAttachedMesh(Attached);
	}
	return true;
}

void UScenarioRunnerSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (Phase == EPhase::Finishing)
	{
		if (--FinishCountdown == ScreenshotFrames)
		{
			FScreenshotRequest::RequestScreenshot(ShotPath, /*bInShowUI*/ false, /*bAddFilenameSuffix*/ false);
		}
		if (FinishCountdown <= 0)
		{
			RequestExit(ExitCode);
		}
		return;
	}
	if (Phase != EPhase::Running)
	{
		return;
	}

	if (FrameIndex == 0 && Camera)
	{
		// Done here, not at BeginPlay: possessing the default pawn would otherwise take the view back.
		if (APlayerController* Controller = GetWorldRef().GetFirstPlayerController())
		{
			Controller->bAutoManageActiveCameraTarget = false;
			Controller->SetViewTarget(Camera);
		}
	}

	if (FApp::CanEverRender())
	{
		const double NowS = FPlatformTime::Seconds();
		if (FrameIndex >= TimingWarmupFrames)
		{
			FrameWallMs.Add(static_cast<float>((NowS - LastFrameWallS) * 1000.0));
			FrameGpuMs.Add(FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles()));
			FrameGameMs.Add(FPlatformTime::ToMilliseconds(GGameThreadTime));
			FrameRenderMs.Add(FPlatformTime::ToMilliseconds(GRenderThreadTime));
		}
		LastFrameWallS = NowS;
	}

	// Tickables run after the physics step of this frame, so the state seen here is after FrameIndex + 1 steps.
	const double SimTimeS = (FrameIndex + 1) * FixedDt;
	SampleFrame(DeltaTime);
	ApplyDueEvents(SimTimeS);

	++FrameIndex;
	if (FrameIndex >= TotalFrames)
	{
		Finish();
	}
}

void UScenarioRunnerSubsystem::SampleFrame(float DeltaTime)
{
	if (FMath::Abs(static_cast<double>(DeltaTime) - FixedDt) > DtToleranceS)
	{
		++DtViolations;
	}

	const USkeletalMeshComponent* Mesh = Ragdoll ? Ragdoll->GetBodyMesh() : nullptr;
	if (!Mesh)
	{
		return;
	}

	TArray<FSandboxBodySample> Bodies;
	FSandboxBodyProbe::SampleBodies(Mesh, Bodies);

	bool bAtRest = true;
	for (const FSandboxBodySample& Body : Bodies)
	{
		const FVector Location = Body.WorldTransform.GetLocation();
		if (Body.WorldTransform.ContainsNaN() || Body.LinearVelocity.ContainsNaN() || Body.AngularVelocityRad.ContainsNaN())
		{
			bSawNaN = true;
			continue;
		}
		if (Location.SizeSquared() > BoundsRadiusCm * BoundsRadiusCm)
		{
			bLeftBounds = true;
		}

		const double Speed = Body.LinearVelocity.Size();
		const double AngularSpeed = Body.AngularVelocityRad.Size();
		MaxBodySpeedCmS = FMath::Max(MaxBodySpeedCmS, Speed);
		MaxAngularSpeedRadS = FMath::Max(MaxAngularSpeedRadS, AngularSpeed);
		MinBodyZCm = FMath::Min(MinBodyZCm, Location.Z);
		if (Speed > RestLinearSpeedCmS || AngularSpeed > RestAngularSpeedRadS)
		{
			bAtRest = false;
		}

		BodyHash = HashTransform(Body.WorldTransform, BodyHash);
	}
	if (!bAtRest)
	{
		LastActiveFrame = FrameIndex;
	}

	// A physics asset can be authored with joint anchors that do not coincide in the reference pose.
	// The solver closes that gap in the first steps; it is reported on its own and kept out of the
	// stretch metric, which is about how far joints are pulled apart during the run.
	FName FrameWorstJoint;
	const double Stretch = FSandboxBodyProbe::MaxJointStretchCm(Mesh, &FrameWorstJoint);
	if (FrameIndex == 0)
	{
		InitialJointOffsetCm = Stretch;
	}
	else if (FrameIndex >= JointSettleFrames && Stretch > MaxJointStretchCm)
	{
		MaxJointStretchCm = Stretch;
		WorstJoint = FrameWorstJoint;
	}

	for (const FPin& Pin : Pins)
	{
		if (const FBodyInstance* Pinned = Mesh->GetBodyInstance(Pin.Body))
		{
			MaxPinErrorCm = FMath::Max(MaxPinErrorCm, FVector::Dist(Pinned->GetUnrealWorldTransform().GetLocation(), Pin.WorldLocation));
		}
	}

	// Component-space bone transforms are what rendering and animation see. Hashing them separately from
	// the bodies is what lets a headless run be compared with a rendered one.
	const TArray<FTransform>& Bones = Mesh->GetComponentSpaceTransforms();
	for (const FTransform& Bone : Bones)
	{
		BoneHash = HashTransform(Bone, BoneHash);
	}

	FinalKineticEnergyJ = FSandboxBodyProbe::TranslationalKineticEnergyJ(Bodies);
	TotalMassKg = FSandboxBodyProbe::TotalMassKg(Bodies);

	const double SimTimeS = (FrameIndex + 1) * FixedDt;
	const bool bLastFrame = FrameIndex + 1 >= TotalFrames;
	if (bLastFrame || SimTimeS + DtToleranceS >= NextCheckpointTimeS)
	{
		while (NextCheckpointTimeS <= SimTimeS + DtToleranceS)
		{
			NextCheckpointTimeS += Spec.CheckpointIntervalS;
		}

		const TSharedRef<FJsonObject> Checkpoint = MakeShared<FJsonObject>();
		Checkpoint->SetNumberField(TEXT("t"), SimTimeS);
		Checkpoint->SetNumberField(TEXT("frame"), FrameIndex);

		TArray<TSharedPtr<FJsonValue>> BodyValues;
		for (const FSandboxBodySample& Body : Bodies)
		{
			const TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("bone"), Body.Bone.ToString());
			Entry->SetArrayField(TEXT("p"), SandboxJson::VectorToArray(Body.WorldTransform.GetLocation()));
			Entry->SetArrayField(TEXT("q"), SandboxJson::QuatToArray(Body.WorldTransform.GetRotation()));
			BodyValues.Add(MakeShared<FJsonValueObject>(Entry));
		}
		Checkpoint->SetArrayField(TEXT("bodies"), BodyValues);

		TArray<TSharedPtr<FJsonValue>> BoneValues;
		for (const FTransform& Bone : Bones)
		{
			const FVector P = Bone.GetLocation();
			const FQuat Q = Bone.GetRotation();
			BoneValues.Add(MakeShared<FJsonValueArray>(TArray<TSharedPtr<FJsonValue>>{
				Number(P.X), Number(P.Y), Number(P.Z), Number(Q.X), Number(Q.Y), Number(Q.Z), Number(Q.W) }));
		}
		Checkpoint->SetArrayField(TEXT("bones"), BoneValues);

		Checkpoints.Add(MakeShared<FJsonValueObject>(Checkpoint));
	}
}

void UScenarioRunnerSubsystem::ApplyDueEvents(double SimTimeS)
{
	USkeletalMeshComponent* Mesh = Ragdoll ? Ragdoll->GetBodyMesh() : nullptr;
	while (NextEventIndex < Spec.Events.Num() && Spec.Events[NextEventIndex].TimeS <= SimTimeS + DtToleranceS)
	{
		const FScenarioEvent& Event = Spec.Events[NextEventIndex++];
		FBodyInstance* Body = Mesh ? Mesh->GetBodyInstance(Event.Body) : nullptr;
		if (!Body || !Body->IsValidBodyInstance())
		{
			UE_LOG(LogSandbox, Error, TEXT("Scenario: event at t=%.3f targets unknown body '%s'"), Event.TimeS, *Event.Body.ToString());
			continue;
		}
		if (Event.Type == TEXT("pin"))
		{
			// A ball joint between the body and the world at the body's current position. Making the body
			// kinematic would not pin it: kinematic bones of a ragdoll follow the component, which itself
			// follows the simulated root.
			const FTransform BodyTransform = Body->GetUnrealWorldTransform();
			FPin& Pin = Pins.AddDefaulted_GetRef();
			Pin.Body = Event.Body;
			Pin.WorldLocation = BodyTransform.GetLocation();
			Pin.Constraint = MakeUnique<FConstraintInstance>();
			Pin.Constraint->SetLinearLimits(LCM_Locked, LCM_Locked, LCM_Locked, 0.0f);
			Pin.Constraint->SetAngularSwing1Motion(ACM_Free);
			Pin.Constraint->SetAngularSwing2Motion(ACM_Free);
			Pin.Constraint->SetAngularTwistMotion(ACM_Free);
			Pin.Constraint->SetRefFrame(EConstraintFrame::Frame1, FTransform::Identity);
			Pin.Constraint->SetRefFrame(EConstraintFrame::Frame2, BodyTransform);
			Pin.Constraint->InitConstraint(Body, nullptr, 1.0f, Ragdoll);
		}
		else
		{
			// 1 N*s = 1 kg*m/s = 100 kg*cm/s in Unreal units.
			Body->AddImpulse(Event.Direction * Event.ImpulseNs * 100.0, /*bVelChange*/ false);
		}
		++EventsApplied;
	}
}

void UScenarioRunnerSubsystem::Finish()
{
	const double WallTimeS = FPlatformTime::Seconds() - WallStartS;
	const double SimTimeS = TotalFrames * FixedDt;
	const USkeletalMeshComponent* Mesh = Ragdoll ? Ragdoll->GetBodyMesh() : nullptr;
	const UPhysicsSettings* PhysicsSettings = UPhysicsSettings::Get();

	const bool bAtRestAtEnd = LastActiveFrame < TotalFrames - 1;
	const double TimeToRestS = (LastActiveFrame + 1) * FixedDt;

	// Checks. The first four always apply; the rest come from the scenario's "expect" block.
	TArray<TSharedPtr<FJsonValue>> Checks;
	bool bPass = true;
	const auto AddCheck = [&Checks, &bPass](const FString& Name, bool bCheckPass, const TSharedPtr<FJsonValue>& Value, const TSharedPtr<FJsonValue>& Limit)
	{
		Checks.Add(MakeShared<FJsonValueObject>(MakeCheck(Name, bCheckPass, Value, Limit)));
		bPass &= bCheckPass;
	};

	AddCheck(TEXT("no_nan"), !bSawNaN, Boolean(bSawNaN), Boolean(false));
	AddCheck(TEXT("stays_in_bounds"), !bLeftBounds, Boolean(bLeftBounds), Boolean(false));
	AddCheck(TEXT("fixed_timestep"), DtViolations == 0, Number(DtViolations), Number(0));
	AddCheck(TEXT("all_events_applied"), EventsApplied == Spec.Events.Num(), Number(EventsApplied), Number(Spec.Events.Num()));
	if (Spec.Expect.RestWithinS.IsSet())
	{
		const double Limit = Spec.Expect.RestWithinS.GetValue();
		AddCheck(TEXT("rest_within_s"), bAtRestAtEnd && TimeToRestS <= Limit, bAtRestAtEnd ? Number(TimeToRestS) : Null(), Number(Limit));
	}
	if (Spec.Expect.MaxJointStretchCm.IsSet())
	{
		const double Limit = Spec.Expect.MaxJointStretchCm.GetValue();
		AddCheck(TEXT("max_joint_stretch_cm"), MaxJointStretchCm <= Limit, Number(MaxJointStretchCm), Number(Limit));
	}

	const TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetBoolField(TEXT("ok"), true);
	Root->SetBoolField(TEXT("pass"), bPass);
	Root->SetStringField(TEXT("scenario"), Spec.Name);
	Root->SetStringField(TEXT("scenario_file"), ScenarioFile);
	Root->SetStringField(TEXT("engine_version"), FEngineVersion::Current().ToString());
	Root->SetBoolField(TEXT("rendered"), FApp::CanEverRender());
	Root->SetStringField(TEXT("map"), GetWorldRef().GetOutermost()->GetName());

	const TSharedRef<FJsonObject> Timing = MakeShared<FJsonObject>();
	Timing->SetNumberField(TEXT("fixed_dt_s"), FixedDt);
	Timing->SetNumberField(TEXT("frames"), TotalFrames);
	Timing->SetNumberField(TEXT("sim_time_s"), SimTimeS);
	Timing->SetNumberField(TEXT("wall_time_s"), WallTimeS);
	Timing->SetNumberField(TEXT("realtime_factor"), WallTimeS > 0.0 ? SimTimeS / WallTimeS : 0.0);
	Timing->SetBoolField(TEXT("physics_async"), PhysicsSettings->bTickPhysicsAsync);
	Timing->SetBoolField(TEXT("substepping"), PhysicsSettings->bSubstepping);
	Timing->SetNumberField(TEXT("max_substep_dt_s"), PhysicsSettings->MaxSubstepDeltaTime);
	Timing->SetNumberField(TEXT("max_substeps"), PhysicsSettings->MaxSubsteps);
	Root->SetObjectField(TEXT("timing"), Timing);

	const TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("mesh"), Spec.MeshPath);
	Body->SetNumberField(TEXT("body_count"), Mesh ? Mesh->Bodies.Num() : 0);
	Body->SetNumberField(TEXT("constraint_count"), Mesh ? Mesh->Constraints.Num() : 0);
	Body->SetNumberField(TEXT("bone_count"), Mesh ? Mesh->GetComponentSpaceTransforms().Num() : 0);
	Body->SetNumberField(TEXT("total_mass_kg"), TotalMassKg);

	// Structure of the physics asset as instanced: enough to check a generated asset against its spec.
	const auto MotionName = [](EAngularConstraintMotion Motion)
	{
		return Motion == ACM_Locked ? TEXT("locked") : Motion == ACM_Limited ? TEXT("limited") : TEXT("free");
	};
	TArray<TSharedPtr<FJsonValue>> BodyList;
	TArray<TSharedPtr<FJsonValue>> JointList;
	if (Mesh)
	{
		for (const FBodyInstance* Instance : Mesh->Bodies)
		{
			if (!Instance || !Instance->IsValidBodyInstance())
			{
				continue;
			}
			const TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("bone"), Mesh->GetBoneName(Instance->InstanceBoneIndex).ToString());
			Entry->SetNumberField(TEXT("mass_kg"), Instance->GetBodyMass());
			BodyList.Add(MakeShared<FJsonValueObject>(Entry));
		}
		for (const FConstraintInstance* Constraint : Mesh->Constraints)
		{
			if (!Constraint)
			{
				continue;
			}
			const TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("name"), Constraint->JointName.ToString());
			Entry->SetStringField(TEXT("child"), Constraint->GetChildBoneName().ToString());
			Entry->SetStringField(TEXT("parent"), Constraint->GetParentBoneName().ToString());
			Entry->SetBoolField(TEXT("linear_locked"),
				Constraint->GetLinearXMotion() == LCM_Locked && Constraint->GetLinearYMotion() == LCM_Locked && Constraint->GetLinearZMotion() == LCM_Locked);
			Entry->SetStringField(TEXT("swing1"), MotionName(Constraint->GetAngularSwing1Motion()));
			Entry->SetNumberField(TEXT("swing1_deg"), Constraint->GetAngularSwing1Limit());
			Entry->SetStringField(TEXT("swing2"), MotionName(Constraint->GetAngularSwing2Motion()));
			Entry->SetNumberField(TEXT("swing2_deg"), Constraint->GetAngularSwing2Limit());
			Entry->SetStringField(TEXT("twist"), MotionName(Constraint->GetAngularTwistMotion()));
			Entry->SetNumberField(TEXT("twist_deg"), Constraint->GetAngularTwistLimit());
			JointList.Add(MakeShared<FJsonValueObject>(Entry));
		}
	}
	Body->SetArrayField(TEXT("bodies"), BodyList);
	Body->SetArrayField(TEXT("joints"), JointList);
	Root->SetObjectField(TEXT("body"), Body);

	const TSharedRef<FJsonObject> Metrics = MakeShared<FJsonObject>();
	Metrics->SetNumberField(TEXT("max_joint_stretch_cm"), MaxJointStretchCm);
	Metrics->SetStringField(TEXT("worst_joint"), WorstJoint.IsNone() ? FString() : WorstJoint.ToString());
	Metrics->SetNumberField(TEXT("initial_joint_offset_cm"), InitialJointOffsetCm);
	Metrics->SetNumberField(TEXT("max_pin_error_cm"), MaxPinErrorCm);
	Metrics->SetNumberField(TEXT("max_body_speed_mps"), MaxBodySpeedCmS * 0.01);
	Metrics->SetNumberField(TEXT("max_angular_speed_rads"), MaxAngularSpeedRadS);
	Metrics->SetBoolField(TEXT("at_rest_at_end"), bAtRestAtEnd);
	Metrics->SetField(TEXT("time_to_rest_s"), bAtRestAtEnd ? Number(TimeToRestS) : Null());
	Metrics->SetNumberField(TEXT("min_body_z_cm"), MinBodyZCm == TNumericLimits<double>::Max() ? 0.0 : MinBodyZCm);
	Metrics->SetNumberField(TEXT("final_kinetic_energy_j"), FinalKineticEnergyJ);
	Metrics->SetNumberField(TEXT("dt_violations"), DtViolations);
	Metrics->SetNumberField(TEXT("events_applied"), EventsApplied);
	Root->SetObjectField(TEXT("metrics"), Metrics);

	const TSharedRef<FJsonObject> Hashes = MakeShared<FJsonObject>();
	Hashes->SetStringField(TEXT("bodies"), FString::Printf(TEXT("%08x"), BodyHash));
	Hashes->SetStringField(TEXT("bones"), FString::Printf(TEXT("%08x"), BoneHash));
	Root->SetObjectField(TEXT("hashes"), Hashes);

	Root->SetArrayField(TEXT("checks"), Checks);

	if (FrameWallMs.Num() > 0)
	{
		// Frame times of a rendered run. In a headset the frame interval is paced by the compositor,
		// so gpu_ms is the figure to hold against the frame budget.
		const TSharedRef<FJsonObject> Performance = MakeShared<FJsonObject>();
		Performance->SetNumberField(TEXT("frames"), FrameWallMs.Num());
		Performance->SetObjectField(TEXT("frame_interval_ms"), Summarise(FrameWallMs));
		Performance->SetObjectField(TEXT("gpu_ms"), Summarise(FrameGpuMs));
		Performance->SetObjectField(TEXT("game_thread_ms"), Summarise(FrameGameMs));
		Performance->SetObjectField(TEXT("render_thread_ms"), Summarise(FrameRenderMs));
		Root->SetObjectField(TEXT("performance"), Performance);
	}

	const TSharedRef<FJsonObject> Renderer = MakeShared<FJsonObject>();
	for (const TCHAR* Name : RendererCVars)
	{
		if (const IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Name))
		{
			Renderer->SetStringField(Name, Variable->GetString());
		}
	}
	Root->SetObjectField(TEXT("renderer"), Renderer);

	const TSharedRef<FJsonObject> Xr = MakeShared<FJsonObject>();
	Xr->SetBoolField(TEXT("stereo"), IsStereo());
	if (GEngine && GEngine->XRSystem.IsValid())
	{
		Xr->SetStringField(TEXT("system"), GEngine->XRSystem->GetSystemName().ToString());
		Xr->SetStringField(TEXT("version"), GEngine->XRSystem->GetVersionString());
		if (IHeadMountedDisplay* Hmd = GEngine->XRSystem->GetHMDDevice())
		{
			const FIntPoint Size = Hmd->GetIdealRenderTargetSize();
			Xr->SetNumberField(TEXT("render_target_width"), Size.X);
			Xr->SetNumberField(TEXT("render_target_height"), Size.Y);
		}
	}
	Root->SetObjectField(TEXT("xr"), Xr);

	const TSharedRef<FJsonObject> Trace = MakeShared<FJsonObject>();
	Trace->SetStringField(TEXT("scenario"), Spec.Name);
	Trace->SetBoolField(TEXT("rendered"), FApp::CanEverRender());
	Trace->SetArrayField(TEXT("checkpoints"), Checkpoints);

	FString Error;
	const bool bSaved =
		SandboxJson::SaveObject(FPaths::Combine(OutDir, TEXT("trace.json")), Trace, Error, /*bPretty*/ false) &&
		SandboxJson::SaveObject(FPaths::Combine(OutDir, TEXT("metrics.json")), Root, Error);
	if (!bSaved)
	{
		UE_LOG(LogSandbox, Error, TEXT("Scenario: %s"), *Error);
		RequestExit(2);
		return;
	}

	UE_LOG(LogSandbox, Display, TEXT("Scenario '%s': %s. %.2f s simulated in %.2f s. Metrics: %s"),
		*Spec.Name, bPass ? TEXT("PASS") : TEXT("FAIL"), SimTimeS, WallTimeS, *FPaths::Combine(OutDir, TEXT("metrics.json")));

	ExitCode = bPass ? 0 : 1;
	if (!ShotPath.IsEmpty() && FApp::CanEverRender())
	{
		// Frame the body wherever it ended up, unless the scenario fixed the camera.
		if (Camera && Mesh && Mesh->Bodies.Num() > 0 && !Spec.bHasCamera && !IsStereo())
		{
			const FVector Center = Mesh->Bounds.Origin;
			const FVector CameraLocation = Center + FVector(-260.0, 180.0, 140.0);
			Camera->SetActorLocationAndRotation(CameraLocation, (Center - CameraLocation).Rotation());
		}
		// The shot is requested a few frames later (see Tick): moving the camera and capturing in the
		// same frame smears the image with motion blur and unconverged temporal anti-aliasing.
		FinishCountdown = CameraSettleFrames + ScreenshotFrames;
		Phase = EPhase::Finishing;
	}
	else
	{
		RequestExit(ExitCode);
	}
}

void UScenarioRunnerSubsystem::FailAndExit(const FString& Message)
{
	UE_LOG(LogSandbox, Error, TEXT("Scenario cannot run: %s"), *Message);

	const TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetBoolField(TEXT("ok"), false);
	Root->SetBoolField(TEXT("pass"), false);
	Root->SetStringField(TEXT("error"), Message);
	Root->SetStringField(TEXT("scenario_file"), ScenarioFile);

	FString Error;
	if (!OutDir.IsEmpty() && !SandboxJson::SaveObject(FPaths::Combine(OutDir, TEXT("metrics.json")), Root, Error))
	{
		UE_LOG(LogSandbox, Error, TEXT("Scenario: %s"), *Error);
	}
	RequestExit(2);
}

void UScenarioRunnerSubsystem::RequestExit(uint8 Code)
{
	for (FPin& Pin : Pins)
	{
		Pin.Constraint->TermConstraint();
	}
	Pins.Reset();
	Phase = EPhase::Idle;
	FPlatformMisc::RequestExitWithStatus(/*Force*/ false, Code);
}
