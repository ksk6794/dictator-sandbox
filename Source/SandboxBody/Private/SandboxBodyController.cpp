#include "SandboxBodyController.h"

#include "SandboxAnimInstance.h"
#include "SandboxBodyProbe.h"
#include "AnimationRuntime.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "PhysicsControlComponent.h"
#include "PhysicsEngine/BodyInstance.h"
#include "PhysicsEngine/ConstraintInstance.h"
#include "Chaos/RigidParticles.h"
#include "Physics/Experimental/PhysInterface_Chaos.h"
#include "PhysicsProxy/SingleParticlePhysicsProxy.h"
#include "SandboxCore.h"
#include "SandboxParamStore.h"

namespace
{
	const FName MuscleSet(TEXT("Muscles"));
	const FName AssistSet(TEXT("Assist"));

	// Physics Control takes a "strength" and turns it into a spring constant of (2 pi strength)^2. With the
	// drives in force mode that constant is used as it is, in Unreal units, so a stiffness in physical units
	// is fed in through the inverse of that formula.
	float StrengthForSpring(double SpringUnrealUnits)
	{
		return static_cast<float>(FMath::Sqrt(FMath::Max(SpringUnrealUnits, 0.0)) / UE_DOUBLE_TWO_PI);
	}

	// 1 N*m = 1e4 kg*cm^2/s^2. Chaos then multiplies angular drive stiffness and damping by 1.5
	// (p.Chaos.JointConstraint.AngularDriveStiffnessScale and ...DampingScale); the factor is divided out
	// here so that the tuning values are the torques the solver really applies. Torque caps are not scaled.
	constexpr double TorqueToUnreal = 1.0e4;
	constexpr double ChaosAngularDriveScale = 1.5;
	// 1 N/m = 1 kg/s^2 in Unreal units as well (force in kg*cm/s^2 per cm), and Chaos does not scale it.
}

USandboxBodyController::USandboxBodyController()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

void USandboxBodyController::OnUnregister()
{
	if (Mesh)
	{
		Mesh->OnComponentHit.RemoveDynamic(this, &USandboxBodyController::OnBodyHit);
	}
	ReleaseIslandRefresh();
	Super::OnUnregister();
}

const FSandboxAssistTuning& USandboxBodyController::AssistTuning(EAssistSlot Slot) const
{
	switch (Slot)
	{
	case EAssistSlot::Chest: return Tuning.Chest;
	case EAssistSlot::Head: return Tuning.Head;
	case EAssistSlot::Feet: return Tuning.Feet;
	default: return Tuning.Pelvis;
	}
}

