#include "SandboxPlaySubsystem.h"

#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "PhysicsEngine/BodyInstance.h"
#include "SandboxBodyController.h"
#include "SandboxCore.h"
#include "SandboxEffectorComponent.h"
#include "SandboxEffectorRig.h"
#include "SandboxJson.h"
#include "SandboxOverlaySubsystem.h"
#include "SandboxParamStore.h"
#include "SandboxPawn.h"
#include "SandboxRagdollActor.h"
#include "SandboxTargetRecording.h"
#include "ScenarioStage.h"
#include "UnrealClient.h"

namespace
{
	// A frame that is this late is not caught up with: after a hitch the session carries on from now.
	constexpr double MaxLatenessS = 0.25;
	// The last stretch before a frame is due is waited out in small yields: a sleep is not that exact.
	constexpr double SleepMarginS = 0.002;
	// How many frames the frame-rate line of the log covers.
	constexpr int32 ReportFrames = 900;
	// Frames rendered after the screenshot request so that it is serviced before the process exits.
	constexpr int32 ScreenshotFrames = 30;
}

bool USandboxPlaySubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	FString Unused;
	return FParse::Value(FCommandLine::Get(), TEXT("Play="), Unused) && Super::ShouldCreateSubsystem(Outer);
}

bool USandboxPlaySubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId USandboxPlaySubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(USandboxPlaySubsystem, STATGROUP_Tickables);
}

void USandboxPlaySubsystem::Fail(const FString& Message)
{
	UE_LOG(LogSandbox, Error, TEXT("Play session cannot start: %s"), *Message);
	if (bSmoke || bDemo)
	{
		const TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetBoolField(TEXT("ok"), false);
		Root->SetBoolField(TEXT("pass"), false);
		Root->SetStringField(TEXT("error"), Message);
		FString Error;
		SandboxJson::SaveObject(FPaths::Combine(OutDir, TEXT("play.json")), Root, Error);
		FPlatformMisc::RequestExitWithStatus(/*Force*/ false, 2);
	}
}

void USandboxPlaySubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	FString ScenarioArg;
	FParse::Value(FCommandLine::Get(), TEXT("Play="), ScenarioArg);
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

	bSmoke = FParse::Param(FCommandLine::Get(), TEXT("PlaySmoke"));
	bDemo = !bSmoke && FParse::Param(FCommandLine::Get(), TEXT("PlayDemo"));
	bRecordFromStart = FParse::Value(FCommandLine::Get(), TEXT("Record="), RecordName) && !RecordName.IsEmpty() && !bDemo && !bSmoke;
	if (!FParse::Value(FCommandLine::Get(), TEXT("Out="), OutDir))
	{
		OutDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Play"), FPaths::GetBaseFilename(ScenarioFile));
	}
	OutDir = FPaths::ConvertRelativePathToFull(OutDir);
	if (FParse::Value(FCommandLine::Get(), TEXT("Shot="), ShotPath))
	{
		ShotPath = FPaths::ConvertRelativePathToFull(ShotPath);
	}

	FString Error;
	if (!FScenarioSpec::LoadFile(ScenarioFile, Spec, Error))
	{
		Fail(Error);
		return;
	}

	// The same physics timing as a scenario: every frame is one fixed step of the simulation.
	FApp::SetFixedDeltaTime(Spec.FixedDt());
	FApp::SetUseFixedTimeStep(true);

	FScenarioStage::SpawnEnvironment(InWorld, Spec, /*bSpawnCamera*/ false);
	Character = FScenarioStage::SpawnBody(InWorld, Spec, Error);
	if (!Character)
	{
		Fail(Error);
		return;
	}
	if (const FBodyInstance* Root = Character->GetBodyMesh()->GetBodyInstance())
	{
		SmokeSpawnPelvis = Root->GetUnrealWorldTransform().GetLocation();
	}

	// The player was spawned when the map opened but has not begun play, so its hands do not exist yet:
	// placed now, they are made in front of the camera where the scenario has it.
	AdoptPawn(InWorld);
	bActive = true;
	UE_LOG(LogSandbox, Display, TEXT("Play session on '%s'%s"), *Spec.Name, bSmoke ? TEXT(" (self-test)") : TEXT(""));
}

