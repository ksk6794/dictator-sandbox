#include "SandboxEffectorComponent.h"

#include "CollisionShape.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "PhysicsEngine/BodyInstance.h"
#include "PhysicsEngine/BodySetup.h"
#include "PhysicsEngine/PhysicsSettings.h"

namespace
{
	// The engine's basic sphere, 100 cm across. Only looked at: the body's shape is the sphere component's own.
	const TCHAR* const SphereMeshPath = TEXT("/Engine/BasicShapes/Sphere.Sphere");
	// The engine's cube is 100 cm along each edge.
	const TCHAR* const CubeMeshPath = TEXT("/Engine/BasicShapes/Cube.Cube");
	constexpr double CubeMeshEdgeCm = 100.0;
	constexpr double SphereMeshRadiusCm = 50.0;
}

FName SandboxEffectors::KindOf(FName Effector)
{
	FString Kind, Side;
	return Effector.ToString().Split(TEXT("_"), &Kind, &Side) ? FName(*Kind) : Effector;
}

bool SandboxEffectors::IsKnown(FName Effector)
{
	return Effector == HandLeft || Effector == HandRight || Effector == FootLeft || Effector == FootRight;
}

USandboxPalmComponent::USandboxPalmComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetMobility(EComponentMobility::Movable);
	SetCollisionProfileName(TEXT("PhysicsActor"));
	SetGenerateOverlapEvents(false);
	SetEnableGravity(false);
	BodyInstance.bUseCCD = true;
	BodyInstance.bNotifyRigidBodyCollision = true;
	BodyInstance.LinearDamping = 0.0f;
	BodyInstance.AngularDamping = 0.0f;
}

void USandboxPalmComponent::OnDestroyPhysicsState()
{
	if (USandboxEffectorComponent* Owner = Hand.Get())
	{
		Owner->ReleasePalmSpring();
	}
	Super::OnDestroyPhysicsState();
}

USandboxEffectorComponent::USandboxEffectorComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetMobility(EComponentMobility::Movable);
	SetCollisionProfileName(TEXT("PhysicsActor"));
	SetGenerateOverlapEvents(false);
	SetEnableGravity(false);
	BodyInstance.bUseCCD = true;
	BodyInstance.bNotifyRigidBodyCollision = true;
	// Nothing but the spring and contacts may slow the body: the engine's default damping is a force too.
	BodyInstance.LinearDamping = 0.0f;
	BodyInstance.AngularDamping = 0.0f;
}

void USandboxEffectorComponent::OnDestroyPhysicsState()
{
	// Before the body goes: the spring is a constraint on it. (Unregistering destroys the physics state
	// first and calls OnUnregister afterwards, which would be too late.) The plate's spring goes with it:
	// whichever of the two components is taken out of the world first, no spring is left on a body that is
	// gone. (The plate lets its own go too, USandboxPalmComponent::OnDestroyPhysicsState.)
	// And a grab: a joint on a body that is gone holds nothing.
	Grab.Detach();
	GrabForceN = 0.0;
	Spring.Detach();
	PalmSpring.Detach();
	Super::OnDestroyPhysicsState();
}

void USandboxEffectorComponent::ReleasePalmSpring()
{
	if (PalmBody && Grab.GetHand() == PalmBody->GetBodyInstance())
	{
		Grab.Detach();
		GrabForceN = 0.0;
	}
	PalmSpring.Detach();
}

void USandboxEffectorComponent::OnComponentDestroyed(bool bDestroyingHierarchy)
{
	// What rides on the hand belongs to the actor, not to this component, and would stay in the world where
	// the hand last was: a ball and a plate hanging in the air after the hands were made anew for a take.
	Grab.Detach();
	if (PalmBody)
	{
		PalmSpring.Detach();
		PalmBody->DestroyComponent();
		PalmBody = nullptr;
	}
	if (Visual)
	{
		Visual->DestroyComponent();
		Visual = nullptr;
	}
	if (PalmVisual)
	{
		PalmVisual->DestroyComponent();
		PalmVisual = nullptr;
	}
	Super::OnComponentDestroyed(bDestroyingHierarchy);
}

