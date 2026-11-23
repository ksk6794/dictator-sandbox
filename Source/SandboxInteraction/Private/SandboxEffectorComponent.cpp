#include "SandboxEffectorComponent.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "PhysicsEngine/BodyInstance.h"

namespace
{
	// The engine's basic sphere, 100 cm across. Only looked at: the body's shape is the sphere component's own.
	const TCHAR* const SphereMeshPath = TEXT("/Engine/BasicShapes/Sphere.Sphere");
	constexpr double SphereMeshRadiusCm = 50.0;
}

FName SandboxEffectors::KindOf(FName Effector)
{
	FString Kind, Side;
	return Effector.ToString().Split(TEXT("_"), &Kind, &Side) ? FName(*Kind) : Effector;
}

bool SandboxEffectors::IsKnown(FName Effector)
{
	return Effector == HandLeft || Effector == HandRight;
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
	// first and calls OnUnregister afterwards, which would be too late.)
	Spring.Detach();
	Super::OnDestroyPhysicsState();
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
	// The radius is the size of a body that already exists.
	const double RadiusCm = Tuning.RadiusCm;
	Tuning = InTuning;
	Tuning.RadiusCm = RadiusCm;
	if (!bStarted)
	{
		return;
	}
	SetMassOverrideInKg(NAME_None, static_cast<float>(Tuning.MassKg), /*bOverrideMass*/ true);
	Spring.SetLinear(Tuning.StiffnessNPerM, Tuning.DampingS, Tuning.MaxForceN);
	Spring.SetAngular(Tuning.AngularStiffnessNmPerRad, Tuning.AngularDampingS, Tuning.MaxTorqueNm);
}

void USandboxEffectorComponent::Update(float DeltaTime)
{
	FBodyInstance* Body = bStarted ? GetBodyInstance() : nullptr;
	if (!Body || !Body->IsValidBodyInstance())
	{
		return;
	}
	// The contacts reported since the last Update belong to the step that has just ended.
	Stats.LastFrameImpulseNs = ImpulseFrame == Frame ? FrameImpulseNs.Size() : 0.0;
	++Frame;
	FrameDt = DeltaTime;

	// A hand left far behind (the camera went through a wall) could only come back through the wall. It is
	// put on its target instead, at rest. That is not physics, so it is counted.
	if (FVector::Dist(Body->GetUnrealWorldTransform().GetLocation(), Target.Location) > Tuning.SnapDistanceCm)
	{
		SetWorldLocationAndRotation(Target.Location, Target.Rotation, /*bSweep*/ false, nullptr, ETeleportType::ResetPhysics);
		Body->SetLinearVelocity(FVector::ZeroVector, /*bAddToCurrent*/ false);
		Body->SetAngularVelocityInRadians(FVector::ZeroVector, /*bAddToCurrent*/ false);
		++Stats.Snaps;
	}

	Spring.SetTarget(Target.Location, Target.Rotation, Target.Velocity);
	Stats.MaxGapCm = FMath::Max(Stats.MaxGapCm, Spring.GetGapCm());
	Stats.MaxDemandN = FMath::Max(Stats.MaxDemandN, Spring.GetDemandN());
	StepStartSpeedCmS = Body->GetUnrealWorldVelocity().Size();
	Stats.MaxSpeedCmS = FMath::Max(Stats.MaxSpeedCmS, StepStartSpeedCmS);
}

void USandboxEffectorComponent::OnEffectorHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComponent, FVector NormalImpulse, const FHitResult& Hit)
{
	// One report per touched body and substep, in kg*cm/s. The normal is given from this body's side, so
	// impulses from several touched bodies add up to the net contact impulse of the frame.
	const double SizeNs = NormalImpulse.Size() * 0.01;
	if (ImpulseFrame != Frame)
	{
		ImpulseFrame = Frame;
		FrameImpulseNs = FVector::ZeroVector;
		++Stats.ContactFrames;
	}
	FrameImpulseNs += Hit.ImpactNormal * SizeNs;
	Stats.MaxFrameImpulseNs = FMath::Max(Stats.MaxFrameImpulseNs, FrameImpulseNs.Size());
	Stats.ContactImpulseNs += SizeNs;

	const FName Touched = !Hit.BoneName.IsNone() ? Hit.BoneName : OtherActor ? OtherActor->GetFName() : NAME_None;
	if (Stats.ImpulseByBody.Num() == 0)
	{
		Stats.FirstTouched = Touched;
		Stats.FirstContactSpeedCmS = StepStartSpeedCmS;
	}
	Stats.ImpulseByBody.FindOrAdd(Touched) += SizeNs;
}