bool USandboxPlaySubsystem::AdoptPawn(UWorld& World)
{
	APlayerController* PlayerController = World.GetFirstPlayerController();
	ASandboxPawn* Found = PlayerController ? Cast<ASandboxPawn>(PlayerController->GetPawn()) : nullptr;
	if (!Found)
	{
		return false;
	}
	Pawn = Found;
	bPawnAdopted = true;

	const FVector Location = Spec.bHasCamera ? Spec.CameraLocationCm : Spec.SpawnLocationCm + FVector(-250.0, 150.0, 160.0);
	const FVector LookAt = Spec.bHasCamera ? Spec.CameraLookAtCm : Spec.SpawnLocationCm + FVector(0.0, 0.0, 110.0);
	Found->SetView(Location, (LookAt - Location).Rotation());

	Found->OnImpulseRequested.AddUObject(this, &USandboxPlaySubsystem::ApplyImpulse);
	Found->OnResetRequested.AddUObject(this, &USandboxPlaySubsystem::ResetCharacter);
	Found->OnRecordingStopped.AddUObject(this, &USandboxPlaySubsystem::SaveRecording);
	if (bRecordFromStart)
	{
		Found->StartRecording();
	}
	Found->ExtraHudLines = [WeakThis = TWeakObjectPtr<const USandboxPlaySubsystem>(this)](TArray<FString>& OutLines)
	{
		if (WeakThis.IsValid())
		{
			WeakThis->AddHudLines(OutLines);
		}
	};
	return true;
}

void USandboxPlaySubsystem::ApplyImpulse(const FHitResult& Hit, const FVector& ImpulseNs)
{
	UPrimitiveComponent* Component = Hit.GetComponent();
	USandboxBodyController* Controller = Character ? Character->GetController() : nullptr;
	if (Controller && Component == Character->GetBodyMesh())
	{
		// The calibrated route: the same entry a scenario's "impulse" event uses, so the character both
		// receives the impulse and feels it.
		Controller->ApplyImpulse(Hit.BoneName, ImpulseNs);
	}
	else if (Component && Component->IsSimulatingPhysics(Hit.BoneName))
	{
		// Any other simulated body just receives it. 1 N*s = 100 kg*cm/s.
		Component->AddImpulse(ImpulseNs * 100.0, Hit.BoneName, /*bVelChange*/ false);
	}
}

void USandboxPlaySubsystem::ResetCharacter()
{
	if (Character)
	{
		Character->Destroy();
		Character = nullptr;
	}
	FString Error;
	Character = FScenarioStage::SpawnBody(GetWorldRef(), Spec, Error);
	++Resets;
	BodySpawnFrame = Pawn.IsValid() ? Pawn->GetSimulatedFrames() : 0;
	if (!Character)
	{
		UE_LOG(LogSandbox, Error, TEXT("Play session: the character could not be put back: %s"), *Error);
	}
}

void USandboxPlaySubsystem::AddHudLines(TArray<FString>& OutLines) const
{
	const USandboxBodyController* Controller = Character ? Character->GetController() : nullptr;
	if (Controller && Controller->GetStateLog().Num() > 0)
	{
		const FSandboxStateChange& Last = Controller->GetStateLog().Last();
		OutLines.Add(FString::Printf(TEXT("Character: %s since %.2f s (%s); felt %.1f N*s from contacts; tuning changes %d"),
			FSandboxBodyTuning::StateName(Last.State), Last.TimeS, *Last.Reason, Controller->GetContactImpulseNs(), Controller->GetTuningChangeCount()));
	}
	else
	{
		OutLines.Add(Character ? TEXT("Character: passive body") : TEXT("Character: none"));
	}
	OutLines.Add(FString::Printf(TEXT("Session: %s, resets %d"), *Spec.Name, Resets));
}