bool USandboxBodyController::Start(USkeletalMeshComponent* InMesh, const FSandboxParamStore& InParams, FString& OutError)
{
	if (bStarted)
	{
		OutError = TEXT("The body controller is already started");
		return false;
	}
	if (!InMesh || InMesh->Bodies.Num() == 0)
	{
		OutError = TEXT("The body controller needs a skeletal mesh with physics bodies");
		return false;
	}
	FSandboxBodyTuning InTuning;
	if (!FSandboxBodyTuning::FromParams(InParams, FSandboxBodyTuning::StateName(ESandboxBodyState::Standing), InTuning, OutError))
	{
		return false;
	}

	const TPair<FName, EAssistSlot> AssistBones[] = {
		{ InTuning.Rig.Pelvis, EAssistSlot::Pelvis },
		{ InTuning.Rig.Chest, EAssistSlot::Chest },
		{ InTuning.Rig.Head, EAssistSlot::Head },
		{ InTuning.Rig.FootLeft, EAssistSlot::Feet },
		{ InTuning.Rig.FootRight, EAssistSlot::Feet } };
	for (const TPair<FName, EAssistSlot>& Entry : AssistBones)
	{
		const FBodyInstance* Body = InMesh->GetBodyInstance(Entry.Key);
		if (!Body || !Body->IsValidBodyInstance() || InMesh->GetBoneName(Body->InstanceBoneIndex) != Entry.Key)
		{
			OutError = FString::Printf(TEXT("Rig bone '%s' has no body in the physics asset"), *Entry.Key.ToString());
			return false;
		}
	}

	Mesh = InMesh;
	Tuning = InTuning;

	// The Physics Control component is ticked by hand from TickComponent, never by the engine.
	Control = NewObject<UPhysicsControlComponent>(GetOwner(), TEXT("PhysicsControl"));
	Control->PrimaryComponentTick.bStartWithTickEnabled = false;
	Control->SetupAttachment(Mesh);
	Control->RegisterComponent();
	Control->SetComponentTickEnabled(false);

	// Muscles: one control per rigid joint, in physics-asset order.
	for (const FConstraintInstance* Joint : Mesh->Constraints)
	{
		if (!Joint ||
			Joint->GetLinearXMotion() != LCM_Locked || Joint->GetLinearYMotion() != LCM_Locked || Joint->GetLinearZMotion() != LCM_Locked)
		{
			continue;
		}
		const FName Child = Joint->GetChildBoneName();
		const FName Parent = Joint->GetParentBoneName();
		if (!Mesh->GetBodyInstance(Child) || !Mesh->GetBodyInstance(Parent))
		{
			continue;
		}

		FMuscle Muscle;
		Muscle.Control = FName(*FString::Printf(TEXT("Muscle_%s"), *Child.ToString()));
		Muscle.ParentBone = Parent;
		Muscle.ChildBone = Child;
		Muscle.Group = FSandboxBodyTuning::GroupOfBone(Child);
		{
			const FReferenceSkeleton& Skeleton = Mesh->GetSkeletalMeshAsset()->GetRefSkeleton();
			const int32 ParentIndex = Skeleton.FindBoneIndex(Parent);
			const int32 ChildIndex = Skeleton.FindBoneIndex(Child);
			if (ParentIndex != INDEX_NONE && ChildIndex != INDEX_NONE)
			{
				Muscle.ReferenceRelative = FAnimationRuntime::GetComponentSpaceTransformRefPose(Skeleton, ParentIndex).GetRotation().Inverse() *
					FAnimationRuntime::GetComponentSpaceTransformRefPose(Skeleton, ChildIndex).GetRotation();
			}
		}
		if (!Control->CreateNamedControl(Muscle.Control, Mesh, Parent, Mesh, Child, FPhysicsControlData(), FPhysicsControlTarget(), MuscleSet))
		{
			OutError = FString::Printf(TEXT("Could not create the muscle control for joint '%s'"), *Child.ToString());
			return false;
		}
		Muscles.Add(Muscle);
	}

	// Balance assist: each body is held where it is now. The mesh has not been simulated yet, so "now" is
	// the pose it was spawned in.
	for (const TPair<FName, EAssistSlot>& Entry : AssistBones)
	{
		FAssist Assist;
		Assist.Control = FName(*FString::Printf(TEXT("Assist_%s"), *Entry.Key.ToString()));
		Assist.Bone = Entry.Key;
		Assist.Slot = Entry.Value;
		Assist.Target = Mesh->GetBodyInstance(Entry.Key)->GetUnrealWorldTransform();
		Assist.Target.SetScale3D(FVector::OneVector);

		// The target is the pose of the whole body; the spring itself acts at the centre of mass.
		FPhysicsControlTarget Target;
		Target.TargetPosition = Assist.Target.GetLocation();
		Target.TargetOrientation = Assist.Target.Rotator();
		Target.bApplyControlPointToTarget = true;
		if (!Control->CreateNamedControl(Assist.Control, nullptr, NAME_None, Mesh, Assist.Bone, FPhysicsControlData(), Target, AssistSet))
		{
			OutError = FString::Printf(TEXT("Could not create the assist control for '%s'"), *Assist.Bone.ToString());
			return false;
		}
		Assists.Add(Assist);
	}

	// Reference values for the assist-load estimate.
	double TotalMassKg = 0.0;
	for (const FBodyInstance* Body : Mesh->Bodies)
	{
		if (Body && Body->IsValidBodyInstance())
		{
			TotalMassKg += Body->GetBodyMass();
		}
	}
	const double GravityMps2 = GetWorld() ? FMath::Abs(GetWorld()->GetGravityZ()) * 0.01 : 9.8;
	WeightN = TotalMassKg * GravityMps2;
	StandingComHeightM = FMath::Max((Mesh->GetSkeletalCenterOfMass().Z - Mesh->GetComponentLocation().Z) * 0.01, 0.01);

	// The sense of balance: what the floor pushes on each body with is collected from the contact events.
	GravityCmS2 = GravityMps2 * 100.0;
	SupportImpulseNs.Init(FVector::ZeroVector, Mesh->Bodies.Num());
	SupportStepS = 0.0;
	LastSupportHeightCm = Mesh->GetComponentLocation().Z;
	Balance = FSandboxBalanceView();

	PushTuningToControls();

	// The target pose: joint angles from the tuning, evaluated by the mesh's animation instance.
	if (!PoseRig.Init(Mesh, OutError))
	{
		return false;
	}
	{
		TArray<FSandboxPoseBone> Bones;
		if (!PoseRig.Build(Tuning.Poses.Stance, Bones, OutError))
		{
			return false;
		}
	}
	ApplyPose();

	// A body under control never sleeps. A standing character is still enough for the solver to put its
	// island to sleep, and the next tick woke it again: one substep in six it was frozen, with no gravity,
	// no contacts and no contact events, so the feet seemed to carry five sixths of the weight. A body that
	// has to sense its balance and answer a touch at once is awake for as long as it is controlled.
	for (FBodyInstance* Body : Mesh->Bodies)
	{
		if (Body && Body->IsValidBodyInstance())
		{
			FPhysicsCommand::ExecuteWrite(Body->GetPhysicsActor(), [](const FPhysicsActorHandle& Actor)
			{
				Actor->GetGameThreadAPI().SetSleepType(Chaos::ESleepType::NeverSleep);
			});
		}
	}
	Mesh->WakeAllRigidBodies();

	// The body learns that it was hit from its own contacts: the physics scene reports every contact of
	// these bodies after the step, with its impulse. Nothing here knows what the other body is.
	// A body of a skeletal mesh asks for contact events only when its own flag and the component's are both
	// set (FBodyInstance::BuildBodyFilterData), and the call below sets the bodies' flags alone. Without
	// the component's flag the character would hear only of bodies that ask for events themselves, as the
	// hands do, and nothing of the floor or of a prop thrown at it.
	Mesh->BodyInstance.bNotifyRigidBodyCollision = true;
	Mesh->SetNotifyRigidBodyCollision(true);
	Mesh->OnComponentHit.AddUniqueDynamic(this, &USandboxBodyController::OnBodyHit);

	// Run after the mesh has evaluated its animation pose for this frame: that pose is the muscles' target.
	AddTickPrerequisiteComponent(Mesh);
	Params = &InParams;
	ParamsRevision = InParams.GetRevision();
	State = ESandboxBodyState::Standing;
	StateLog.Reset();
	StateLog.Add(FSandboxStateChange{ 0.0, State, TEXT("started") });
	bStarted = true;

	UE_LOG(LogSandbox, Display, TEXT("Body controller started: %d muscles, %d assist controls, %.1f kg, centre of mass %.2f m above the origin"),
		Muscles.Num(), Assists.Num(), TotalMassKg, StandingComHeightM);
	return true;
}

