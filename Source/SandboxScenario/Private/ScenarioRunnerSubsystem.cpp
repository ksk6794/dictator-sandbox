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
#include "HAL/FileManager.h"
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
#include "PBDRigidsSolver.h"
#include "Physics/Experimental/PhysScene_Chaos.h"
#include "PhysicsEngine/BodyInstance.h"
#include "PhysicsEngine/ConstraintInstance.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/PhysicsSettings.h"
#include "RenderTimer.h"
#include "StereoRendering.h"
#include "SandboxBodyController.h"
#include "SandboxBodyProbe.h"
#include "SandboxCore.h"
#include "SandboxJson.h"
#include "SandboxParamStore.h"
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
	// The body has fallen once its pelvis is below this fraction of the height it started at.
	constexpr double FallenPelvisHeightFraction = 0.5;

	/**
	 * How far a joint is outside its limits, in degrees, axis by axis. The engine limits the two swings
	 * together, as an ellipse, so this box test can read 0 for a joint that is slightly outside; what it
	 * does report is real.
	 */
	double JointLimitViolationDeg(const FConstraintInstance& Constraint)
	{
		const auto Excess = [](EAngularConstraintMotion Motion, double AngleRad, double LimitDeg)
		{
			return Motion == ACM_Free ? 0.0 : FMath::Max(FMath::Abs(FMath::RadiansToDegrees(AngleRad)) - (Motion == ACM_Limited ? LimitDeg : 0.0), 0.0);
		};
		return FMath::Max3(
			Excess(Constraint.GetAngularTwistMotion(), Constraint.GetCurrentTwist(), Constraint.GetAngularTwistLimit()),
			Excess(Constraint.GetAngularSwing1Motion(), Constraint.GetCurrentSwing1(), Constraint.GetAngularSwing1Limit()),
			Excess(Constraint.GetAngularSwing2Motion(), Constraint.GetCurrentSwing2(), Constraint.GetAngularSwing2Limit()));
	}

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

	/** One entry of trace.json: world transforms of the bodies and component-space transforms of the bones. */
	TSharedPtr<FJsonValue> MakeCheckpoint(double TimeS, int32 Frame, const TArray<FSandboxBodySample>& Bodies, const TArray<FTransform>& Bones)
	{
		const TSharedRef<FJsonObject> Checkpoint = MakeShared<FJsonObject>();
		Checkpoint->SetNumberField(TEXT("t"), TimeS);
		Checkpoint->SetNumberField(TEXT("frame"), Frame);

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
		return MakeShared<FJsonValueObject>(Checkpoint);
	}
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

	StartPhysicsTimer(InWorld);
	WallStartS = FPlatformTime::Seconds();
	Phase = EPhase::Running;
	UE_LOG(LogSandbox, Display, TEXT("Scenario '%s': %d frames at %.6f s, output %s"), *Spec.Name, TotalFrames, FixedDt, *OutDir);
}

void UScenarioRunnerSubsystem::Deinitialize()
{
	StopPhysicsTimer();
	Super::Deinitialize();
}

void UScenarioRunnerSubsystem::StartPhysicsTimer(UWorld& World)
{
	FPhysScene* Scene = World.GetPhysicsScene();
	Chaos::FPhysicsSolver* Solver = Scene ? Scene->GetSolver() : nullptr;
	if (!Solver)
	{
		return;
	}
	// The solver raises this pair around every substep. Timing them is measurement only: nothing in the
	// simulation reads the result.
	PreSolveHandle = Solver->AddPreAdvanceCallback(FSolverPreAdvance::FDelegate::CreateWeakLambda(this, [this](Chaos::FReal)
	{
		SolveStartCycles.store(FPlatformTime::Cycles64());
	}));
	PostSolveHandle = Solver->AddPostAdvanceCallback(FSolverPostAdvance::FDelegate::CreateWeakLambda(this, [this](Chaos::FReal)
	{
		FrameSolveCycles.fetch_add(FPlatformTime::Cycles64() - SolveStartCycles.load());
		FrameSolveSteps.fetch_add(1);
	}));
}