void USandboxPlaySubsystem::SaveRecording(const TSharedRef<FSandboxTargetRecording>& Recording, int64 FirstFrame)
{
	LastRecordingScenario.Reset();
	LastRecordingError.Reset();
	if (Recording->NumFrames() == 0)
	{
		LastRecordingError = TEXT("the recording has no frames: the world did not step while it ran");
		UE_LOG(LogSandbox, Warning, TEXT("Play session: %s"), *LastRecordingError);
		return;
	}
	Recording->Fps = Spec.FixedFps;
	++RecordingsSaved;
	FString Name = RecordName.IsEmpty() ? Spec.Name + TEXT("_recorded") : RecordName;
	if (RecordingsSaved > 1)
	{
		Name += FString::Printf(TEXT("_%d"), RecordingsSaved);
	}
	const FString Dir = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Recordings"), Name));
	const FString RecordingFile = FPaths::Combine(Dir, Name + TEXT(".recording.json"));
	if (!Recording->Save(RecordingFile, LastRecordingError))
	{
		UE_LOG(LogSandbox, Error, TEXT("Play session: %s"), *LastRecordingError);
		return;
	}

	// The scenario that replays it: the session's own scene, no events, the hands from the recording, and a run
	// as long as the body had lived when the recording ended, so that both end at the same moment. The body of
	// a session gets up by itself, so the replay's does too.
	const bool bBodyReplaced = BodySpawnFrame > FirstFrame;
	const int64 StartFrame = bBodyReplaced ? 0 : FirstFrame - BodySpawnFrame;
	const int64 EndFrame = StartFrame + Recording->NumFrames();
	if (bBodyReplaced)
	{
		UE_LOG(LogSandbox, Warning, TEXT("Play session: the character was reset while '%s' was recorded; its scenario cannot bring that back and will not end as the session did"), *Name);
	}
	FString RelativeRecording = RecordingFile;
	FPaths::MakePathRelativeTo(RelativeRecording, *FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()));
	TSharedPtr<FJsonObject> Scenario;
	if (!SandboxJson::LoadObject(ScenarioFile, Scenario, LastRecordingError))
	{
		UE_LOG(LogSandbox, Error, TEXT("Play session: %s"), *LastRecordingError);
		return;
	}
	Scenario->SetStringField(TEXT("name"), Name);
	Scenario->SetNumberField(TEXT("duration_s"), static_cast<double>(EndFrame) / Spec.FixedFps);
	Scenario->SetArrayField(TEXT("events"), TArray<TSharedPtr<FJsonValue>>());
	Scenario->SetObjectField(TEXT("expect"), MakeShared<FJsonObject>());
	Scenario->RemoveField(TEXT("end_when_standing_s"));
	Scenario->SetStringField(TEXT("recording"), RelativeRecording);
	Scenario->SetNumberField(TEXT("recording_start_s"), static_cast<double>(StartFrame) / Spec.FixedFps);
	const TSharedPtr<FJsonObject>* Body = nullptr;
	if (Spec.Control == EScenarioBodyControl::Active && Scenario->TryGetObjectField(TEXT("body"), Body))
	{
		(*Body)->SetBoolField(TEXT("get_up"), true);
	}
	const FString ReplayFile = FPaths::Combine(Dir, Name + TEXT(".json"));
	if (!SandboxJson::SaveObject(ReplayFile, Scenario.ToSharedRef(), LastRecordingError))
	{
		UE_LOG(LogSandbox, Error, TEXT("Play session: %s"), *LastRecordingError);
		return;
	}

	// What the session saw at the moment the recording ended, in the terms of a scenario's result file, for the
	// replay to be held against (Tools\record-replay.ps1).
	const TSharedRef<FJsonObject> Seen = MakeShared<FJsonObject>();
	Seen->SetStringField(TEXT("name"), Name);
	Seen->SetStringField(TEXT("scenario"), Spec.Name);
	Seen->SetNumberField(TEXT("fps"), Spec.FixedFps);
	Seen->SetNumberField(TEXT("frames"), Recording->NumFrames());
	Seen->SetNumberField(TEXT("start_frame"), static_cast<double>(StartFrame));
	Seen->SetNumberField(TEXT("end_frame"), static_cast<double>(EndFrame));
	Seen->SetBoolField(TEXT("character_reset_while_recording"), bBodyReplaced);
	// A session that is closed while it records comes here as the world is taken apart: the character may be gone.
	const bool bCharacter = IsValid(Character) && Character->GetBodyMesh();
	const USandboxBodyController* Controller = bCharacter ? Character->GetController() : nullptr;
	if (Controller)
	{
		FString Sequence;
		TArray<TSharedPtr<FJsonValue>> States;
		for (const FSandboxStateChange& Change : Controller->GetStateLog())
		{
			const FString State = FSandboxBodyTuning::StateName(Change.State);
			Sequence += (Sequence.IsEmpty() ? TEXT("") : TEXT(">")) + State;
			const TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("state"), State);
			Entry->SetNumberField(TEXT("t"), Change.TimeS);
			Entry->SetNumberField(TEXT("frame"), FMath::RoundToInt32(Change.TimeS * Spec.FixedFps));
			Entry->SetStringField(TEXT("reason"), Change.Reason);
			States.Add(MakeShared<FJsonValueObject>(Entry));
		}
		Seen->SetStringField(TEXT("state_sequence"), Sequence);
		Seen->SetArrayField(TEXT("states"), States);
		Seen->SetNumberField(TEXT("felt_impulse_ns"), Controller->GetContactImpulseNs());
	}
	const FBodyInstance* Root = bCharacter ? Character->GetBodyMesh()->GetBodyInstance() : nullptr;
	if (Root && Root->IsValidBodyInstance())
	{
		// The pelvis's centre of mass from where the character was put, as body.bodies[].final_location_cm has it.
		Seen->SetArrayField(TEXT("pelvis_cm"), SandboxJson::VectorToArray(Root->GetCOMPosition() - Spec.SpawnLocationCm));
	}
	const TSharedRef<FJsonObject> Hands = MakeShared<FJsonObject>();
	const ASandboxEffectorRig* Rig = Pawn.IsValid() ? Pawn->GetRig() : nullptr;
	for (const USandboxEffectorComponent* Effector : Rig ? Rig->GetEffectors() : TArray<TObjectPtr<USandboxEffectorComponent>>())
	{
		const FSandboxEffectorStats& Stats = Effector->GetStats();
		const TSharedRef<FJsonObject> Hand = MakeShared<FJsonObject>();
		Hand->SetNumberField(TEXT("contact_impulse_ns"), Stats.ContactImpulseNs);
		Hand->SetNumberField(TEXT("contact_frames"), Stats.ContactFrames);
		Hand->SetStringField(TEXT("first_touched"), Stats.FirstTouched.IsNone() ? FString() : Stats.FirstTouched.ToString());
		Hand->SetNumberField(TEXT("first_contact_speed_mps"), Stats.FirstContactSpeedCmS * 0.01);
		Hand->SetNumberField(TEXT("max_gap_cm"), Stats.MaxGapCm);
		Hands->SetObjectField(Effector->GetId().ToString(), Hand);
	}
	Seen->SetObjectField(TEXT("hands"), Hands);
	FString Error;
	if (!SandboxJson::SaveObject(FPaths::Combine(Dir, Name + TEXT(".session.json")), Seen, Error))
	{
		UE_LOG(LogSandbox, Error, TEXT("Play session: %s"), *Error);
	}

	LastRecordingScenario = ReplayFile;
	UE_LOG(LogSandbox, Display, TEXT("Play session: %d frames of the hands recorded from frame %lld of the character's life; replay with -File %s"),
		Recording->NumFrames(), StartFrame, *ReplayFile);
}