bool USandboxEffectorComponent::Start(FName InId, const FSandboxEffectorTuning& InTuning, const FTransform& Pose, FString& OutError)
{
	if (bStarted)
	{
		OutError = FString::Printf(TEXT("Effector '%s' is already started"), *Id.ToString());
		return false;
	}
	UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, SphereMeshPath);
	if (!Sphere)
	{
		OutError = FString::Printf(TEXT("Cannot load the effector mesh '%s'"), SphereMeshPath);
		return false;
	}

	Id = InId;
	Tuning = InTuning;
	SetSphereRadius(static_cast<float>(Tuning.RadiusCm), /*bUpdateOverlaps*/ false);
	SetWorldTransform(FTransform(Pose.GetRotation(), Pose.GetLocation()));
	SetSimulatePhysics(true);

	Visual = NewObject<UStaticMeshComponent>(GetOwner());
	Visual->SetupAttachment(this);
	Visual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Visual->SetGenerateOverlapEvents(false);
	Visual->SetStaticMesh(Sphere);
	Visual->SetRelativeScale3D(FVector(Tuning.RadiusCm / SphereMeshRadiusCm));
	Visual->RegisterComponent();

	// The open hand, for the eye. Without the engine's cube there is none, and nothing else changes.
	// (An effector whose tuning has no open hand, a foot, has no plate.)
	UStaticMesh* Cube = Tuning.PalmLengthCm > 0.0 ? LoadObject<UStaticMesh>(nullptr, CubeMeshPath) : nullptr;
	if (Cube)
	{
		PalmVisual = NewObject<UStaticMeshComponent>(GetOwner());
		PalmVisual->SetupAttachment(this);
		PalmVisual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		PalmVisual->SetGenerateOverlapEvents(false);
		PalmVisual->SetStaticMesh(Cube);
		PalmVisual->SetRelativeLocation(PalmOffset());
		PalmVisual->SetRelativeScale3D(FVector(Tuning.PalmLengthCm, Tuning.PalmThicknessCm, Tuning.PalmWidthCm) / CubeMeshEdgeCm);
		PalmVisual->SetVisibility(false);
		PalmVisual->RegisterComponent();
	}

	FBodyInstance* Body = GetBodyInstance();
	if (!Body || !Body->IsValidBodyInstance() || !Body->IsInstanceSimulatingPhysics())
	{
		OutError = FString::Printf(TEXT("Effector '%s' got no simulating physics body (registered %d, physics state %d, body valid %d, collision %d)"),
			*Id.ToString(), IsRegistered(), IsPhysicsStateCreated(), Body && Body->IsValidBodyInstance(), static_cast<int32>(GetCollisionEnabled()));
		return false;
	}

	OnComponentHit.AddUniqueDynamic(this, &USandboxEffectorComponent::OnEffectorHit);
	Target.Location = Pose.GetLocation();
	Target.Rotation = Pose.GetRotation();
	Spring.Attach(Body, FTransform::Identity, this, /*bAngular*/ true);
	bStarted = true;
	ApplyTuning(Tuning);
	Spring.SetTarget(Target.Location, Target.Rotation, FVector::ZeroVector);
	return true;
}

void USandboxEffectorComponent::ApplyTuning(const FSandboxEffectorTuning& InTuning)
{
	// The radius and the plate are the sizes of bodies that already exist.
	const FSandboxEffectorTuning Made = Tuning;
	Tuning = InTuning;
	Tuning.RadiusCm = Made.RadiusCm;
	Tuning.PalmLengthCm = Made.PalmLengthCm;
	Tuning.PalmWidthCm = Made.PalmWidthCm;
	Tuning.PalmThicknessCm = Made.PalmThicknessCm;
	Tuning.PalmForwardCm = Made.PalmForwardCm;
	if (!bStarted)
	{
		return;
	}
	SetMassOverrideInKg(NAME_None, static_cast<float>(Tuning.MassKg), /*bOverrideMass*/ true);
	// (The share is 1 for every effector but a foot at rest, and a product with 1 is the number itself.)
	Spring.SetLinear(StiffnessNow(), Tuning.DampingS, CapNow());
	Spring.SetAngular(Tuning.AngularStiffnessNmPerRad, Tuning.AngularDampingS, Tuning.MaxTorqueNm);
	if (PalmBody)
	{
		PalmBody->SetMassOverrideInKg(NAME_None, static_cast<float>(Tuning.PalmMassKg), /*bOverrideMass*/ true);
		PalmSpring.SetLinear(StiffnessNow(), Tuning.DampingS, CapNow());
		PalmSpring.SetAngular(Tuning.AngularStiffnessNmPerRad, Tuning.AngularDampingS, Tuning.MaxTorqueNm);
	}
}

void USandboxEffectorComponent::SetForceShare(double Share)
{
	// Never nothing: a cap of zero is "no cap" to the solver, which would be the strongest hand there is.
	const double Wanted = FMath::Clamp(Share, 0.01, 1.0);
	if (Wanted == ForceShare)
	{
		return;
	}
	ForceShare = Wanted;
	if (bStarted)
	{
		// The cap of the spring that holds the hand now; the other one gets it when it takes over (SwitchBody
		// and ApplyTuning set the whole cap, and an effector that changes its body is never one that rests weak).
		ActiveSpring().SetLinear(StiffnessNow(), Tuning.DampingS, CapNow());
	}
}

double USandboxEffectorComponent::StiffnessNow() const
{
	// A cap alone does little for a blow: the spring asks for its stiffness times the gap, and a target 15 cm
	// inside a body is 450 N at the hand's 3000 N/m. A blow has a stiffness of its own.
	return bStrikeOn && Tuning.StrikeForceN > Tuning.MaxForceN && Tuning.StrikeStiffnessNPerM > Tuning.StiffnessNPerM
		? Tuning.StiffnessNPerM + StrikeShare * (Tuning.StrikeStiffnessNPerM - Tuning.StiffnessNPerM)
		: Tuning.StiffnessNPerM;
}

double USandboxEffectorComponent::CapNow() const
{
	// The spring's cap as things are: the hand's own, or a blow's while one lasts, and of that the share a
	// source gives the effector (1 for all but a resting foot).
	return (bStrikeOn && Tuning.StrikeForceN > Tuning.MaxForceN ? Tuning.MaxForceN + StrikeShare * (Tuning.StrikeForceN - Tuning.MaxForceN) : Tuning.MaxForceN) * ForceShare;
}

