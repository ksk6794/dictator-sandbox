#include "SandboxOverlaySubsystem.h"

#include "Algo/Find.h"
#include "CanvasItem.h"
#include "Components/LineBatchComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/HUD.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "PhysicsEngine/BodyInstance.h"
#include "PhysicsEngine/ConstraintInstance.h"
#include "SandboxBodyController.h"
#include "SandboxBodyProbe.h"
#include "SandboxCore.h"
#include "SandboxEffectorComponent.h"
#include "SandboxEffectorRig.h"
#include "SandboxPawn.h"
#include "SandboxPoseRig.h"
#include "SandboxRagdollActor.h"
#include "ScenarioStage.h"

namespace
{
	// How the overlay looks. These are not tuning values: they change nothing but the picture.
	const FLinearColor ColorGood(0.15f, 1.0f, 0.25f);
	const FLinearColor ColorWarn(1.0f, 0.85f, 0.1f);
	const FLinearColor ColorBad(1.0f, 0.15f, 0.1f);
	const FLinearColor ColorBone(0.9f, 0.9f, 0.9f);
	const FLinearColor ColorSlack(0.45f, 0.45f, 0.45f);
	const FLinearColor ColorWeak(1.0f, 0.45f, 0.0f);
	const FLinearColor ColorSupport(1.0f, 0.9f, 0.0f);
	const FLinearColor ColorAssist(0.0f, 0.9f, 1.0f);
	/** The led get-up's pull from the world (FSandboxCarry): violet, apart from the old assist's cyan. */
	const FLinearColor ColorCarry(0.65f, 0.35f, 1.0f);
	const FLinearColor ColorStaticContact(0.35f, 0.6f, 1.0f);
	const FLinearColor ColorHit(1.0f, 0.3f, 0.0f);
	const FLinearColor ColorHandle(1.0f, 0.2f, 1.0f);
	const FLinearColor ColorCaptureInside(0.2f, 1.0f, 0.9f);

	// Line widths are in the world, not on the screen: a line is as wide from the session's camera at arm's
	// length as it is from a scenario's camera three metres away.
	constexpr float ThinCm = 0.3f;
	constexpr float ThickCm = 0.6f;
	// A muscle's error is green up to the first angle, yellow at the second and red from the third.
	constexpr double ErrorGoodDeg = 2.0;
	constexpr double ErrorWarnDeg = 8.0;
	constexpr double ErrorBadDeg = 20.0;
	// Contact arrows: this many centimetres per newton, between a shortest and a longest arrow.
	constexpr double ArrowCmPerN = 0.05;
	constexpr double ArrowMinCm = 3.0;
	constexpr double ArrowMaxCm = 60.0;
	// Contacts weaker than this are not drawn: a resting arm against the side.
	constexpr double MinContactN = 1.0;

	struct FLayerName
	{
		const TCHAR* Name;
		uint32 Bit;
	};
	const FLayerName LayerTable[] = {
		{ TEXT("com"), SandboxOverlay::Com },
		{ TEXT("support"), SandboxOverlay::Support },
		{ TEXT("capture"), SandboxOverlay::Capture },
		{ TEXT("joints"), SandboxOverlay::Joints },
		{ TEXT("assist"), SandboxOverlay::Assist },
		{ TEXT("contacts"), SandboxOverlay::Contacts },
		{ TEXT("state"), SandboxOverlay::State },
		{ TEXT("hands"), SandboxOverlay::Hands } };

	FLinearColor ErrorColor(double ErrorDeg)
	{
		if (ErrorDeg <= ErrorGoodDeg)
		{
			return ColorGood;
		}
		if (ErrorDeg <= ErrorWarnDeg)
		{
			return FMath::Lerp(ColorGood, ColorWarn, static_cast<float>((ErrorDeg - ErrorGoodDeg) / (ErrorWarnDeg - ErrorGoodDeg)));
		}
		return FMath::Lerp(ColorWarn, ColorBad, static_cast<float>(FMath::Clamp((ErrorDeg - ErrorWarnDeg) / (ErrorBadDeg - ErrorWarnDeg), 0.0, 1.0)));
	}

	FAutoConsoleCommandWithWorldAndArgs OverlayCommand(
		TEXT("Sandbox.Overlay"),
		TEXT("Debug overlays: Sandbox.Overlay all | off | toggle | com,support,capture,joints,assist,contacts,state,hands | -joints. Without an argument it prints what is on."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			USandboxOverlaySubsystem* Overlay = World ? World->GetSubsystem<USandboxOverlaySubsystem>() : nullptr;
			if (!Overlay)
			{
				UE_LOG(LogSandbox, Display, TEXT("Sandbox.Overlay: this world has no overlays (it is not a game world)"));
				return;
			}
			if (Args.Num() == 0)
			{
				UE_LOG(LogSandbox, Display, TEXT("Sandbox.Overlay: %s. Layers: com support capture joints assist contacts state hands; also all, off, toggle"), *SandboxOverlay::LayerNames(Overlay->GetLayers()));
				return;
			}
			uint32 Layers = 0;
			FString Error;
			if (!SandboxOverlay::ParseLayers(FString::Join(Args, TEXT(" ")), Overlay->GetLayers(), Layers, Error))
			{
				// Display, not Error: a mistyped console command is not a failure of the run.
				UE_LOG(LogSandbox, Display, TEXT("Sandbox.Overlay: %s"), *Error);
				return;
			}
			Overlay->SetLayers(Layers);
		}));
}