void USandboxPlaySubsystem::TickDemo()
{
	// The hands alone, for a recording that a scenario can replay to the same end: no tool of the session that
	// a scenario has no word for (the drag handle, the reset, the time control) is touched.
	ASandboxPawn* Player = Pawn.Get();
	if (!Player)
	{
		if (DemoFrame++ > 10)
		{
			LastRecordingError = TEXT("the player has no ASandboxPawn");
			FinishDemo();
		}
		return;
	}
	// Looking at the character's front from the side it faces (+y), level, at arm's length.
	const FVector Chest = Spec.SpawnLocationCm + FVector(0.0, 0.0, 128.0);
	switch (DemoFrame)
	{
	case 5:
		Player->SelectTool(ESandboxTool::Hands);
		Player->SetView(Chest + FVector(0.0, 85.0, 0.0), FRotator(0.0, -90.0, 0.0));
		break;
	// The hands have come to rest in front of the new view by now.
	case 60:
		if (!Player->StartRecording())
		{
			LastRecordingError = TEXT("the pawn could not start a recording (see the log)");
			FinishDemo();
			return;
		}
		break;
	// The right hand reaches into the chest and comes back; then the left.
	case 90:
		Player->SetPrimary(true);
		break;
	case 115:
		Player->SetPrimary(false);
		break;
	case 170:
		Player->SetSecondary(true);
		break;
	case 190:
		Player->SetSecondary(false);
		break;
	case 300:
		Player->StopRecording();
		FinishDemo();
		return;
	default:
		break;
	}
	++DemoFrame;
}