void UScenarioRunnerSubsystem::StopPhysicsTimer()
{
	if (!PreSolveHandle.IsValid() && !PostSolveHandle.IsValid())
	{
		return;
	}
	const UWorld* World = GetWorld();
	FPhysScene* Scene = World ? World->GetPhysicsScene() : nullptr;
	if (Chaos::FPhysicsSolver* Solver = Scene ? Scene->GetSolver() : nullptr)
	{
		Solver->RemovePreAdvanceCallback(PreSolveHandle);
		Solver->RemovePostAdvanceCallback(PostSolveHandle);
	}
	PreSolveHandle.Reset();
	PostSolveHandle.Reset();
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

	// The tuning files are looked at from Tick, on fixed frames, instead of by the store's own poll: an
	// edit made during a run then takes effect on the same frame every time.
	FSandboxParamStore& Params = FSandboxParamStore::Get();
	Params.SetAutoPoll(false);

	if (Spec.Control == EScenarioBodyControl::Active)
	{
		// The body's tuning: the files of Config/Tuning (or -TuningDir=), with -Tune="name=value,..." on
		// top for this run only. Both are read by the store.
		if (Spec.HasEvent(TEXT("edit_tuning_file")) && !CopyTuningFiles(OutError))
		{
			return false;
		}
		if (!Params.EnsureLoaded(OutError) || !Ragdoll->StartActiveControl(Params, OutError))
		{
			return false;
		}
		const USandboxBodyController* Controller = Ragdoll->GetController();
		Controller->GetTuning().ListValues(LastTuningValues);
		HeadBone = Controller->GetTuning().Rig.Head;
	}

	// Where the body starts: the reference for the drift metrics. The pelvis is the root body.
	const USkeletalMeshComponent* BodyMesh = Ragdoll->GetBodyMesh();
	if (const FBodyInstance* Root = BodyMesh->GetBodyInstance())
	{
		PelvisBone = BodyMesh->GetBoneName(Root->InstanceBoneIndex);
		PelvisStartCm = Root->GetUnrealWorldTransform().GetLocation();
		PelvisStartHeightCm = PelvisStartCm.Z - Spec.SpawnLocationCm.Z;
		FinalPelvisHeightCm = PelvisStartHeightCm;
	}
	TArray<FSandboxBodySample> Bodies;
	FSandboxBodyProbe::SampleBodies(BodyMesh, Bodies);
	ComStartCm = FSandboxBodyProbe::CenterOfMass(Bodies);

	// Joint angles of the spawn pose against the joint limits.
	for (const FConstraintInstance* Constraint : BodyMesh->Constraints)
	{
		if (!Constraint)
		{
			continue;
		}
		const FVector Angles(
			FMath::RadiansToDegrees(Constraint->GetCurrentTwist()),
			FMath::RadiansToDegrees(Constraint->GetCurrentSwing1()),
			FMath::RadiansToDegrees(Constraint->GetCurrentSwing2()));
		SpawnJointAnglesDeg.Add(Constraint->JointName, Angles);
		SpawnJointOffsetCm.Add(Constraint->JointName, FSandboxBodyProbe::JointStretchCm(BodyMesh, *Constraint));

		const auto Excess = [](EAngularConstraintMotion Motion, double Angle, double Limit)
		{
			return Motion == ACM_Free ? 0.0 : FMath::Max(FMath::Abs(Angle) - (Motion == ACM_Limited ? Limit : 0.0), 0.0);
		};
		const double Violation = FMath::Max3(
			Excess(Constraint->GetAngularTwistMotion(), Angles.X, Constraint->GetAngularTwistLimit()),
			Excess(Constraint->GetAngularSwing1Motion(), Angles.Y, Constraint->GetAngularSwing1Limit()),
			Excess(Constraint->GetAngularSwing2Motion(), Angles.Z, Constraint->GetAngularSwing2Limit()));
		if (Violation > SpawnLimitViolationDeg)
		{
			SpawnLimitViolationDeg = Violation;
			SpawnWorstLimitJoint = Constraint->JointName;
		}
	}

	// The trace opens with the pose the body was spawned in, before any physics step (frame -1).
	Checkpoints.Add(MakeCheckpoint(0.0, -1, Bodies, BodyMesh->GetComponentSpaceTransforms()));
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

	// Solver time of the frame that has just been stepped. The first frames are left out: they carry
	// one-off set-up work.
	const double SolveMs = FPlatformTime::ToMilliseconds64(FrameSolveCycles.exchange(0));
	const int32 SolveSteps = FrameSolveSteps.exchange(0);
	if (FrameIndex >= JointSettleFrames && SolveSteps > 0)
	{
		PhysicsStepMs.Add(static_cast<float>(SolveMs));
		SolverStepsTotal += SolveSteps;
	}

	// Tickables run after the physics step of this frame, so the state seen here is after FrameIndex + 1 steps.
	const double SimTimeS = (FrameIndex + 1) * FixedDt;
	SampleFrame(DeltaTime);
	ContinuePushes();
	ApplyDueEvents(SimTimeS);

	// Hot reload: a look at the tuning files' size and time every few frames. It reads no clock and does
	// nothing while the files stay as they are (and nothing at all in a passive run, which never loads them).
	if (FrameIndex % FSandboxParamStore::AutoPollIntervalFrames == 0)
	{
		FSandboxParamStore::Get().PollFiles();
	}

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

	// The state sampled here is the one after FrameIndex + 1 steps.
	const bool bSettled = (FrameIndex + 1) * FixedDt > Spec.SettleS + DtToleranceS;

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
		if (bSettled)
		{
			MaxSettledSpeedCmS = FMath::Max(MaxSettledSpeedCmS, Speed);
			MaxSettledAngularSpeedRadS = FMath::Max(MaxSettledAngularSpeedRadS, AngularSpeed);
			FVector2D& Fastest = SettledBodySpeeds.FindOrAdd(Body.Bone, FVector2D::ZeroVector);
			Fastest.X = FMath::Max(Fastest.X, Speed);
			Fastest.Y = FMath::Max(Fastest.Y, AngularSpeed);
		}
		if (Body.Bone == PelvisBone)
		{
			FinalPelvisDriftCm = FVector::Dist(Location, PelvisStartCm);
			MaxPelvisDriftCm = FMath::Max(MaxPelvisDriftCm, FinalPelvisDriftCm);
			FinalPelvisHeightCm = Location.Z - Spec.SpawnLocationCm.Z;
			if (FallFrame < 0 && FinalPelvisHeightCm < FallenPelvisHeightFraction * PelvisStartHeightCm)
			{
				FallFrame = FrameIndex;
			}
		}
		if (Body.Bone == HeadBone)
		{
			MinHeadHeightCm = FMath::Min(MinHeadHeightCm, Body.CenterOfMass.Z - Spec.SpawnLocationCm.Z);
		}

		BodyHash = HashTransform(Body.WorldTransform, BodyHash);
	}
	if (!bAtRest)
	{
		LastActiveFrame = FrameIndex;
	}

	if (!bSawNaN)
	{
		MaxComDriftCm = FMath::Max(MaxComDriftCm, FVector::Dist2D(FSandboxBodyProbe::CenterOfMass(Bodies), ComStartCm));
	}

	FName FrameFastestJoint;
	const double JointSpeed = FSandboxBodyProbe::MaxJointAngularSpeedRadS(Mesh, &FrameFastestJoint);
	if (JointSpeed > MaxJointSpeedRadS)
	{
		MaxJointSpeedRadS = JointSpeed;
		FastestJoint = FrameFastestJoint;
	}

	// Joints pushed past their limits. The first frames are left out, as for stretch.
	if (FrameIndex >= JointSettleFrames)
	{
		for (const FConstraintInstance* Constraint : Mesh->Constraints)
		{
			const double Violation = Constraint ? JointLimitViolationDeg(*Constraint) : 0.0;
			if (Violation > MaxJointLimitViolationDeg)
			{
				MaxJointLimitViolationDeg = Violation;
				WorstLimitJoint = Constraint->JointName;
			}
		}
	}

	if (const USandboxBodyController* Controller = Ragdoll->GetController())
	{
		if (bSettled)
		{
			const FSandboxAssistLoad& Load = Controller->GetAssistLoad();
			AssistForceShareSum += Load.ForceShare;
			AssistForceShareMax = FMath::Max(AssistForceShareMax, Load.ForceShare);
			AssistTorqueShareSum += Load.TorqueShare;
			AssistTorqueShareMax = FMath::Max(AssistTorqueShareMax, Load.TorqueShare);
			++AssistSamples;
		}

		if (Controller->GetStateLog().Num() != SeenStateChanges)
		{
			// The timeline is kept in the run's own time, like everything else in the result: a state seen
			// here for the first time is the one the physics step of this frame was made in.
			for (int32 Index = SeenStateChanges; Index < Controller->GetStateLog().Num(); ++Index)
			{
				const FSandboxStateChange& Change = Controller->GetStateLog()[Index];
				const TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
				Entry->SetNumberField(TEXT("t"), FrameIndex * FixedDt);
				Entry->SetNumberField(TEXT("frame"), FrameIndex);
				Entry->SetStringField(TEXT("state"), FSandboxBodyTuning::StateName(Change.State));
				Entry->SetStringField(TEXT("reason"), Change.Reason);
				StateTimeline.Add(MakeShared<FJsonValueObject>(Entry));
			}

			// A new state brings its own tuning. That is not a tuning change: only an edited file or an
			// override counts as one, so the values to compare the next change against start here.
			SeenStateChanges = Controller->GetStateLog().Num();
			if (Controller->GetTuningChangeCount() == SeenTuningChanges)
			{
				Controller->GetTuning().ListValues(LastTuningValues);
			}
		}

		if (Controller->GetTuningChangeCount() != SeenTuningChanges)
		{
			// The controller took new values before this frame's physics step: an edited tuning file or an
			// override. Recorded with what changed, so the metrics can be read against it.
			SeenTuningChanges = Controller->GetTuningChangeCount();
			TArray<TPair<FString, double>> Values;
			Controller->GetTuning().ListValues(Values);
			const TSharedRef<FJsonObject> Changed = MakeShared<FJsonObject>();
			for (int32 Index = 0; Index < Values.Num() && Index < LastTuningValues.Num(); ++Index)
			{
				if (Values[Index].Value != LastTuningValues[Index].Value)
				{
					const TSharedRef<FJsonObject> FromTo = MakeShared<FJsonObject>();
					FromTo->SetNumberField(TEXT("from"), LastTuningValues[Index].Value);
					FromTo->SetNumberField(TEXT("to"), Values[Index].Value);
					Changed->SetObjectField(Values[Index].Key, FromTo);
				}
			}
			LastTuningValues = MoveTemp(Values);

			const TSharedRef<FJsonObject> Change = MakeShared<FJsonObject>();
			Change->SetNumberField(TEXT("frame"), FrameIndex);
			Change->SetNumberField(TEXT("t"), FrameIndex * FixedDt);
			Change->SetNumberField(TEXT("pelvis_height_cm"), FinalPelvisHeightCm);
			Change->SetObjectField(TEXT("values"), Changed);
			TuningChanges.Add(MakeShared<FJsonValueObject>(Change));
		}
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

		Checkpoints.Add(MakeCheckpoint(SimTimeS, FrameIndex, Bodies, Bones));
	}
}