bool USandboxBodyController::ApplyTuning(const FSandboxBodyTuning& InTuning, FString& OutError)
{
	if (!bStarted)
	{
		OutError = TEXT("The body controller is not started");
		return false;
	}
	if (!InTuning.Validate(OutError))
	{
		return false;
	}
	const FSandboxBodyRig Rig = Tuning.Rig;
	const FSandboxSolverTuning Solver = Tuning.Solver;
	Tuning = InTuning;
	Tuning.Rig = Rig;
	PushTuningToControls();
	ApplyPose();
	if (Solver.PositionIterations != Tuning.Solver.PositionIterations || Solver.VelocityIterations != Tuning.Solver.VelocityIterations ||
		Solver.ProjectionIterations != Tuning.Solver.ProjectionIterations)
	{
		RefreshSolverIsland();
	}
	return true;
}

void USandboxBodyController::ApplyPose()
{
	USandboxAnimInstance* Animation = Mesh ? Cast<USandboxAnimInstance>(Mesh->GetAnimInstance()) : nullptr;
	if (!Animation)
	{
		// A mesh without the sandbox animation instance holds its reference pose; that is all it can do.
		return;
	}
	TArray<FSandboxPoseBone> Bones;
	FString Error;
	if (!PoseRig.Build(Tuning.Poses.Stance, Bones, Error))
	{
		UE_LOG(LogSandbox, Error, TEXT("Body controller keeps its pose: %s"), *Error);
		return;
	}
	Animation->SetTargetPose(Bones, Tuning.Poses.BlendS);
}

void USandboxBodyController::UpdatePoseTurns()
{
	USandboxAnimInstance* Animation = Cast<USandboxAnimInstance>(Mesh->GetAnimInstance());
	if (!Animation)
	{
		return;
	}
	TArray<FSandboxPoseTurn> Turns;
	// The flinch's pose offset: as strong as the hit, up to reactions.flinch_pose_full_ns, and gone by the time
	// the muscles are back to full strength. It fades by its own clock, not with the state: a hit that knocks
	// the body over ends the flinch at once, and an offset dropped in one step had the muscles snap the body
	// back (13.8 J put into a falling body). Once the body lies, nothing is left of it.
	if (FlinchStartedS >= 0.0 && State != ESandboxBodyState::Lying)
	{
		const FSandboxReactionTuning& Reactions = Tuning.Reactions;
		const double Strength = FMath::Clamp(FlinchImpulseNs.Size() / Reactions.FlinchPoseFullNs, 0.0, 1.0);
		const double Left = Reactions.FlinchRecoveryS > 0.0 ? FMath::Clamp(1.0 - (ElapsedS - FlinchStartedS) / Reactions.FlinchRecoveryS, 0.0, 1.0) : 0.0;
		const double Weight = Strength * Left;
		PoseRig.BuildTurns(Tuning.Poses.Flinch, Weight, Turns);

		// The struck body gives way: its joint turns so that the body moves along the hit, about the axis
		// across the line from the joint to the body's centre of mass and the hit.
		const int32 StruckIndex = PoseRig.MeshBoneIndexOf(FlinchBone);
		const FBodyInstance* Struck = Mesh->GetBodyInstance(FlinchBone);
		if (Weight > 0.0 && StruckIndex != INDEX_NONE && Struck && Struck->IsValidBodyInstance())
		{
			const FTransform Bone = Struck->GetUnrealWorldTransform();
			const FVector Axis = FVector::CrossProduct(Struck->GetCOMPosition() - Bone.GetLocation(), FlinchImpulseNs).GetSafeNormal();
			if (!Axis.IsNearlyZero())
			{
				const FQuat Give(Bone.GetRotation().UnrotateVector(Axis), FMath::DegreesToRadians(Reactions.FlinchGiveDeg * Weight));
				FSandboxPoseTurn* Existing = Turns.FindByPredicate([StruckIndex](const FSandboxPoseTurn& Turn) { return Turn.MeshBoneIndex == StruckIndex; });
				if (Existing)
				{
					Existing->Turn = (Existing->Turn * Give).GetNormalized();
				}
				else
				{
					Turns.Add(FSandboxPoseTurn{ StruckIndex, Give });
				}
			}
		}
	}
	// Nothing to hand over while there is no offset now and there was none before.
	if (Turns.Num() > 0 || bPoseTurnsSet)
	{
		Animation->SetTurns(Turns);
		bPoseTurnsSet = Turns.Num() > 0;
	}
}