void USandboxPlaySubsystem::FinishDemo()
{
	const bool bPass = LastRecordingError.IsEmpty() && !LastRecordingScenario.IsEmpty();
	const TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetBoolField(TEXT("ok"), true);
	Root->SetBoolField(TEXT("pass"), bPass);
	Root->SetStringField(TEXT("scenario"), Spec.Name);
	Root->SetNumberField(TEXT("frames"), DemoFrame);
	Root->SetStringField(TEXT("replay_scenario"), LastRecordingScenario);
	Root->SetStringField(TEXT("error"), LastRecordingError);
	FString Error;
	if (!SandboxJson::SaveObject(FPaths::Combine(OutDir, TEXT("play.json")), Root, Error))
	{
		UE_LOG(LogSandbox, Error, TEXT("Play demo: %s"), *Error);
	}
	bSmokePass = bPass;
	ExitCountdown = 0;
}

void USandboxPlaySubsystem::HoldFrameRate()
{
	// With a fixed timestep the engine runs as fast as it can. A person needs one simulated second per
	// second, so each frame waits until it is due. This is the only place that reads the clock, and it
	// decides when a frame happens, never what happens in it.
	const double Now = FPlatformTime::Seconds();
	if (NextFrameS <= 0.0 || Now > NextFrameS + MaxLatenessS)
	{
		NextFrameS = Now;
	}
	NextFrameS += Spec.FixedDt();
	const double Wait = NextFrameS - Now;
	if (Wait > SleepMarginS)
	{
		FPlatformProcess::SleepNoStats(static_cast<float>(Wait - SleepMarginS));
	}
	while (FPlatformTime::Seconds() < NextFrameS)
	{
		FPlatformProcess::YieldThread();
	}

	// Every ten seconds of frames the log says how long they took: a session that cannot hold the rate
	// runs slower than the clock, and this is where that shows.
	const double Clock = FPlatformTime::Seconds();
	if (PacedFrames == 0)
	{
		PacedSinceS = Clock;
	}
	if (++PacedFrames > ReportFrames)
	{
		UE_LOG(LogSandbox, Display, TEXT("Play session: %d frames in %.2f s, %.1f per second (the simulation's rate is %d)"),
			ReportFrames, Clock - PacedSinceS, ReportFrames / FMath::Max(Clock - PacedSinceS, 1.0e-6), Spec.FixedFps);
		PacedFrames = 0;
	}
}

void USandboxPlaySubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (ExitCountdown >= 0)
	{
		if (ExitCountdown == ScreenshotFrames && !ShotPath.IsEmpty() && FApp::CanEverRender())
		{
			FScreenshotRequest::RequestScreenshot(ShotPath, /*bInShowUI*/ false, /*bAddFilenameSuffix*/ false);
		}
		if (--ExitCountdown < 0)
		{
			bActive = false;
			FPlatformMisc::RequestExitWithStatus(/*Force*/ false, bSmokePass ? 0 : 1);
		}
		return;
	}

	if (!bPawnAdopted)
	{
		AdoptPawn(GetWorldRef());
	}

	if (bSmoke)
	{
		TickSmoke();
	}
	else if (bDemo)
	{
		TickDemo();
	}
	else if (FApp::CanEverRender())
	{
		HoldFrameRate();
	}
}

