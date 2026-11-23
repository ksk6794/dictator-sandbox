#include "ScenarioRunnerSubsystem.h"

#include "Camera/CameraActor.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/HUD.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpectatorPawn.h"
#include "HAL/PlatformMisc.h"
#include "DynamicRHI.h"
#include "Engine/Engine.h"
#include "Engine/OverlapResult.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "IHeadMountedDisplay.h"
#include "IXRTrackingSystem.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/Crc.h"
#include "Misc/EngineVersion.h"
#include "Misc/FileHelper.h"
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
#include "SandboxEffectorComponent.h"
#include "SandboxEffectorRig.h"
#include "SandboxJson.h"
#include "SandboxParamStore.h"
#include "SandboxRagdollActor.h"
#include "SandboxScriptedTargetSource.h"
#include "ScenarioStage.h"
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
	// A body that went down is at rest when it moves and turns no faster than this as a whole (root mean
	// square over its bodies, weighted by mass). The runner's own figures, whatever the tuning says.
	constexpr double RestRmsSpeedMps = 0.05;
	constexpr double RestRmsAngularSpeedRadS = 0.2;
	// Deepest two bodies of a character under control may overlap in its spawn pose, cm.
	constexpr double SpawnSelfPenetrationLimitCm = 0.1;
	// The floor's top face, which gravitational energy is counted from.
	constexpr double FloorZCm = 0.0;

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

void UScenarioRunnerSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// A scenario's world holds the body and whatever its file puts there, nothing else. The project's own
	// pawn has physical hands, so the player of a scenario run is a spectator without a body instead. The
	// game mode does not exist yet at this point; it is caught when it initialises.
	GameModeInitializedHandle = FGameModeEvents::OnGameModeInitializedEvent().AddWeakLambda(this, [this](AGameModeBase* GameMode)
	{
		if (GameMode && GameMode->GetWorld() == GetWorld())
		{
			GameMode->DefaultPawnClass = ASpectatorPawn::StaticClass();
			GameMode->PlayerControllerClass = APlayerController::StaticClass();
			GameMode->HUDClass = AHUD::StaticClass();
		}
	});
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

	// Screenshots at moments of the run, next to the final one. A comma separates the times, so the value
	// is read whole.
	FString Moments;
	if (!ShotPath.IsEmpty() && FParse::Value(FCommandLine::Get(), TEXT("ShotAt="), Moments, /*bShouldStopOnSeparator*/ false))
	{
		TArray<FString> Times;
		Moments.ParseIntoArray(Times, TEXT(","), /*bCullEmpty*/ true);
		for (const FString& Time : Times)
		{
			// The picture taken after frame N shows the state after N + 1 steps.
			const double AtS = FCString::Atod(*Time);
			const int32 Frame = FMath::Clamp(FMath::RoundToInt32(AtS / FixedDt) - 1, 0, TotalFrames - 1);
			MomentShots.Emplace(Frame, FPaths::Combine(FPaths::GetPath(ShotPath), FString::Printf(TEXT("shot_%.3f.png"), AtS)));
		}
	}

	// Decouple simulation time from wall-clock time. The tool scripts also pass -UseFixedTimeStep -FPS=
	// so that even the first frame is fixed; DtViolations reports any frame that was not.
	FApp::SetFixedDeltaTime(FixedDt);
	FApp::SetUseFixedTimeStep(true);

	Camera = FScenarioStage::SpawnEnvironment(InWorld, Spec, /*bSpawnCamera*/ true);
	if (!SpawnBody(InWorld, Error) || !SpawnEffectors(InWorld, Error))
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
	FGameModeEvents::OnGameModeInitializedEvent().Remove(GameModeInitializedHandle);
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

bool UScenarioRunnerSubsystem::SpawnEffectors(UWorld& World, FString& OutError)
{
	// Every "effector" event is a trajectory of one hand. The hands named by the events exist from the
	// start of the run, each resting on the first key of its first trajectory.
	const TSharedRef<FSandboxScriptedTargetSource> Source = MakeShared<FSandboxScriptedTargetSource>();
	for (const FScenarioEvent& Event : Spec.Events)
	{
		if (Event.Type == TEXT("effector") && !Source->AddPath(Event.Effector, Event.TimeS, Event.Path, OutError))
		{
			return false;
		}
	}
	TArray<FName> Names;
	Source->GetEffectors(Names);
	if (Names.Num() == 0)
	{
		return true;
	}

	FSandboxParamStore& Params = FSandboxParamStore::Get();
	if (!Params.EnsureLoaded(OutError))
	{
		return false;
	}
	EffectorRig = World.SpawnActor<ASandboxEffectorRig>();
	if (!EffectorRig || !EffectorRig->Start(Params, OutError))
	{
		return false;
	}
	for (const FName Name : Names)
	{
		FSandboxEffectorTarget Start;
		Source->Evaluate(Name, 0.0, Start);
		if (!EffectorRig->AddEffector(Name, FTransform(Start.Rotation, Start.Location), OutError))
		{
			return false;
		}
		EffectorFirstContactFrame.Add(-1);
		EffectorTracks.AddDefaulted();
	}
	EffectorSource = Source;
	EffectorRig->SetSource(EffectorSource);
	return true;
}