void USandboxBodyController::RefreshSolverIsland()
{
	// Chaos works out a simulation island's iteration counts from its bodies only when something joins or
	// leaves the island (FPBDIslandManager, "this won't handle updates to per-body iterations after being
	// added to an island"). A standing character's island never changes, so new counts would be ignored.
	// A constraint to the world that leaves every axis free is such a change and applies no force; it is
	// taken away again on the next tick, which is a second change and leaves nothing behind.
	ReleaseIslandRefresh();
	FBodyInstance* Body = Mesh->GetBodyInstance(Tuning.Rig.Pelvis);
	if (!Body || !Body->IsValidBodyInstance())
	{
		return;
	}
	IslandRefresh = MakeUnique<FConstraintInstance>();
	IslandRefresh->SetLinearLimits(LCM_Free, LCM_Free, LCM_Free, 0.0f);
	IslandRefresh->SetAngularSwing1Motion(ACM_Free);
	IslandRefresh->SetAngularSwing2Motion(ACM_Free);
	IslandRefresh->SetAngularTwistMotion(ACM_Free);
	IslandRefresh->SetRefFrame(EConstraintFrame::Frame1, FTransform::Identity);
	IslandRefresh->SetRefFrame(EConstraintFrame::Frame2, Body->GetUnrealWorldTransform());
	IslandRefresh->InitConstraint(Body, nullptr, 1.0f, this);
}

void USandboxBodyController::ReleaseIslandRefresh()
{
	if (IslandRefresh)
	{
		IslandRefresh->TermConstraint();
		IslandRefresh.Reset();
	}
}

bool USandboxBodyController::ReadStateTuning(FSandboxBodyTuning& OutTuning, FString& OutError) const
{
	return FSandboxBodyTuning::FromParams(*Params, FSandboxBodyTuning::StateName(State), OutTuning, OutError);
}

double USandboxBodyController::MuscleScale(ESandboxMuscleGroup Group) const
{
	if (State != ESandboxBodyState::Flinch || Group != FlinchGroup)
	{
		return 1.0;
	}
	// Weak at the moment of the hit, back to full strength in a straight line.
	const double Recovered = Tuning.Reactions.FlinchRecoveryS > 0.0 ? FMath::Clamp((ElapsedS - StateEnteredS) / Tuning.Reactions.FlinchRecoveryS, 0.0, 1.0) : 1.0;
	return FMath::Lerp(Tuning.Reactions.FlinchWeakening, 1.0, Recovered);
}

void USandboxBodyController::EnterState(ESandboxBodyState NewState, const FString& Reason)
{
	State = NewState;
	StateEnteredS = ElapsedS;
	RestS = 0.0;
	StateLog.Add(FSandboxStateChange{ ElapsedS, NewState, Reason });
	UE_LOG(LogSandbox, Display, TEXT("Body controller: %s at %.3f s (%s)"), FSandboxBodyTuning::StateName(NewState), ElapsedS, *Reason);

	// Each state has its own values. The store has checked every state that sets any, so this reads.
	FSandboxBodyTuning NewTuning;
	FString Error;
	if (!ReadStateTuning(NewTuning, Error) || !ApplyTuning(NewTuning, Error))
	{
		UE_LOG(LogSandbox, Error, TEXT("Body controller keeps the previous state's tuning: %s"), *Error);
	}
}

void USandboxBodyController::ApplyImpulse(FName Bone, const FVector& ImpulseNs)
{
	FBodyInstance* Body = bStarted ? Mesh->GetBodyInstance(Bone) : nullptr;
	if (!Body || !Body->IsValidBodyInstance())
	{
		return;
	}
	// 1 N*s = 1 kg*m/s = 100 kg*cm/s in Unreal units.
	Body->AddImpulse(ImpulseNs * 100.0, /*bVelChange*/ false);
	FeelImpulse(Bone, ImpulseNs);
}