void USandboxPlaySubsystem::Check(const TCHAR* Name, bool bPass, const FString& Detail)
{
	const TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
	Entry->SetStringField(TEXT("name"), Name);
	Entry->SetBoolField(TEXT("pass"), bPass);
	Entry->SetStringField(TEXT("detail"), Detail);
	Entry->SetNumberField(TEXT("frame"), SmokeFrame);
	SmokeChecks.Add(MakeShared<FJsonValueObject>(Entry));
	bSmokePass &= bPass;
	UE_LOG(LogSandbox, Display, TEXT("Play self-test [%s] %s: %s"), bPass ? TEXT("ok") : TEXT("FAIL"), Name, *Detail);
}

void USandboxPlaySubsystem::TickSmoke()
{
	// A person's session in a few seconds: each tool is used through the functions its keys call, and the
	// world is asked what happened. Frames are counted here, paused ones included.
	ASandboxPawn* Player = Pawn.Get();
	if (!Player)
	{
		if (SmokeFrame++ > 10)
		{
			Check(TEXT("pawn"), false, TEXT("the player has no ASandboxPawn"));
			FinishSmoke();
		}
		return;
	}
	const USandboxBodyController* Controller = Character ? Character->GetController() : nullptr;
	const ASandboxEffectorRig* Rig = Player->GetRig();
	const auto Pelvis = [this]()
	{
		const FBodyInstance* Root = Character ? Character->GetBodyMesh()->GetBodyInstance() : nullptr;
		return Root ? Root->GetUnrealWorldTransform() : FTransform::Identity;
	};
	const auto StateNames = [](const USandboxBodyController* From)
	{
		FString Names;
		for (const FSandboxStateChange& Change : From ? From->GetStateLog() : TArray<FSandboxStateChange>())
		{
			Names += (Names.IsEmpty() ? TEXT("") : TEXT(">")) + FString(FSandboxBodyTuning::StateName(Change.State));
		}
		return Names;
	};
	// Looking at the character's front from the side it faces (+y), level.
	const FVector Chest = Spec.SpawnLocationCm + FVector(0.0, 0.0, 128.0);
	const FRotator TowardCharacter(0.0, -90.0, 0.0);

	USandboxOverlaySubsystem* Overlay = GetWorldRef().GetSubsystem<USandboxOverlaySubsystem>();

	switch (SmokeFrame)
	{
	// The overlays, through the key a person would press. With a renderer they draw; without one they must
	// stay off and do nothing. Whatever the session was started with is put back afterwards.
	case 18:
		SmokeOverlayBefore = Overlay ? Overlay->GetLayers() : 0;
		if (Overlay)
		{
			Overlay->SetLayers(SandboxOverlay::None);
		}
		Player->ToggleOverlays();
		break;
	case 26:
	{
		const uint32 Layers = Overlay ? Overlay->GetLayers() : 0;
		const int32 Lines = Overlay ? Overlay->GetDrawnLineCount() : 0;
		const int32 Labels = Overlay ? Overlay->GetDrawnLabelCount() : 0;
		const FString Detail = FString::Printf(TEXT("layers '%s': %d line segments, %d lines of text"), *SandboxOverlay::LayerNames(Layers), Lines, Labels);
		if (FApp::CanEverRender())
		{
			Check(TEXT("overlays_draw"), Layers == SandboxOverlay::All && Lines > 50 && Labels > 3, Detail);
		}
		else
		{
			Check(TEXT("overlays_do_nothing_without_a_renderer"), Overlay && Layers == SandboxOverlay::None && Lines == 0 && Labels == 0, Detail);
		}
		if (Overlay)
		{
			Overlay->SetLayers(SmokeOverlayBefore);
		}
		break;
	}

	case 30:
	{
		Check(TEXT("hands_exist"), Rig && Rig->GetEffectors().Num() == 2, FString::Printf(TEXT("%d effectors"), Rig ? Rig->GetEffectors().Num() : 0));
		double WorstGapCm = 0.0;
		for (const USandboxEffectorComponent* Effector : Rig ? Rig->GetEffectors() : TArray<TObjectPtr<USandboxEffectorComponent>>())
		{
			WorstGapCm = FMath::Max(WorstGapCm, Effector->GetGapCm());
		}
		Check(TEXT("hands_rest_on_their_targets"), Rig && WorstGapCm < 1.0, FString::Printf(TEXT("%.2f cm from target"), WorstGapCm));
		Check(TEXT("character_stands"), Controller && Controller->GetState() == ESandboxBodyState::Standing, StateNames(Controller));
		break;
	}

	// The impulse tool: one click on the chest.
	case 40:
		Player->SetView(Chest + FVector(0.0, 150.0, 0.0), TowardCharacter);
		Player->SelectTool(ESandboxTool::Impulse);
		Player->SetPrimary(true);
		break;
	case 41:
		Player->SetPrimary(false);
		break;
	case 60:
	{
		const FString Expected = FString::Printf(TEXT("hit of %.1f N*s"), Player->GetImpulseNs());
		const bool bFelt = Controller && Controller->GetStateLog().ContainsByPredicate([&Expected](const FSandboxStateChange& Change)
		{
			return Change.State == ESandboxBodyState::Flinch && Change.Reason.Contains(Expected);
		});
		Check(TEXT("impulse_tool_makes_the_character_flinch"), bFelt, FString::Printf(TEXT("states %s; looked for a flinch from a '%s'"), *StateNames(Controller), *Expected));
		break;
	}

	// Pause, one step, resume.
	case 100:
		Player->TogglePause();
		break;
	case 110:
		SmokeFramesAtPause = Player->GetSimulatedFrames();
		SmokePelvisAtPause = Pelvis();
		break;
	case 140:
		Check(TEXT("pause_stops_the_world"), Player->GetSimulatedFrames() == SmokeFramesAtPause && Pelvis().Equals(SmokePelvisAtPause, 0.0),
			FString::Printf(TEXT("%lld frames simulated in 30 paused frames"), Player->GetSimulatedFrames() - SmokeFramesAtPause));
		Player->StepOnce();
		break;
	case 150:
		Check(TEXT("step_advances_one_frame"), Player->GetSimulatedFrames() == SmokeFramesAtPause + 1,
			FString::Printf(TEXT("%lld frames simulated after one step"), Player->GetSimulatedFrames() - SmokeFramesAtPause));
		Player->TogglePause();
		break;

	// Slow motion: the world steps on one frame in several.
	case 160:
		SmokeFramesAtSlow = Player->GetSimulatedFrames();
		Player->ToggleSlowMotion();
		break;
	case 240:
	{
		const int64 Simulated = Player->GetSimulatedFrames() - SmokeFramesAtSlow;
		const int64 Expected = 80 / FMath::Max(FMath::RoundToInt32(Player->GetTuning().Tools.SlowMotionDivisor), 1);
		Check(TEXT("slow_motion_leaves_frames_out"), FMath::Abs(Simulated - Expected) <= 2, FString::Printf(TEXT("%lld frames simulated in 80, expected about %lld"), Simulated, Expected));
		Player->ToggleSlowMotion();
		break;
	}

	// The drag handle: take the left forearm and lift it.
	case 260:
	{
		// Aimed at where the forearm is, not at a point written down for one physics asset: the collarbones
		// of M3 let the arm hang 2 to 3 cm lower and closer to the body, and a fixed point missed it.
		const FBodyInstance* ForearmBody = Character ? Character->GetBodyMesh()->GetBodyInstance(TEXT("lowerarm_l")) : nullptr;
		const FVector Forearm = ForearmBody ? ForearmBody->GetCOMPosition() : Spec.SpawnLocationCm + FVector(40.0, 0.0, 107.0);
		Player->SetView(FVector(Forearm.X, Spec.SpawnLocationCm.Y + 120.0, Forearm.Z), TowardCharacter);
		Player->SelectTool(ESandboxTool::Drag);
		Player->SetPrimary(true);
		const FSandboxDragHandle& Handle = Player->GetDragHandle();
		Check(TEXT("drag_handle_takes_hold"), Handle.IsHolding() && Character && Handle.GetComponent() == Character->GetBodyMesh(),
			FString::Printf(TEXT("holding '%s'"), *Handle.GetBone().ToString()));
		SmokeGripStart = Handle.GetGripLocation();
		break;
	}
	case 265:
		Player->SetView(Player->GetActorLocation() + FVector(0.0, 0.0, 30.0), TowardCharacter);
		break;
	case 330:
	{
		const FSandboxDragHandle& Handle = Player->GetDragHandle();
		const double RaisedCm = Handle.IsHolding() ? Handle.GetGripLocation().Z - SmokeGripStart.Z : 0.0;
		Check(TEXT("drag_handle_lifts_the_arm"), RaisedCm > 10.0, FString::Printf(TEXT("held point raised by %.1f cm of 30, %.1f cm behind its target"), RaisedCm, Handle.GetGapCm()));
		Player->SetPrimary(false);
		Check(TEXT("drag_handle_lets_go"), !Handle.IsHolding(), TEXT(""));
		break;
	}

	// The hands: the right one reaches into the chest. The character must feel it through its own contacts.
	case 340:
		Player->SelectTool(ESandboxTool::Hands);
		Player->SetView(Chest + FVector(0.0, 85.0, 0.0), TowardCharacter);
		break;
	case 370:
		Player->SetPrimary(true);
		break;
	case 390:
		Player->SetPrimary(false);
		break;
	case 420:
	{
		const USandboxEffectorComponent* RightHand = Rig ? Rig->FindEffector(SandboxEffectors::HandRight) : nullptr;
		const double HandNs = RightHand ? RightHand->GetStats().ContactImpulseNs : 0.0;
		const double FeltNs = Controller ? Controller->GetContactImpulseNs() : 0.0;
		Check(TEXT("hand_touches_the_character"), HandNs > 1.0, FString::Printf(TEXT("the hand delivered %.1f N*s"), HandNs));
		Check(TEXT("character_feels_the_hand"), FeltNs > 1.0, FString::Printf(TEXT("the character felt %.1f N*s from contacts; states %s"), FeltNs, *StateNames(Controller)));
		break;
	}

	// Reset: a fresh character on its feet where the first one started.
	case 430:
		Player->RequestReset();
		break;
	case 470:
	{
		const double OffCm = FVector::Dist(Pelvis().GetLocation(), SmokeSpawnPelvis);
		Check(TEXT("reset_puts_the_character_back"), Resets == 1 && Controller && Controller->GetState() == ESandboxBodyState::Standing && Controller->GetStateLog().Num() == 1 && OffCm < 3.0,
			FString::Printf(TEXT("resets %d, states %s, pelvis %.2f cm from where it started"), Resets, *StateNames(Controller), OffCm));
		break;
	}
	case 480:
		FinishSmoke();
		return;
	default:
		break;
	}
	++SmokeFrame;
}

void USandboxPlaySubsystem::FinishSmoke()
{
	const TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetBoolField(TEXT("ok"), true);
	Root->SetBoolField(TEXT("pass"), bSmokePass);
	Root->SetStringField(TEXT("scenario"), Spec.Name);
	Root->SetBoolField(TEXT("rendered"), FApp::CanEverRender());
	Root->SetNumberField(TEXT("frames"), SmokeFrame);
	Root->SetArrayField(TEXT("checks"), SmokeChecks);
	FString Error;
	if (!SandboxJson::SaveObject(FPaths::Combine(OutDir, TEXT("play.json")), Root, Error))
	{
		UE_LOG(LogSandbox, Error, TEXT("Play self-test: %s"), *Error);
		bSmokePass = false;
	}
	UE_LOG(LogSandbox, Display, TEXT("Play self-test: %s"), bSmokePass ? TEXT("PASS") : TEXT("FAIL"));
	ExitCountdown = !ShotPath.IsEmpty() && FApp::CanEverRender() ? ScreenshotFrames : 0;
}