bool SandboxOverlay::ParseLayers(const FString& Text, uint32 Current, uint32& OutLayers, FString& OutError)
{
	TArray<FString> Tokens;
	Text.ToLower().ParseIntoArray(Tokens, TEXT(","), /*bCullEmpty*/ true);
	TArray<FString> Words;
	for (const FString& Token : Tokens)
	{
		TArray<FString> Parts;
		Token.ParseIntoArrayWS(Parts);
		Words.Append(Parts);
	}
	if (Words.Num() == 0)
	{
		OutError = TEXT("no layer given (all, off, toggle, com, support, capture, joints, assist, contacts, state, hands)");
		return false;
	}

	// Names switch on exactly those layers; names with a minus take layers away from what is on.
	uint32 Added = 0;
	uint32 Removed = 0;
	bool bAnyAdded = false;
	for (const FString& Word : Words)
	{
		if (Word == TEXT("all") || Word == TEXT("on"))
		{
			Added |= All;
			bAnyAdded = true;
			continue;
		}
		if (Word == TEXT("off") || Word == TEXT("none"))
		{
			Removed |= All;
			continue;
		}
		if (Word == TEXT("toggle"))
		{
			if (Current == 0)
			{
				Added |= All;
				bAnyAdded = true;
			}
			else
			{
				Removed |= All;
			}
			continue;
		}
		const bool bMinus = Word.StartsWith(TEXT("-"));
		const FString Name = bMinus ? Word.RightChop(1) : Word;
		const FLayerName* Layer = Algo::FindByPredicate(LayerTable, [&Name](const FLayerName& Candidate) { return Name == Candidate.Name; });
		if (!Layer)
		{
			OutError = FString::Printf(TEXT("'%s' is not a layer (all, off, toggle, com, support, capture, joints, assist, contacts, state, hands)"), *Word);
			return false;
		}
		(bMinus ? Removed : Added) |= Layer->Bit;
		bAnyAdded |= !bMinus;
	}
	OutLayers = ((bAnyAdded ? Added : Current) & ~Removed) & All;
	return true;
}

FString SandboxOverlay::LayerNames(uint32 Layers)
{
	FString Names;
	for (const FLayerName& Layer : LayerTable)
	{
		if (Layers & Layer.Bit)
		{
			Names += (Names.IsEmpty() ? TEXT("") : TEXT(" ")) + FString(Layer.Name);
		}
	}
	return Names.IsEmpty() ? FString(TEXT("off")) : Names;
}

ASandboxOverlayActor::ASandboxOverlayActor()
{
	PrimaryActorTick.bCanEverTick = false;
	Lines = CreateDefaultSubobject<ULineBatchComponent>(TEXT("Lines"));
	SetRootComponent(Lines);
	SetCanBeDamaged(false);
}

void ASandboxOverlayActor::OnWatchedHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComponent, FVector NormalImpulse, const FHitResult& Hit)
{
	// A hand on the character is reported by both of them. The character's report names the bone that was
	// touched, so that is the one kept.
	if (CharacterMesh.IsValid() && HitComponent != CharacterMesh.Get() && OtherComponent == CharacterMesh.Get())
	{
		return;
	}
	FContact& Contact = PendingContacts.AddDefaulted_GetRef();
	Contact.Location = Hit.ImpactPoint;
	// The normal is given from the watched body's side: the impulse on that body is along it.
	Contact.Normal = Hit.ImpactNormal;
	Contact.ImpulseNs = NormalImpulse.Size() * 0.01;
	Contact.bStatic = !OtherComponent || !OtherComponent->IsSimulatingPhysics(Hit.BoneName);
	Contact.Bone = !Hit.MyBoneName.IsNone() ? Hit.MyBoneName : HitComponent ? HitComponent->GetFName() : NAME_None;
}