void UScenarioRunnerSubsystem::ApplyDueEvents(double SimTimeS)
{
	USkeletalMeshComponent* Mesh = Ragdoll ? Ragdoll->GetBodyMesh() : nullptr;
	while (NextEventIndex < Spec.Events.Num() && Spec.Events[NextEventIndex].TimeS <= SimTimeS + DtToleranceS)
	{
		const FScenarioEvent& Event = Spec.Events[NextEventIndex++];
		if (Event.Type == TEXT("console"))
		{
			// The path a command typed into the console of a running session takes.
			if (GEngine && GEngine->Exec(&GetWorldRef(), *Event.Command))
			{
				++EventsApplied;
			}
			else
			{
				UE_LOG(LogSandbox, Error, TEXT("Scenario: event at t=%.3f: console command '%s' was not recognised"), Event.TimeS, *Event.Command);
			}
			continue;
		}
		if (Event.Type == TEXT("edit_tuning_file"))
		{
			FString Error;
			if (EditTuningFiles(Event, Error))
			{
				++EventsApplied;
			}
			else
			{
				UE_LOG(LogSandbox, Error, TEXT("Scenario: event at t=%.3f could not edit the tuning files: %s"), Event.TimeS, *Error);
			}
			continue;
		}

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
			// A blow lands at once. A push is spread evenly over the frames of its duration, the first
			// share now and the rest from ContinuePushes.
			const int32 Frames = FMath::Max(FMath::RoundToInt32(Event.DurationS / FixedDt), 1);
			const FVector ShareNs = Event.Direction * (Event.ImpulseNs / Frames);
			ApplyImpulse(Event.Body, ShareNs);
			if (Frames > 1)
			{
				Pushes.Add(FPush{ Event.Body, ShareNs, Frames - 1 });
			}
		}
		++EventsApplied;
	}
}

