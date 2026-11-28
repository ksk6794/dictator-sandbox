#include "SandboxPlaySubsystem.h"

#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
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
#include "SandboxStandInHeadset.h"
#include "SandboxTargetRecording.h"
#include "SandboxTrackedTargetSource.h"
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
	if (bSmoke || bDemo || bExitOnFail)
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
	bDemoTakes = !bSmoke && FParse::Param(FCommandLine::Get(), TEXT("PlayDemoTakes"));
	bDemoGrab = !bSmoke && !bDemoTakes && FParse::Param(FCommandLine::Get(), TEXT("PlayDemoGrab"));
	bDemoWalk = !bSmoke && !bDemoTakes && !bDemoGrab && FParse::Param(FCommandLine::Get(), TEXT("PlayDemoWalk"));
	bDemo = !bSmoke && (bDemoTakes || bDemoGrab || bDemoWalk || FParse::Param(FCommandLine::Get(), TEXT("PlayDemo")));
	bRecordFromStart = FParse::Value(FCommandLine::Get(), TEXT("Record="), RecordName) && !RecordName.IsEmpty() && !bDemo && !bSmoke;
	bPerception = FParse::Param(FCommandLine::Get(), TEXT("PlayPerception"));
	FParse::Value(FCommandLine::Get(), TEXT("PlaySeconds="), PlaySeconds);
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

	// Asked for the headset and there is none: a window with desktop hands would look like a session that
	// worked. The session ends and says why.
	if (FParse::Param(FCommandLine::Get(), TEXT("vr")) && !FScenarioStage::IsStereo())
	{
		bExitOnFail = true;
		Fail(TEXT("-vr was asked for and no headset is showing the world: is it on, and is its OpenXR runtime (SteamVR) running?"));
		return;
	}

	// The same physics timing as a scenario: every frame is one fixed step of the simulation.
	FApp::SetFixedDeltaTime(Spec.FixedDt());
	FApp::SetUseFixedTimeStep(true);

	FScenarioStage::SpawnEnvironment(InWorld, Spec, /*bSpawnCamera*/ false);
	RespawnProps();
	Character = FScenarioStage::SpawnBody(InWorld, Spec, Error);
	if (!Character)
	{
		Fail(Error);
		return;
	}
	// R10, step 2: the session's seed, from a counter of its own (never the clock): said in the log, written into every recording's
	// replay, told to every character the session puts into the world.
	SessionSeed = NextSessionSeed();
	UE_LOG(LogSandbox, Display, TEXT("Play session: the seed of the body's choices is %d"), SessionSeed);
	if (USandboxBodyController* Seeded = Character->GetController())
	{
		Seeded->SetChanceSeed(SessionSeed);
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
	if (bDemoWalk)
	{
		// A session without a person in a headset that a made-up one stands in for, before the pawn begins to play: a
		// head 40 cm ahead and 30 to the left of the play space's middle, turned 20 degrees to the right and a little
		// down, with the controllers held beside the hips.
		StandIn = MakeShared<FSandboxStandInHeadset>();
		StandIn->HeadLocation = FVector(40.0, -30.0, 165.0);
		StandIn->HeadRotation = FRotator(-5.0, 20.0, 0.0).Quaternion();
		StandIn->HoldHandsByTheHips();
		Found->UseStandInHeadset(StandIn);
	}
	if (FScenarioStage::IsStereo() || bDemoWalk)
	{
		// In the headset the pawn is the middle of the play space. It is put on the floor, on the side of the
		// character the desktop camera starts on, at arm's length from what that camera looks at, facing it.
		FVector Away = (Location - LookAt).GetSafeNormal2D();
		if (Away.IsNearlyZero())
		{
			Away = FVector(0.0, 1.0, 0.0);
		}
		FSandboxInteractionTuning Tuning;
		FString TuningError;
		const double DistanceCm = FSandboxInteractionTuning::FromParams(FSandboxParamStore::Get(), Tuning, TuningError) ? Tuning.Tools.HeadsetStandDistanceCm : 0.0;
		const FVector Floor(LookAt.X + Away.X * DistanceCm, LookAt.Y + Away.Y * DistanceCm, Spec.SpawnLocationCm.Z);
		Found->SetView(Floor, (-Away).Rotation());
		WalkStandPoint = Floor;
		WalkStandYawDeg = (-Away).Rotation().Yaw;
		UE_LOG(LogSandbox, Display, TEXT("Play session: in the headset, the player's head is put over %.0f, %.0f on the floor, %.0f cm from the character, facing it"), Floor.X, Floor.Y, DistanceCm);
	}
	else
	{
		Found->SetView(Location, (LookAt - Location).Rotation());
	}

	Found->OnImpulseRequested.AddUObject(this, &USandboxPlaySubsystem::ApplyImpulse);
	Found->OnResetRequested.AddUObject(this, &USandboxPlaySubsystem::RequestReset);
	Found->OnRecordingStopped.AddUObject(this, &USandboxPlaySubsystem::SaveRecording);
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

void USandboxPlaySubsystem::RequestReset()
{
	// Carried out in this subsystem's tick, which comes after the frame's physics: a character put into the
	// world there has its first step in the next frame, as a scenario's character has, and not half a frame
	// of its own in the middle of this one.
	bResetPending = true;
}

void USandboxPlaySubsystem::RespawnProps()
{
	for (const TWeakObjectPtr<AActor>& Prop : Props)
	{
		if (AActor* Old = Prop.Get())
		{
			// Out of the way of its own name: the thing that takes its place is to be called as it was, so that
			// what a hand touched in a take reads the same in the take's replay.
			Old->Rename(*MakeUniqueObjectName(Old->GetOuter(), AActor::StaticClass(), TEXT("GoneProp")).ToString(), nullptr, REN_DontCreateRedirectors | REN_NonTransactional | REN_DoNotDirty);
			Old->Destroy();
		}
	}
	Props.Reset();
	for (AActor* Prop : FScenarioStage::SpawnProps(GetWorldRef(), Spec))
	{
		Props.Add(Prop);
	}
}

void USandboxPlaySubsystem::ResetCharacter()
{
	if (Character)
	{
		Character->Destroy();
		Character = nullptr;
	}
	// The things of the scene go back to their places with the character: a replay of what follows starts from them.
	if (Spec.Props.Num() > 0)
	{
		RespawnProps();
	}
	FString Error;
	Character = FScenarioStage::SpawnBody(GetWorldRef(), Spec, Error);
	++Resets;
	BodySpawnFrame = Pawn.IsValid() ? Pawn->GetSimulatedFrames() : 0;
	if (USandboxBodyController* Seeded = Character ? Character->GetController() : nullptr)
	{
		Seeded->SetChanceSeed(SessionSeed);
	}
	if (!Character)
	{
		UE_LOG(LogSandbox, Error, TEXT("Play session: the character could not be put back: %s"), *Error);
	}
}

namespace
{
	// R10, step 8 in part (decision 42: the console in any session; a scenario's console event in a run): tells every body of the world the
	// command, and a session that records keeps it with its frame.
	FAutoConsoleCommandWithWorldAndArgs SandboxCommandCommand(
		TEXT("Sandbox.Command"),
		TEXT("Tells the body a command (R10): Sandbox.Command get_up | stop | give_hand. Answered only with behaviour.commands on (give_hand with behaviour.give_hand too)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (!World)
			{
				UE_LOG(LogSandbox, Error, TEXT("Sandbox.Command: there is no world to tell"));
				return;
			}
			// The name as the body and a recording know it, whatever case it was typed in (a recording refuses another).
			const FString Name = Args.Num() == 1 ? Args[0].ToLower() : FString();
			if (Name != TEXT("get_up") && Name != TEXT("stop") && Name != TEXT("give_hand"))
			{
				UE_LOG(LogSandbox, Error, TEXT("Sandbox.Command: give one of get_up, stop, give_hand"));
				return;
			}
			const ESandboxCommand Command = Name == TEXT("get_up") ? ESandboxCommand::GetUp : Name == TEXT("give_hand") ? ESandboxCommand::GiveHand : ESandboxCommand::Stop;
			int32 Told = 0;
			for (TActorIterator<ASandboxRagdollActor> It(World); It; ++It)
			{
				if (USandboxBodyController* Controller = It->GetController())
				{
					Controller->TellCommand(Command);
					++Told;
				}
			}
			if (USandboxPlaySubsystem* Play = World->GetSubsystem<USandboxPlaySubsystem>())
			{
				Play->NoteCommand(Name);
			}
			UE_LOG(LogSandbox, Display, TEXT("Sandbox.Command: %s told to %d body(ies)"), *Name, Told);
		}));
}