bool USandboxBodyController::SetFootTarget(FName Foot, const FVector& WorldLocation, double Weight, FString& OutError)
{
	USandboxAnimInstance* Animation = bStarted ? Cast<USandboxAnimInstance>(Mesh->GetAnimInstance()) : nullptr;
	if (!Animation)
	{
		OutError = TEXT("The body is not started, or its mesh has no sandbox animation instance");
		return false;
	}
	const FReferenceSkeleton& Skeleton = Mesh->GetSkeletalMeshAsset()->GetRefSkeleton();
	const int32 End = Skeleton.FindBoneIndex(Foot);
	const int32 Middle = End != INDEX_NONE ? Skeleton.GetParentIndex(End) : INDEX_NONE;
	const int32 Root = Middle != INDEX_NONE ? Skeleton.GetParentIndex(Middle) : INDEX_NONE;
	if (Root == INDEX_NONE || !Mesh->GetBodyInstance(Foot) || !Mesh->GetBodyInstance(Skeleton.GetBoneName(Middle)) ||
		!Mesh->GetBodyInstance(Skeleton.GetBoneName(Root)))
	{
		OutError = FString::Printf(TEXT("'%s' is not the end of two segments with bodies"), *Foot.ToString());
		return false;
	}
	Animation->SetLimbTarget(End, WorldLocation, Weight, Tuning.Poses.BlendS);
	return true;
}

void USandboxBodyController::OnBodyHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComponent, FVector NormalImpulse, const FHitResult& Hit)
{
	// An arm resting against the character's own side is neither a hit nor support.
	if (!bStarted || !OtherComponent || OtherComponent == Mesh)
	{
		return;
	}
	// One report per pair of bodies and substep, in kg*cm/s. The impulse vector's sign follows the order
	// the solver holds the pair in; the normal is always given from this body's side, so the two together
	// give impulses that add up properly when one hand touches several of the character's bodies.
	const FVector ImpulseNs = Hit.ImpactNormal * (NormalImpulse.Size() * 0.01);

	// A hit is a contact with another body that moves under physics: a hand, a thrown prop. The floor
	// under the feet pushes back with the character's whole weight in every step and is no hit: it is what
	// the body stands on, and is kept for the sense of balance.
	if (!OtherComponent->IsSimulatingPhysics(Hit.BoneName))
	{
		const FBodyInstance* Body = Mesh->GetBodyInstance(Hit.MyBoneName);
		if (Body && SupportImpulseNs.IsValidIndex(Body->InstanceBodyIndex))
		{
			SupportImpulseNs[Body->InstanceBodyIndex] += ImpulseNs;
		}
		return;
	}
	ContactImpulseNs += ImpulseNs.Size();
	++ContactReports;
	FeelImpulse(Hit.MyBoneName, ImpulseNs);
}

void USandboxBodyController::FeelImpulse(FName Bone, const FVector& ImpulseNs)
{
	// What is felt is the hit as a whole: impulses that follow each other closely are added up.
	if (ElapsedS - HitStartedS > Tuning.Thresholds.FlinchWindowS)
	{
		HitImpulseNs = FVector::ZeroVector;
		HitStartedS = ElapsedS;
	}
	HitImpulseNs += ImpulseNs;

	if (State == ESandboxBodyState::Standing && HitImpulseNs.Size() >= Tuning.Thresholds.FlinchImpulseNs)
	{
		FlinchGroup = FSandboxBodyTuning::GroupOfBone(Bone);
		FlinchBone = Bone;
		FlinchImpulseNs = HitImpulseNs;
		FlinchStartedS = ElapsedS;
		EnterState(ESandboxBodyState::Flinch, FString::Printf(TEXT("hit of %.1f N*s on %s"), HitImpulseNs.Size(), *Bone.ToString()));
		HitImpulseNs = FVector::ZeroVector;
	}
}