FBodyInstance* USandboxEffectorComponent::ActiveBody() const
{
	return bPalmActive && PalmBody ? PalmBody->GetBodyInstance() : GetBodyInstance();
}

FTransform USandboxEffectorComponent::GetHandTransform() const
{
	const FBodyInstance* Body = ActiveBody();
	if (!Body || !Body->IsValidBodyInstance())
	{
		return FTransform::Identity;
	}
	const FTransform BodyPose = Body->GetUnrealWorldTransform();
	// The ball's pose is the hand's. The plate is held at a point behind its middle.
	return bPalmActive ? FTransform(BodyPose.GetRotation(), BodyPose.TransformPositionNoScale(-PalmOffset())) : BodyPose;
}

FVector USandboxEffectorComponent::GetHandVelocity() const
{
	const FBodyInstance* Body = ActiveBody();
	if (!Body || !Body->IsValidBodyInstance())
	{
		return FVector::ZeroVector;
	}
	if (!bPalmActive)
	{
		return Body->GetUnrealWorldVelocity();
	}
	const FVector Arm = Body->GetUnrealWorldTransform().GetRotation().RotateVector(-PalmOffset());
	return Body->GetUnrealWorldVelocity() + FVector::CrossProduct(Body->GetUnrealWorldAngularVelocityInRadians(), Arm);
}

double USandboxEffectorComponent::GetHandMassKg() const
{
	const FBodyInstance* Body = ActiveBody();
	return Body && Body->IsValidBodyInstance() ? Body->GetBodyMass() : 0.0;
}

void USandboxEffectorComponent::GetProbe(FCollisionShape& OutShape, FVector& OutCenter, FQuat& OutRotation, double& OutThinHalfCm) const
{
	const FBodyInstance* Body = ActiveBody();
	const FTransform BodyPose = Body && Body->IsValidBodyInstance() ? Body->GetUnrealWorldTransform() : FTransform::Identity;
	OutCenter = BodyPose.GetLocation();
	if (bPalmActive)
	{
		const FVector HalfExtent(Tuning.PalmLengthCm * 0.5, Tuning.PalmThicknessCm * 0.5, Tuning.PalmWidthCm * 0.5);
		OutShape = FCollisionShape::MakeBox(HalfExtent);
		OutRotation = BodyPose.GetRotation();
		OutThinHalfCm = HalfExtent.GetMin();
	}
	else
	{
		// A ball looks the same however it is turned.
		OutShape = FCollisionShape::MakeSphere(GetScaledSphereRadius());
		OutRotation = FQuat::Identity;
		OutThinHalfCm = GetScaledSphereRadius();
	}
}

void USandboxEffectorComponent::SetPassesBodies(bool bPass)
{
	bPassesBodies = bPass;
	const ECollisionResponse Response = bPass ? ECR_Ignore : ECR_Block;
	SetCollisionResponseToChannel(ECC_PhysicsBody, Response);
	if (PalmBody)
	{
		PalmBody->SetCollisionResponseToChannel(ECC_PhysicsBody, Response);
	}
}

double USandboxEffectorComponent::DepthInBodiesCm() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return 0.0;
	}
	FCollisionShape Shape;
	FVector Center;
	FQuat Rotation;
	double ThinHalfCm = 0.0;
	GetProbe(Shape, Center, Rotation, ThinHalfCm);
	// The kind of body a holding hand passes, the other hand among them; only this hand's own two are left out.
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(ECC_PhysicsBody);
	FCollisionQueryParams NotThisHand(SCENE_QUERY_STAT(SandboxHandDepth), /*bTraceComplex*/ false);
	NotThisHand.AddIgnoredComponent(static_cast<const UPrimitiveComponent*>(this));
	if (PalmBody)
	{
		NotThisHand.AddIgnoredComponent(static_cast<const UPrimitiveComponent*>(PalmBody.Get()));
	}
	TArray<FOverlapResult> Overlaps;
	World->OverlapMultiByObjectType(Overlaps, Center, Rotation, Objects, Shape, NotThisHand);

	double DepthCm = 0.0;
	TArray<const UPrimitiveComponent*, TInlineAllocator<4>> Seen;
	for (const FOverlapResult& Overlap : Overlaps)
	{
		UPrimitiveComponent* Component = Overlap.GetComponent();
		if (!Component || Seen.Contains(Component))
		{
			continue;
		}
		Seen.Add(Component);
		FMTDResult Separation;
		if (const USkeletalMeshComponent* Skeletal = Cast<USkeletalMeshComponent>(Component))
		{
			// A skeletal mesh is asked body by body, as in FindGrabbable.
			for (const FBodyInstance* Part : Skeletal->Bodies)
			{
				if (Part && Part->IsValidBodyInstance() && Part->OverlapTest(Center, Rotation, Shape, &Separation))
				{
					DepthCm = FMath::Max(DepthCm, static_cast<double>(Separation.Distance));
				}
			}
		}
		else if (Component->ComputePenetration(Separation, Shape, Center, Rotation))
		{
			DepthCm = FMath::Max(DepthCm, static_cast<double>(Separation.Distance));
		}
	}
	return DepthCm;
}