void ASandboxOverlayActor::PostRenderFor(APlayerController* PlayerController, UCanvas* Canvas, FVector CameraPosition, FVector CameraDir)
{
	Super::PostRenderFor(PlayerController, Canvas, CameraPosition, CameraDir);
	if (!Canvas || !GEngine)
	{
		return;
	}

	const auto Draw = [Canvas](const FString& Text, const FLinearColor& Color, double X, double Y, const UFont* Font)
	{
		FCanvasTextItem Item(FVector2D(X, Y), FText::FromString(Text), Font, Color);
		Item.EnableShadow(FLinearColor::Black);
		Canvas->DrawItem(Item);
	};

	// The corner block stands at the bottom left: the session's own HUD lines are at the top left.
	int32 CornerLines = 0;
	for (const FSandboxOverlayLabel& Label : Labels)
	{
		CornerLines += Label.bInWorld ? 0 : 1;
	}
	const UFont* CornerFont = GEngine->GetMediumFont();
	const UFont* WorldFont = GEngine->GetSmallFont();
	const double LineHeight = 20.0;
	double Y = Canvas->ClipY - 24.0 - LineHeight * CornerLines;
	for (const FSandboxOverlayLabel& Label : Labels)
	{
		if (!Label.bInWorld)
		{
			Draw(Label.Text, Label.Color, 24.0, Y, CornerFont);
			Y += LineHeight;
			continue;
		}
		// In front of the camera only: a point behind it projects onto the screen as well.
		if (FVector::DotProduct(Label.Location - CameraPosition, CameraDir) > 0.0)
		{
			const FVector Screen = Canvas->Project(Label.Location);
			Draw(Label.Text, Label.Color, Screen.X + 8.0, Screen.Y - 8.0, WorldFont);
		}
	}
}

bool USandboxOverlaySubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId USandboxOverlaySubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(USandboxOverlaySubsystem, STATGROUP_Tickables);
}

void USandboxOverlaySubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	FString Text;
	// Layers are separated by commas, so the value is read whole.
	if (FParse::Value(FCommandLine::Get(), TEXT("Overlay="), Text, /*bShouldStopOnSeparator*/ false))
	{
		uint32 Wanted = 0;
		FString Error;
		if (SandboxOverlay::ParseLayers(Text, 0, Wanted, Error))
		{
			SetLayers(Wanted);
		}
		else
		{
			UE_LOG(LogSandbox, Warning, TEXT("-Overlay=%s: %s"), *Text, *Error);
		}
	}
}

void USandboxOverlaySubsystem::Deinitialize()
{
	ReleaseActor();
	Super::Deinitialize();
}

void USandboxOverlaySubsystem::SetLayers(uint32 InLayers)
{
	InLayers &= SandboxOverlay::All;
	if (!FApp::CanEverRender())
	{
		// Nothing to look at: the overlay stays off, so a run without a renderer does no work for it.
		if (InLayers != 0)
		{
			UE_LOG(LogSandbox, Display, TEXT("Overlays need a renderer; this run has none, so they stay off"));
		}
		return;
	}
	if (InLayers == Layers)
	{
		return;
	}
	Layers = InLayers;
	if (Layers == 0)
	{
		ReleaseActor();
	}
	UE_LOG(LogSandbox, Display, TEXT("Overlays: %s"), *SandboxOverlay::LayerNames(Layers));
}

void USandboxOverlaySubsystem::EnsureActor()
{
	if (!Actor)
	{
		FActorSpawnParameters Parameters;
		Parameters.ObjectFlags |= RF_Transient;
		Actor = GetWorldRef().SpawnActor<ASandboxOverlayActor>(Parameters);
		LastWorldTimeS = -1.0;
	}
}

void USandboxOverlaySubsystem::ReleaseActor()
{
	UnwatchAll();
	if (AHUD* Old = Hud.Get())
	{
		if (Actor)
		{
			Old->RemovePostRenderedActor(Actor);
		}
		Old->bShowOverlays = bHudShowedOverlays;
	}
	Hud.Reset();
	if (Actor)
	{
		Actor->Destroy();
		Actor = nullptr;
	}
	ShownContacts.Reset();
	DrawnLines = 0;
	DrawnLabels = 0;
}

void USandboxOverlaySubsystem::Watch(UPrimitiveComponent* Component)
{
	if (Component && Actor && !Watched.Contains(Component))
	{
		Component->OnComponentHit.AddUniqueDynamic(Actor.Get(), &ASandboxOverlayActor::OnWatchedHit);
		Watched.Add(Component);
	}
}

void USandboxOverlaySubsystem::UnwatchAll()
{
	for (const TWeakObjectPtr<UPrimitiveComponent>& Component : Watched)
	{
		if (Component.IsValid() && Actor)
		{
			Component->OnComponentHit.RemoveDynamic(Actor.Get(), &ASandboxOverlayActor::OnWatchedHit);
		}
	}
	Watched.Reset();
}

void USandboxOverlaySubsystem::AttachToHud()
{
	// Text goes through the HUD's actor overlays: whatever class the HUD is, it draws them on the canvas
	// that ends up in screenshots. A headset has no such canvas, so there the overlay is lines only.
	const APlayerController* PlayerController = GetWorldRef().GetFirstPlayerController();
	AHUD* Current = PlayerController && !FScenarioStage::IsStereo() ? PlayerController->GetHUD() : nullptr;
	if (Current == Hud.Get())
	{
		return;
	}
	if (AHUD* Old = Hud.Get())
	{
		Old->RemovePostRenderedActor(Actor);
		Old->bShowOverlays = bHudShowedOverlays;
	}
	Hud = Current;
	if (Current)
	{
		bHudShowedOverlays = Current->bShowOverlays;
		Current->bShowOverlays = true;
		Current->AddPostRenderedActor(Actor);
	}
}