void USandboxBodyController::UpdateState(float DeltaTime)
{
	if (State == ESandboxBodyState::Standing || State == ESandboxBodyState::Flinch)
	{
		// Balance level 0 has no way to win balance back: once the pelvis has left the place the assist
		// holds it in, by distance, by height or by tilt, the body is let go.
		const FAssist* Pelvis = Assists.FindByPredicate([](const FAssist& Assist) { return Assist.Slot == EAssistSlot::Pelvis; });
		const FBodyInstance* Body = Pelvis ? Mesh->GetBodyInstance(Pelvis->Bone) : nullptr;
		if (Body && Body->IsValidBodyInstance())
		{
			const FTransform Now = Body->GetUnrealWorldTransform();
			const double OffsetCm = FVector::Dist2D(Now.GetLocation(), Pelvis->Target.GetLocation());
			const double DropCm = Pelvis->Target.GetLocation().Z - Now.GetLocation().Z;
			// How far the direction that pointed up when standing has turned away from up.
			const FVector Up = Now.GetRotation().RotateVector(Pelvis->Target.GetRotation().UnrotateVector(FVector::UpVector));
			const double TiltDeg = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(Up.Z, -1.0, 1.0)));

			const FSandboxThresholdTuning& Limits = Tuning.Thresholds;
			if (OffsetCm > Limits.FallPelvisOffsetCm)
			{
				EnterState(ESandboxBodyState::Falling, FString::Printf(TEXT("pelvis %.1f cm from where it stood"), OffsetCm));
			}
			else if (DropCm > Limits.FallPelvisDropCm)
			{
				EnterState(ESandboxBodyState::Falling, FString::Printf(TEXT("pelvis %.1f cm below its standing height"), DropCm));
			}
			else if (TiltDeg > Limits.FallTiltDeg)
			{
				EnterState(ESandboxBodyState::Falling, FString::Printf(TEXT("pelvis tilted by %.1f degrees"), TiltDeg));
			}
		}
	}

	if (State == ESandboxBodyState::Flinch)
	{
		if (ElapsedS - StateEnteredS >= Tuning.Reactions.FlinchRecoveryS)
		{
			EnterState(ESandboxBodyState::Standing, TEXT("recovered from the hit"));
		}
		else
		{
			// The hit muscles regain strength every tick.
			PushTuningToControls();
		}
	}
	else if (State == ESandboxBodyState::Falling)
	{
		// The speed of the body as a whole: the root mean square over its bodies, weighted by mass. A hand
		// that trembles against the floor does not keep an 80 kg character from being at rest; a moving
		// trunk does.
		double MassKg = 0.0;
		double LinearSquared = 0.0;
		double AngularSquared = 0.0;
		for (const FBodyInstance* Body : Mesh->Bodies)
		{
			if (Body && Body->IsValidBodyInstance())
			{
				const double BodyMassKg = Body->GetBodyMass();
				MassKg += BodyMassKg;
				LinearSquared += BodyMassKg * (Body->GetUnrealWorldVelocity() * 0.01).SizeSquared();
				AngularSquared += BodyMassKg * Body->GetUnrealWorldAngularVelocityInRadians().SizeSquared();
			}
		}
		const bool bAtRest = MassKg > 0.0 &&
			FMath::Sqrt(LinearSquared / MassKg) <= Tuning.Thresholds.RestSpeedMps &&
			FMath::Sqrt(AngularSquared / MassKg) <= Tuning.Thresholds.RestAngularSpeedRadS;
		RestS = bAtRest ? RestS + DeltaTime : 0.0;
		if (bAtRest && RestS >= Tuning.Thresholds.RestTimeS)
		{
			EnterState(ESandboxBodyState::Lying, FString::Printf(TEXT("at rest for %.2f s"), RestS));
		}
	}
}

void USandboxBodyController::FollowParams()
{
	ParamsRevision = Params->GetRevision();

	// The store has already put a new set through the body's own checks, so a failure here is not expected.
	FSandboxBodyTuning NewTuning;
	FString Error;
	if (!ReadStateTuning(NewTuning, Error))
	{
		UE_LOG(LogSandbox, Error, TEXT("Body controller keeps its tuning: %s"), *Error);
		return;
	}
	if (!(NewTuning.Rig == Tuning.Rig))
	{
		UE_LOG(LogSandbox, Warning, TEXT("Body controller: the rig bones changed in the tuning files; that takes effect when the body is next started"));
	}
	if (NewTuning.HasSameValues(Tuning))
	{
		return;
	}
	if (!ApplyTuning(NewTuning, Error))
	{
		UE_LOG(LogSandbox, Error, TEXT("Body controller keeps its tuning: %s"), *Error);
		return;
	}
	++TuningChangeCount;
	UE_LOG(LogSandbox, Display, TEXT("Body controller: new tuning values applied (change %d)"), TuningChangeCount);
}