bool USandboxEffectorComponent::WouldOverlap(bool bAsPalm) const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}
	const FTransform HandPose = GetHandTransform();
	const FVector HalfExtent(Tuning.PalmLengthCm * 0.5, Tuning.PalmThicknessCm * 0.5, Tuning.PalmWidthCm * 0.5);
	const FCollisionShape Shape = bAsPalm ? FCollisionShape::MakeBox(HalfExtent) : FCollisionShape::MakeSphere(GetUnscaledSphereRadius());
	const FVector Center = bAsPalm ? HandPose.TransformPositionNoScale(PalmOffset()) : HandPose.GetLocation();
	// By object type: a character's bodies do not answer the visibility channel. The hands themselves are left out.
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(ECC_WorldStatic);
	Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
	Objects.AddObjectTypesToQuery(ECC_PhysicsBody);
	FCollisionQueryParams NotTheHands(SCENE_QUERY_STAT(SandboxHandRoom), /*bTraceComplex*/ false);
	NotTheHands.AddIgnoredActor(GetOwner());
	return World->OverlapAnyTestByObjectType(Center, bAsPalm ? HandPose.GetRotation() : FQuat::Identity, Objects, Shape, NotTheHands);
}

bool USandboxEffectorComponent::MakePalmBody()
{
	if (PalmBody)
	{
		return true;
	}
	PalmBody = NewObject<USandboxPalmComponent>(GetOwner());
	PalmBody->Hand = this;
	PalmBody->SetBoxExtent(FVector(Tuning.PalmLengthCm * 0.5, Tuning.PalmThicknessCm * 0.5, Tuning.PalmWidthCm * 0.5), /*bUpdateOverlaps*/ false);
	PalmBody->SetWorldTransform(GetHandTransform());
	PalmBody->RegisterComponent();
	PalmBody->OnComponentHit.AddUniqueDynamic(this, &USandboxEffectorComponent::OnEffectorHit);
	if (bPassesBodies)
	{
		PalmBody->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Ignore);
	}
	return PalmBody->IsRegistered();
}

void USandboxEffectorComponent::SwitchBody(bool bToPalm)
{
	if (bToPalm == bPalmActive || (bToPalm && !MakePalmBody()))
	{
		return;
	}
	// The hand as it is now: where, how turned, and how it moves.
	const FBodyInstance* From = ActiveBody();
	const FTransform HandPose = GetHandTransform();
	const FVector Linear = GetHandVelocity();
	const FVector Angular = From && From->IsValidBodyInstance() ? From->GetUnrealWorldAngularVelocityInRadians() : FVector::ZeroVector;
	const FVector Arm = HandPose.GetRotation().RotateVector(PalmOffset());

	if (bToPalm)
	{
		// The ball steps aside: no spring, no collision, not simulated. The plate takes its pose and motion.
		Spring.Detach();
		SetSimulatePhysics(false);
		SetCollisionEnabled(ECollisionEnabled::NoCollision);

		PalmBody->SetWorldLocationAndRotation(HandPose.GetLocation() + Arm, HandPose.GetRotation(), /*bSweep*/ false, nullptr, ETeleportType::ResetPhysics);
		PalmBody->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		PalmBody->SetSimulatePhysics(true);
		PalmBody->SetMassOverrideInKg(NAME_None, static_cast<float>(Tuning.PalmMassKg), /*bOverrideMass*/ true);
		if (FBodyInstance* Plate = PalmBody->GetBodyInstance())
		{
			Plate->SetLinearVelocity(Linear + FVector::CrossProduct(Angular, Arm), /*bAddToCurrent*/ false);
			Plate->SetAngularVelocityInRadians(Angular, /*bAddToCurrent*/ false);
			PalmSpring.Attach(Plate, FTransform(-PalmOffset()), this, /*bAngular*/ true);
			PalmSpring.SetLinear(StiffnessNow(), Tuning.DampingS, CapNow());
			PalmSpring.SetAngular(Tuning.AngularStiffnessNmPerRad, Tuning.AngularDampingS, Tuning.MaxTorqueNm);
		}
	}
	else
	{
		PalmSpring.Detach();
		PalmBody->SetSimulatePhysics(false);
		PalmBody->SetCollisionEnabled(ECollisionEnabled::NoCollision);

		SetWorldLocationAndRotation(HandPose.GetLocation(), HandPose.GetRotation(), /*bSweep*/ false, nullptr, ETeleportType::ResetPhysics);
		SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		SetSimulatePhysics(true);
		SetMassOverrideInKg(NAME_None, static_cast<float>(Tuning.MassKg), /*bOverrideMass*/ true);
		if (FBodyInstance* Ball = GetBodyInstance())
		{
			Ball->SetLinearVelocity(Linear, /*bAddToCurrent*/ false);
			Ball->SetAngularVelocityInRadians(Angular, /*bAddToCurrent*/ false);
			Spring.Attach(Ball, FTransform::Identity, this, /*bAngular*/ true);
			Spring.SetLinear(StiffnessNow(), Tuning.DampingS, CapNow());
			Spring.SetAngular(Tuning.AngularStiffnessNmPerRad, Tuning.AngularDampingS, Tuning.MaxTorqueNm);
		}
	}
	bPalmActive = bToPalm;
	++Stats.ShapeChanges;

	// What is seen rides on the body that is the hand (which of the two is shown is settled in Update).
	if (PalmVisual)
	{
		PalmVisual->AttachToComponent(bPalmActive ? static_cast<USceneComponent*>(PalmBody) : this, FAttachmentTransformRules::KeepRelativeTransform);
		PalmVisual->SetRelativeLocation(bPalmActive ? FVector::ZeroVector : PalmOffset());
	}
}