void USandboxOverlaySubsystem::Line(const FVector& From, const FVector& To, const FLinearColor& Color, float Thickness)
{
	// In front of everything: most of what is drawn lies inside the character.
	Actor->Lines->DrawLine(From, To, Color, SDPG_Foreground, Thickness, 0.0f);
}

void USandboxOverlaySubsystem::Cross(const FVector& At, double SizeCm, const FLinearColor& Color, float Thickness)
{
	Line(At - FVector(SizeCm, 0.0, 0.0), At + FVector(SizeCm, 0.0, 0.0), Color, Thickness);
	Line(At - FVector(0.0, SizeCm, 0.0), At + FVector(0.0, SizeCm, 0.0), Color, Thickness);
	Line(At - FVector(0.0, 0.0, SizeCm), At + FVector(0.0, 0.0, SizeCm), Color, Thickness);
}

void USandboxOverlaySubsystem::Ring(const FVector& Center, double RadiusCm, const FLinearColor& Color, float Thickness)
{
	constexpr int32 Sides = 16;
	FVector Previous = Center + FVector(RadiusCm, 0.0, 0.0);
	for (int32 Side = 1; Side <= Sides; ++Side)
	{
		const double Angle = UE_DOUBLE_TWO_PI * Side / Sides;
		const FVector Next = Center + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.0) * RadiusCm;
		Line(Previous, Next, Color, Thickness);
		Previous = Next;
	}
}

void USandboxOverlaySubsystem::Arrow(const FVector& From, const FVector& To, const FLinearColor& Color, float Thickness)
{
	Line(From, To, Color, Thickness);
	const FVector Along = (To - From).GetSafeNormal();
	if (Along.IsNearlyZero())
	{
		return;
	}
	FVector Side, Up;
	Along.FindBestAxisVectors(Side, Up);
	const double Head = FMath::Min(FVector::Dist(From, To) * 0.3, 6.0);
	Line(To, To - Along * Head + Side * Head * 0.5, Color, Thickness);
	Line(To, To - Along * Head - Side * Head * 0.5, Color, Thickness);
	Line(To, To - Along * Head + Up * Head * 0.5, Color, Thickness);
	Line(To, To - Along * Head - Up * Head * 0.5, Color, Thickness);
}

void USandboxOverlaySubsystem::WorldLabel(const FVector& At, const FString& Text, const FLinearColor& Color)
{
	Actor->Labels.Add(FSandboxOverlayLabel{ Text, Color, At, true });
}

void USandboxOverlaySubsystem::CornerLabel(const FString& Text, const FLinearColor& Color)
{
	Actor->Labels.Add(FSandboxOverlayLabel{ Text, Color, FVector::ZeroVector, false });
}

void USandboxOverlaySubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (Layers == 0)
	{
		return;
	}
	UWorld& World = GetWorldRef();
	EnsureActor();
	if (!Actor)
	{
		return;
	}
	AttachToHud();
	Actor->Lines->Flush();
	Actor->Labels.Reset();

	// The world's time moves only on frames that made a physics step. The contacts reported since the last
	// such frame are that step's; on a paused frame the ones of the last step stay on the screen.
	if (World.GetTimeSeconds() != LastWorldTimeS)
	{
		LastWorldTimeS = World.GetTimeSeconds();
		StepDtS = World.GetDeltaSeconds();
		ShownContacts = MoveTemp(Actor->PendingContacts);
		Actor->PendingContacts.Reset();
	}

	CornerLabel(FString::Printf(TEXT("Overlay: %s"), *SandboxOverlay::LayerNames(Layers)), ColorSlack);
	Actor->CharacterMesh.Reset();
	for (TActorIterator<ASandboxRagdollActor> Character(&World); Character; ++Character)
	{
		DrawCharacter(**Character);
	}
	if (Layers & SandboxOverlay::Hands)
	{
		for (TActorIterator<ASandboxEffectorRig> Rig(&World); Rig; ++Rig)
		{
			DrawHands(**Rig);
		}
		DrawDragHandle();
	}
	else
	{
		// The hands' contacts with the world are contacts all the same.
		for (TActorIterator<ASandboxEffectorRig> Rig(&World); Rig; ++Rig)
		{
			for (USandboxEffectorComponent* Effector : Rig->GetEffectors())
			{
				Watch(Effector);
			}
		}
	}
	if (Layers & SandboxOverlay::Contacts)
	{
		DrawContacts();
	}

	DrawnLines = Actor->Lines->BatchedLines.Num();
	DrawnLabels = Actor->Labels.Num();
}