void USandboxBodyController::PushTuningToControls()
{
	for (FBodyInstance* Body : Mesh->Bodies)
	{
		if (Body && Body->IsValidBodyInstance())
		{
			Body->SetPositionSolverIterationCount(static_cast<uint8>(Tuning.Solver.PositionIterations));
			Body->SetVelocitySolverIterationCount(static_cast<uint8>(Tuning.Solver.VelocityIterations));
			Body->SetProjectionSolverIterationCount(static_cast<uint8>(Tuning.Solver.ProjectionIterations));
			Body->SetOverrideIterationCounts(true);
		}
	}

	for (const FMuscle& Muscle : Muscles)
	{
		const FSandboxMuscleTuning& Group = Tuning.Muscle(Muscle.Group);
		const double Spring = Group.StiffnessNmPerRad * MuscleScale(Muscle.Group) * TorqueToUnreal / ChaosAngularDriveScale;
		FPhysicsControlData Data;
		// Force mode: the spring is a real torque per radian. In the plugin's default (acceleration) mode
		// it would be scaled by the inertia of the two jointed bodies alone, which leaves an ankle or a hip
		// far too weak for the weight of everything above it.
		Data.bUseAccelerationDriveMode = false;
		Data.LinearStrength = 0.0f; // the joint itself holds the bodies together
		Data.AngularStrength = StrengthForSpring(Spring);
		Data.AngularDampingRatio = 0.0f;
		Data.AngularExtraDamping = static_cast<float>(Spring * Group.DampingS);
		Data.MaxTorque = static_cast<float>(Group.MaxTorqueNm * TorqueToUnreal);
		Data.bUseSkeletalAnimation = true;
		// The two bodies are jointed and must never collide, whatever this extra constraint says.
		Data.bDisableCollision = true;
		Control->SetControlData(Muscle.Control, Data);
	}

	for (const FAssist& Assist : Assists)
	{
		const FSandboxAssistTuning& Slot = AssistTuning(Assist.Slot);
		const double AngularSpring = Slot.AngularNmPerRad * TorqueToUnreal / ChaosAngularDriveScale;
		FPhysicsControlData Data;
		Data.bUseAccelerationDriveMode = false;
		// Linear strength and damping are 1 times a per-axis multiplier, so that the horizontal and the
		// vertical pull can differ.
		Data.LinearStrength = 1.0f;
		Data.LinearDampingRatio = 0.0f;
		Data.LinearExtraDamping = 1.0f;
		Data.AngularStrength = StrengthForSpring(AngularSpring);
		Data.AngularDampingRatio = 0.0f;
		Data.AngularExtraDamping = static_cast<float>(AngularSpring * Slot.DampingS);
		Data.bUseSkeletalAnimation = false; // the target is fixed in the world, not taken from the mesh
		Control->SetControlData(Assist.Control, Data);

		FPhysicsControlMultiplier Multiplier;
		Multiplier.LinearStrengthMultiplier = FVector(
			StrengthForSpring(Slot.HorizontalNPerM), StrengthForSpring(Slot.HorizontalNPerM), StrengthForSpring(Slot.VerticalNPerM));
		Multiplier.LinearExtraDampingMultiplier = FVector(
			Slot.HorizontalNPerM * Slot.DampingS, Slot.HorizontalNPerM * Slot.DampingS, Slot.VerticalNPerM * Slot.DampingS);
		Control->SetControlMultiplier(Assist.Control, Multiplier);
	}
}

void USandboxBodyController::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!bStarted || !Control || !Mesh || TickType != LEVELTICK_All)
	{
		return;
	}
	// A body spawned in the middle of a frame (the session's reset) is ticked in that same frame, before
	// its mesh has a pose: there are no bone transforms yet for the muscles' targets to be taken from, and
	// Physics Control would warn about every one of them. The controller starts with the mesh's first pose,
	// which is also when it starts for a body spawned before the world's first tick.
	if (Mesh->GetEditableComponentSpaceTransforms().Num() == 0)
	{
		return;
	}

	ReleaseIslandRefresh();

	// One integer compared per tick: nothing else happens here while the tuning stays as it is.
	if (Params && Params->GetRevision() != ParamsRevision)
	{
		FollowParams();
	}

	// What the body senses of its balance at the start of this step: the bodies as the last step left them,
	// and the floor's push on the feet during it. The impulses collected since are then cleared, and the
	// events of the coming step fill them again.
	MeasureBalance(Balance);
	if (Balance.IsSupported())
	{
		LastSupportHeightCm = Balance.SupportHeightCm;
	}
	for (FVector& Impulse : SupportImpulseNs)
	{
		Impulse = FVector::ZeroVector;
	}
	SupportStepS = DeltaTime;

	Control->UpdateTargetCaches(DeltaTime);
	UpdateState(DeltaTime);
	UpdatePoseTurns();
	UpdateAssistLoad();
	Control->UpdateControls(DeltaTime);

	// Advanced last: during a tick this is the time at which the coming physics step begins.
	ElapsedS += DeltaTime;
}

void USandboxBodyController::MeasureBalance(FSandboxBalanceView& OutView) const
{
	OutView = FSandboxBalanceView();
	// Before the first step under the controller no contact has been reported: nothing is known yet.
	if (!bStarted || !Mesh || SupportStepS <= 0.0)
	{
		return;
	}

	TArray<FSandboxBodySample> Bodies;
	FSandboxBodyProbe::SampleBodies(Mesh, Bodies);
	OutView.CenterOfMass = FSandboxBodyProbe::CenterOfMass(Bodies);
	OutView.Velocity = FSandboxBodyProbe::CenterOfMassVelocity(Bodies);

	// A foot stands when the floor pushed up on it hard enough over the last step.
	const FName Feet[2] = { Tuning.Rig.FootLeft, Tuning.Rig.FootRight };
	for (int32 Side = 0; Side < 2; ++Side)
	{
		const FBodyInstance* Foot = Mesh->GetBodyInstance(Feet[Side]);
		if (Foot && SupportImpulseNs.IsValidIndex(Foot->InstanceBodyIndex))
		{
			OutView.FootForceN[Side] = SupportImpulseNs[Foot->InstanceBodyIndex].Z / SupportStepS;
			if (OutView.FootForceN[Side] >= Tuning.Balance.FootContactMinN)
			{
				OutView.StandingFeet.Add(Feet[Side]);
			}
		}
	}

	const double SoleHeightCm = FSandboxBodyProbe::SupportPolygon(Mesh, OutView.StandingFeet, Tuning.Balance.SoleToleranceCm, OutView.SupportPolygon);
	OutView.SupportHeightCm = OutView.IsSupported() ? SoleHeightCm : LastSupportHeightCm;
	OutView.CapturePoint = FSandboxBodyProbe::CapturePoint(OutView.CenterOfMass, OutView.Velocity, OutView.CenterOfMass.Z - OutView.SupportHeightCm, GravityCmS2);
	if (OutView.IsSupported())
	{
		OutView.CenterOfMassMarginCm = FSandboxBodyProbe::MarginInsidePolygon(OutView.SupportPolygon, FVector2D(OutView.CenterOfMass.X, OutView.CenterOfMass.Y));
		OutView.CaptureMarginCm = FSandboxBodyProbe::MarginInsidePolygon(OutView.SupportPolygon, OutView.CapturePoint);
	}
	OutView.bValid = true;
}