void USandboxEffectorComponent::Update(float DeltaTime)
{
	FBodyInstance* Body = bStarted ? ActiveBody() : nullptr;
	if (!Body || !Body->IsValidBodyInstance())
	{
		return;
	}
	// How strongly the target is to be followed from this step on (1 for all but a resting foot).
	SetForceShare(Target.Strength);
	// A blow. A hand that came fast and has met something has, for a moment, the arm's push behind it: a
	// higher force cap (effectors.hand.strike_force_n for strike_s). A hand that pushes, holds or drags has
	// the cap it always had. Without it a punch was as hard as the time it stayed in the body, at 300 N.
	if (Grab.IsAttached())
	{
		// A hand that holds something strikes with nothing, and a blow that was landing as it took hold does
		// not come back when it lets go.
		StrikeLeftS = 0.0;
	}
	if (StrikeLeftS > 0.0)
	{
		if (!bStrikeOn)
		{
			bStrikeOn = true;
			++Stats.Strikes;
			ActiveSpring().SetLinear(StiffnessNow(), Tuning.DampingS, CapNow());
		}
		StrikeLeftS -= DeltaTime;
	}
	else if (bStrikeOn)
	{
		bStrikeOn = false;
		StrikeLeftS = 0.0;
		ActiveSpring().SetLinear(StiffnessNow(), Tuning.DampingS, CapNow());
	}
	// The contacts reported since the last Update belong to the step that has just ended.
	Stats.LastFrameImpulseNs = ImpulseFrame == Frame ? FrameImpulseNs.Size() : 0.0;
	++Frame;
	FrameDt = DeltaTime;

	// A hand left far behind (the camera went through a wall) could only come back through the wall. It is
	// put on its target instead, at rest. That is not physics, so it is counted.
	if (FVector::Dist(GetHandTransform().GetLocation(), Target.Location) > Tuning.SnapDistanceCm)
	{
		if (Grab.IsAttached())
		{
			// What the hand holds is not carried along.
			Grab.Detach();
			GrabForceN = 0.0;
			bGripCounted = true;
			++Stats.GrabSlips;
		}
		if (bPalmActive)
		{
			PalmBody->SetWorldLocationAndRotation(Target.Location + Target.Rotation.RotateVector(PalmOffset()), Target.Rotation, /*bSweep*/ false, nullptr, ETeleportType::ResetPhysics);
		}
		else
		{
			SetWorldLocationAndRotation(Target.Location, Target.Rotation, /*bSweep*/ false, nullptr, ETeleportType::ResetPhysics);
		}
		Body->SetLinearVelocity(FVector::ZeroVector, /*bAddToCurrent*/ false);
		Body->SetAngularVelocityInRadians(FVector::ZeroVector, /*bAddToCurrent*/ false);
		++Stats.Snaps;
	}

	// Fist or open hand, by the grip.
	const bool bWasFist = bFist;
	if (Target.Grip > Tuning.FistGrip + Tuning.FistGripSlack)
	{
		bFist = true;
	}
	else if (Target.Grip < Tuning.FistGrip - Tuning.FistGripSlack)
	{
		bFist = false;
	}
	if (Grab.IsAttached() || (Tuning.Grab > 0.5 && bFist && !bWasFist))
	{
		UpdateGrab(DeltaTime, bFist && !bWasFist);
	}
	// A hand that held something collides with bodies again once it has let go and lies in none, or no deeper
	// in one than it rests on what it presses (effectors.hand.grab_clear_cm): put back while it is deep in
	// one, it would be thrown out of it; left passing while it only touches, it would sink into what it has
	// just let go of, toward a target that is a little way inside.
	if (bPassesBodies && !Grab.IsAttached() && DepthInBodiesCm() <= Tuning.GrabClearCm)
	{
		SetPassesBodies(false);
	}
	// And, where the open hand is a body, the body that goes with it. The two have other shapes: a ball put
	// where a plate lies flat on a chest would be 3 cm inside the chest and be thrown out of it. So the hand
	// changes its body only where the other one has room, and waits until then. A grip that closes on something
	// is counted: that hand is not making a fist.
	// A hand that holds something stays the body it took hold with.
	const bool bWantPalm = Grab.IsAttached() ? bPalmActive : !bFist && Tuning.PalmCollides > 0.5;
	if (bWantPalm == bPalmActive)
	{
		bGripCounted = false;
	}
	else if (!WouldOverlap(bWantPalm))
	{
		SwitchBody(bWantPalm);
		bGripCounted = false;
		Body = ActiveBody();
		if (!Body || !Body->IsValidBodyInstance())
		{
			return;
		}
	}
	else if (!bWantPalm && !bGripCounted)
	{
		bGripCounted = true;
		++Stats.GripsOnBody;
		Stats.LastGripped = LastTouchedFrame >= Frame - 3 ? LastTouched : NAME_None;
	}
	if (bPalmActive)
	{
		// The ball is not the hand now; it is kept where the hand is for whoever looks at the component.
		const FTransform HandPose = GetHandTransform();
		SetWorldLocationAndRotation(HandPose.GetLocation(), HandPose.GetRotation(), /*bSweep*/ false, nullptr, ETeleportType::TeleportPhysics);
	}

	// What is seen: the plate while the hand is open, and also while the plate is the body though the grip has
	// closed (a grip closed on something: the hand is still lying there as a plate); the ball otherwise.
	const bool bShowPlate = !bFist || bPalmActive;
	if (PalmVisual && PalmVisual->IsVisible() != bShowPlate)
	{
		PalmVisual->SetVisibility(bShowPlate);
	}
	if (Visual && Visual->IsVisible() == bPalmActive)
	{
		Visual->SetVisibility(!bPalmActive);
	}

	// A blow is carried through. A player's own hand stops where it sees the body's surface and is on its way
	// back a frame later, and a fist that follows it delivers its own 1.5 kg and little else (8 to 34 N*s in the
	// owner's session in the headset, where a scripted punch that goes 15 cm into the body brings 77 to 113).
	// While a blow lasts the fist is asked to be at least so far beyond where it landed, the way it came: as
	// much of effectors.hand.strike_through_cm as the blow has of its strength. A target that is further on
	// than that (a hand that pushes on) is followed as it is; one that is short of it, or on its way back, is
	// moved up to it along the blow's way and loses its speed backward (which would brake the fist through
	// the spring's damping). The body is in the way: what is seen is a fist that presses on for a moment and
	// then goes back to the hand.
	if (bStrikeOn)
	{
		const double AlongCm = FVector::DotProduct(Target.Location - StrikePoint, StrikeDirection);
		const double ShortCm = FMath::Max(Tuning.StrikeThroughCm * StrikeShare - AlongCm, 0.0);
		const double BackCmS = ShortCm > 0.0 ? FMath::Min(FVector::DotProduct(Target.Velocity, StrikeDirection), 0.0) : 0.0;
		ActiveSpring().SetTarget(Target.Location + StrikeDirection * ShortCm, Target.Rotation, Target.Velocity - StrikeDirection * BackCmS);
	}
	else
	{
		ActiveSpring().SetTarget(Target.Location, Target.Rotation, Target.Velocity);
	}
	Stats.MaxGapCm = FMath::Max(Stats.MaxGapCm, ActiveSpring().GetGapCm());
	Stats.MaxDemandN = FMath::Max(Stats.MaxDemandN, ActiveSpring().GetDemandN());
	StepStartVelocityCmS = bPalmActive ? GetHandVelocity() : Body->GetUnrealWorldVelocity();
	StepStartSpeedCmS = StepStartVelocityCmS.Size();
	Stats.MaxSpeedCmS = FMath::Max(Stats.MaxSpeedCmS, StepStartSpeedCmS);
}