void USandboxPlaySubsystem::NoteCommand(const FString& Name)
{
	if (Pawn.IsValid() && Pawn->IsRecording() && Pawn->GetRig())
	{
		Pawn->GetRig()->NoteCommand(Name);
	}
}

int32 USandboxPlaySubsystem::NextSessionSeed()
{
	// A counter of the sessions' own in Saved\Play\session_seed.txt: each session takes the next number (1 the first), never the clock.
	const FString File = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Play"), TEXT("session_seed.txt"));
	FString Text;
	int32 Last = 0;
	if (FFileHelper::LoadFileToString(Text, *File))
	{
		Last = FCString::Atoi(*Text.TrimStartAndEnd());
	}
	const int32 Seed = FMath::Max(Last, 0) + 1;
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(File), /*Tree*/ true);
	if (!FFileHelper::SaveStringToFile(FString::FromInt(Seed), *File))
	{
		UE_LOG(LogSandbox, Warning, TEXT("Play session: the seed counter %s could not be written; the next session takes the same seed"), *File);
	}
	return Seed;
}

void USandboxPlaySubsystem::TellPerception()
{
	if (!Character)
	{
		return;
	}
	// A session that records sees the hands while a recording runs. Before the first one they fly from where the pawn made
	// them to where the recording begins, which its replay cannot know: watched, that flight turned the session's head and
	// the replay's body ended 2 cm off the session's. The viewer the same way: a replay has the head only from its first frame.
	const bool bHandsSeen = RecordName.IsEmpty() || (Pawn.IsValid() && Pawn->IsRecording());
	// R10, step 1: the player's head (the camera on the desktop, the head in a headset) is told as a viewer in every session, whether
	// or not the body perceives; with every behaviour's switch at 0 nothing reads it.
	FSandboxSeen Viewer;
	const bool bViewer = bHandsSeen && Pawn.IsValid();
	if (bViewer)
	{
		Viewer.Name = TEXT("head");
		Viewer.Kind = ESandboxSeenKind::Viewer;
		Viewer.LocationCm = Pawn->GetHeadLocation();
		// Per frame of the session's fixed step (a paused frame that moves the camera reads as a speed; nothing reads it yet).
		Viewer.VelocityCmS = bViewerToldLastFrame ? (Viewer.LocationCm - LastViewerCm) / Spec.FixedDt() : FVector::ZeroVector;
		LastViewerCm = Viewer.LocationCm;
	}
	bViewerToldLastFrame = bViewer;
	if (!bPerception)
	{
		// The head, the player's capsule in a headset (a viewer too) and the hands (for give_hand alone): the body does not perceive.
		FScenarioStage::TellBodyWhatMoves(bHandsSeen && Pawn.IsValid() ? Pawn->GetRig() : nullptr, {}, Character,
			bViewer ? TConstArrayView<FSandboxSeen>(&Viewer, 1) : TConstArrayView<FSandboxSeen>(), /*bViewersOnly*/ true);
		return;
	}
	// Every frame, as a scenario's run does: a character put back after a reset starts without it.
	if (USandboxBodyController* Controller = Character->GetController())
	{
		Controller->SetPerceptionAllowed(true);
	}
	TArray<TObjectPtr<AActor>> Things;
	for (const TWeakObjectPtr<AActor>& Prop : Props)
	{
		if (Prop.IsValid())
		{
			Things.Add(Prop.Get());
		}
	}
	FScenarioStage::TellBodyWhatMoves(bHandsSeen && Pawn.IsValid() ? Pawn->GetRig() : nullptr, Things, Character,
		bViewer ? TConstArrayView<FSandboxSeen>(&Viewer, 1) : TConstArrayView<FSandboxSeen>());
}