void UScenarioRunnerSubsystem::ApplyImpulse(FName Bone, const FVector& ImpulseNs)
{
	// An actively controlled body takes the impulse itself, so that it can react to being hit.
	if (USandboxBodyController* Controller = Ragdoll ? Ragdoll->GetController() : nullptr)
	{
		Controller->ApplyImpulse(Bone, ImpulseNs);
	}
	else if (FBodyInstance* Body = Ragdoll ? Ragdoll->GetBodyMesh()->GetBodyInstance(Bone) : nullptr)
	{
		// 1 N*s = 1 kg*m/s = 100 kg*cm/s in Unreal units.
		Body->AddImpulse(ImpulseNs * 100.0, /*bVelChange*/ false);
	}
}

void UScenarioRunnerSubsystem::ContinuePushes()
{
	for (int32 Index = Pushes.Num() - 1; Index >= 0; --Index)
	{
		ApplyImpulse(Pushes[Index].Body, Pushes[Index].ImpulsePerFrameNs);
		if (--Pushes[Index].FramesLeft <= 0)
		{
			Pushes.RemoveAt(Index);
		}
	}
}

bool UScenarioRunnerSubsystem::CopyTuningFiles(FString& OutError)
{
	FSandboxParamStore& Params = FSandboxParamStore::Get();
	const FString Source = Params.GetDirectory();
	const FString Copy = FPaths::Combine(OutDir, TEXT("tuning"));

	IFileManager& Files = IFileManager::Get();
	Files.DeleteDirectory(*Copy, /*RequireExists*/ false, /*Tree*/ true);
	Files.MakeDirectory(*Copy, /*Tree*/ true);
	TArray<FString> Names;
	Files.FindFiles(Names, *FPaths::Combine(Source, TEXT("*.json")), /*Files*/ true, /*Directories*/ false);
	if (Names.Num() == 0)
	{
		OutError = FString::Printf(TEXT("No tuning files (*.json) in '%s'"), *Source);
		return false;
	}
	for (const FString& Name : Names)
	{
		if (Files.Copy(*FPaths::Combine(Copy, Name), *FPaths::Combine(Source, Name)) != COPY_OK)
		{
			OutError = FString::Printf(TEXT("Cannot copy tuning file '%s' to '%s'"), *Name, *Copy);
			return false;
		}
	}
	Params.SetDirectory(Copy);
	bOwnTuningFiles = true;
	return true;
}