void USandboxOverlaySubsystem::DrawCharacter(const ASandboxRagdollActor& Character)
{
	USkeletalMeshComponent* Mesh = Character.GetBodyMesh();
	const USandboxBodyController* Controller = Character.GetController();
	TArray<FSandboxBodySample> Bodies;
	FSandboxBodyProbe::SampleBodies(Mesh, Bodies);
	if (!Mesh || Bodies.Num() == 0)
	{
		return;
	}
	// Only a body under control reports its contacts.
	if (Controller)
	{
		Watch(Mesh);
		Actor->CharacterMesh = Mesh;
	}

	const FVector Com = FSandboxBodyProbe::CenterOfMass(Bodies);
	double LowestCm = TNumericLimits<double>::Max();
	for (const FSandboxBodySample& Body : Bodies)
	{
		LowestCm = FMath::Min(LowestCm, Body.WorldTransform.GetLocation().Z);
	}

	// The body's own sense of balance, as of the physics step that was last made: the feet the floor pushed
	// on, the support polygon under them and the capture point. What is drawn is what the controller goes by.
	// A passive body senses nothing.
	FSandboxBalanceView Balance;
	if (Controller)
	{
		Controller->MeasureBalance(Balance);
	}
	const TArray<FVector2D>& Polygon = Balance.SupportPolygon;
	const int32 StandingFeet = Balance.StandingFeet.Num();
	const double FloorCm = Polygon.Num() > 0 ? Balance.SupportHeightCm : LowestCm;
	const double MarginCm = FSandboxBodyProbe::MarginInsidePolygon(Polygon, FVector2D(Com.X, Com.Y));

	if ((Layers & SandboxOverlay::Support) && Polygon.Num() > 0)
	{
		// Just above the floor, or the floor would hide part of it in views that do not draw it in front.
		const double Z = FloorCm + 0.3;
		for (int32 Index = 0; Index < Polygon.Num(); ++Index)
		{
			const FVector2D& From = Polygon[Index];
			const FVector2D& To = Polygon[(Index + 1) % Polygon.Num()];
			Line(FVector(From.X, From.Y, Z), FVector(To.X, To.Y, Z), ColorSupport, ThickCm);
		}
	}

	if (Layers & SandboxOverlay::Com)
	{
		const FLinearColor Color = Polygon.Num() >= 3 ? (MarginCm > 0.0 ? ColorGood : ColorBad) : ColorSlack;
		const FVector Ground(Com.X, Com.Y, FloorCm + 0.3);
		Cross(Com, 6.0, Color, ThickCm);
		Line(Com, Ground, Color, ThinCm);
		Ring(Ground, 3.0, Color, ThickCm);
	}

	if ((Layers & SandboxOverlay::Capture) && Balance.bValid)
	{
		// Where the body would have to be supported to come to rest: ahead of the centre of mass by its
		// velocity. Inside the support polygon the body can stop where it stands; outside it needs a step.
		const FLinearColor Color = Polygon.Num() >= 3 ? (Balance.CaptureMarginCm > 0.0 ? ColorCaptureInside : ColorBad) : ColorSlack;
		const FVector Ground(Com.X, Com.Y, FloorCm + 0.3);
		const FVector Capture(Balance.CapturePoint.X, Balance.CapturePoint.Y, FloorCm + 0.3);
		Line(Ground, Capture, Color, ThickCm);
		Ring(Capture, 5.0, Color, ThickCm);
		Cross(Capture, 5.0, Color, ThinCm);

		// A recovery step: where the capture point was expected when the foot lands, where the foot is aimed, and
		// the point the swing has reached (magenta while the foot is in the air).
		if (Controller && Controller->GetStepLog().Num() > 0)
		{
			const FSandboxStepRecord& Step = Controller->GetStepLog().Last();
			const bool bSwinging = Controller->IsStepping();
			const FLinearColor StepColor = bSwinging ? FLinearColor(1.0f, 0.2f, 1.0f) : ColorSlack;
			const FVector Aim(Step.Target.X, Step.Target.Y, FloorCm + 0.3);
			Ring(Aim, 8.0, StepColor, ThickCm);
			Line(FVector(Step.From.X, Step.From.Y, FloorCm + 0.3), Aim, StepColor, ThinCm);
			Cross(FVector(Step.CaptureAtLanding.X, Step.CaptureAtLanding.Y, FloorCm + 0.3), 4.0, StepColor, ThinCm);
			if (bSwinging)
			{
				Cross(Controller->GetSwingPoint(), 4.0, StepColor, ThickCm);
			}
		}
	}

	FSandboxBodyControlView View;
	if (Controller)
	{
		Controller->GetControlView(View);
	}

	FName WorstMuscle;
	double WorstErrorDeg = 0.0;
	if (Layers & SandboxOverlay::Joints)
	{
		for (const FSandboxMuscleView& Muscle : View.Muscles)
		{
			const FBodyInstance* Child = Mesh->GetBodyInstance(Muscle.ChildBone);
			if (!Child || !Child->IsValidBodyInstance())
			{
				continue;
			}
			const FTransform Actual = Child->GetUnrealWorldTransform();
			const FVector Joint = Actual.GetLocation();
			const double ErrorDeg = FMath::RadiansToDegrees(Muscle.ErrorRad);
			if (Muscle.bDriven && ErrorDeg > WorstErrorDeg)
			{
				WorstErrorDeg = ErrorDeg;
				WorstMuscle = Muscle.ChildBone;
			}

			// The bone is drawn from its joint to the joints of the bodies that hang from it; a body at the
			// end of a chain (a hand, a foot, the head) to its centre of mass.
			TArray<FVector, TInlineAllocator<4>> Tips;
			for (const FConstraintInstance* Constraint : Mesh->Constraints)
			{
				FName NextBone, OnBone;
				const bool bNext = Constraint && FSandboxPoseRig::JointBones(Mesh->GetSkeletalMeshAsset()->GetRefSkeleton(), *Constraint, NextBone, OnBone) && OnBone == Muscle.ChildBone;
				const FBodyInstance* Next = bNext ? Mesh->GetBodyInstance(NextBone) : nullptr;
				if (Next && Next->IsValidBodyInstance())
				{
					Tips.Add(Next->GetUnrealWorldTransform().GetLocation());
				}
			}
			if (Tips.Num() == 0)
			{
				Tips.Add(Child->GetCOMPosition());
			}

			// Where the bone is: white, orange while a flinch has weakened its muscle, grey without a drive.
			const FLinearColor BoneColor = !Muscle.bDriven ? ColorSlack : Muscle.Strength < 1.0 ? ColorWeak : ColorBone;
			for (const FVector& Tip : Tips)
			{
				Line(Joint, Tip, BoneColor, ThinCm);
				if (Muscle.bDriven)
				{
					// Where the muscle turns it to: the same bone, turned about the joint.
					const FVector Target = Joint + Muscle.TargetRotation.RotateVector(Actual.GetRotation().UnrotateVector(Tip - Joint));
					const FLinearColor Color = ErrorColor(ErrorDeg);
					Line(Joint, Target, Color, ThickCm);
					Line(Tip, Target, Color, ThinCm);
				}
			}
		}
	}

	if (Layers & SandboxOverlay::Assist)
	{
		for (const FSandboxAssistView& Assist : View.Assists)
		{
			const FBodyInstance* Body = Assist.bActive ? Mesh->GetBodyInstance(Assist.Bone) : nullptr;
			if (Body && Body->IsValidBodyInstance())
			{
				// The target is the bone's pose; the cross marks it and the line is how far the body is off.
				Cross(Assist.Target.GetLocation(), 3.0, ColorAssist, ThinCm);
				Line(Body->GetUnrealWorldTransform().GetLocation(), Assist.Target.GetLocation(), ColorAssist, ThickCm);
			}
		}
		// The led get-up: where the world pulls each body's centre of mass to (a small cross), and the force it pulls with, from
		// the centre of mass (0.05 cm a newton: a body's weight of 80 kg would be 39 cm).
		for (const FSandboxCarryView& Carried : View.Carry)
		{
			Cross(Carried.TargetComCm, 2.0, ColorCarry, ThinCm);
			if (!Carried.ForceN.IsNearlyZero(1.0))
			{
				Line(Carried.ComCm, Carried.ComCm + Carried.ForceN * 0.05, ColorCarry, ThickCm);
			}
		}
	}

	if (Layers & SandboxOverlay::State)
	{
		if (Controller && Controller->GetStateLog().Num() > 0)
		{
			const FSandboxStateChange& Last = Controller->GetStateLog().Last();
			const FLinearColor Color = Last.State == ESandboxBodyState::Standing ? ColorGood
				: Last.State == ESandboxBodyState::Flinch || Last.State == ESandboxBodyState::Stagger ? ColorWeak
				: Last.State == ESandboxBodyState::Falling ? ColorBad : ColorSlack;
			const FString StateText = FString::Printf(TEXT("%s for %.2f s (%s)"),
				*FString(FSandboxBodyTuning::StateName(Last.State)).ToUpper(), Controller->GetElapsedS() - Last.TimeS, *Last.Reason);

			const FBodyInstance* Head = Mesh->GetBodyInstance(Controller->GetTuning().Rig.Head);
			const FVector Above = (Head && Head->IsValidBodyInstance() ? Head->GetCOMPosition() : Com) + FVector(0.0, 0.0, 28.0);
			WorldLabel(Above, StateText, Color);

			CornerLabel(FString::Printf(TEXT("State: %s"), *StateText), Color);
			const FSandboxAssistLoad& Load = Controller->GetAssistLoad();
			CornerLabel(FString::Printf(TEXT("Assist: force %.1f%% of body weight, torque %.1f%%; felt %.1f N*s from contacts"),
				Load.ForceShare * 100.0, Load.TorqueShare * 100.0, Controller->GetContactImpulseNs()));
			// The same body by body and by direction (control.authority), and what the led get-up is: the old assist or the carry.
			{
				double UpN = 0.0;
				double OverN = 0.0;
				double DownN = 0.0;
				int32 Carried = 0;
				for (const FSandboxAuthorityEntry& Entry : Controller->GetAuthority().Entries)
				{
					UpN += FMath::Max(Entry.ForceN.Z, 0.0);
					OverN += Entry.ForceN.Size2D();
					DownN += FMath::Max(-Entry.ForceN.Z, 0.0);
					Carried += Entry.Source == FSandboxAuthorityEntry::ESource::Carry ? 1 : 0;
				}
				const double WeightN = FMath::Max(Controller->GetWeightN(), 1.0);
				const FSandboxGetUpView& GetUps = Controller->GetGetUpView();
				CornerLabel(FString::Printf(TEXT("Authority: up %.2f, over the floor %.2f, down %.2f of body weight; %d bodies carried%s"),
					UpN / WeightN, OverN / WeightN, DownN / WeightN, Carried,
					GetUps.NotLedAttempts > 0 ? *FString::Printf(TEXT("; GET-UP NOT LED: %s"), *GetUps.NotLed) : TEXT("")),
					GetUps.NotLedAttempts > 0 ? ColorBad : FLinearColor::White);
			}
			// What the controller makes of its balance: the margin it decides on, a step needed, the strategies.
			const FVector2D Ankle = Controller->GetAnkleLeanDeg();
			const FVector2D Hip = Controller->GetHipBendDeg();
			CornerLabel(FString::Printf(TEXT("Balance: decided on %.1f cm %s%s; ankles %.1f / %.1f deg, hips %.1f deg; held push %.0f N, lean %.1f cm"),
				FMath::Abs(Controller->GetDecisionMarginCm()), Controller->GetDecisionMarginCm() >= 0.0 ? TEXT("inside") : TEXT("outside"),
				Controller->IsStepNeeded() ? TEXT(", NEEDS A STEP") : TEXT(""),
				Ankle.X, Ankle.Y, Hip.X, Controller->GetPressureN().Size(), Controller->GetLeanCm().Size()),
				Controller->IsStepNeeded() ? ColorWeak : FLinearColor::White);
		}
		else
		{
			CornerLabel(TEXT("State: none (a passive body)"), ColorSlack);
		}

		if (Polygon.Num() >= 3)
		{
			CornerLabel(FString::Printf(TEXT("Centre of mass: %.0f cm above the soles, %.1f cm %s the support polygon (%d feet, %.0f and %.0f N)"),
				Com.Z - FloorCm, FMath::Abs(MarginCm), MarginCm > 0.0 ? TEXT("inside") : TEXT("OUTSIDE"), StandingFeet, Balance.FootForceN[0], Balance.FootForceN[1]), MarginCm > 0.0 ? ColorGood : ColorBad);
			CornerLabel(FString::Printf(TEXT("Capture point: %.1f cm %s the support polygon, %.1f cm ahead of the centre of mass (%.2f m/s)"),
				FMath::Abs(Balance.CaptureMarginCm), Balance.CaptureMarginCm > 0.0 ? TEXT("inside") : TEXT("OUTSIDE"),
				FVector2D::Distance(Balance.CapturePoint, FVector2D(Com.X, Com.Y)), Balance.Velocity.Size2D() * 0.01), Balance.CaptureMarginCm > 0.0 ? ColorGood : ColorBad);
		}
		else
		{
			CornerLabel(FString::Printf(TEXT("Centre of mass: %.0f cm above the lowest body; no foot stands"), Com.Z - FloorCm), ColorSlack);
		}

		FName FastestJoint;
		const double JointSpeed = FSandboxBodyProbe::MaxJointAngularSpeedRadS(Mesh, &FastestJoint);
		FString Joints = FString::Printf(TEXT("Fastest joint: %s %.1f rad/s"), *FastestJoint.ToString(), JointSpeed);
		if (Layers & SandboxOverlay::Joints)
		{
			Joints += FString::Printf(TEXT("; muscle furthest from its target: %s %.1f deg"), *WorstMuscle.ToString(), WorstErrorDeg);
		}
		CornerLabel(Joints, JointSpeed > 30.0 ? ColorBad : FLinearColor::White);
	}
}