void USandboxBodyController::GetControlView(FSandboxBodyControlView& OutView) const
{
	OutView.Muscles.Reset();
	OutView.Assists.Reset();
	// Physics Control has cached a target pose only once it has been updated.
	if (!bStarted || !Control || !Mesh || ElapsedS <= 0.0)
	{
		return;
	}

	for (const FMuscle& Muscle : Muscles)
	{
		const FBodyInstance* Parent = Mesh->GetBodyInstance(Muscle.ParentBone);
		const FBodyInstance* Child = Mesh->GetBodyInstance(Muscle.ChildBone);
		if (!Parent || !Child || !Parent->IsValidBodyInstance() || !Child->IsValidBodyInstance())
		{
			continue;
		}
		// The muscle holds the child's orientation relative to the parent as it is in the cached target
		// pose. Carried over to where the parent body really is, that is where the child should point.
		const FQuat CachedParent = Control->GetCachedBoneTransform(Mesh, Muscle.ParentBone).GetRotation();
		const FQuat CachedChild = Control->GetCachedBoneTransform(Mesh, Muscle.ChildBone).GetRotation();
		const FQuat Relative = CachedParent.Inverse() * CachedChild;

		FSandboxMuscleView& View = OutView.Muscles.AddDefaulted_GetRef();
		View.ParentBone = Muscle.ParentBone;
		View.ChildBone = Muscle.ChildBone;
		View.Group = Muscle.Group;
		View.Strength = MuscleScale(Muscle.Group);
		View.bDriven = Tuning.Muscle(Muscle.Group).StiffnessNmPerRad > 0.0;
		View.TargetRotation = (Parent->GetUnrealWorldTransform().GetRotation() * Relative).GetNormalized();
		View.ErrorRad = View.TargetRotation.AngularDistance(Child->GetUnrealWorldTransform().GetRotation());
		View.TargetOffsetRad = Relative.AngularDistance(Muscle.ReferenceRelative);
	}

	for (const FAssist& Assist : Assists)
	{
		const FSandboxAssistTuning& Slot = AssistTuning(Assist.Slot);
		FSandboxAssistView& View = OutView.Assists.AddDefaulted_GetRef();
		View.Bone = Assist.Bone;
		View.Target = Assist.Target;
		View.bActive = Slot.HorizontalNPerM > 0.0 || Slot.VerticalNPerM > 0.0 || Slot.AngularNmPerRad > 0.0;
	}
}

void USandboxBodyController::UpdateAssistLoad()
{
	// The solver does not report drive forces, so the load is rebuilt from the spring law: stiffness times
	// error minus damping times velocity, for the state at the start of this step. The solver integrates
	// the springs implicitly, so this is a close estimate rather than a reading.
	double ForceN = 0.0;
	double TorqueNm = 0.0;
	for (const FAssist& Assist : Assists)
	{
		const FBodyInstance* Body = Mesh->GetBodyInstance(Assist.Bone);
		if (!Body || !Body->IsValidBodyInstance())
		{
			continue;
		}
		const FSandboxAssistTuning& Slot = AssistTuning(Assist.Slot);

		// Centimetres to metres.
		const FVector TargetCom = Assist.Target.TransformPositionNoScale(Body->GetMassSpaceLocal().GetLocation());
		const FVector ErrorM = (TargetCom - Body->GetCOMPosition()) * 0.01;
		const FVector VelocityMps = Body->GetUnrealWorldVelocity() * 0.01;
		const FVector Stiffness(Slot.HorizontalNPerM, Slot.HorizontalNPerM, Slot.VerticalNPerM);
		ForceN += (Stiffness * (ErrorM - VelocityMps * Slot.DampingS)).Size();

		const FQuat Delta = (Assist.Target.GetRotation() * Body->GetUnrealWorldTransform().GetRotation().Inverse()).GetShortestArcWith(FQuat::Identity);
		const FVector ErrorRad = Delta.ToRotationVector();
		TorqueNm += Slot.AngularNmPerRad * (ErrorRad - Body->GetUnrealWorldAngularVelocityInRadians() * Slot.DampingS).Size();
	}

	AssistLoad.ForceShare = WeightN > 0.0 ? ForceN / WeightN : 0.0;
	AssistLoad.TorqueShare = WeightN > 0.0 ? TorqueNm / (WeightN * StandingComHeightM) : 0.0;
}