void USandboxEffectorComponent::UpdateGrab(float DeltaTime, bool bGripClosed)
{
	GrabForceN = 0.0;
	if (Grab.IsAttached())
	{
		// Let go: the grip opened, what was held is gone, or the hand has been too far behind its target for
		// too long. The last is a grip that slips: the hand's spring is at its cap from a few centimetres on, so
		// a hand left further behind pulls no harder, it only is no longer where the player's hand is.
		const double GapCm = FVector::Dist(GetHandTransform().GetLocation(), Target.Location);
		GrabSlipS = Tuning.GrabSlipCm > 0.0 && GapCm > Tuning.GrabSlipCm ? GrabSlipS + DeltaTime : 0.0;
		// And the joint's own load. The hand's spring is capped, so the joint carries more than that cap only
		// when something else holds the hand back: a body that rolls onto the hand, a hand caught at a table's
		// edge. Such a load, kept up, tears the grip open; a jolt of a frame or two does not.
		const UPhysicsSettings* Physics = UPhysicsSettings::Get();
		const int32 Substeps = Physics && Physics->bSubstepping && Physics->MaxSubstepDeltaTime > 0.0f
			? FMath::Clamp(FMath::CeilToInt32(DeltaTime / Physics->MaxSubstepDeltaTime - 1.0e-3f), 1, FMath::Max(Physics->MaxSubsteps, 1)) : 1;
		const double ForceN = Grab.GetForceN(DeltaTime / Substeps);
		GrabOverloadS = Tuning.GrabMaxForceN > 0.0 && ForceN > Tuning.GrabMaxForceN ? GrabOverloadS + DeltaTime : 0.0;
		if (!Grab.IsHolding())
		{
			++Stats.GrabsLost;
			Grab.Detach();
			bGripCounted = true;
		}
		else if (!bFist)
		{
			Grab.Detach();
		}
		else if (GrabSlipS > 0.0 && GrabSlipS >= Tuning.GrabSlipS)
		{
			// The grip is still closed, and where the open hand is a body it now waits for room to be a fist:
			// that wait is not a grip that closed on something, and is not counted as one.
			++Stats.GrabSlips;
			Grab.Detach();
			bGripCounted = true;
		}
		else if (GrabOverloadS > 0.0 && GrabOverloadS >= Tuning.GrabOverloadS)
		{
			++Stats.GrabOverloads;
			Grab.Detach();
			bGripCounted = true;
		}
		else
		{
			++Stats.GrabFrames;
			GrabDrawLeftS = FMath::Max(GrabDrawLeftS - DeltaTime, 0.0);
			if (GrabDrawLeftS <= 0.0)
			{
				Stats.MaxGrabStretchCm = FMath::Max(Stats.MaxGrabStretchCm, Grab.GetStretchCm());
			}
			Stats.MaxGrabForceN = FMath::Max(Stats.MaxGrabForceN, ForceN);
			Stats.GrabForceSumN += ForceN;
			GrabForceN = ForceN;
		}
		return;
	}
	// Take hold: the grip has just closed, and a simulated body is within reach of the hand.
	UPrimitiveComponent* Near = nullptr;
	FName NearBone;
	if (!bGripClosed || !FindGrabbable(Near, NearBone))
	{
		return;
	}
	// Held at the hand's own point, which the held body turns about. And from here on the hand holds a point
	// and is no shape among simulated bodies: jointed to one segment of a trunk and pulled along it, a hand
	// that still collided was caught against the next segment, and its joint carried 556 N of a 300 N hand
	// (measured; the grip was torn open within a second). What it holds does not move against it anyway.
	// A limb of a character is held at that point and turns freely about it. A thing is also held in the
	// direction it was taken in, so that it follows the hand's turning: a stick that only hung from the hand
	// could not be struck with.
	const bool bThing = !Near->IsA<USkeletalMeshComponent>();
	// A thing may name a handle (a component of its actor with the tag "SandboxHandle"): taken within reach of
	// it, the thing is held by the handle, and the joint draws the handle into the hand. The hand does not know
	// what the thing is; it knows that it has a place to be held by.
	const FVector HandPoint = GetHandTransform().GetLocation();
	FVector HandlePoint = FVector::ZeroVector;
	bool bByHandle = false;
	if (bThing && Tuning.GrabHandleCm > 0.0 && Near->GetOwner())
	{
		double BestCm = Tuning.GrabHandleCm;
		for (const UActorComponent* Component : Near->GetOwner()->GetComponents())
		{
			const USceneComponent* Handle = Cast<USceneComponent>(Component);
			// A handle of the body that is being taken, not of another body of the same actor.
			if (Handle && Handle->GetAttachParent() == Near && Handle->ComponentHasTag(TEXT("SandboxHandle")))
			{
				const double AwayCm = FVector::Dist(Handle->GetComponentLocation(), HandPoint);
				if (AwayCm <= BestCm)
				{
					BestCm = AwayCm;
					HandlePoint = Handle->GetComponentLocation();
					bByHandle = true;
				}
			}
		}
	}
	if (Grab.Attach(ActiveBody(), HandPoint, Near, NearBone, this,
		bThing ? Tuning.GrabHoldNmPerRad : 0.0, Tuning.GrabHoldDampingS, Tuning.GrabHoldMaxTorqueNm, bByHandle ? &HandlePoint : nullptr))
	{
		Stats.HandleGrabs += bByHandle ? 1 : 0;
		// While the handle is drawn in, the two held points are apart by design: that is not the joint giving way.
		GrabDrawLeftS = bByHandle ? Tuning.GrabDrawS : 0.0;
		SetPassesBodies(true);
		++Stats.Grabs;
		Stats.LastGrabbed = !NearBone.IsNone() ? NearBone : Near->GetOwner() ? Near->GetOwner()->GetFName() : NAME_None;
		GrabSlipS = 0.0;
		GrabOverloadS = 0.0;
	}
}