void USandboxOverlaySubsystem::DrawHands(const ASandboxEffectorRig& Rig)
{
	for (USandboxEffectorComponent* Effector : Rig.GetEffectors())
	{
		if (!Effector)
		{
			continue;
		}
		Watch(Effector);
		// The body that is the hand now: the ball, or the open hand's plate.
		const FTransform Hand = Effector->GetHandTransform();
		const FVector At = Hand.GetLocation();
		const FVector Target = Effector->GetTarget().Location;
		const double CapN = Effector->GetTuning().MaxForceN;
		const double Share = CapN > 0.0 ? Effector->GetDemandN() / CapN : 0.0;
		const FLinearColor Color = Share >= 1.0 ? ColorBad : Share >= 0.5 ? ColorWarn : ColorGood;

		// Where the hand should be, and the spring between that and where it is.
		Ring(Target, Effector->GetTuning().RadiusCm, Color, ThinCm);
		Cross(Target, 2.0, Color, ThinCm);
		Line(At, Target, Color, ThickCm);
		if (!Effector->IsFist())
		{
			// The open hand's plate, as its edges: where it is and which way its palm faces.
			const FSandboxEffectorTuning& Shape = Effector->GetTuning();
			const FVector Half(Shape.PalmLengthCm * 0.5, Shape.PalmThicknessCm * 0.5, Shape.PalmWidthCm * 0.5);
			const FVector Middle(Shape.PalmForwardCm, 0.0, 0.0);
			const auto Corner = [&Hand, &Half, &Middle](int32 Index)
			{
				return Hand.TransformPositionNoScale(Middle + FVector((Index & 1) ? Half.X : -Half.X, (Index & 2) ? Half.Y : -Half.Y, (Index & 4) ? Half.Z : -Half.Z));
			};
			for (int32 Index = 0; Index < 8; ++Index)
			{
				for (const int32 Bit : { 1, 2, 4 })
				{
					if (!(Index & Bit))
					{
						Line(Corner(Index), Corner(Index | Bit), Color, ThinCm);
					}
				}
			}
		}
		if (Layers & SandboxOverlay::State)
		{
			const FString Holds = Effector->IsGrabbing() ? FString::Printf(TEXT(", holds %s"), Effector->GetGrabbedBone().IsNone() ? TEXT("a body") : *Effector->GetGrabbedBone().ToString()) : FString();
			CornerLabel(FString::Printf(TEXT("%s (%s%s): %.1f cm from its target, spring asks %.0f N of %.0f, presses with %.0f N, %.1f m/s"),
				*Effector->GetId().ToString(), Effector->IsFist() ? TEXT("fist") : Effector->IsPalmActive() ? TEXT("open, a plate") : TEXT("open, shown only"), *Holds,
				Effector->GetGapCm(), Effector->GetDemandN(), CapN, Effector->GetContactForceN(), Effector->GetHandVelocity().Size() * 0.01), Color);
		}
	}
}