bool UScenarioRunnerSubsystem::SpawnBody(UWorld& World, FString& OutError)
{
	// The tuning files are looked at from Tick, on fixed frames, instead of by the store's own poll: an
	// edit made during a run then takes effect on the same frame every time.
	FSandboxParamStore& Params = FSandboxParamStore::Get();
	Params.SetAutoPoll(false);
	if (Spec.Control == EScenarioBodyControl::Active && Spec.HasEvent(TEXT("edit_tuning_file")) && !CopyTuningFiles(OutError))
	{
		return false;
	}

	Ragdoll = FScenarioStage::SpawnBody(World, Spec, OutError);
	if (!Ragdoll)
	{
		return false;
	}
	if (const USandboxBodyController* Controller = Ragdoll->GetController())
	{
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
	GravityCmS2 = FMath::Abs(World.GetGravityZ());

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

	// Where the hands should be at the start of the coming step, which begins at SimTimeS.
	if (EffectorRig)
	{
		EffectorSource->SetTime(SimTimeS);
		EffectorRig->UpdateTargets(static_cast<float>(FixedDt));
	}

	// Hot reload: a look at the tuning files' size and time every few frames. It reads no clock and does
	// nothing while the files stay as they are (and nothing at all in a passive run, which never loads them).
	if (FrameIndex % FSandboxParamStore::AutoPollIntervalFrames == 0)
	{
		FSandboxParamStore::Get().PollFiles();
	}

	for (const TPair<int32, FString>& Moment : MomentShots)
	{
		if (Moment.Key == FrameIndex && FApp::CanEverRender())
		{
			FScreenshotRequest::RequestScreenshot(Moment.Value, /*bInShowUI*/ false, /*bAddFilenameSuffix*/ false);
		}
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

	// The hands are part of the run: they are held to the same checks and go into the same hash. (A run
	// without hands hashes exactly what it did before there were any.)
	if (EffectorRig)
	{
		const TArray<TObjectPtr<USandboxEffectorComponent>>& Effectors = EffectorRig->GetEffectors();
		for (int32 Index = 0; Index < Effectors.Num(); ++Index)
		{
			const FBodyInstance* Body = Effectors[Index]->GetBodyInstance();
			const FTransform Transform = Body ? Body->GetUnrealWorldTransform() : FTransform::Identity;
			if (Transform.ContainsNaN())
			{
				bSawNaN = true;
				continue;
			}
			if (Transform.GetLocation().SizeSquared() > BoundsRadiusCm * BoundsRadiusCm)
			{
				bLeftBounds = true;
			}
			BodyHash = HashTransform(Transform, BodyHash);
			if (EffectorFirstContactFrame[Index] < 0 && Effectors[Index]->GetStats().ContactFrames > 0)
			{
				EffectorFirstContactFrame[Index] = FrameIndex;
			}
			TrackEffector(Index, *Effectors[Index], Transform.GetLocation(), Bodies);
		}
		PreviousBodies = Bodies;
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

		// The target pose: how far the muscles' targets are from the reference pose, and how well they hold it.
		FSandboxBodyControlView View;
		Controller->GetControlView(View);
		PoseOffsetMaxDeg = 0.0;
		PoseOffsetJoint = NAME_None;
		double ErrorSumDeg = 0.0;
		int32 Driven = 0;
		for (const FSandboxMuscleView& Muscle : View.Muscles)
		{
			const double OffsetDeg = FMath::RadiansToDegrees(Muscle.TargetOffsetRad);
			if (OffsetDeg > PoseOffsetMaxDeg)
			{
				PoseOffsetMaxDeg = OffsetDeg;
				PoseOffsetJoint = Muscle.ChildBone;
			}
			if (Muscle.bDriven)
			{
				const double ErrorDeg = FMath::RadiansToDegrees(Muscle.ErrorRad);
				ErrorSumDeg += ErrorDeg;
				++Driven;
				if (bSettled && Controller->GetState() == ESandboxBodyState::Standing && ErrorDeg > PoseErrorWorstDeg)
				{
					PoseErrorWorstDeg = ErrorDeg;
					PoseErrorWorstJoint = Muscle.ChildBone;
				}
			}
		}
		if (bSettled && Controller->GetState() == ESandboxBodyState::Standing && Driven > 0)
		{
			const double MeanDeg = ErrorSumDeg / Driven;
			PoseErrorSumDeg += MeanDeg;
			PoseErrorMeanMaxDeg = FMath::Max(PoseErrorMeanMaxDeg, MeanDeg);
			++PoseErrorFrames;
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
				if (Change.State == ESandboxBodyState::Falling && FallingStartFrame < 0)
				{
					FallingStartFrame = FrameIndex;
					LastUnrestFrame = FrameIndex;
					ComAtFallStartCm = FSandboxBodyProbe::CenterOfMass(Bodies);
				}
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
			bInputPending = true;
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

	// Energy, rest, balance and penetration of this frame, for any simulated body: summed up for the result
	// and written to series.csv. All of it is read from the bodies and asked of the scene; nothing is changed.
	if (Spec.bSimulate && !bSawNaN)
	{
		const USandboxBodyController* Controller = Ragdoll->GetController();
		const ESandboxBodyState State = Controller ? Controller->GetState() : ESandboxBodyState::Standing;
		const double KineticJ = FSandboxBodyProbe::KineticEnergyJ(Bodies);
		const double PotentialJ = FSandboxBodyProbe::PotentialEnergyJ(Bodies, GravityCmS2, FloorZCm);
		double RmsSpeedMps = 0.0;
		double RmsAngularSpeedRadS = 0.0;
		FSandboxBodyProbe::MassWeightedSpeeds(Bodies, RmsSpeedMps, RmsAngularSpeedRadS);
		PeakKineticJ = FMath::Max(PeakKineticJ, KineticJ);
		FinalKineticJ = KineticJ;
		FinalPotentialJ = PotentialJ;
		ComFinalCm = FSandboxBodyProbe::CenterOfMass(Bodies);

		// A hand that touched something in this step fed or drained the body, as a scripted impulse does.
		if (EffectorRig)
		{
			int32 ContactFrames = 0;
			for (const USandboxEffectorComponent* Effector : EffectorRig->GetEffectors())
			{
				ContactFrames += Effector->GetStats().ContactFrames;
			}
			bInputPending |= ContactFrames != SeenEffectorContactFrames;
			SeenEffectorContactFrames = ContactFrames;
		}

		// The energy of a body nothing acts on can only fall. A body under control is watched while it is
		// falling or lying, when its muscles are weak and its assist is off; upright it holds itself up.
		const bool bDown = Controller && (State == ESandboxBodyState::Falling || State == ESandboxBodyState::Lying);
		const bool bEnergyWatched = FrameIndex >= JointSettleFrames && (!Controller || bDown);
		if (!bEnergyWatched || bInputPending)
		{
			EnergyFloorJ = TNumericLimits<double>::Max();
		}
		if (bEnergyWatched)
		{
			const double TotalJ = KineticJ + PotentialJ;
			if (EnergyFloorJ != TNumericLimits<double>::Max() && TotalJ - EnergyFloorJ > MaxEnergyGainJ)
			{
				MaxEnergyGainJ = TotalJ - EnergyFloorJ;
				MaxEnergyGainFrame = FrameIndex;
			}
			EnergyFloorJ = FMath::Min(EnergyFloorJ, TotalJ);
		}
		bInputPending = false;

		if (FallingStartFrame >= 0 && (RmsSpeedMps > RestRmsSpeedMps || RmsAngularSpeedRadS > RestRmsAngularSpeedRadS))
		{
			LastUnrestFrame = FrameIndex;
		}

		// Balance, as the body senses it itself, while it is upright.
		FSandboxBalanceView Balance;
		if (Controller)
		{
			Controller->MeasureBalance(Balance);
		}
		const bool bUpright = Controller && (State == ESandboxBodyState::Standing || State == ESandboxBodyState::Flinch);
		if (Balance.bValid && bUpright && bSettled)
		{
			++BalanceFrames;
			++FeetFrames[FMath::Clamp(Balance.StandingFeet.Num(), 0, 2)];
			if (Balance.IsSupported())
			{
				++SupportedFrames;
				MinComMarginCm = FMath::Min(MinComMarginCm, Balance.CenterOfMassMarginCm);
				ComMarginSum += Balance.CenterOfMassMarginCm;
				MinCaptureMarginCm = FMath::Min(MinCaptureMarginCm, Balance.CaptureMarginCm);
				CaptureMarginSum += Balance.CaptureMarginCm;
				CaptureOutsideFrames += Balance.CaptureMarginCm < 0.0 ? 1 : 0;
				const double WeightN = FSandboxBodyProbe::TotalMassKg(Bodies) * GravityCmS2 * 0.01;
				SupportShareSum += WeightN > 0.0 ? (Balance.FootForceN[0] + Balance.FootForceN[1]) / WeightN : 0.0;
			}
			else
			{
				++UnsupportedFrames;
			}
		}

		// Penetration. The first frames may still be closing what the spawn pose overlapped.
		FSandboxPenetration Penetration;
		const AActor* const NotTheWorld[] = { EffectorRig.Get() };
		FSandboxBodyProbe::MeasurePenetration(Mesh, NotTheWorld, Penetration);
		UnmeasuredShapes = Penetration.UnmeasuredShapes;
		SelfCollisionPairs = Penetration.SelfPairs;
		if (FrameIndex < JointSettleFrames)
		{
			SpawnWorldPenetrationCm = FMath::Max(SpawnWorldPenetrationCm, Penetration.WorldCm);
			SpawnSelfPenetrationCm = FMath::Max(SpawnSelfPenetrationCm, Penetration.SelfCm);
		}
		else
		{
			if (Penetration.WorldCm > MaxWorldPenetrationCm)
			{
				MaxWorldPenetrationCm = Penetration.WorldCm;
				WorldPenetrationBone = Penetration.WorldBone;
				WorldPenetrationInto = Penetration.WorldInto;
				WorldPenetrationFrame = FrameIndex;
			}
			if (Penetration.SelfCm > MaxSelfPenetrationCm)
			{
				MaxSelfPenetrationCm = Penetration.SelfCm;
				SelfPenetrationBoneA = Penetration.SelfBoneA;
				SelfPenetrationBoneB = Penetration.SelfBoneB;
				SelfPenetrationFrame = FrameIndex;
			}
		}

		if (Series.IsEmpty())
		{
			Series = TEXT("t,state,feet,foot_left_n,foot_right_n,kinetic_j,potential_j,rms_speed_mps,rms_angular_speed_rads,com_margin_cm,capture_margin_cm,penetration_world_cm,penetration_self_cm,assist_force_share,fastest_joint_rads,fastest_joint\n");
		}
		const auto Optional = [](bool bHave, double Value, int32 Digits) { return bHave ? FString::Printf(TEXT("%.*f"), Digits, Value) : FString(); };
		Series += FString::Printf(TEXT("%.4f,%s,%s,%s,%s,%.3f,%.3f,%.4f,%.4f,%s,%s,%.3f,%.3f,%s,%.2f,%s\n"),
			(FrameIndex + 1) * FixedDt,
			Controller ? FSandboxBodyTuning::StateName(State) : TEXT("passive"),
			*Optional(Balance.bValid, Balance.StandingFeet.Num(), 0),
			*Optional(Balance.bValid, Balance.FootForceN[0], 1),
			*Optional(Balance.bValid, Balance.FootForceN[1], 1),
			KineticJ, PotentialJ, RmsSpeedMps, RmsAngularSpeedRadS,
			*Optional(Balance.bValid && Balance.IsSupported(), Balance.CenterOfMassMarginCm, 2),
			*Optional(Balance.bValid && Balance.IsSupported(), Balance.CaptureMarginCm, 2),
			Penetration.WorldCm, Penetration.SelfCm,
			*Optional(Controller != nullptr, Controller ? Controller->GetAssistLoad().ForceShare : 0.0, 4),
			JointSpeed, FrameFastestJoint.IsNone() ? TEXT("") : *FrameFastestJoint.ToString());
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
	// A body that is not simulated has no joints being solved: the gap between its anchors is the one the
	// asset was authored with, reported above, and not a stretch.
	else if (Spec.bSimulate && FrameIndex >= JointSettleFrames && Stretch > MaxJointStretchCm)
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

void UScenarioRunnerSubsystem::TrackEffector(int32 Index, const USandboxEffectorComponent& Effector, const FVector& Center, const TArray<FSandboxBodySample>& Bodies)
{
	// Measured from outside, with scene queries on the state after this frame's step. Queries change
	// nothing, and they do not depend on the solver's contacts: a hand the solver let through is seen here.
	FEffectorTrack& Track = EffectorTracks[Index];
	const FSandboxEffectorStats& Stats = Effector.GetStats();
	const USkeletalMeshComponent* Mesh = Ragdoll ? Ragdoll->GetBodyMesh() : nullptr;
	const UWorld& World = GetWorldRef();
	const double RadiusCm = Effector.GetScaledSphereRadius();
	const FCollisionShape Sphere = FCollisionShape::MakeSphere(static_cast<float>(RadiusCm));

	// Penetration: how far the sphere would have to move to be clear of each thing it overlaps.
	double DepthCm = 0.0;
	FName Into;
	if (Mesh)
	{
		for (const FBodyInstance* Body : Mesh->Bodies)
		{
			FMTDResult Separation;
			if (Body && Body->IsValidBodyInstance() && Body->OverlapTest(Center, FQuat::Identity, Sphere, &Separation) && Separation.Distance > DepthCm)
			{
				DepthCm = Separation.Distance;
				Into = Mesh->GetBoneName(Body->InstanceBoneIndex);
			}
		}
	}
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(ECC_WorldStatic);
	Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
	Objects.AddObjectTypesToQuery(ECC_PhysicsBody);
	FCollisionQueryParams NotTheHands(SCENE_QUERY_STAT(SandboxEffectorProbe), /*bTraceComplex*/ false);
	NotTheHands.AddIgnoredActor(EffectorRig);
	FCollisionQueryParams TheWorld = NotTheHands;
	if (Ragdoll)
	{
		// The character's bodies were asked one by one above.
		TheWorld.AddIgnoredActor(Ragdoll);
	}
	TArray<FOverlapResult> Overlaps;
	World.OverlapMultiByObjectType(Overlaps, Center, FQuat::Identity, Objects, Sphere, TheWorld);
	for (const FOverlapResult& Overlap : Overlaps)
	{
		UPrimitiveComponent* Component = Overlap.GetComponent();
		FMTDResult Separation;
		if (Component && Component->ComputePenetration(Separation, Sphere, Center, FQuat::Identity) && Separation.Distance > DepthCm)
		{
			DepthCm = Separation.Distance;
			Into = Component->GetOwner() ? Component->GetOwner()->GetFName() : Component->GetFName();
		}
	}
	if (DepthCm > Track.MaxPenetrationCm)
	{
		Track.MaxPenetrationCm = DepthCm;
		Track.DeepestInto = Into;
	}

	// Tunnelling: the centre of a sphere cannot get closer to a surface than its radius. If the straight
	// line from where the centre was a frame ago to where it is now crosses a surface, or the centre lies
	// inside a shape, the hand went through something. A snap is not motion and is left out; it fails the
	// run on its own.
	const bool bMoved = Track.bHaveLast && Stats.Snaps == Track.LastSnaps;
	FHitResult Crossed;
	const bool bCrossed = bMoved && World.LineTraceSingleByObjectType(Crossed, Track.LastCenter, Center, Objects, NotTheHands);
	if (bCrossed || DepthCm >= RadiusCm)
	{
		++Track.TunnelFrames;
		if (FirstTunnelFrame < 0)
		{
			FirstTunnelFrame = FrameIndex;
			FirstTunnelled = !bCrossed ? Into : !Crossed.BoneName.IsNone() ? Crossed.BoneName : Crossed.GetActor() ? Crossed.GetActor()->GetFName() : NAME_None;
		}
	}
	Track.LastCenter = Center;
	Track.LastSnaps = Stats.Snaps;
	Track.bHaveLast = true;

	// The struck body: the body of the character the effector touched first, and how far it moves from
	// where it was just before (the sample of the frame before the one that brought the contact).
	if (!Track.bFirstContactSeen && Stats.ContactFrames > 0)
	{
		Track.bFirstContactSeen = true;
		const FSandboxBodySample* Before = PreviousBodies.FindByPredicate([&Stats](const FSandboxBodySample& Body) { return Body.Bone == Stats.FirstTouched; });
		if (Before)
		{
			Track.StruckBone = Stats.FirstTouched;
			Track.StruckStart = Before->CenterOfMass;
		}
	}
	if (!Track.StruckBone.IsNone())
	{
		if (const FSandboxBodySample* Now = Bodies.FindByPredicate([&Track](const FSandboxBodySample& Body) { return Body.Bone == Track.StruckBone; }))
		{
			Track.StruckDisplacementCm = FMath::Max(Track.StruckDisplacementCm, FVector::Dist(Now->CenterOfMass, Track.StruckStart));
		}
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
				bInputPending = true;
			}
			else
			{
				UE_LOG(LogSandbox, Error, TEXT("Scenario: event at t=%.3f: console command '%s' was not recognised"), Event.TimeS, *Event.Command);
			}
			continue;
		}
		if (Event.Type == TEXT("effector"))
		{
			// Nothing to do at this moment: the trajectory is in the hands' target source, which follows
			// the run's time. The event counts as applied when its hand exists.
			if (EffectorRig && EffectorRig->FindEffector(Event.Effector))
			{
				++EventsApplied;
			}
			else
			{
				UE_LOG(LogSandbox, Error, TEXT("Scenario: event at t=%.3f names effector '%s', which does not exist"), Event.TimeS, *Event.Effector.ToString());
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
			const EAngularConstraintMotion Turning = Event.bLockRotation ? ACM_Locked : ACM_Free;
			Pin.Constraint->SetAngularSwing1Motion(Turning);
			Pin.Constraint->SetAngularSwing2Motion(Turning);
			Pin.Constraint->SetAngularTwistMotion(Turning);
			Pin.Constraint->SetRefFrame(EConstraintFrame::Frame1, FTransform::Identity);
			Pin.Constraint->SetRefFrame(EConstraintFrame::Frame2, BodyTransform);
			Pin.Constraint->InitConstraint(Body, nullptr, 1.0f, Ragdoll);
			bInputPending = true;
		}
		else if (Event.Type == TEXT("foot_target"))
		{
			USandboxBodyController* Controller = Ragdoll ? Ragdoll->GetController() : nullptr;
			const FVector Target = Body->GetUnrealWorldTransform().GetLocation() + Event.OffsetCm;
			FString Error;
			if (!Controller || !Controller->SetFootTarget(Event.Body, Target, Event.Weight, Error))
			{
				UE_LOG(LogSandbox, Error, TEXT("Scenario: event at t=%.3f could not place '%s': %s"), Event.TimeS, *Event.Body.ToString(),
					Controller ? *Error : TEXT("the body is not under active control"));
				continue;
			}
			FootTargets.RemoveAll([&Event](const FFootTarget& Placed) { return Placed.Body == Event.Body; });
			if (Event.Weight > 0.0)
			{
				FootTargets.Add(FFootTarget{ Event.Body, Target, Event.OffsetCm, Event.Weight });
			}
			bInputPending = true;
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

double UScenarioRunnerSubsystem::FootTargetErrorCm(const FFootTarget& Placed) const
{
	const FBodyInstance* Foot = Ragdoll ? Ragdoll->GetBodyMesh()->GetBodyInstance(Placed.Body) : nullptr;
	return Foot && Foot->IsValidBodyInstance() ? FVector::Dist(Foot->GetUnrealWorldTransform().GetLocation(), Placed.WorldLocation) : -1.0;
}

void UScenarioRunnerSubsystem::ApplyImpulse(FName Bone, const FVector& ImpulseNs)
{
	bInputPending = true;
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
	// The solver must keep its contacts in a fixed order (p.Chaos.Solver.Deterministic, set in
	// Config/DefaultEngine.ini): without it a run on a busy machine can differ from the one before it.
	const IConsoleVariable* SolverDeterministic = IConsoleManager::Get().FindConsoleVariable(TEXT("p.Chaos.Solver.Deterministic"));
	const int32 SolverDeterministicValue = SolverDeterministic ? SolverDeterministic->GetInt() : -1;
	AddCheck(TEXT("deterministic_solver"), SolverDeterministicValue == 1, Number(SolverDeterministicValue), Number(1));
	AddCheck(TEXT("all_events_applied"), EventsApplied == Spec.Events.Num(), Number(EventsApplied), Number(Spec.Events.Num()));
	if (Spec.Expect.RestWithinS.IsSet())
	{
		const double Limit = Spec.Expect.RestWithinS.GetValue();
		AddCheck(TEXT("rest_within_s"), bAtRestAtEnd && TimeToRestS <= Limit, bAtRestAtEnd ? Number(TimeToRestS) : Null(), Number(Limit));
	}
	if (Spec.Expect.MaxJointStretchCm.IsSet())
	{
		const double Limit = Spec.Expect.MaxJointStretchCm.GetValue();
		AddCheck(TEXT("max_joint_stretch_cm"), Spec.bSimulate && MaxJointStretchCm <= Limit, Spec.bSimulate ? Number(MaxJointStretchCm) : Null(), Number(Limit));
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
		// Bodies that may collide must not overlap in the pose the character is spawned in: they would push
		// each other apart from the first step on, and the pose could not be held.
		AddCheck(TEXT("spawn_pose_clear_of_itself"), SpawnSelfPenetrationCm <= SpawnSelfPenetrationLimitCm, Number(SpawnSelfPenetrationCm), Number(SpawnSelfPenetrationLimitCm));
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

	// The hands. A hand that was put back on its target did not get there by physics.
	double EffectorImpulseNs = 0.0;
	double EffectorForceN = 0.0;
	double EffectorFinalForceN = 0.0;
	int32 EffectorSnaps = 0;
	if (EffectorRig)
	{
		for (const USandboxEffectorComponent* Effector : EffectorRig->GetEffectors())
		{
			EffectorImpulseNs += Effector->GetStats().ContactImpulseNs;
			EffectorForceN = FMath::Max(EffectorForceN, Effector->GetMaxContactForceN());
			EffectorFinalForceN = FMath::Max(EffectorFinalForceN, Effector->GetContactForceN());
			EffectorSnaps += Effector->GetStats().Snaps;
		}
		AddCheck(TEXT("effectors_not_snapped"), EffectorSnaps == 0, Number(EffectorSnaps), Number(0));
	}

	// What the runner measured about the hands from outside. Tunnelling is never acceptable, so it is
	// checked in every run that has hands.
	double EffectorPenetrationCm = 0.0;
	int32 EffectorTunnelFrames = 0;
	double SlowestImpactMps = TNumericLimits<double>::Max();
	double SmallestStruckCm = TNumericLimits<double>::Max();
	if (EffectorRig)
	{
		const TArray<TObjectPtr<USandboxEffectorComponent>>& Effectors = EffectorRig->GetEffectors();
		for (int32 Index = 0; Index < Effectors.Num(); ++Index)
		{
			const FEffectorTrack& Track = EffectorTracks[Index];
			EffectorPenetrationCm = FMath::Max(EffectorPenetrationCm, Track.MaxPenetrationCm);
			EffectorTunnelFrames += Track.TunnelFrames;
			if (Effectors[Index]->GetStats().ContactFrames > 0)
			{
				SlowestImpactMps = FMath::Min(SlowestImpactMps, Effectors[Index]->GetStats().FirstContactSpeedCmS * 0.01);
			}
			if (!Track.StruckBone.IsNone())
			{
				SmallestStruckCm = FMath::Min(SmallestStruckCm, Track.StruckDisplacementCm);
			}
		}
		AddCheck(TEXT("effectors_not_tunnelled"), EffectorTunnelFrames == 0, Number(EffectorTunnelFrames), Number(0));
	}
	const bool bAnyImpact = SlowestImpactMps != TNumericLimits<double>::Max();
	const bool bAnyStruck = SmallestStruckCm != TNumericLimits<double>::Max();
	if (Spec.Expect.MaxEffectorPenetrationCm.IsSet())
	{
		const double Limit = Spec.Expect.MaxEffectorPenetrationCm.GetValue();
		AddCheck(TEXT("max_effector_penetration_cm"), EffectorRig && EffectorPenetrationCm <= Limit, EffectorRig ? Number(EffectorPenetrationCm) : Null(), Number(Limit));
	}
	if (Spec.Expect.MinEffectorImpactSpeedMps.IsSet())
	{
		const double Limit = Spec.Expect.MinEffectorImpactSpeedMps.GetValue();
		AddCheck(TEXT("min_effector_impact_speed_mps"), bAnyImpact && SlowestImpactMps >= Limit, bAnyImpact ? Number(SlowestImpactMps) : Null(), Number(Limit));
	}
	if (Spec.Expect.MinStruckDisplacementCm.IsSet())
	{
		const double Limit = Spec.Expect.MinStruckDisplacementCm.GetValue();
		AddCheck(TEXT("min_struck_displacement_cm"), bAnyStruck && SmallestStruckCm >= Limit, bAnyStruck ? Number(SmallestStruckCm) : Null(), Number(Limit));
	}
	if (Spec.Expect.MinEffectorImpulseNs.IsSet())
	{
		const double Limit = Spec.Expect.MinEffectorImpulseNs.GetValue();
		AddCheck(TEXT("min_effector_impulse_ns"), EffectorRig && EffectorImpulseNs >= Limit, EffectorRig ? Number(EffectorImpulseNs) : Null(), Number(Limit));
	}
	if (Spec.Expect.MaxEffectorImpulseNs.IsSet())
	{
		const double Limit = Spec.Expect.MaxEffectorImpulseNs.GetValue();
		AddCheck(TEXT("max_effector_impulse_ns"), EffectorRig && EffectorImpulseNs <= Limit, EffectorRig ? Number(EffectorImpulseNs) : Null(), Number(Limit));
	}
	if (Spec.Expect.MaxEffectorForceN.IsSet())
	{
		const double Limit = Spec.Expect.MaxEffectorForceN.GetValue();
		AddCheck(TEXT("max_effector_force_n"), EffectorRig && EffectorForceN <= Limit, EffectorRig ? Number(EffectorForceN) : Null(), Number(Limit));
	}
	if (Spec.Expect.MinEffectorFinalForceN.IsSet())
	{
		const double Limit = Spec.Expect.MinEffectorFinalForceN.GetValue();
		AddCheck(TEXT("min_effector_final_force_n"), EffectorRig && EffectorFinalForceN >= Limit, EffectorRig ? Number(EffectorFinalForceN) : Null(), Number(Limit));
	}
	if (Spec.Expect.MaxEffectorFinalForceN.IsSet())
	{
		const double Limit = Spec.Expect.MaxEffectorFinalForceN.GetValue();
		AddCheck(TEXT("max_effector_final_force_n"), EffectorRig && EffectorFinalForceN <= Limit, EffectorRig ? Number(EffectorFinalForceN) : Null(), Number(Limit));
	}

	// Balance, energy, the end of the fall and penetration.
	const bool bHaveMargins = Controller && SupportedFrames > 0;
	if (Spec.Expect.MinComMarginCm.IsSet())
	{
		const double Limit = Spec.Expect.MinComMarginCm.GetValue();
		AddCheck(TEXT("min_com_margin_cm"), bHaveMargins && UnsupportedFrames == 0 && MinComMarginCm >= Limit, bHaveMargins ? Number(MinComMarginCm) : Null(), Number(Limit));
	}
	if (Spec.Expect.MinCaptureMarginCm.IsSet())
	{
		const double Limit = Spec.Expect.MinCaptureMarginCm.GetValue();
		AddCheck(TEXT("min_capture_margin_cm"), bHaveMargins && UnsupportedFrames == 0 && MinCaptureMarginCm >= Limit, bHaveMargins ? Number(MinCaptureMarginCm) : Null(), Number(Limit));
	}
	if (Spec.Expect.MaxEnergyGainJ.IsSet())
	{
		const double Limit = Spec.Expect.MaxEnergyGainJ.GetValue();
		AddCheck(TEXT("max_energy_gain_j"), Spec.bSimulate && MaxEnergyGainJ <= Limit, Spec.bSimulate ? Number(MaxEnergyGainJ) : Null(), Number(Limit));
	}
	const bool bWentDown = FallingStartFrame >= 0;
	const bool bRestAfterFall = bWentDown && LastUnrestFrame < TotalFrames - 1;
	const double RestAfterFallS = (LastUnrestFrame + 1 - FallingStartFrame) * FixedDt;
	if (Spec.Expect.MaxRestAfterFallS.IsSet())
	{
		const double Limit = Spec.Expect.MaxRestAfterFallS.GetValue();
		AddCheck(TEXT("max_rest_after_fall_s"), bRestAfterFall && RestAfterFallS <= Limit + DtToleranceS, bRestAfterFall ? Number(RestAfterFallS) : Null(), Number(Limit));
	}
	if (Spec.Expect.MaxBodyPenetrationCm.IsSet())
	{
		const double Limit = Spec.Expect.MaxBodyPenetrationCm.GetValue();
		AddCheck(TEXT("max_body_penetration_cm"), Spec.bSimulate && MaxWorldPenetrationCm <= Limit, Spec.bSimulate ? Number(MaxWorldPenetrationCm) : Null(), Number(Limit));
	}
	if (Spec.Expect.MaxSelfPenetrationCm.IsSet())
	{
		const double Limit = Spec.Expect.MaxSelfPenetrationCm.GetValue();
		AddCheck(TEXT("max_self_penetration_cm"), Spec.bSimulate && MaxSelfPenetrationCm <= Limit, Spec.bSimulate ? Number(MaxSelfPenetrationCm) : Null(), Number(Limit));
	}
	if (Spec.Expect.MaxPoseErrorMeanDeg.IsSet())
	{
		const double Limit = Spec.Expect.MaxPoseErrorMeanDeg.GetValue();
		const double Mean = PoseErrorFrames > 0 ? PoseErrorSumDeg / PoseErrorFrames : 0.0;
		AddCheck(TEXT("max_pose_error_mean_deg"), PoseErrorFrames > 0 && Mean <= Limit, PoseErrorFrames > 0 ? Number(Mean) : Null(), Number(Limit));
	}
	if (Spec.Expect.MaxFootTargetErrorCm.IsSet())
	{
		const double Limit = Spec.Expect.MaxFootTargetErrorCm.GetValue();
		double Worst = -1.0;
		for (const FFootTarget& Placed : FootTargets)
		{
			Worst = FMath::Max(Worst, FootTargetErrorCm(Placed));
		}
		AddCheck(TEXT("max_foot_target_error_cm"), Worst >= 0.0 && Worst <= Limit, Worst >= 0.0 ? Number(Worst) : Null(), Number(Limit));
	}
	if (Spec.Expect.MinPoseOffsetDeg.IsSet())
	{
		const double Limit = Spec.Expect.MinPoseOffsetDeg.GetValue();
		AddCheck(TEXT("min_pose_offset_deg"), PoseOffsetMaxDeg >= Limit, Number(PoseOffsetMaxDeg), Number(Limit));
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
			// Principal moments of inertia as the solver uses them: from mass and shapes, times the asset's
			// inertia scale. Inertia conditioning of the solver, if any, is not included.
			const FVector Inertia = Instance->GetBodyInertiaTensor();
			TArray<TSharedPtr<FJsonValue>> InertiaJson;
			for (int32 Axis = 0; Axis < 3; ++Axis)
			{
				InertiaJson.Add(MakeShared<FJsonValueNumber>(Inertia[Axis]));
			}
			Entry->SetArrayField(TEXT("inertia_kg_cm2"), InertiaJson);
			// Where the body's centre of mass is at the end of the run, relative to the spawn point.
			const FVector Final = Instance->GetCOMPosition() - Spec.SpawnLocationCm;
			TArray<TSharedPtr<FJsonValue>> FinalJson;
			for (int32 Axis = 0; Axis < 3; ++Axis)
			{
				FinalJson.Add(MakeShared<FJsonValueNumber>(Final[Axis]));
			}
			Entry->SetArrayField(TEXT("final_location_cm"), FinalJson);
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
	// Null for a body that is not simulated: nothing solves its joints, so there is no stretch to measure.
	Metrics->SetField(TEXT("max_joint_stretch_cm"), Spec.bSimulate ? Number(MaxJointStretchCm) : Null());
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

		// The target pose and how well the muscles hold it while the body stands.
		const TSharedRef<FJsonObject> PoseJson = MakeShared<FJsonObject>();
		PoseJson->SetNumberField(TEXT("target_offset_max_deg"), PoseOffsetMaxDeg);
		PoseJson->SetStringField(TEXT("target_offset_joint"), PoseOffsetJoint.IsNone() ? FString() : PoseOffsetJoint.ToString());
		PoseJson->SetNumberField(TEXT("error_mean_deg"), PoseErrorFrames > 0 ? PoseErrorSumDeg / PoseErrorFrames : 0.0);
		PoseJson->SetNumberField(TEXT("error_mean_max_deg"), PoseErrorMeanMaxDeg);
		PoseJson->SetNumberField(TEXT("error_worst_deg"), PoseErrorWorstDeg);
		PoseJson->SetStringField(TEXT("error_worst_joint"), PoseErrorWorstJoint.IsNone() ? FString() : PoseErrorWorstJoint.ToString());
		PoseJson->SetNumberField(TEXT("frames"), PoseErrorFrames);
		ControlJson->SetObjectField(TEXT("pose"), PoseJson);

		// Feet the target pose was asked to place, and how far each is from its point at the end.
		TArray<TSharedPtr<FJsonValue>> FeetJson;
		for (const FFootTarget& Placed : FootTargets)
		{
			const TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("bone"), Placed.Body.ToString());
			TArray<TSharedPtr<FJsonValue>> Offset;
			for (int32 Axis = 0; Axis < 3; ++Axis)
			{
				Offset.Add(MakeShared<FJsonValueNumber>(Placed.OffsetCm[Axis]));
			}
			Entry->SetArrayField(TEXT("offset_cm"), Offset);
			Entry->SetNumberField(TEXT("weight"), Placed.Weight);
			Entry->SetNumberField(TEXT("final_error_cm"), FootTargetErrorCm(Placed));
			FeetJson.Add(MakeShared<FJsonValueObject>(Entry));
		}
		ControlJson->SetArrayField(TEXT("foot_targets"), FeetJson);

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

		// Balance as the body senses it, over the settled frames in which it was upright.
		const TSharedRef<FJsonObject> BalanceJson = MakeShared<FJsonObject>();
		BalanceJson->SetNumberField(TEXT("frames"), BalanceFrames);
		BalanceJson->SetNumberField(TEXT("frames_no_foot"), FeetFrames[0]);
		BalanceJson->SetNumberField(TEXT("frames_one_foot"), FeetFrames[1]);
		BalanceJson->SetNumberField(TEXT("frames_both_feet"), FeetFrames[2]);
		BalanceJson->SetField(TEXT("com_margin_min_cm"), bHaveMargins ? Number(MinComMarginCm) : Null());
		BalanceJson->SetField(TEXT("com_margin_mean_cm"), bHaveMargins ? Number(ComMarginSum / SupportedFrames) : Null());
		BalanceJson->SetField(TEXT("capture_margin_min_cm"), bHaveMargins ? Number(MinCaptureMarginCm) : Null());
		BalanceJson->SetField(TEXT("capture_margin_mean_cm"), bHaveMargins ? Number(CaptureMarginSum / SupportedFrames) : Null());
		BalanceJson->SetNumberField(TEXT("capture_outside_frames"), CaptureOutsideFrames);
		// The floor's push on the two feet over the body's weight: 1 when the feet carry all of it.
		BalanceJson->SetField(TEXT("support_share_mean"), bHaveMargins ? Number(SupportShareSum / SupportedFrames) : Null());
		ControlJson->SetObjectField(TEXT("balance"), BalanceJson);

		// The fall: when the controller let the body go, how long until it lay still, and which way it went
		// (the angle of the centre of mass's way over the floor: 0 is +x, 90 is +y).
		const TSharedRef<FJsonObject> FallJson = MakeShared<FJsonObject>();
		const FVector FallWay = ComFinalCm - ComAtFallStartCm;
		FallJson->SetField(TEXT("started_t"), bWentDown ? Number(FallingStartFrame * FixedDt) : Null());
		FallJson->SetField(TEXT("rest_after_s"), bRestAfterFall ? Number(RestAfterFallS) : Null());
		FallJson->SetField(TEXT("distance_cm"), bWentDown ? Number(FallWay.Size2D()) : Null());
		FallJson->SetField(TEXT("direction_deg"), bWentDown ? Number(FMath::Fmod(FMath::RadiansToDegrees(FMath::Atan2(FallWay.Y, FallWay.X)) + 360.0, 360.0)) : Null());
		ControlJson->SetObjectField(TEXT("fall"), FallJson);

		// What the body took from contacts with other simulated bodies: the hits it could feel.
		ControlJson->SetNumberField(TEXT("contact_impulse_ns"), Controller->GetContactImpulseNs());
		ControlJson->SetNumberField(TEXT("contact_reports"), Controller->GetContactReportCount());
	}
	Root->SetObjectField(TEXT("control"), ControlJson);

	if (EffectorRig)
	{
		// The player's hands: what each one touched and how hard, from its own contact events.
		const TSharedRef<FJsonObject> EffectorsJson = MakeShared<FJsonObject>();
		EffectorsJson->SetNumberField(TEXT("contact_impulse_ns"), EffectorImpulseNs);
		EffectorsJson->SetNumberField(TEXT("max_contact_force_n"), EffectorForceN);
		EffectorsJson->SetNumberField(TEXT("tuning_changes"), EffectorRig->GetTuningChangeCount());
		// Measured by the runner with scene queries after every frame, not taken from the solver's contacts.
		EffectorsJson->SetNumberField(TEXT("max_penetration_cm"), EffectorPenetrationCm);
		EffectorsJson->SetNumberField(TEXT("tunnel_frames"), EffectorTunnelFrames);
		EffectorsJson->SetField(TEXT("first_tunnel_frame"), FirstTunnelFrame >= 0 ? Number(FirstTunnelFrame) : Null());
		EffectorsJson->SetField(TEXT("first_tunnel_t"), FirstTunnelFrame >= 0 ? Number(FirstTunnelFrame * FixedDt) : Null());
		EffectorsJson->SetStringField(TEXT("first_tunnelled"), FirstTunnelled.IsNone() ? FString() : FirstTunnelled.ToString());

		const TSharedRef<FJsonObject> EffectorTuningJson = MakeShared<FJsonObject>();
		TArray<TPair<FString, double>> EffectorValues;
		EffectorRig->GetTuning().ListValues(EffectorValues);
		for (const TPair<FString, double>& Value : EffectorValues)
		{
			if (Value.Key.StartsWith(TEXT("effectors.")))
			{
				EffectorTuningJson->SetNumberField(Value.Key, Value.Value);
			}
		}
		EffectorsJson->SetObjectField(TEXT("tuning"), EffectorTuningJson);

		const TSharedRef<FJsonObject> List = MakeShared<FJsonObject>();
		const TArray<TObjectPtr<USandboxEffectorComponent>>& Effectors = EffectorRig->GetEffectors();
		for (int32 Index = 0; Index < Effectors.Num(); ++Index)
		{
			const USandboxEffectorComponent* Effector = Effectors[Index];
			const FSandboxEffectorStats& Stats = Effector->GetStats();
			const TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetNumberField(TEXT("mass_kg"), Effector->GetMass());
			Entry->SetNumberField(TEXT("contact_impulse_ns"), Stats.ContactImpulseNs);
			Entry->SetNumberField(TEXT("max_contact_force_n"), Effector->GetMaxContactForceN());
			Entry->SetNumberField(TEXT("final_contact_force_n"), Effector->GetContactForceN());
			Entry->SetNumberField(TEXT("contact_frames"), Stats.ContactFrames);
			const int32 FirstFrame = EffectorFirstContactFrame[Index];
			Entry->SetField(TEXT("first_contact_frame"), FirstFrame >= 0 ? Number(FirstFrame) : Null());
			Entry->SetField(TEXT("first_contact_t"), FirstFrame >= 0 ? Number(FirstFrame * FixedDt) : Null());
			const TSharedRef<FJsonObject> Touched = MakeShared<FJsonObject>();
			for (const TPair<FName, double>& TouchedBody : Stats.ImpulseByBody)
			{
				Touched->SetNumberField(TouchedBody.Key.ToString(), TouchedBody.Value);
			}
			Entry->SetObjectField(TEXT("touched_impulse_ns"), Touched);
			Entry->SetNumberField(TEXT("max_gap_cm"), Stats.MaxGapCm);
			Entry->SetNumberField(TEXT("final_gap_cm"), Effector->GetGapCm());
			Entry->SetNumberField(TEXT("max_speed_mps"), Stats.MaxSpeedCmS * 0.01);
			Entry->SetNumberField(TEXT("max_demand_n"), Stats.MaxDemandN);
			Entry->SetNumberField(TEXT("snaps"), Stats.Snaps);
			// The strike: what was touched first and how fast the hand arrived, then what the runner measured.
			const FEffectorTrack& Track = EffectorTracks[Index];
			Entry->SetStringField(TEXT("first_touched"), Stats.FirstTouched.IsNone() ? FString() : Stats.FirstTouched.ToString());
			Entry->SetField(TEXT("first_contact_speed_mps"), Stats.ContactFrames > 0 ? Number(Stats.FirstContactSpeedCmS * 0.01) : Null());
			Entry->SetStringField(TEXT("struck_bone"), Track.StruckBone.IsNone() ? FString() : Track.StruckBone.ToString());
			Entry->SetField(TEXT("struck_displacement_cm"), Track.StruckBone.IsNone() ? Null() : Number(Track.StruckDisplacementCm));
			Entry->SetNumberField(TEXT("max_penetration_cm"), Track.MaxPenetrationCm);
			Entry->SetStringField(TEXT("deepest_into"), Track.DeepestInto.IsNone() ? FString() : Track.DeepestInto.ToString());
			Entry->SetNumberField(TEXT("tunnel_frames"), Track.TunnelFrames);
			Entry->SetArrayField(TEXT("final_location_cm"), SandboxJson::VectorToArray(Effector->GetComponentLocation()));
			List->SetObjectField(Effector->GetId().ToString(), Entry);
		}
		EffectorsJson->SetObjectField(TEXT("list"), List);
		Root->SetObjectField(TEXT("effectors"), EffectorsJson);
	}

	if (Spec.bSimulate)
	{
		// Energy of the bodies: kinetic (translation and rotation) and gravitational, above the floor.
		const TSharedRef<FJsonObject> EnergyJson = MakeShared<FJsonObject>();
		EnergyJson->SetNumberField(TEXT("peak_kinetic_j"), PeakKineticJ);
		EnergyJson->SetNumberField(TEXT("final_kinetic_j"), FinalKineticJ);
		EnergyJson->SetNumberField(TEXT("final_potential_j"), FinalPotentialJ);
		EnergyJson->SetNumberField(TEXT("max_gain_j"), MaxEnergyGainJ);
		EnergyJson->SetField(TEXT("max_gain_t"), MaxEnergyGainFrame >= 0 ? Number((MaxEnergyGainFrame + 1) * FixedDt) : Null());
		Root->SetObjectField(TEXT("energy"), EnergyJson);

		// Penetration of the body into the world and into itself, by scene queries after every frame.
		const auto NameOrEmpty = [](FName Name) { return Name.IsNone() ? FString() : Name.ToString(); };
		const TSharedRef<FJsonObject> PenetrationJson = MakeShared<FJsonObject>();
		PenetrationJson->SetNumberField(TEXT("max_world_cm"), MaxWorldPenetrationCm);
		PenetrationJson->SetStringField(TEXT("world_bone"), NameOrEmpty(WorldPenetrationBone));
		PenetrationJson->SetStringField(TEXT("world_into"), NameOrEmpty(WorldPenetrationInto));
		PenetrationJson->SetField(TEXT("world_t"), WorldPenetrationFrame >= 0 ? Number((WorldPenetrationFrame + 1) * FixedDt) : Null());
		PenetrationJson->SetNumberField(TEXT("max_self_cm"), MaxSelfPenetrationCm);
		PenetrationJson->SetStringField(TEXT("self_bone_a"), NameOrEmpty(SelfPenetrationBoneA));
		PenetrationJson->SetStringField(TEXT("self_bone_b"), NameOrEmpty(SelfPenetrationBoneB));
		PenetrationJson->SetField(TEXT("self_t"), SelfPenetrationFrame >= 0 ? Number((SelfPenetrationFrame + 1) * FixedDt) : Null());
		PenetrationJson->SetNumberField(TEXT("spawn_world_cm"), SpawnWorldPenetrationCm);
		PenetrationJson->SetNumberField(TEXT("spawn_self_cm"), SpawnSelfPenetrationCm);
		PenetrationJson->SetNumberField(TEXT("unmeasured_shapes"), UnmeasuredShapes);
		// Pairs of bodies the physics asset lets collide. With 0 the body passes through itself, and max_self_cm reads 0 whatever happens.
		PenetrationJson->SetNumberField(TEXT("self_collision_pairs"), SelfCollisionPairs);
		Root->SetObjectField(TEXT("penetration"), PenetrationJson);
	}

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
	Xr->SetBoolField(TEXT("stereo"), FScenarioStage::IsStereo());
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
	bool bSaved =
		SandboxJson::SaveObject(FPaths::Combine(OutDir, TEXT("trace.json")), Trace, Error, /*bPretty*/ false) &&
		SandboxJson::SaveObject(FPaths::Combine(OutDir, TEXT("metrics.json")), Root, Error);
	const FString SeriesFile = FPaths::Combine(OutDir, TEXT("series.csv"));
	if (bSaved && !Series.IsEmpty() && !FFileHelper::SaveStringToFile(Series, *SeriesFile, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		Error = FString::Printf(TEXT("Cannot write '%s'"), *SeriesFile);
		bSaved = false;
	}
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
		if (Camera && Mesh && Mesh->Bodies.Num() > 0 && !Spec.bHasCamera && !FScenarioStage::IsStereo())
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