bool USandboxEffectorComponent::FindGrabbable(UPrimitiveComponent*& OutComponent, FName& OutBone) const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}
	// Everything the hand's shape, made larger by the reach, lies in. A hand that presses on a body is in
	// reach of it, and so is one that hovers a little off it: a soft part gives way to a touch (a head on its
	// neck leans back from a hand), and a player in a headset closes the grip where the hand looks to be.
	FCollisionShape Shape;
	FVector Center;
	FQuat Rotation;
	double ThinHalfCm = 0.0;
	GetProbe(Shape, Center, Rotation, ThinHalfCm);
	const float ReachCm = static_cast<float>(Tuning.GrabReachCm);
	const FCollisionShape Wider = Shape.IsBox() ? FCollisionShape::MakeBox(Shape.GetExtent() + FVector(ReachCm)) : FCollisionShape::MakeSphere(Shape.GetSphereRadius() + ReachCm);
	// By object type: a character's bodies do not answer the visibility channel. Only the kind of body a
	// holding hand passes (SetPassesBodies): one of another kind would be jointed to a hand that still collides
	// with it. Not the hands' own bodies: two hands that meet do not hold on to each other.
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(ECC_PhysicsBody);
	FCollisionQueryParams NotTheHands(SCENE_QUERY_STAT(SandboxGrabReach), /*bTraceComplex*/ false);
	NotTheHands.AddIgnoredActor(GetOwner());
	TArray<FOverlapResult> Overlaps;
	World->OverlapMultiByObjectType(Overlaps, Center, Rotation, Objects, Wider, NotTheHands);

	// Of those, the simulated body nearest to the hand's own point. A skeletal mesh answers the query as one
	// thing, without saying which of its bodies it was (the item index is not filled in), so each of its
	// bodies is asked.
	const FVector HandPoint = GetHandTransform().GetLocation();
	double BestSquared = TNumericLimits<double>::Max();
	OutComponent = nullptr;
	const auto Consider = [&](UPrimitiveComponent* Component, const FBodyInstance* Body, FName Bone)
	{
		if (!Body || !Body->IsValidBodyInstance() || !Body->IsInstanceSimulatingPhysics())
		{
			return;
		}
		FVector OnBody;
		float Squared = 0.0f;
		// A shape the engine cannot measure to counts as far, and is still taken when it is the only one.
		const double DistanceSquared = Body->GetSquaredDistanceToBody(HandPoint, Squared, OnBody) ? Squared : TNumericLimits<float>::Max();
		if (DistanceSquared < BestSquared)
		{
			BestSquared = DistanceSquared;
			OutComponent = Component;
			OutBone = Bone;
		}
	};
	TArray<const UPrimitiveComponent*, TInlineAllocator<4>> Seen;
	for (const FOverlapResult& Overlap : Overlaps)
	{
		UPrimitiveComponent* Component = Overlap.GetComponent();
		if (!Component || Seen.Contains(Component))
		{
			continue;
		}
		Seen.Add(Component);
		if (const USkeletalMeshComponent* Skeletal = Cast<USkeletalMeshComponent>(Component))
		{
			for (const FBodyInstance* Part : Skeletal->Bodies)
			{
				const UBodySetup* Setup = Part ? Part->GetBodySetup() : nullptr;
				if (Setup && Part->IsValidBodyInstance() && Part->OverlapTest(Center, Rotation, Wider))
				{
					Consider(Component, Part, Setup->BoneName);
				}
			}
		}
		else
		{
			Consider(Component, Component->GetBodyInstance(), NAME_None);
		}
	}
	return OutComponent != nullptr;
}

