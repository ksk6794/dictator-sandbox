#include "SandboxBodyController.h"

#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "PhysicsControlComponent.h"
#include "PhysicsEngine/BodyInstance.h"
#include "PhysicsEngine/ConstraintInstance.h"
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
		Muscle.Group = FSandboxBodyTuning::GroupOfBone(Child);
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

	PushTuningToControls();

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
	if (Solver.PositionIterations != Tuning.Solver.PositionIterations || Solver.VelocityIterations != Tuning.Solver.VelocityIterations ||
		Solver.ProjectionIterations != Tuning.Solver.ProjectionIterations)
	{
		RefreshSolverIsland();
	}
	return true;
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

	ReleaseIslandRefresh();

	// One integer compared per tick: nothing else happens here while the tuning stays as it is.
	if (Params && Params->GetRevision() != ParamsRevision)
	{
		FollowParams();
	}

	Control->UpdateTargetCaches(DeltaTime);
	UpdateState(DeltaTime);
	UpdateAssistLoad();
	Control->UpdateControls(DeltaTime);

	// Advanced last: during a tick this is the time at which the coming physics step begins.
	ElapsedS += DeltaTime;
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