void USandboxPlaySubsystem::TellNotLed()
{
	const USandboxBodyController* Controller = Character ? Character->GetController() : nullptr;
	if (!Controller)
	{
		return;
	}
	if (NotLedOf.Get() != Controller)
	{
		// A character put back is counted from its own start.
		NotLedOf = Controller;
		NotLedTold = 0;
	}
	const FSandboxGetUpView& GetUps = Controller->GetGetUpView();
	if (GetUps.NotLedAttempts > NotLedTold)
	{
		NotLedTold = GetUps.NotLedAttempts;
		UE_LOG(LogSandbox, Error, TEXT("Play session: the character's get-up could not be led (%d time(s)): %s; the body stays down"),
			NotLedTold, *GetUps.NotLed);
	}
}

void USandboxPlaySubsystem::AddHudLines(TArray<FString>& OutLines) const
{
	const USandboxBodyController* Controller = Character ? Character->GetController() : nullptr;
	if (Controller && Controller->GetGetUpView().NotLedAttempts > 0)
	{
		OutLines.Add(FString::Printf(TEXT("GET-UP NOT LED, %d time(s): %s; the body stays down"),
			Controller->GetGetUpView().NotLedAttempts, *Controller->GetGetUpView().NotLed));
	}
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
	// What the session's hands could do that the tuning files do not give them (a session started with -Tune):
	// the replay switches the same on before its first step, or a grip that took hold in the session would
	// close on nothing in the replay.
	TArray<TSharedPtr<FJsonValue>> ReplayEvents;
	const ASandboxEffectorRig* RecordedRig = Pawn.IsValid() ? Pawn->GetRig() : nullptr;
	if (RecordedRig)
	{
		const FSandboxEffectorTuning& Hand = RecordedRig->GetTuning().Hand;
		const TPair<const TCHAR*, double> Switches[] = {
			{ TEXT("effectors.hand.grab"), Hand.Grab },
			{ TEXT("effectors.hand.palm_collides"), Hand.PalmCollides } };
		for (const TPair<const TCHAR*, double>& Switch : Switches)
		{
			if (Switch.Value > 0.5)
			{
				const TSharedRef<FJsonObject> Event = MakeShared<FJsonObject>();
				Event->SetNumberField(TEXT("t"), 0.0);
				Event->SetStringField(TEXT("type"), TEXT("console"));
				Event->SetStringField(TEXT("command"), FString::Printf(TEXT("Sandbox.Tune %s=1"), Switch.Key));
				ReplayEvents.Add(MakeShared<FJsonValueObject>(Event));
			}
		}
	}
	// A session with perception: its replay judges what moves as the session did, with the same switches of what the
	// body does with it (without them the replay's body would not turn its head or raise a guard where the session's did).
	if (bPerception)
	{
		const TSharedPtr<FJsonObject>* SeeingBody = nullptr;
		if (Scenario->TryGetObjectField(TEXT("body"), SeeingBody))
		{
			(*SeeingBody)->SetBoolField(TEXT("perception"), true);
		}
		if (const USandboxBodyController* Seeing = Character ? Character->GetController() : nullptr)
		{
			const FSandboxPerceptionTuning& Look = Seeing->GetTuning().Perception;
			const TPair<const TCHAR*, double> Switches[] = {
				{ TEXT("perception.gaze"), Look.Gaze },
				{ TEXT("perception.anticipate"), Look.Anticipate },
				{ TEXT("perception.guard"), Look.Guard } };
			for (const TPair<const TCHAR*, double>& Switch : Switches)
			{
				if (Switch.Value > 0.5)
				{
					const TSharedRef<FJsonObject> Event = MakeShared<FJsonObject>();
					Event->SetNumberField(TEXT("t"), 0.0);
					Event->SetStringField(TEXT("type"), TEXT("console"));
					Event->SetStringField(TEXT("command"), FString::Printf(TEXT("Sandbox.Tune %s=1"), Switch.Key));
					ReplayEvents.Add(MakeShared<FJsonValueObject>(Event));
				}
			}
		}
	}
	// R10: the session's behaviours are replayed as the session had them, on or off, whatever the files say when it is replayed (on in
	// the files since decision 67; a session may have switched one off by -Tune); all in one command, as stay_down needs fear to be taken.
	if (const USandboxBodyController* Choosing = Character ? Character->GetController() : nullptr)
	{
		const FSandboxBehaviourTuning& Behaviour = Choosing->GetTuning().Behaviour;
		const auto Switch = [](const TCHAR* Name, double Value) { return FString::Printf(TEXT("behaviour.%s=%d"), Name, Value > 0.5 ? 1 : 0); };
		const TArray<FString> Switches = { Switch(TEXT("fear"), Behaviour.Fear), Switch(TEXT("stay_down"), Behaviour.StayDown),
			Switch(TEXT("protect"), Behaviour.Protect), Switch(TEXT("commands"), Behaviour.Commands), Switch(TEXT("give_hand"), Behaviour.GiveHand),
			Switch(TEXT("struggle"), Behaviour.Struggle), Switch(TEXT("protect_held_leg"), Behaviour.ProtectHeldLeg),
			Switch(TEXT("command_cover_react"), Behaviour.CommandCoverReact) };
		const TSharedRef<FJsonObject> Event = MakeShared<FJsonObject>();
		Event->SetNumberField(TEXT("t"), 0.0);
		Event->SetStringField(TEXT("type"), TEXT("console"));
		Event->SetStringField(TEXT("command"), FString(TEXT("Sandbox.Tune ")) + FString::Join(Switches, TEXT(",")));
		ReplayEvents.Add(MakeShared<FJsonValueObject>(Event));
	}
	// R10, step 8 in part: the commands given while it was recorded, at their frames.
	for (const TPair<int32, FString>& Given : Recording->Commands)
	{
		const TSharedRef<FJsonObject> Event = MakeShared<FJsonObject>();
		Event->SetNumberField(TEXT("t"), static_cast<double>(StartFrame + Given.Key) / Spec.FixedFps);
		Event->SetStringField(TEXT("type"), TEXT("console"));
		Event->SetStringField(TEXT("command"), FString(TEXT("Sandbox.Command ")) + Given.Value);
		ReplayEvents.Add(MakeShared<FJsonValueObject>(Event));
	}
	Scenario->SetArrayField(TEXT("events"), ReplayEvents);
	Scenario->SetObjectField(TEXT("expect"), MakeShared<FJsonObject>());
	Scenario->RemoveField(TEXT("end_when_standing_s"));
	Scenario->SetStringField(TEXT("recording"), RelativeRecording);
	Scenario->SetNumberField(TEXT("recording_start_s"), static_cast<double>(StartFrame) / Spec.FixedFps);
	// R10, step 2: the replay draws as the session did.
	Scenario->SetNumberField(TEXT("seed"), SessionSeed);
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
		// R10: the commands the recording kept (the body's last as many as the recording has: every one told while it ran was kept), with
		// the body's answers, in the terms of a run's control.commands, for the replay, which gives the recording's commands again, to be
		// held against. (Matched first by the body's clock a frame behind the recording's: a body a frame later would have dropped one.)
		TArray<TSharedPtr<FJsonValue>> Commands;
		const TArray<FSandboxBehaviourView::FCommand>& Told = Controller->GetBehaviourView().Commands;
		for (int32 Index = FMath::Max(Told.Num() - Recording->Commands.Num(), 0); Index < Told.Num(); ++Index)
		{
			const TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("command"), Told[Index].Name);
			Entry->SetNumberField(TEXT("tell_s"), Told[Index].TellS);
			Entry->SetStringField(TEXT("answer"), Told[Index].Answer);
			Commands.Add(MakeShared<FJsonValueObject>(Entry));
		}
		Seen->SetArrayField(TEXT("commands"), Commands);
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
		// In a headset: what the tracking gave for this hand over the whole session so far.
		const FSandboxTrackedTargetSource* Tracked = Pawn->GetTrackedSource();
		if (const FSandboxTrackedHandStats* Tracking = Tracked ? Tracked->GetStats(Effector->GetId()) : nullptr)
		{
			const TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetNumberField(TEXT("tracked_frames"), Tracking->TrackedFrames);
			Entry->SetNumberField(TEXT("lost_frames"), Tracking->LostFrames);
			Entry->SetNumberField(TEXT("estimated_frames"), Tracking->EstimatedFrames);
			Entry->SetNumberField(TEXT("estimated_followed_frames"), Tracking->EstimatedFollowedFrames);
			Entry->SetNumberField(TEXT("moved_without_velocity_frames"), Tracking->MovedWithoutVelocityFrames);
			Entry->SetNumberField(TEXT("catch_ups"), Tracking->CatchUps);
			Entry->SetNumberField(TEXT("catch_up_frames"), Tracking->CatchUpFrames);
			Entry->SetNumberField(TEXT("no_linear_velocity_frames"), Tracking->NoLinearVelocityFrames);
			Entry->SetNumberField(TEXT("no_angular_velocity_frames"), Tracking->NoAngularVelocityFrames);
			Entry->SetNumberField(TEXT("max_target_speed_mps"), Tracking->MaxTargetSpeedCmS * 0.01);
			Entry->SetNumberField(TEXT("max_grip"), Tracking->MaxGrip);
			Hand->SetObjectField(TEXT("tracking"), Entry);
		}
		Hands->SetObjectField(Effector->GetId().ToString(), Hand);
	}
	Seen->SetObjectField(TEXT("hands"), Hands);
	if (Pawn.IsValid() && Pawn->IsInHeadset())
	{
		// In a headset: how the player moved through the session so far (FR-42).
		const FSandboxLocomotionStats& Moved = Pawn->GetLocomotion().GetStats();
		const TSharedRef<FJsonObject> Moves = MakeShared<FJsonObject>();
		Moves->SetNumberField(TEXT("walked_m"), Moved.WalkedCm * 0.01);
		Moves->SetNumberField(TEXT("moving_frames"), Moved.MovingFrames);
		Moves->SetNumberField(TEXT("max_speed_mps"), Moved.MaxSpeedCmS * 0.01);
		Moves->SetNumberField(TEXT("snaps_left"), Moved.SnapsLeft);
		Moves->SetNumberField(TEXT("snaps_right"), Moved.SnapsRight);
		Moves->SetNumberField(TEXT("held_back_frames"), Moved.HeldBackFrames);
		Moves->SetNumberField(TEXT("max_body_gap_cm"), Moved.MaxBodyGapCm);
		Moves->SetNumberField(TEXT("max_view_darkening"), Pawn->GetMaxHeadFade());
		Seen->SetObjectField(TEXT("moves"), Moves);
	}
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
	if (bDemoWalk)
	{
		TickWalkDemo(*Player);
		return;
	}
	// Looking at the character's front from the side it faces (+y), level, at arm's length.
	const FVector Chest = Spec.SpawnLocationCm + FVector(0.0, 0.0, 128.0);
	if (bDemoTakes)
	{
		// Three takes: each begins with the character put back and the hands made anew, and is recorded by
		// itself. In the first and the third the left hand touches the chest, in the second the right.
		switch (DemoFrame)
		{
		case 5:
			Player->SelectTool(ESandboxTool::Hands);
			Player->SetView(Chest + FVector(0.0, 85.0, 0.0), FRotator(0.0, -90.0, 0.0));
			// The hands are open for all three takes: a fist that is sent at the body is a punch, and the body
			// steps; an open hand reaches.
			Player->SetHandsOpen(true);
			break;
		case 60:
		case 260:
		case 460:
			Player->RequestNewTake();
			break;
		// A touch, not a shove: an open hand (2 m/s, no blow), which turns back as it arrives. A shove leaves the body on the edge of a
		// second step, where the smallest difference between the session and its replay tells two stories.
		// And a second into the take, not at once: a fresh body is still bringing its arms down.
		case 160:
		case 560:
			Player->SetSecondary(true);
			break;
		case 176:
		case 576:
			Player->SetSecondary(false);
			break;
		case 360:
			Player->SetPrimary(true);
			break;
		case 376:
			Player->SetPrimary(false);
			break;
		case 700:
			Player->StopRecording();
			if (Player->GetTakes() != 3 || RecordingsSaved != 3)
			{
				LastRecordingError = FString::Printf(TEXT("%d takes begun and %d recordings written, 3 of each expected"), Player->GetTakes(), RecordingsSaved);
			}
			FinishDemo();
			return;
		default:
			break;
		}
		++DemoFrame;
		return;
	}

	if (bDemoGrab)
	{
		// The desktop's way to hold, and the one path of a grab that no scenario reaches: the hands open, the
		// right one reaches into the chest, the grip closes and takes hold; the character is put back while
		// it is held (what the hand held is gone); the hand comes back, takes hold of the new character, pulls
		// and lets go.
		const ASandboxEffectorRig* Rig = Player->GetRig();
		const USandboxEffectorComponent* Hand = Rig ? Rig->FindEffector(SandboxEffectors::HandRight) : nullptr;
		if (Hand && (DemoFrame % 10 == 0 || DemoFrame == 101 || DemoFrame == 321))
		{
			// What the hand is doing, every ninth of a second: the demo's own record in the log.
			const FSandboxEffectorStats& Seen = Hand->GetStats();
			UE_LOG(LogSandbox, Display, TEXT("Play demo (grab) frame %d: %s, %s, grabs %d lost %d slipped %d torn %d, %.1f cm from its target, presses with %.0f N, %.1f m/s, passing bodies %d"),
				DemoFrame, Hand->IsFist() ? TEXT("fist") : TEXT("open"), Hand->IsGrabbing() ? *Hand->GetGrabbedBone().ToString() : TEXT("holds nothing"),
				Seen.Grabs, Seen.GrabsLost, Seen.GrabSlips, Seen.GrabOverloads, Hand->GetGapCm(), Hand->GetContactForceN(), Hand->GetHandVelocity().Size() * 0.01, Hand->IsPassingBodies() ? 1 : 0);
		}
		switch (DemoFrame)
		{
		case 5:
			Player->SelectTool(ESandboxTool::Hands);
			Player->SetView(Chest + FVector(0.0, 85.0, 0.0), FRotator(0.0, -90.0, 0.0));
			break;
		case 60:
			Player->SetHandsOpen(true);
			break;
		case 80:
			Player->SetPrimary(true);
			break;
		// The grip closes while the hand still presses on the chest: a body that is pushed gives way, and a
		// moment later it is out of the hand's reach.
		case 100:
			Player->SetHandsOpen(false);
			break;
		case 150:
			bGrabHeld = Hand && Hand->IsGrabbing();
			break;
		case 200:
			Player->RequestReset();
			break;
		case 215:
			bGrabLostOnReset = Hand && !Hand->IsGrabbing() && Hand->GetStats().GrabsLost == 1;
			Player->SetPrimary(false);
			break;
		case 280:
			Player->SetHandsOpen(true);
			break;
		case 300:
			Player->SetPrimary(true);
			break;
		case 320:
			Player->SetHandsOpen(false);
			break;
		case 370:
			bGrabHeldAgain = Hand && Hand->IsGrabbing() && Hand->GetStats().Grabs == 2;
			Player->SetPrimary(false);
			break;
		case 440:
			Player->SetHandsOpen(true);
			break;
		case 460:
			bGrabLetGo = Hand && !Hand->IsGrabbing();
			Player->SetHandsOpen(false);
			break;
		// And the desktop's kick, at what the camera looks at: from the same place, looking down at the thighs.
		case 480:
			Player->SetView(Chest + FVector(0.0, 85.0, 0.0), FRotator(-35.0, -90.0, 0.0));
			break;
		case 510:
		{
			const USandboxEffectorComponent* Foot = Rig ? Rig->FindEffector(SandboxEffectors::FootRight) : nullptr;
			const USandboxBodyController* Kicked = Character ? Character->GetController() : nullptr;
			KickImpulseBeforeNs = Foot ? Foot->GetStats().ContactImpulseNs : 0.0;
			KickHitsBefore = 0;
			for (const FSandboxBodyEvent& Event : Kicked ? Kicked->GetEvents() : TArray<FSandboxBodyEvent>())
			{
				KickHitsBefore += Event.Type == ESandboxBodyEventType::Hit ? 1 : 0;
			}
			Player->Kick();
			break;
		}
		case 570:
		{
			// Landed: the foot delivered something after the key, and the character told of a hit of its own.
			const USandboxEffectorComponent* Foot = Rig ? Rig->FindEffector(SandboxEffectors::FootRight) : nullptr;
			const USandboxBodyController* Kicked = Character ? Character->GetController() : nullptr;
			int32 Hits = 0;
			for (const FSandboxBodyEvent& Event : Kicked ? Kicked->GetEvents() : TArray<FSandboxBodyEvent>())
			{
				Hits += Event.Type == ESandboxBodyEventType::Hit ? 1 : 0;
			}
			KickImpulseNs = Foot ? Foot->GetStats().ContactImpulseNs - KickImpulseBeforeNs : 0.0;
			bKickLanded = KickImpulseNs > 10.0 && Hits > KickHitsBefore;
			break;
		}
		case 595:
		{
			// And the foot is home again, without having been put there: it came back at the kick's speed.
			const USandboxEffectorComponent* Foot = Rig ? Rig->FindEffector(SandboxEffectors::FootRight) : nullptr;
			bKickLanded = bKickLanded && Foot && Foot->GetGapCm() < 5.0 && Foot->GetStats().Snaps == 0;
			break;
		}
		case 600:
			FinishDemo();
			return;
		default:
			break;
		}
		++DemoFrame;
		return;
	}

	switch (DemoFrame)
	{
	case 5:
		Player->SelectTool(ESandboxTool::Hands);
		Player->SetView(Chest + FVector(0.0, 85.0, 0.0), FRotator(0.0, -90.0, 0.0));
		// Open hands, as in the takes: this session is held against its replay, and a fist sent at the chest is
		// a punch that makes the body step, where the smallest difference between the two tells two stories.
		Player->SetHandsOpen(true);
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
	// The right hand reaches to the chest, touches it and comes back; then the left.
	case 90:
		Player->SetPrimary(true);
		break;
	case 109:
		Player->SetPrimary(false);
		break;
	case 170:
		Player->SetSecondary(true);
		break;
	case 189:
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

void USandboxPlaySubsystem::TickWalkDemo(ASandboxPawn& Player)
{
	// The way a person in the headset takes, without one (FR-42): the head stands where the session put it; the right
	// stick walks the player into the character and on along it; the left stick turns to the right and back; the stick
	// walks the player away. What each did is measured as it happens and judged at the end (DescribeWalk). The sticks
	// set here are read by the pawn in the next frame.
	FSandboxStandInHeadset& Headset = *StandIn;
	const ASandboxEffectorRig* Rig = Player.GetRig();
	const USandboxEffectorComponent* Body = Rig ? Rig->FindEffector(SandboxEffectors::Body) : nullptr;
	const auto Facing = [&Player, &Headset]() { return FSandboxLocomotion::LevelForward(Player.GetActorQuat() * Headset.HeadRotation); };
	const auto YawDeg = [](const FVector& Direction) { return FMath::RadiansToDegrees(FMath::Atan2(Direction.Y, Direction.X)); };
	const auto EachHand = [Rig](TFunctionRef<void(const USandboxEffectorComponent&)> Each)
	{
		for (const FName Hand : { SandboxEffectors::HandLeft, SandboxEffectors::HandRight })
		{
			if (const USandboxEffectorComponent* Effector = Rig ? Rig->FindEffector(Hand) : nullptr)
			{
				Each(*Effector);
			}
		}
	};
	const int32 Frame = DemoFrame;
	if (Frame == 10)
	{
		if (!Player.IsInHeadset() || !Rig || !Body)
		{
			LastRecordingError = TEXT("the pawn did not take the stand-in for a headset, or made no hands and no body in it");
			FinishDemo();
			return;
		}
		Walk.bChecked = true;
		Walk.PlacedOffCm = FVector::Dist2D(Player.GetHeadLocation(), WalkStandPoint);
		Walk.PlacedOffDeg = FMath::Abs(FRotator::NormalizeAxis(YawDeg(Facing()) - WalkStandYawDeg));
	}
	else if (Frame == 60 && !Player.StartRecording())
	{
		LastRecordingError = TEXT("the pawn could not start a recording (see the log)");
		FinishDemo();
		return;
	}

	FVector2D Move = FVector2D::ZeroVector;
	FVector2D Turn = FVector2D::ZeroVector;
	// Into the character at full speed, pressing on for 3 s; then forward and to the right, so that what holds the body
	// back takes the forward half and the player slides along.
	if (Frame >= 90 && Frame < 360)
	{
		Move = FVector2D(0.0, 1.0);
	}
	else if (Frame >= 360 && Frame < 450)
	{
		Move = FVector2D(1.0, 1.0) / FMath::Sqrt(2.0);
	}
	else if (Frame >= 540 && Frame < 630)
	{
		Move = FVector2D(0.0, -1.0);
	}
	if (Frame == 360)
	{
		const FVector Ahead = Facing();
		Walk.SlideStart = Player.GetHeadLocation();
		Walk.SlideRight = FVector(-Ahead.Y, Ahead.X, 0.0);
	}
	else if (Frame == 450)
	{
		Walk.SlideCm = FVector::DotProduct(Player.GetHeadLocation() - Walk.SlideStart, Walk.SlideRight);
	}
	// Two snap turns, to the right and back: the head stays where it is, and the hands are not flung.
	if ((Frame >= 480 && Frame < 490) || (Frame >= 520 && Frame < 530))
	{
		Turn = FVector2D(Frame < 500 ? 1.0 : -1.0, 0.0);
	}
	if (Frame == 479 || Frame == 519)
	{
		Walk.HeadBeforeSnap = Player.GetHeadLocation();
		Walk.YawBeforeSnapDeg = YawDeg(Facing());
	}
	else if (Frame == 490 || Frame == 530)
	{
		Walk.SnapHeadOffCm = FMath::Max(Walk.SnapHeadOffCm, FVector::Dist(Player.GetHeadLocation(), Walk.HeadBeforeSnap));
		Walk.SnapTurnsDeg[Frame == 490 ? 0 : 1] = FRotator::NormalizeAxis(YawDeg(Facing()) - Walk.YawBeforeSnapDeg);
	}
	if (Frame >= 480 && Frame < 560)
	{
		EachHand([this](const USandboxEffectorComponent& Hand) { Walk.HandsMaxSpeedAfterSnapMps = FMath::Max(Walk.HandsMaxSpeedAfterSnapMps, Hand.GetHandVelocity().Size() * 0.01); });
	}
	// The hands keep up with a walk: before the body meets the character, and walking away from it.
	if ((Frame >= 100 && Frame < 120) || (Frame >= 560 && Frame < 630))
	{
		EachHand([this](const USandboxEffectorComponent& Hand) { Walk.HandsMaxGapWalkingCm = FMath::Max(Walk.HandsMaxGapWalkingCm, Hand.GetGapCm()); });
	}
	// The body pressed against the character, and what it pushes with; the view while walking into it.
	if (Frame >= 200 && Frame < 360 && Body)
	{
		Walk.BodyForceSumN += Body->GetContactForceN();
		++Walk.BodyForceFrames;
		Walk.BodyTouchingFrames += Body->GetContactForceN() > 1.0 ? 1 : 0;
		Walk.BodyMaxForceN = FMath::Max(Walk.BodyMaxForceN, Body->GetContactForceN());
	}
	if (Frame >= 90 && Frame < 450)
	{
		Walk.MaxFadeWalking = FMath::Max(Walk.MaxFadeWalking, Player.GetHeadFade());
	}
	if (Frame == 690)
	{
		Player.StopRecording();
		const USandboxBodyController* Controller = Character ? Character->GetController() : nullptr;
		Walk.CharacterStateAtEnd = Controller ? FSandboxBodyTuning::StateName(Controller->GetState()) : FString();
		for (const FSandboxBehaviourView::FCommand& Given : Controller ? Controller->GetBehaviourView().Commands : TArray<FSandboxBehaviourView::FCommand>())
		{
			Walk.CommandNames += (Walk.CommandNames.IsEmpty() ? TEXT("") : TEXT(",")) + Given.Name;
			Walk.CommandAnswers += FString::Printf(TEXT("%s%s at %.2f s: %s"), Walk.CommandAnswers.IsEmpty() ? TEXT("") : TEXT("; "), *Given.Name, Given.TellS, *Given.Answer);
		}
		FinishDemo();
		return;
	}
	Headset.Right.Stick = Move;
	Headset.Left.Stick = Turn;
	// R10 (decision 42): the right controller's first face button (A: get_up), then its second (B: stop), then the left's first (X:
	// give_hand), each held down for six frames: each reaches the body once, through the console, and the recording keeps it.
	Headset.Right.bFirstButton = Frame >= 640 && Frame < 646;
	Headset.Right.bSecondButton = Frame >= 660 && Frame < 666;
	// And the left controller's first (X: give_hand).
	Headset.Left.bFirstButton = Frame >= 675 && Frame < 681;
	++DemoFrame;
}

TSharedRef<FJsonObject> USandboxPlaySubsystem::DescribeWalk(bool& bOutPass) const
{
	const TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> Checks;
	bOutPass = Walk.bChecked;
	const auto Check = [&](const TCHAR* Name, bool bPass, const FString& Detail)
	{
		const TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"), Name);
		Entry->SetBoolField(TEXT("pass"), bPass);
		Entry->SetStringField(TEXT("detail"), Detail);
		Checks.Add(MakeShared<FJsonValueObject>(Entry));
		bOutPass = bOutPass && bPass;
	};
	const ASandboxPawn* Player = Pawn.Get();
	if (!Player)
	{
		bOutPass = false;
		return Out;
	}
	const FSandboxInteractionTuning& Tuning = Player->GetTuning();
	const FSandboxLocomotionStats& Moved = Player->GetLocomotion().GetStats();
	const double MeanForceN = Walk.BodyForceFrames > 0 ? Walk.BodyForceSumN / Walk.BodyForceFrames : 0.0;
	Check(TEXT("head_stands_where_put"), Walk.PlacedOffCm >= 0.0 && Walk.PlacedOffCm <= 1.0 && Walk.PlacedOffDeg <= 1.0,
		FString::Printf(TEXT("%.2f cm from the stand point, %.2f degrees from facing the character"), Walk.PlacedOffCm, Walk.PlacedOffDeg));
	Check(TEXT("walk_stops_at_the_character"), Moved.HeldBackFrames > 30 && Moved.MostHeldBack >= 0.99,
		FString::Printf(TEXT("the stick was held back in %d frames, by %.0f%% at most"), Moved.HeldBackFrames, Moved.MostHeldBack * 100.0));
	// The character gives way and steps back, and the player follows it: the body touches it in part of the frames, and
	// pushes on average no harder than its cap (a blow of a frame is no push).
	Check(TEXT("body_pushes_no_harder_than_a_hand"), Walk.BodyTouchingFrames * 4 >= Walk.BodyForceFrames && Walk.BodyForceFrames > 0 && MeanForceN <= 1.05 * Tuning.Body.MaxForceN,
		FString::Printf(TEXT("touching in %d of %d frames, with %.0f N on average (%.0f N in one frame at most); the body's cap is %.0f N"),
			Walk.BodyTouchingFrames, Walk.BodyForceFrames, MeanForceN, Walk.BodyMaxForceN, Tuning.Body.MaxForceN));
	Check(TEXT("view_not_darkened_by_walking_into_it"), Walk.MaxFadeWalking < 0.5,
		FString::Printf(TEXT("darkened by %.0f%% at most while walking into the character and along it"), Walk.MaxFadeWalking * 100.0));
	Check(TEXT("slides_along_the_character"), Walk.SlideCm >= 30.0, FString::Printf(TEXT("%.1f cm to the right in 1 s of forward and right"), Walk.SlideCm));
	Check(TEXT("snap_turns_about_the_head"), Walk.SnapHeadOffCm <= 0.1 && FMath::Abs(Walk.SnapTurnsDeg[0] - Tuning.Tools.HeadsetSnapTurnDeg) <= 0.1 && FMath::Abs(Walk.SnapTurnsDeg[1] + Tuning.Tools.HeadsetSnapTurnDeg) <= 0.1,
		FString::Printf(TEXT("turned %.2f and %.2f degrees, the head %.3f cm from where it was at most"), Walk.SnapTurnsDeg[0], Walk.SnapTurnsDeg[1], Walk.SnapHeadOffCm));
	Check(TEXT("hands_not_flung_by_a_turn"), Walk.HandsMaxSpeedAfterSnapMps <= Tuning.Hand.StrikeSpeedMps,
		FString::Printf(TEXT("%.2f m/s at most after the turns (a blow begins at %.1f)"), Walk.HandsMaxSpeedAfterSnapMps, Tuning.Hand.StrikeSpeedMps));
	Check(TEXT("hands_keep_up_with_a_walk"), Walk.HandsMaxGapWalkingCm > 0.0 && Walk.HandsMaxGapWalkingCm <= 4.0,
		FString::Printf(TEXT("%.2f cm behind their targets at most while walking"), Walk.HandsMaxGapWalkingCm));
	// R10: the face buttons give the body a command once a press (the answers are the body's: a standing body refuses get_up).
	Check(TEXT("face_buttons_reach_the_body"), Walk.CommandNames == TEXT("get_up,stop,give_hand"),
		FString::Printf(TEXT("A and then B on the right controller and X on the left, each held 6 frames: %s"), Walk.CommandAnswers.IsEmpty() ? TEXT("no command") : *Walk.CommandAnswers));
	Out->SetArrayField(TEXT("checks"), Checks);
	Out->SetNumberField(TEXT("walked_m"), Moved.WalkedCm * 0.01);
	Out->SetNumberField(TEXT("max_speed_mps"), Moved.MaxSpeedCmS * 0.01);
	Out->SetNumberField(TEXT("snaps_left"), Moved.SnapsLeft);
	Out->SetNumberField(TEXT("snaps_right"), Moved.SnapsRight);
	Out->SetNumberField(TEXT("max_body_gap_cm"), Moved.MaxBodyGapCm);
	Out->SetNumberField(TEXT("max_view_darkening"), Player->GetMaxHeadFade());
	Out->SetStringField(TEXT("character_state_at_end"), Walk.CharacterStateAtEnd);
	return Out;
}

void USandboxPlaySubsystem::FinishDemo()
{
	bool bPass = LastRecordingError.IsEmpty() && !LastRecordingScenario.IsEmpty();
	const TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	if (bDemoWalk)
	{
		bool bWalkPass = false;
		Root->SetObjectField(TEXT("walk"), DescribeWalk(bWalkPass));
		bPass = bPass && bWalkPass;
	}
	if (bDemoGrab)
	{
		bPass = bGrabHeld && bGrabLostOnReset && bGrabHeldAgain && bGrabLetGo && bKickLanded;
		const TSharedRef<FJsonObject> Grab = MakeShared<FJsonObject>();
		Grab->SetBoolField(TEXT("held"), bGrabHeld);
		Grab->SetBoolField(TEXT("lost_on_reset"), bGrabLostOnReset);
		Grab->SetBoolField(TEXT("held_again"), bGrabHeldAgain);
		Grab->SetBoolField(TEXT("let_go"), bGrabLetGo);
		Grab->SetBoolField(TEXT("kick_landed"), bKickLanded);
		const ASandboxEffectorRig* Rig = Pawn.IsValid() ? Pawn->GetRig() : nullptr;
		if (const USandboxEffectorComponent* Foot = Rig ? Rig->FindEffector(SandboxEffectors::FootRight) : nullptr)
		{
			Grab->SetNumberField(TEXT("kick_impulse_ns"), KickImpulseNs);
			Grab->SetNumberField(TEXT("foot_impulse_before_the_kick_ns"), KickImpulseBeforeNs);
			Grab->SetNumberField(TEXT("foot_max_speed_mps"), Foot->GetStats().MaxSpeedCmS * 0.01);
		}
		if (const USandboxEffectorComponent* Hand = Rig ? Rig->FindEffector(SandboxEffectors::HandRight) : nullptr)
		{
			const FSandboxEffectorStats& Stats = Hand->GetStats();
			Grab->SetNumberField(TEXT("grabs"), Stats.Grabs);
			Grab->SetNumberField(TEXT("grabs_lost"), Stats.GrabsLost);
			Grab->SetNumberField(TEXT("grab_slips"), Stats.GrabSlips);
			Grab->SetNumberField(TEXT("grab_overloads"), Stats.GrabOverloads);
			Grab->SetNumberField(TEXT("grab_frames"), Stats.GrabFrames);
			Grab->SetNumberField(TEXT("max_grab_force_n"), Stats.MaxGrabForceN);
			Grab->SetNumberField(TEXT("max_speed_mps"), Stats.MaxSpeedCmS * 0.01);
			Grab->SetStringField(TEXT("last_grabbed"), Stats.LastGrabbed.IsNone() ? FString() : Stats.LastGrabbed.ToString());
			Grab->SetBoolField(TEXT("passing_bodies_at_end"), Hand->IsPassingBodies());
		}
		const USandboxBodyController* Controller = Character ? Character->GetController() : nullptr;
		Grab->SetStringField(TEXT("character_state_at_end"), Controller ? FSandboxBodyTuning::StateName(Controller->GetState()) : FString());
		Root->SetObjectField(TEXT("grab"), Grab);
	}
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

void USandboxPlaySubsystem::HoldFrameRate(bool bWait)
{
	// With a fixed timestep the engine runs as fast as it can. A person needs one simulated second per
	// second, so each frame waits until it is due. This is the only place that reads the clock, and it
	// decides when a frame happens, never what happens in it. In a headset the runtime paces the frames, and
	// a second wait on top of its own would only beat against it: there the frames are only counted.
	if (bWait)
	{
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
	}

	// Every ten seconds of frames the log says how long they took: a session that cannot hold the rate
	// runs slower than the clock, a headset that runs at another rate than the simulation's makes the world
	// run faster or slower than the clock, and this is where either shows.
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

	if (bResetPending)
	{
		bResetPending = false;
		ResetCharacter();
		if (Pawn.IsValid() && Pawn->IsTakeWaiting())
		{
			Pawn->BeginTake();
		}
	}
	// The character is told what the hands hold of it; neither knows the other.
	FScenarioStage::TellBodyOfHolds(Pawn.IsValid() ? Pawn->GetRig() : nullptr, Character);
	TellPerception();
	TellNotLed();

	// A recording from the first frame begins once the pawn has its hands, which it makes when it begins to
	// play: later than the session adopts it. (Asked for at the adoption, as it first was, the recording never
	// began and nothing said so.)
	if (bRecordFromStart && !bRecordStarted && Pawn.IsValid())
	{
		bRecordStarted = Pawn->StartRecording();
		if (bRecordStarted)
		{
			UE_LOG(LogSandbox, Display, TEXT("Play session: recording the hands as '%s' from simulated frame %lld"), *RecordName, Pawn->GetSimulatedFrames());
		}
		else if (++RecordStartTries == 90)
		{
			UE_LOG(LogSandbox, Error, TEXT("Play session: the recording '%s' could not be started in 90 frames: the pawn has no hands"), *RecordName);
		}
	}

	// A session with an end of its own: it stops its recording and quits as Escape would. For a person in a
	// headset, who cannot see the keyboard, and for a check of the whole way without a person.
	if (PlaySeconds > 0.0 && !bSmoke && !bDemo && Pawn.IsValid() && Pawn->GetSimulatedFrames() >= FMath::RoundToInt64(PlaySeconds * Spec.FixedFps))
	{
		UE_LOG(LogSandbox, Display, TEXT("Play session: %.1f s have been simulated; the session ends as it was asked to"), PlaySeconds);
		Pawn->StopRecording();
		bSmokePass = true;
		ExitCountdown = 0;
		return;
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
		HoldFrameRate(/*bWait*/ !FScenarioStage::IsStereo());
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
		Check(TEXT("hands_exist"), Rig && Rig->GetEffectors().Num() == 3 && Rig->FindEffector(SandboxEffectors::FootRight), FString::Printf(TEXT("%d effectors (two hands and the kicking foot)"), Rig ? Rig->GetEffectors().Num() : 0));
		double WorstGapCm = 0.0;
		for (const USandboxEffectorComponent* Effector : Rig ? Rig->GetEffectors() : TArray<TObjectPtr<USandboxEffectorComponent>>())
		{
			// The hands: the foot rests weakly below the camera and may lie against the floor of a scene with a low camera.
			if (SandboxEffectors::KindOf(Effector->GetId()) == FName(TEXT("hand")))
			{
				WorstGapCm = FMath::Max(WorstGapCm, Effector->GetGapCm());
			}
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
		// R10, step 8 in part: the U key's way to the body, through the console (refused here: commands are off in the files, and it stands).
		Player->CommandGetUp();
		break;
	}
	case 471:
	{
		const TArray<FSandboxBehaviourView::FCommand>& Told = Controller ? Controller->GetBehaviourView().Commands : TArray<FSandboxBehaviourView::FCommand>();
		Check(TEXT("command_key_reaches_the_body"), Told.Num() == 1 && Told[0].Name == TEXT("get_up") && Told[0].Answer.StartsWith(TEXT("refused")),
			Told.Num() > 0 ? FString::Printf(TEXT("%d command(s); the first %s: %s"), Told.Num(), *Told[0].Name, *Told[0].Answer) : FString(TEXT("no command reached the body")));
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