void USandboxEffectorComponent::OnEffectorHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComponent, FVector NormalImpulse, const FHitResult& Hit)
{
	// One report per touched body and substep, in kg*cm/s. The normal is given from this body's side, so
	// impulses from several touched bodies add up to the net contact impulse of the frame.
	const double SizeNs = NormalImpulse.Size() * 0.01;
	if (ImpulseFrame != Frame)
	{
		// The first touch after a frame without one, by a hand that was fast as the step began: a blow lands.
		// How much of one grows with the speed it came at, from none at strike_speed_mps to all of it at
		// strike_full_speed_mps, so that no speed is an edge between a push and a punch.
		// One at a time: a hand that leaves what it struck for a frame and meets it again is the same blow.
		// Only a fist strikes: an open hand that lands is a slap, with the hand's own mass and strength.
		if (ImpulseFrame < Frame - 1 && StrikeLeftS <= 0.0 && !bStrikeOn && bFist && !bPalmActive && Tuning.StrikeForceN > Tuning.MaxForceN && !Grab.IsAttached())
		{
			const double SpeedMps = StepStartSpeedCmS * 0.01;
			const double Share = Tuning.StrikeFullSpeedMps > Tuning.StrikeSpeedMps
				? FMath::Clamp((SpeedMps - Tuning.StrikeSpeedMps) / (Tuning.StrikeFullSpeedMps - Tuning.StrikeSpeedMps), 0.0, 1.0)
				: (SpeedMps >= Tuning.StrikeSpeedMps ? 1.0 : 0.0);
			if (Share > 0.0)
			{
				StrikeLeftS = Tuning.StrikeS;
				StrikeShare = Share;
				StrikeDirection = StepStartVelocityCmS.GetSafeNormal();
				StrikePoint = GetHandTransform().GetLocation();
			}
		}
		ImpulseFrame = Frame;
		FrameImpulseNs = FVector::ZeroVector;
		++Stats.ContactFrames;
	}
	FrameImpulseNs += Hit.ImpactNormal * SizeNs;
	Stats.MaxFrameImpulseNs = FMath::Max(Stats.MaxFrameImpulseNs, FrameImpulseNs.Size());
	Stats.ContactImpulseNs += SizeNs;

	const FName Touched = !Hit.BoneName.IsNone() ? Hit.BoneName : OtherActor ? OtherActor->GetFName() : NAME_None;
	LastTouched = Touched;
	LastTouchedFrame = Frame;
	if (Stats.ImpulseByBody.Num() == 0)
	{
		Stats.FirstTouched = Touched;
		Stats.FirstContactSpeedCmS = StepStartSpeedCmS;
	}
	Stats.ImpulseByBody.FindOrAdd(Touched) += SizeNs;
}