bool UScenarioRunnerSubsystem::EditTuningFiles(const FScenarioEvent& Event, FString& OutError)
{
	if (!bOwnTuningFiles)
	{
		OutError = TEXT("the run has no tuning files of its own (the body is not under active control)");
		return false;
	}

	// The values are written the way an editor would save them: into the file, and nothing else. The
	// running session has to notice the change by itself.
	const FSandboxParamStore& Params = FSandboxParamStore::Get();
	TMap<FString, TSharedPtr<FJsonObject>> Documents;
	for (const TPair<FString, double>& Value : Event.Values)
	{
		const FString File = Params.FindFile(Value.Key);
		if (File.IsEmpty())
		{
			OutError = FString::Printf(TEXT("'%s' is not in the tuning files"), *Value.Key);
			return false;
		}
		TSharedPtr<FJsonObject>& Document = Documents.FindOrAdd(File);
		if (!Document.IsValid() && !SandboxJson::LoadObject(FPaths::Combine(Params.GetDirectory(), File), Document, OutError))
		{
			return false;
		}

		TArray<FString> Path;
		Value.Key.ParseIntoArray(Path, TEXT("."));
		TSharedPtr<FJsonObject> Object = Document;
		for (int32 Index = 0; Index + 1 < Path.Num(); ++Index)
		{
			const TSharedPtr<FJsonObject>* Child = nullptr;
			if (!Object->TryGetObjectField(Path[Index], Child))
			{
				OutError = FString::Printf(TEXT("'%s' is not written as nested objects in %s"), *Value.Key, *File);
				return false;
			}
			Object = *Child;
		}
		Object->SetNumberField(Path.Last(), Value.Value);
	}
	for (const TPair<FString, TSharedPtr<FJsonObject>>& Document : Documents)
	{
		if (!SandboxJson::SaveObject(FPaths::Combine(Params.GetDirectory(), Document.Key), Document.Value.ToSharedRef(), OutError))
		{
			return false;
		}
	}
	return true;
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

	const USandboxBodyController* Controller = Ragdoll ? Ragdoll->GetController() : nullptr;
	const bool bHaveHead = MinHeadHeightCm != TNumericLimits<double>::Max();
	TArray<float> SortedStepMs = PhysicsStepMs;
	double PhysicsStepMeanMs = 0.0;
	for (const float Ms : SortedStepMs)
	{
		PhysicsStepMeanMs += Ms;
	}
	PhysicsStepMeanMs = SortedStepMs.Num() > 0 ? PhysicsStepMeanMs / SortedStepMs.Num() : 0.0;

	if (Spec.Control == EScenarioBodyControl::Active)
	{
		// One muscle per joint of the physics asset, or the body is only partly driven.
		const int32 Muscles = Controller ? Controller->GetMuscleCount() : 0;
		int32 Joints = 0;
		for (const FConstraintInstance* Constraint : Mesh ? Mesh->Constraints : TArray<FConstraintInstance*>())
		{
			Joints += Constraint && Constraint->GetLinearXMotion() == LCM_Locked && Constraint->GetLinearYMotion() == LCM_Locked &&
				Constraint->GetLinearZMotion() == LCM_Locked ? 1 : 0;
		}
		AddCheck(TEXT("every_joint_has_a_muscle"), Controller && Muscles == Joints, Number(Muscles), Number(Joints));
	}
	if (Spec.Expect.MaxPelvisDriftCm.IsSet())
	{
		const double Limit = Spec.Expect.MaxPelvisDriftCm.GetValue();
		AddCheck(TEXT("max_pelvis_drift_cm"), MaxPelvisDriftCm <= Limit, Number(MaxPelvisDriftCm), Number(Limit));
	}
	if (Spec.Expect.MinHeadHeightCm.IsSet())
	{
		const double Limit = Spec.Expect.MinHeadHeightCm.GetValue();
		AddCheck(TEXT("min_head_height_cm"), bHaveHead && MinHeadHeightCm >= Limit, bHaveHead ? Number(MinHeadHeightCm) : Null(), Number(Limit));
	}
	if (Spec.Expect.MaxSettledSpeedMps.IsSet())
	{
		const double Limit = Spec.Expect.MaxSettledSpeedMps.GetValue();
		AddCheck(TEXT("max_settled_speed_mps"), MaxSettledSpeedCmS * 0.01 <= Limit, Number(MaxSettledSpeedCmS * 0.01), Number(Limit));
	}
	if (Spec.Expect.MaxSettledAngularSpeedRadS.IsSet())
	{
		const double Limit = Spec.Expect.MaxSettledAngularSpeedRadS.GetValue();
		AddCheck(TEXT("max_settled_angular_speed_rads"), MaxSettledAngularSpeedRadS <= Limit, Number(MaxSettledAngularSpeedRadS), Number(Limit));
	}
	if (Spec.Expect.MinSettledAngularSpeedRadS.IsSet())
	{
		const double Limit = Spec.Expect.MinSettledAngularSpeedRadS.GetValue();
		AddCheck(TEXT("min_settled_angular_speed_rads"), MaxSettledAngularSpeedRadS >= Limit, Number(MaxSettledAngularSpeedRadS), Number(Limit));
	}
	if (Spec.Expect.MaxJointSpeedRadS.IsSet())
	{
		const double Limit = Spec.Expect.MaxJointSpeedRadS.GetValue();
		AddCheck(TEXT("max_joint_speed_rads"), MaxJointSpeedRadS <= Limit, Number(MaxJointSpeedRadS), Number(Limit));
	}
	if (Spec.Expect.MaxPhysicsStepMeanMs.IsSet())
	{
		const double Limit = Spec.Expect.MaxPhysicsStepMeanMs.GetValue();
		const bool bMeasured = SortedStepMs.Num() > 0;
		AddCheck(TEXT("max_physics_step_mean_ms"), bMeasured && PhysicsStepMeanMs <= Limit, bMeasured ? Number(PhysicsStepMeanMs) : Null(), Number(Limit));
	}

	const bool bFell = FallFrame >= 0;
	const double FallTimeS = (FallFrame + 1) * FixedDt;
	if (Spec.Expect.Fell.IsSet())
	{
		const bool bExpected = Spec.Expect.Fell.GetValue();
		AddCheck(TEXT("fell"), bFell == bExpected, Boolean(bFell), Boolean(bExpected));
	}
	if (Spec.Expect.MinFallTimeS.IsSet())
	{
		const double Limit = Spec.Expect.MinFallTimeS.GetValue();
		AddCheck(TEXT("min_fall_time_s"), !bFell || FallTimeS + DtToleranceS >= Limit, bFell ? Number(FallTimeS) : Null(), Number(Limit));
	}
	if (Spec.Expect.TuningChanges.IsSet())
	{
		const double Limit = Spec.Expect.TuningChanges.GetValue();
		AddCheck(TEXT("tuning_changes"), TuningChanges.Num() == Limit, Number(TuningChanges.Num()), Number(Limit));
	}
	if (Spec.Expect.MaxJointLimitViolationDeg.IsSet())
	{
		const double Limit = Spec.Expect.MaxJointLimitViolationDeg.GetValue();
		AddCheck(TEXT("max_joint_limit_violation_deg"), MaxJointLimitViolationDeg <= Limit, Number(MaxJointLimitViolationDeg), Number(Limit));
	}

	// The states the body went through, as names joined by '>'.
	FString StateSequence;
	FString EndState;
	if (Controller)
	{
		for (const FSandboxStateChange& Change : Controller->GetStateLog())
		{
			EndState = FSandboxBodyTuning::StateName(Change.State);
			StateSequence += (StateSequence.IsEmpty() ? TEXT("") : TEXT(">")) + EndState;
		}
	}
	const auto Text = [](const FString& Value) -> TSharedPtr<FJsonValue> { return MakeShared<FJsonValueString>(Value); };
	if (Spec.Expect.EndState.IsSet())
	{
		AddCheck(TEXT("end_state"), Controller && EndState == Spec.Expect.EndState.GetValue(), Controller ? Text(EndState) : Null(), Text(Spec.Expect.EndState.GetValue()));
	}
	if (Spec.Expect.StateSequence.IsSet())
	{
		AddCheck(TEXT("state_sequence"), Controller && StateSequence == Spec.Expect.StateSequence.GetValue(), Controller ? Text(StateSequence) : Null(), Text(Spec.Expect.StateSequence.GetValue()));
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
			if (const FVector2D* Fastest = SettledBodySpeeds.Find(Mesh->GetBoneName(Instance->InstanceBoneIndex)))
			{
				// Fastest this body moved and turned after the settle time: shows which body jitters.
				Entry->SetNumberField(TEXT("max_settled_speed_mps"), Fastest->X * 0.01);
				Entry->SetNumberField(TEXT("max_settled_angular_speed_rads"), Fastest->Y);
			}
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
			if (const FVector* Spawn = SpawnJointAnglesDeg.Find(Constraint->JointName))
			{
				// Where the joint was in the spawn pose, in the same terms as its limits.
				Entry->SetNumberField(TEXT("spawn_twist_deg"), Spawn->X);
				Entry->SetNumberField(TEXT("spawn_swing1_deg"), Spawn->Y);
				Entry->SetNumberField(TEXT("spawn_swing2_deg"), Spawn->Z);
			}
			if (const double* Offset = SpawnJointOffsetCm.Find(Constraint->JointName))
			{
				// Gap between the joint's two anchors in the spawn pose; the solver closes it at once.
				Entry->SetNumberField(TEXT("spawn_anchor_offset_cm"), *Offset);
			}
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
	Metrics->SetNumberField(TEXT("spawn_limit_violation_deg"), SpawnLimitViolationDeg);
	Metrics->SetStringField(TEXT("spawn_worst_limit_joint"), SpawnWorstLimitJoint.IsNone() ? FString() : SpawnWorstLimitJoint.ToString());
	Metrics->SetNumberField(TEXT("settle_s"), Spec.SettleS);
	Metrics->SetNumberField(TEXT("max_pelvis_drift_cm"), MaxPelvisDriftCm);
	Metrics->SetNumberField(TEXT("final_pelvis_drift_cm"), FinalPelvisDriftCm);
	Metrics->SetNumberField(TEXT("max_com_drift_cm"), MaxComDriftCm);
	Metrics->SetField(TEXT("min_head_height_cm"), bHaveHead ? Number(MinHeadHeightCm) : Null());
	Metrics->SetNumberField(TEXT("final_pelvis_height_cm"), FinalPelvisHeightCm);
	Metrics->SetNumberField(TEXT("fall_pelvis_height_cm"), FallenPelvisHeightFraction * PelvisStartHeightCm);
	Metrics->SetBoolField(TEXT("fell"), bFell);
	Metrics->SetField(TEXT("fall_time_s"), bFell ? Number(FallTimeS) : Null());
	Metrics->SetNumberField(TEXT("max_settled_speed_mps"), MaxSettledSpeedCmS * 0.01);
	Metrics->SetNumberField(TEXT("max_settled_angular_speed_rads"), MaxSettledAngularSpeedRadS);
	Metrics->SetNumberField(TEXT("max_joint_speed_rads"), MaxJointSpeedRadS);
	Metrics->SetStringField(TEXT("fastest_joint"), FastestJoint.IsNone() ? FString() : FastestJoint.ToString());
	Metrics->SetNumberField(TEXT("max_joint_limit_violation_deg"), MaxJointLimitViolationDeg);
	Metrics->SetStringField(TEXT("worst_limit_joint"), WorstLimitJoint.IsNone() ? FString() : WorstLimitJoint.ToString());
	Root->SetObjectField(TEXT("metrics"), Metrics);

	const TSharedRef<FJsonObject> ControlJson = MakeShared<FJsonObject>();
	ControlJson->SetStringField(TEXT("mode"), Controller ? TEXT("active") : TEXT("passive"));
	if (Controller)
	{
		ControlJson->SetNumberField(TEXT("muscles"), Controller->GetMuscleCount());
		ControlJson->SetNumberField(TEXT("assist_controls"), Controller->GetAssistCount());
		// Estimated load of the balance assist after the settle time: commanded force over body weight,
		// and commanded torque over weight times centre-of-mass height.
		const TSharedRef<FJsonObject> Assist = MakeShared<FJsonObject>();
		Assist->SetNumberField(TEXT("force_share_mean"), AssistSamples > 0 ? AssistForceShareSum / AssistSamples : 0.0);
		Assist->SetNumberField(TEXT("force_share_max"), AssistForceShareMax);
		Assist->SetNumberField(TEXT("torque_share_mean"), AssistSamples > 0 ? AssistTorqueShareSum / AssistSamples : 0.0);
		Assist->SetNumberField(TEXT("torque_share_max"), AssistTorqueShareMax);
		ControlJson->SetObjectField(TEXT("assist_load"), Assist);

		// The tuning the body had at the end of the run, where it came from and every change on the way,
		// so a result can always be traced back to its parameters.
		const TSharedRef<FJsonObject> TuningJson = MakeShared<FJsonObject>();
		TArray<TPair<FString, double>> TuningValues;
		Controller->GetTuning().ListValues(TuningValues);
		for (const TPair<FString, double>& Value : TuningValues)
		{
			TuningJson->SetNumberField(Value.Key, Value.Value);
		}
		ControlJson->SetObjectField(TEXT("tuning"), TuningJson);

		const FSandboxParamStore& Params = FSandboxParamStore::Get();
		const TSharedRef<FJsonObject> Source = MakeShared<FJsonObject>();
		Source->SetStringField(TEXT("directory"), Params.GetDirectory());
		TArray<FString> FileNames;
		Params.GetFileNames(FileNames);
		TArray<TSharedPtr<FJsonValue>> FileValues;
		for (const FString& FileName : FileNames)
		{
			FileValues.Add(MakeShared<FJsonValueString>(FileName));
		}
		Source->SetArrayField(TEXT("files"), FileValues);
		const TSharedRef<FJsonObject> OverridesJson = MakeShared<FJsonObject>();
		for (const TPair<FString, double>& Override : Params.GetOverrides())
		{
			OverridesJson->SetNumberField(Override.Key, Override.Value);
		}
		Source->SetObjectField(TEXT("overrides"), OverridesJson);
		ControlJson->SetObjectField(TEXT("tuning_source"), Source);

		// The state timeline: every state with the time it began and what caused it.
		ControlJson->SetArrayField(TEXT("states"), StateTimeline);
		ControlJson->SetStringField(TEXT("end_state"), EndState);
		ControlJson->SetStringField(TEXT("state_sequence"), StateSequence);
		ControlJson->SetArrayField(TEXT("tuning_changes"), TuningChanges);
	}
	Root->SetObjectField(TEXT("control"), ControlJson);

	if (SortedStepMs.Num() > 0)
	{
		// Wall-clock time of the solver per frame (all substeps together), headless or rendered.
		const TSharedRef<FJsonObject> Physics = Summarise(MoveTemp(SortedStepMs));
		Physics->SetNumberField(TEXT("frames"), PhysicsStepMs.Num());
		Physics->SetNumberField(TEXT("solver_steps_per_frame"), static_cast<double>(SolverStepsTotal) / PhysicsStepMs.Num());
		Root->SetObjectField(TEXT("physics_step_ms"), Physics);
	}

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
	StopPhysicsTimer();
	for (FPin& Pin : Pins)
	{
		Pin.Constraint->TermConstraint();
	}
	Pins.Reset();
	Phase = EPhase::Idle;
	FPlatformMisc::RequestExitWithStatus(/*Force*/ false, Code);
}