void USandboxOverlaySubsystem::DrawDragHandle()
{
	const APlayerController* PlayerController = GetWorldRef().GetFirstPlayerController();
	const ASandboxPawn* Pawn = PlayerController ? Cast<ASandboxPawn>(PlayerController->GetPawn()) : nullptr;
	if (!Pawn || !Pawn->GetDragHandle().IsHolding())
	{
		return;
	}
	const FSandboxDragHandle& Handle = Pawn->GetDragHandle();
	Cross(Handle.GetTargetLocation(), 3.0, ColorHandle, ThinCm);
	Cross(Handle.GetGripLocation(), 2.0, ColorHandle, ThinCm);
	Line(Handle.GetGripLocation(), Handle.GetTargetLocation(), ColorHandle, ThickCm);
	if (Layers & SandboxOverlay::State)
	{
		CornerLabel(FString::Printf(TEXT("Drag handle on %s: %.1f cm from its target, spring asks %.0f N of %.0f"),
			*Handle.GetBone().ToString(), Handle.GetGapCm(), Handle.GetDemandN(), Handle.GetMaxForceN()), ColorHandle);
	}
}

void USandboxOverlaySubsystem::DrawContacts()
{
	if (StepDtS <= 0.0)
	{
		return;
	}
	// A pair of bodies reports once per substep. Per body and kind of contact the reports of a frame are
	// added up, which gives the net force of that frame.
	struct FSum
	{
		FName Bone;
		bool bStatic = false;
		FVector ImpulseNs = FVector::ZeroVector;
		FVector WeightedLocation = FVector::ZeroVector;
		double Weight = 0.0;
	};
	TArray<FSum, TInlineAllocator<16>> Sums;
	for (const ASandboxOverlayActor::FContact& Contact : ShownContacts)
	{
		FSum* Sum = Sums.FindByPredicate([&Contact](const FSum& Candidate) { return Candidate.Bone == Contact.Bone && Candidate.bStatic == Contact.bStatic; });
		if (!Sum)
		{
			Sum = &Sums.AddDefaulted_GetRef();
			Sum->Bone = Contact.Bone;
			Sum->bStatic = Contact.bStatic;
		}
		Sum->ImpulseNs += Contact.Normal * Contact.ImpulseNs;
		Sum->WeightedLocation += Contact.Location * Contact.ImpulseNs;
		Sum->Weight += Contact.ImpulseNs;
	}

	for (const FSum& Sum : Sums)
	{
		const double ForceN = Sum.ImpulseNs.Size() / StepDtS;
		if (ForceN < MinContactN || Sum.Weight <= 0.0)
		{
			continue;
		}
		const FVector At = Sum.WeightedLocation / Sum.Weight;
		const FVector Tip = At + Sum.ImpulseNs.GetSafeNormal() * FMath::Clamp(ForceN * ArrowCmPerN, ArrowMinCm, ArrowMaxCm);
		const FLinearColor Color = Sum.bStatic ? ColorStaticContact : ColorHit;
		Arrow(At, Tip, Color, Sum.bStatic ? ThinCm : ThickCm);
		if (!Sum.bStatic)
		{
			WorldLabel(Tip, FString::Printf(TEXT("%s %.0f N"), *Sum.Bone.ToString(), ForceN), Color);
		}
	}
}
