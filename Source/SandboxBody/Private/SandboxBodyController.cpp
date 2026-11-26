#include "SandboxBodyController.h"

#include "SandboxAnimInstance.h"
#include "SandboxBodyProbe.h"
#include "Animation/AnimSequence.h"
#include "AnimationRuntime.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "PhysicsControlComponent.h"
#include "PhysicsEngine/BodyInstance.h"
#include "PhysicsEngine/ConstraintInstance.h"
#include "Chaos/PBDCollisionConstraints.h"
#include "Chaos/RigidParticles.h"
#include "PBDRigidsSolver.h"
#include "Physics/Experimental/PhysInterface_Chaos.h"
#include "Physics/Experimental/PhysScene_Chaos.h"
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

/** A regrouping step's weight shift is asked for at most this many times the way to the stance foot. */
// A tolerance that takes in every corner of a sole, however the foot is tilted (FSandboxBodyProbe::SupportPolygon).
static constexpr double WholeSoleToleranceCm = 30.0;

void USandboxBodyController::OnUnregister()
{
	if (Mesh)
	{
		Mesh->OnComponentHit.RemoveDynamic(this, &USandboxBodyController::OnBodyHit);
	}
	ReleaseIslandRefresh();
	StopCollectingPushes();
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

	// A body whose head is a body of its own on top of the one the rig names (the neck's): that one carries the head
	// (rig.head_if_own; none names nothing). The rig as the files have it is kept, to tell a change of the files from this.
	TunedRig = InTuning.Rig;
	if (!InTuning.Rig.HeadIfOwn.IsNone() && InTuning.Rig.HeadIfOwn != InTuning.Rig.Head)
	{
		const FBodyInstance* OwnHead = InMesh->GetBodyInstance(InTuning.Rig.HeadIfOwn);
		const FReferenceSkeleton& Skeleton = InMesh->GetSkeletalMeshAsset()->GetRefSkeleton();
		const int32 OwnIndex = Skeleton.FindBoneIndex(InTuning.Rig.HeadIfOwn);
		const int32 NeckIndex = Skeleton.FindBoneIndex(InTuning.Rig.Head);
		if (OwnHead && OwnHead->IsValidBodyInstance() && InMesh->GetBoneName(OwnHead->InstanceBoneIndex) == InTuning.Rig.HeadIfOwn &&
			OwnIndex != INDEX_NONE && NeckIndex != INDEX_NONE && Skeleton.BoneIsChildOf(OwnIndex, NeckIndex))
		{
			InTuning.Rig.Head = InTuning.Rig.HeadIfOwn;
		}
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
		if (Entry.Value == EAssistSlot::Feet)
		{
			Assist.Side = Entry.Key == InTuning.Rig.FootLeft ? 0 : 1;
			Assist.StartRotation = Assist.Target.GetRotation();
		}

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

	// The anchors of pelvis, chest and head belong to the support, not to the place of spawn: they are kept in
	// the frame of the feet's anchors, and follow it when a foot is planted elsewhere.
	{
		FVector SupportLocation;
		FQuat SupportRotation;
		SupportFrame(SupportLocation, SupportRotation);
		for (FAssist& Assist : Assists)
		{
			if (Assist.Slot != EAssistSlot::Feet)
			{
				Assist.SupportLocation = SupportRotation.UnrotateVector(Assist.Target.GetLocation() - SupportLocation);
				Assist.SupportRotation = SupportRotation.Inverse() * Assist.Target.GetRotation();
			}
		}

		// Where the centre of mass stands over the feet: what the balance strategies bring it back to.
		TArray<FSandboxBodySample> Bodies;
		FSandboxBodyProbe::SampleBodies(Mesh, Bodies);
		const FVector CenterOfMass = FSandboxBodyProbe::CenterOfMass(Bodies);
		ComSupportLocation = SupportRotation.UnrotateVector(CenterOfMass - SupportLocation);

		// Where the feet stand from each other: what a regrouping step brings them back to.
		FVector FootAt[2] = { SupportLocation, SupportLocation };
		for (const FAssist& Assist : Assists)
		{
			if (Assist.Slot == EAssistSlot::Feet && Assist.Side >= 0)
			{
				FootAt[Assist.Side] = Assist.Target.GetLocation();
			}
		}
		const FVector Apart = SupportRotation.UnrotateVector(FootAt[1] - FootAt[0]);
		StanceRightFromLeft = FVector2D(Apart.X, Apart.Y);

		// How high the pelvis stands above the soles: where a get-up's own rise goes to (getup.rise).
		{
			TArray<FVector2D> Soles;
			const double SolesZ = FSandboxBodyProbe::SupportPolygon(Mesh, TArray<FName>{ InTuning.Rig.FootLeft, InTuning.Rig.FootRight }, InTuning.Balance.SoleToleranceCm, Soles);
			const FAssist* Pelvis = Assists.FindByPredicate([](const FAssist& Assist) { return Assist.Slot == EAssistSlot::Pelvis; });
			StandPelvisHeightCm = Pelvis ? Pelvis->Target.GetLocation().Z - SolesZ : 0.0;
		}

		// A lean about the ankles moves each body in proportion to its height.
		ComHeightCm = FMath::Max(CenterOfMass.Z - SupportLocation.Z, 1.0);
		for (FAssist& Assist : Assists)
		{
			if (Assist.Slot != EAssistSlot::Feet)
			{
				Assist.LeanFactor = (Assist.Target.GetLocation().Z - SupportLocation.Z) / ComHeightCm;
			}
		}
	}

	// The legs the strategies turn: each foot, and the thigh two bones above it.
	{
		const FReferenceSkeleton& Skeleton = Mesh->GetSkeletalMeshAsset()->GetRefSkeleton();
		const FName Feet[2] = { InTuning.Rig.FootLeft, InTuning.Rig.FootRight };
		for (int32 Side = 0; Side < 2; ++Side)
		{
			FootBoneName[Side] = Feet[Side];
			ToeBoneName[Side] = NAME_None;
			for (const FConstraintInstance* Joint : Mesh->Constraints)
			{
				if (Joint && Joint->GetParentBoneName() == Feet[Side])
				{
					ToeBoneName[Side] = Joint->GetChildBoneName();
				}
			}
			FootBoneIndex[Side] = Skeleton.FindBoneIndex(Feet[Side]);
			const int32 Calf = FootBoneIndex[Side] != INDEX_NONE ? Skeleton.GetParentIndex(FootBoneIndex[Side]) : INDEX_NONE;
			ThighBoneIndex[Side] = Calf != INDEX_NONE ? Skeleton.GetParentIndex(Calf) : INDEX_NONE;
			ThighBoneName[Side] = ThighBoneIndex[Side] != INDEX_NONE ? Skeleton.GetBoneName(ThighBoneIndex[Side]) : NAME_None;
			CalfBoneName[Side] = Calf != INDEX_NONE ? Skeleton.GetBoneName(Calf) : NAME_None;
			if (ThighBoneIndex[Side] == INDEX_NONE || !Mesh->GetBodyInstance(ThighBoneName[Side]))
			{
				OutError = FString::Printf(TEXT("Foot '%s' is not the end of a leg with a thigh body"), *Feet[Side].ToString());
				return false;
			}
			// How long the leg is, for a pelvis that has to stay within reach of a planted foot.
			const FVector Hip = FAnimationRuntime::GetComponentSpaceTransformRefPose(Skeleton, ThighBoneIndex[Side]).GetLocation();
			const FVector Knee = FAnimationRuntime::GetComponentSpaceTransformRefPose(Skeleton, Calf).GetLocation();
			const FVector Ankle = FAnimationRuntime::GetComponentSpaceTransformRefPose(Skeleton, FootBoneIndex[Side]).GetLocation();
			HipReferenceCs[Side] = Hip;
			LegLengthCm[Side] = FVector::Dist(Hip, Knee) + FVector::Dist(Knee, Ankle);
			// How high the bone stands over the floor when the foot is flat on it, as spawned.
			TArray<FVector2D> Sole;
			const double SoleZ = FSandboxBodyProbe::SupportPolygon(Mesh, TArray<FName>{ Feet[Side] }, InTuning.Balance.SoleToleranceCm, Sole);
			const FBodyInstance* FootBody = Mesh->GetBodyInstance(Feet[Side]);
			FootStandHeightCm[Side] = FootBody && Sole.Num() > 0 ? FootBody->GetUnrealWorldTransform().GetLocation().Z - SoleZ : 0.0;
		}
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
	StartSpace = Mesh->GetComponentTransform();
	StartSpace.SetScale3D(FVector::OneVector);

	PushTuningToControls();

	// The chain the weight is worked out along: every body after the one it hangs on.
	WeightBodies.Reset();
	WeightBodies.AddDefaulted_GetRef().Bone = InTuning.Rig.Pelvis;
	for (bool bAdded = true; bAdded;)
	{
		bAdded = false;
		for (int32 Index = 0; Index < Muscles.Num(); ++Index)
		{
			const FMuscle& Each = Muscles[Index];
			const int32 Parent = WeightBodies.IndexOfByPredicate([&Each](const FWeightBody& Body) { return Body.Bone == Each.ParentBone; });
			if (Parent != INDEX_NONE && !WeightBodies.ContainsByPredicate([&Each](const FWeightBody& Body) { return Body.Bone == Each.ChildBone; }))
			{
				FWeightBody& Body = WeightBodies.AddDefaulted_GetRef();
				Body.Bone = Each.ChildBone;
				Body.Parent = Parent;
				Body.Muscle = Index;
				bAdded = true;
			}
		}
	}

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

	FingerRig.Init(Mesh->GetSkeletalMeshAsset()->GetRefSkeleton());
	{
		// Where the head faces, in the head body's frame: the mesh faces +y in its reference pose (N5).
		const FReferenceSkeleton& Skeleton = Mesh->GetSkeletalMeshAsset()->GetRefSkeleton();
		const int32 HeadIndex = Skeleton.FindBoneIndex(Tuning.Rig.Head);
		if (HeadIndex != INDEX_NONE)
		{
			const FTransform HeadRef = FAnimationRuntime::GetComponentSpaceTransformRefPose(Skeleton, HeadIndex);
			HeadForwardLocal = HeadRef.GetRotation().UnrotateVector(FVector::YAxisVector);
		}
		// The gaze turns the joint of the bone that carries the head (neck_01 in the NPC's asset), seen from the chest.
		const int32 NeckIndex = Skeleton.FindBoneIndex(TunedRig.Head);
		const int32 ChestIndex = Skeleton.FindBoneIndex(Tuning.Rig.Chest);
		if (NeckIndex != INDEX_NONE && ChestIndex != INDEX_NONE)
		{
			NeckRefRotation = FAnimationRuntime::GetComponentSpaceTransformRefPose(Skeleton, NeckIndex).GetRotation();
			ChestRefRotation = FAnimationRuntime::GetComponentSpaceTransformRefPose(Skeleton, ChestIndex).GetRotation();
			NeckMeshIndex = NeckIndex;
		}
	}

	// The idle's motion, for the bones the muscles of trunk, neck and arms take their targets from: each such
	// joint's own bone and the bones between it and the body it hangs on.
	IdleMotion = FSandboxIdleMotion();
	if (!Tuning.Poses.IdleAnimation.IsEmpty())
	{
		const UAnimSequence* Clip = LoadObject<UAnimSequence>(nullptr, *Tuning.Poses.IdleAnimation);
		if (!Clip)
		{
			OutError = FString::Printf(TEXT("poses.idle.animation: no animation at '%s' (the engine's mannequin comes with Tools\\fetch-stock-content.ps1)"),
				*Tuning.Poses.IdleAnimation);
			return false;
		}
		const FReferenceSkeleton& Skeleton = Mesh->GetSkeletalMeshAsset()->GetRefSkeleton();
		TArray<int32> Bones;
		for (const FMuscle& Muscle : Muscles)
		{
			if (Muscle.Group == ESandboxMuscleGroup::Legs)
			{
				continue;
			}
			const int32 Stop = Skeleton.FindBoneIndex(Muscle.ParentBone);
			for (int32 Bone = Skeleton.FindBoneIndex(Muscle.ChildBone); Bone != INDEX_NONE && Bone != Stop; Bone = Skeleton.GetParentIndex(Bone))
			{
				Bones.AddUnique(Bone);
			}
		}
		if (!IdleMotion.Load(Clip, Mesh, Bones, Tuning.Rig.Pelvis, OutError))
		{
			return false;
		}
		UE_LOG(LogSandbox, Display, TEXT("Body controller: idle motion from '%s', %d bones, a loop of %.2f s"),
			*Tuning.Poses.IdleAnimation, IdleMotion.GetBoneCount(), IdleMotion.GetPeriodS());
	}
	IdleShare = 0.0;
	IdleSwayCm = FVector2D::ZeroVector;

	// A foot the pose places is never turned beyond its ankle's range.
	if (USandboxAnimInstance* Animation = Cast<USandboxAnimInstance>(Mesh->GetAnimInstance()))
	{
		for (int32 Side = 0; Side < 2; ++Side)
		{
			FSandboxJointLimits Limits;
			if (FootBoneIndex[Side] != INDEX_NONE && FSandboxPoseRig::ReadJointLimits(Mesh, FootBoneName[Side], Limits))
			{
				Animation->SetLimbEndLimits(FootBoneIndex[Side], Limits);
			}
		}
	}

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
	// Upright the body holds its stance. Going down, and lying, it holds the pose of its fall, unless the fall does
	// not happen because something holds the body up (UpdateHeld): then it hangs, in its stance's pose again and
	// with the falling body's soft muscles.
	// Getting up, the clip's pose lies over this one (SetLayer): under it the body keeps the pose it lay in.
	// A body that rises by itself from a get-up's crouch (getup.rise) goes over to its stance under the clip's fading layer.
	const bool bDown = (State == ESandboxBodyState::Falling || State == ESandboxBodyState::Lying ||
		(State == ESandboxBodyState::GetUp && !GetUpRun.bRising)) && !bHeldUp;
	FSandboxPose Mirror;
	const FSandboxPose* Pose = &Tuning.Poses.Stance;
	if (bDown)
	{
		switch (FallWay)
		{
		case EFallWay::Back: Pose = &Tuning.Poses.FallBack; break;
		case EFallWay::Forward: Pose = &Tuning.Poses.FallForward; break;
		case EFallWay::Left: Pose = &Tuning.Poses.FallSide; break;
		case EFallWay::Right: Mirror = FSandboxPoseTuning::Mirrored(Tuning.Poses.FallSide); Pose = &Mirror; break;
		}
	}
	TArray<FSandboxPoseBone> Bones;
	FString Error;
	if (!PoseRig.Build(*Pose, Bones, Error))
	{
		UE_LOG(LogSandbox, Error, TEXT("Body controller keeps its pose: %s"), *Error);
		return;
	}
	Animation->SetTargetPose(Bones, bDown ? Tuning.Poses.FallBlendS : Tuning.Poses.BlendS);
}

void USandboxBodyController::ChooseFallWay()
{
	// Where the body goes: the way its pelvis leans, and the way it moves. Either alone can mislead (a body that
	// sinks straight down leans nowhere; one knocked back while it leant forward still leans forward for a moment).
	const FAssist* Pelvis = Assists.FindByPredicate([](const FAssist& Assist) { return Assist.Slot == EAssistSlot::Pelvis; });
	const FBodyInstance* Body = Pelvis ? Mesh->GetBodyInstance(Pelvis->Bone) : nullptr;
	if (!Body || !Body->IsValidBodyInstance())
	{
		return;
	}
	const FQuat Turned = Body->GetUnrealWorldTransform().GetRotation() * Pelvis->Target.GetRotation().Inverse();
	FVector SupportLocation;
	FQuat SupportRotation;
	SupportFrame(SupportLocation, SupportRotation);
	// The body's own forward and left on the floor (it faces +y and its left is +x when it stands unturned).
	FVector Forward = Turned.RotateVector(SupportRotation.RotateVector(FVector(0.0, 1.0, 0.0)));
	Forward.Z = 0.0;
	if (!Forward.Normalize())
	{
		return;
	}
	const FVector Left(Forward.Y, -Forward.X, 0.0);
	const FVector Up = Turned.RotateVector(FVector::UpVector);
	// A lean of 30 degrees counts as much as 1 m/s.
	constexpr double LeanPerMps = 0.5;
	const FVector Way = FVector(Up.X, Up.Y, 0.0) / LeanPerMps + FVector(Balance.Velocity.X, Balance.Velocity.Y, 0.0) * 0.01;
	const double Ahead = FVector::DotProduct(Way, Forward);
	const double ToLeft = FVector::DotProduct(Way, Left);
	FallWay = FMath::Abs(Ahead) >= FMath::Abs(ToLeft) ? (Ahead >= 0.0 ? EFallWay::Forward : EFallWay::Back) : (ToLeft >= 0.0 ? EFallWay::Left : EFallWay::Right);
}

void USandboxBodyController::UpdateHeld(double DeltaTime)
{
	const bool bTouched = bWorldTouched;
	bWorldTouched = false;
	const bool bWasHeld = bHeldUp;
	if (IsUpright() || !Balance.bValid)
	{
		bHeldUp = false;
		HeldWatchS = 0.0;
		HeldLowestCm = Balance.CenterOfMass.Z;
		// A body that stands again takes its stance by the state's own change of pose (EnterState).
		return;
	}
	// The floor under any part of the body, or the centre of mass lower than it has been: the fall goes on, or is
	// over on the floor. A body that swings from a hand goes up and down, but not below the lowest it has been.
	const double HeightCm = Balance.CenterOfMass.Z;
	const bool bComesDown = HeightCm < HeldLowestCm - Tuning.Thresholds.HeldDescentCm;
	HeldLowestCm = FMath::Min(HeldLowestCm, HeightCm);
	if (bTouched || bComesDown)
	{
		HeldWatchS = 0.0;
		bHeldUp = false;
		if (bComesDown)
		{
			HeldLowestCm = HeightCm;
		}
	}
	else
	{
		HeldWatchS += DeltaTime;
		bHeldUp = bHeldUp || (Tuning.Thresholds.HeldTimeS > 0.0 && HeldWatchS >= Tuning.Thresholds.HeldTimeS - KINDA_SMALL_NUMBER);
	}
	if (bHeldUp != bWasHeld)
	{
		if (!bHeldUp)
		{
			ChooseFallWay();
		}
		ApplyPose();
		UE_LOG(LogSandbox, Display, TEXT("Body controller: %s at %.3f s"),
			bHeldUp ? TEXT("held up (nothing touched and no way down): the fall's pose is let go and the body hangs")
				: bTouched ? TEXT("no longer held up (the body touches the world): the fall's pose again") : TEXT("no longer held up (the body comes down): the fall's pose again"),
			ElapsedS);
	}
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
	if (FlinchStartedS >= 0.0 && ElapsedS >= FlinchStartedS && State != ESandboxBodyState::Lying)
	{
		const FSandboxReactionTuning& Reactions = Tuning.Reactions;
		const double Strength = FMath::Clamp(FlinchImpulseNs.Size() / Reactions.FlinchPoseFullNs, 0.0, 1.0);
		const double Left = Reactions.FlinchRecoveryS > 0.0 ? FMath::Clamp(1.0 - (ElapsedS - FlinchStartedS) / Reactions.FlinchRecoveryS, 0.0, 1.0) : 0.0;
		const double Weight = Strength * Left;
		FlinchPoseWeightNow = Weight;
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
	// Anticipation (N5): the flinch's pose before the contact, eased in and out by its own clock. Once the hit's own flinch is on, only
	// what this one has beyond it is added: the two are the same pose, and laid on each other they asked for twice of it.
	const double AnticipateExtra = FMath::SmoothStep(0.0, 1.0, AnticipateWeight) - FlinchPoseWeightNow;
	FlinchPoseWeightNow = 0.0;
	if (AnticipateExtra > 0.0)
	{
		TArray<FSandboxPoseTurn> Drawn;
		PoseRig.BuildTurns(Tuning.Poses.Flinch, AnticipateExtra, Drawn);
		for (const FSandboxPoseTurn& Each : Drawn)
		{
			const int32 Bone = Each.MeshBoneIndex;
			if (FSandboxPoseTurn* Existing = Turns.FindByPredicate([Bone](const FSandboxPoseTurn& Turn) { return Turn.MeshBoneIndex == Bone; }))
			{
				Existing->Turn = (Existing->Turn * Each.Turn).GetNormalized();
			}
			else
			{
				Turns.Add(Each);
			}
		}
	}
	// The gaze (N5): the neck turned toward what the body looks at, as a turn in the component's frame made one of the neck's own.
	if (NeckMeshIndex != INDEX_NONE && (GazeYawDeg != 0.0 || GazePitchDeg != 0.0))
	{
		const FQuat InComponent = (FQuat(FVector::ZAxisVector, FMath::DegreesToRadians(GazeYawDeg)) * FQuat(FVector::XAxisVector, FMath::DegreesToRadians(GazePitchDeg))).GetNormalized();
		const FQuat Look = (NeckRefRotation.Inverse() * InComponent * NeckRefRotation).GetNormalized();
		if (FSandboxPoseTurn* Existing = Turns.FindByPredicate([this](const FSandboxPoseTurn& Turn) { return Turn.MeshBoneIndex == NeckMeshIndex; }))
		{
			Existing->Turn = (Existing->Turn * Look).GetNormalized();
		}
		else
		{
			Turns.Add(FSandboxPoseTurn{ NeckMeshIndex, Look });
		}
	}
	// Doubled over: the pose of it, as much as is on now (FeelFrame), on top of whatever else turns the joints.
	if (DoubledWeight > 0.0)
	{
		TArray<FSandboxPoseTurn> Bend;
		PoseRig.BuildTurns(Tuning.Poses.DoubleOver, FMath::SmoothStep(0.0, 1.0, DoubledWeight), Bend);
		for (const FSandboxPoseTurn& Each : Bend)
		{
			const int32 Bone = Each.MeshBoneIndex;
			if (FSandboxPoseTurn* Existing = Turns.FindByPredicate([Bone](const FSandboxPoseTurn& Turn) { return Turn.MeshBoneIndex == Bone; }))
			{
				Existing->Turn = (Existing->Turn * Each.Turn).GetNormalized();
			}
			else
			{
				Turns.Add(Each);
			}
		}
	}
	// What the body adds for its balance and while it idles is its own, and gives way to a pose asked for from
	// outside (SetClipPose) as that pose comes in.
	const double Posed = IsPosedFromOutside() ? ClipWeight() : 0.0;
	if (Posed <= 0.0)
	{
		// The idle's motion, as much of it as the body carries now (UpdateIdle).
		if (IdleShare > 0.0)
		{
			IdleMotion.AddTurns(ElapsedS, FMath::SmoothStep(0.0, 1.0, IdleShare) * Tuning.Poses.IdleWeight, Turns);
		}
		AddArmTurns(Turns);
		AddStrategyTurns(Turns);
	}
	else if (Posed < 1.0)
	{
		TArray<FSandboxPoseTurn> Own;
		if (IdleShare > 0.0)
		{
			IdleMotion.AddTurns(ElapsedS, FMath::SmoothStep(0.0, 1.0, IdleShare) * Tuning.Poses.IdleWeight, Own);
		}
		AddArmTurns(Own);
		AddStrategyTurns(Own);
		for (const FSandboxPoseTurn& Each : Own)
		{
			const FQuat Left = FQuat::MakeFromRotationVector(Each.Turn.ToRotationVector() * (1.0 - Posed));
			const int32 Bone = Each.MeshBoneIndex;
			if (FSandboxPoseTurn* Existing = Turns.FindByPredicate([Bone](const FSandboxPoseTurn& Turn) { return Turn.MeshBoneIndex == Bone; }))
			{
				Existing->Turn = (Existing->Turn * Left).GetNormalized();
			}
			else
			{
				Turns.Add(FSandboxPoseTurn{ Bone, Left.GetNormalized() });
			}
		}
	}

	// N7 (getup.rise_lean_s): a rising body brings its centre of mass over its feet as the hip strategy does standing: the trunk bends
	// toward where it should be, each thigh staying while the pelvis turns, so relative to the pelvis the thigh turns back. (By the ankles,
	// as the ankle strategy leans, it tipped over its toes: a centre of mass ahead of the toes is beyond what a foot can push back.)
	if (!RiseLeanRad.IsZero())
	{
		const FVector Toward(RiseLeanRad.GetSafeNormal(), 0.0);
		const FVector Axis = FVector::CrossProduct(FVector::UpVector, Toward).GetSafeNormal();
		const FQuat Trunk = Axis.IsNearlyZero() ? FQuat::Identity : FQuat(Axis, RiseLeanRad.Size());
		for (int32 Side = 0; Side < 2; ++Side)
		{
			const FBodyInstance* Thigh = Mesh->GetBodyInstance(ThighBoneName[Side]);
			const int32 Bone = ThighBoneIndex[Side];
			if (!Thigh || !Thigh->IsValidBodyInstance() || Bone == INDEX_NONE)
			{
				continue;
			}
			const FQuat ThighWorld = Thigh->GetUnrealWorldTransform().GetRotation();
			const FQuat Turn = (ThighWorld.Inverse() * Trunk.Inverse() * ThighWorld).GetNormalized();
			if (FSandboxPoseTurn* Existing = Turns.FindByPredicate([Bone](const FSandboxPoseTurn& Each) { return Each.MeshBoneIndex == Bone; }))
			{
				Existing->Turn = (Existing->Turn * Turn).GetNormalized();
			}
			else
			{
				Turns.Add(FSandboxPoseTurn{ Bone, Turn });
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

void USandboxBodyController::UpdateFingers(double DeltaTime)
{
	USandboxAnimInstance* Animation = Cast<USandboxAnimInstance>(Mesh->GetAnimInstance());
	const FSandboxFingerTuning& Fingers = Tuning.Poses.Fingers;
	if (!Animation || !FingerRig.IsValid())
	{
		return;
	}
	if (Fingers.Weight <= 0.0)
	{
		if (bFingersSet)
		{
			Animation->SetFingerTurns(TArray<FSandboxPoseTurn>());
			bFingersSet = false;
			FMemory::Memzero(FingerShare);
		}
		return;
	}
	static const FName HandBones[2] = { TEXT("hand_l"), TEXT("hand_r") };
	TArray<FSandboxPoseTurn> Turns;
	for (int32 Side = 0; Side < 2; ++Side)
	{
		// What the body does: at rest the hand is relaxed; a body that braces has it half closed; one that goes down has
		// the fingers spread; a hand that the world pushes on lies flat.
		double Goal[4] = { 1.0, 0.0, 0.0, 0.0 };
		const FBodyInstance* Hand = Mesh->GetBodyInstance(HandBones[Side]);
		const bool bCarries = Hand && SupportImpulseNs.IsValidIndex(Hand->InstanceBodyIndex) && !SupportImpulseNs[Hand->InstanceBodyIndex].IsZero();
		if (State == ESandboxBodyState::Falling && !bHeldUp)
		{
			Goal[0] = 0.0;
			Goal[2] = 1.0;
		}
		else if (bCarries && State != ESandboxBodyState::Lying)
		{
			Goal[0] = 0.0;
			Goal[3] = 1.0;
		}
		else if (State == ESandboxBodyState::Flinch || State == ESandboxBodyState::Stagger)
		{
			Goal[0] = 0.5;
			Goal[1] = 0.5;
		}
		// Toward it with a lag: no jump, whichever pose is asked for and however often that changes.
		const bool bFirst = !bFingersSet;
		const double Follow = bFirst || Fingers.BlendS <= 0.0 ? 1.0 : 1.0 - FMath::Exp(-3.0 * DeltaTime / Fingers.BlendS);
		for (int32 Pose = 0; Pose < 4; ++Pose)
		{
			FingerShare[Side][Pose] += (Goal[Pose] - FingerShare[Side][Pose]) * Follow;
		}
		const TPair<const FSandboxFingerPose*, double> Shares[] = {
			TPair<const FSandboxFingerPose*, double>(&Fingers.Rest, FingerShare[Side][0] * Fingers.Weight),
			TPair<const FSandboxFingerPose*, double>(&Fingers.Fist, FingerShare[Side][1] * Fingers.Weight),
			TPair<const FSandboxFingerPose*, double>(&Fingers.Splayed, FingerShare[Side][2] * Fingers.Weight),
			TPair<const FSandboxFingerPose*, double>(&Fingers.Flat, FingerShare[Side][3] * Fingers.Weight) };
		FingerRig.BuildTurns(Side, FSandboxFingerRig::Blend(Shares), Turns);
	}
	Animation->SetFingerTurns(Turns);
	bFingersSet = true;
}

bool USandboxBodyController::SetClipPose(const FString& ClipPath, double TimeS, double Rate, FString& OutError)
{
	if (State == ESandboxBodyState::GetUp)
	{
		GiveUpGetUp(TEXT("a pose was asked for from outside"));
	}
	if (!HoldClip(ClipPath, TimeS, Rate, OutError))
	{
		return false;
	}
	ClipHold.bOutside = true;
	return true;
}

const FSandboxClipPose* USandboxBodyController::FindOrReadClip(const FString& ClipPath, FString& OutPath, FString& OutError)
{
	USandboxAnimInstance* Animation = bStarted ? Cast<USandboxAnimInstance>(Mesh->GetAnimInstance()) : nullptr;
	if (!Animation)
	{
		OutError = TEXT("The body is not started, or its mesh has no sandbox animation instance");
		return nullptr;
	}
	// "/Game/Folder/Clip" is the object "/Game/Folder/Clip.Clip".
	FString Path = ClipPath;
	int32 Slash = INDEX_NONE;
	if (!Path.Contains(TEXT(".")) && Path.FindLastChar(TEXT('/'), Slash))
	{
		Path += TEXT(".") + Path.Mid(Slash + 1);
	}
	if (!Clips.Contains(Path))
	{
		const UAnimSequence* Sequence = LoadObject<UAnimSequence>(nullptr, *Path);
		if (!Sequence)
		{
			OutError = FString::Printf(TEXT("no animation at '%s' (the clips of Epic's Game Animation Sample come with Tools\\fetch-sample-anims.ps1)"), *Path);
			return nullptr;
		}
		// Every joint the muscles hold. The body of the head takes the head's own orientation; arms and legs point
		// where the clip's point.
		TArray<FSandboxClipJoint> Joints;
		const FReferenceSkeleton& Skeleton = Mesh->GetSkeletalMeshAsset()->GetRefSkeleton();
		static const FName HeadBone(TEXT("head"));
		for (const FMuscle& Muscle : Muscles)
		{
			FSandboxClipJoint& Joint = Joints.AddDefaulted_GetRef();
			Joint.ChildBone = Muscle.ChildBone;
			Joint.ParentBone = Muscle.ParentBone;
			Joint.bAlignDirection = Muscle.Group == ESandboxMuscleGroup::Arms || Muscle.Group == ESandboxMuscleGroup::Legs;
			if (Muscle.ChildBone == Tuning.Rig.Head && Muscle.ChildBone != HeadBone)
			{
				// Only a head bone that this body carries: below its bone, with no body of its own.
				for (int32 Bone = Skeleton.FindBoneIndex(HeadBone); Bone != INDEX_NONE; Bone = Skeleton.GetParentIndex(Bone))
				{
					const FName Name = Skeleton.GetBoneName(Bone);
					if (Name == Muscle.ChildBone)
					{
						Joint.OrientationBone = HeadBone;
						break;
					}
					if (Mesh->GetBodyInstance(Name))
					{
						break;
					}
				}
			}
		}
		FSandboxClipPose Clip;
		if (!Clip.Load(Sequence, Mesh, Tuning.Rig.Pelvis, Joints, OutError))
		{
			return nullptr;
		}
		UE_LOG(LogSandbox, Display, TEXT("Body controller: clip '%s' read, %d keys over %.2f s"), *Path, Clip.GetKeyCount(), Clip.GetLengthS());
		Clips.Add(Path, MoveTemp(Clip));
	}
	OutPath = Path;
	return Clips.Find(Path);
}

const FSandboxClipPose* USandboxBodyController::ReadClip(const FString& ClipPath, FString& OutError)
{
	FString Path;
	return FindOrReadClip(ClipPath, Path, OutError);
}

void USandboxBodyController::GetMuscleJoints(TArray<TPair<FName, FName>>& OutJoints) const
{
	OutJoints.Reset();
	for (const FMuscle& Muscle : Muscles)
	{
		OutJoints.Emplace(Muscle.ChildBone, Muscle.ParentBone);
	}
}

bool USandboxBodyController::HoldClip(const FString& ClipPath, double TimeS, double Rate, FString& OutError)
{
	FString Path;
	const FSandboxClipPose* Read = FindOrReadClip(ClipPath, Path, OutError);
	if (!Read)
	{
		return false;
	}
	const FSandboxClipPose& Clip = *Read;
	if (!ClipHold.Path.IsEmpty() && ClipHold.Last.Num() > 0)
	{
		// From the clip pose the target has now, so that it does not jump.
		ClipHold.From = ClipHold.Last;
		ClipHold.FromPelvis = ClipHold.LastPelvis;
		ClipHold.CrossShare = 0.0;
	}
	else
	{
		ClipHold = FClipHold();
	}
	ClipHold.Path = Path;
	ClipHold.TimeS = FMath::Clamp(TimeS, 0.0, Clip.GetLengthS());
	ClipHold.Rate = Rate;
	ClipHold.ShareCap = 1.0;
	ClipHold.bReleased = false;
	ClipHold.bOutside = false;
	if (ClipHold.CrossShare >= 1.0)
	{
		ClipHold.LastPelvis = Clip.PelvisAt(ClipHold.TimeS);
	}
	UE_LOG(LogSandbox, Display, TEXT("Body controller: the pose of clip '%s' at %.2f s is asked for at %.3f s (rate %.2f)"), *Path, ClipHold.TimeS, ElapsedS, Rate);
	return true;
}

void USandboxBodyController::ReleaseClipPose()
{
	if (!ClipHold.Path.IsEmpty() && !ClipHold.bReleased)
	{
		ClipHold.bReleased = true;
		ClipHold.Rate = 0.0;
		UE_LOG(LogSandbox, Display, TEXT("Body controller: the clip's pose is let go at %.3f s"), ElapsedS);
	}
}

bool USandboxBodyController::GetClipPose(FString& OutClipPath, double& OutTimeS, double& OutWeight) const
{
	OutClipPath = ClipHold.Path;
	OutTimeS = ClipHold.TimeS;
	OutWeight = ClipWeight();
	return !ClipHold.Path.IsEmpty();
}

bool USandboxBodyController::GetClipPelvis(FTransform& OutPelvis, double& OutWeight) const
{
	OutPelvis = ClipHold.LastPelvis;
	OutWeight = ClipWeight();
	return !ClipHold.Path.IsEmpty();
}

void USandboxBodyController::UpdateClipPose(double DeltaTime)
{
	if (ClipHold.Path.IsEmpty())
	{
		return;
	}
	USandboxAnimInstance* Animation = Cast<USandboxAnimInstance>(Mesh->GetAnimInstance());
	const FSandboxClipPose* Clip = Clips.Find(ClipHold.Path);
	if (!Animation || !Clip)
	{
		ClipHold = FClipHold();
		return;
	}
	const double Step = Tuning.Poses.BlendS > 0.0 ? DeltaTime / Tuning.Poses.BlendS : 1.0;
	ClipHold.Share = FMath::Clamp(ClipHold.Share + (ClipHold.bReleased ? -Step : Step), 0.0, ClipHold.bReleased ? 1.0 : ClipHold.ShareCap);
	ClipHold.CrossShare = FMath::Min(ClipHold.CrossShare + Step, 1.0);
	if (ClipHold.bReleased && ClipHold.Share <= 0.0)
	{
		Animation->SetLayer(TArray<FSandboxPoseBone>(), 0.0);
		ClipHold = FClipHold();
		return;
	}
	ClipHold.TimeS = FMath::Clamp(ClipHold.TimeS + ClipHold.Rate * DeltaTime, 0.0, Clip->GetLengthS());
	TArray<FSandboxPoseBone> Bones;
	Clip->Evaluate(ClipHold.TimeS, Bones);
	ClipHold.LastPelvis = Clip->PelvisAt(ClipHold.TimeS);
	if (ClipHold.CrossShare < 1.0 && ClipHold.From.Num() == Bones.Num())
	{
		const double Over = FMath::SmoothStep(0.0, 1.0, ClipHold.CrossShare);
		ClipHold.LastPelvis = FTransform(FQuat::Slerp(ClipHold.FromPelvis.GetRotation(), ClipHold.LastPelvis.GetRotation(), Over).GetNormalized(),
			FMath::Lerp(ClipHold.FromPelvis.GetLocation(), ClipHold.LastPelvis.GetLocation(), Over));
		for (int32 Index = 0; Index < Bones.Num(); ++Index)
		{
			Bones[Index].Rotation = FQuat::Slerp(ClipHold.From[Index].Rotation, Bones[Index].Rotation, Over).GetNormalized();
		}
	}
	Animation->SetLayer(Bones, ClipWeight());
	ClipHold.Last = MoveTemp(Bones);
}

void USandboxBodyController::UpdateIdle(double DeltaTime)
{
	if (!IdleMotion.IsLoaded())
	{
		return;
	}
	const double Step = Tuning.Poses.BlendS > 0.0 ? DeltaTime / Tuning.Poses.BlendS : 1.0;
	IdleShare = FMath::Clamp(IdleShare + (State == ESandboxBodyState::Standing ? Step : -Step), 0.0, 1.0);
	const double Weight = FMath::SmoothStep(0.0, 1.0, IdleShare) * Tuning.Poses.IdleSwayWeight;
	if (Weight <= 0.0)
	{
		IdleSwayCm = FVector2D::ZeroVector;
		return;
	}
	// The clip's sway is in the body's own space; the body stands as its support does.
	FVector SupportLocation;
	FQuat SupportRotation;
	SupportFrame(SupportLocation, SupportRotation);
	const FVector2D Sway = IdleMotion.SwayCm(ElapsedS) * Weight;
	const FVector World = SupportRotation.RotateVector(FVector(Sway.X, Sway.Y, 0.0));
	IdleSwayCm = FVector2D(World.X, World.Y);
}

void USandboxBodyController::UpdateStrategies(double DeltaTime)
{
	// Mechanics first (N4): the goals of the strategies, the arms' and the lean's stay what they were at the hit.
	if (InMechanics())
	{
		return;
	}
	const FSandboxBalanceTuning& Limits = Tuning.Balance;
	const bool bUpright = IsUpright();
	FVector2D LeanGoal = FVector2D::ZeroVector;
	FVector2D HipGoal = FVector2D::ZeroVector;
	double ArmGoal[ArmWayCount] = { 0.0, 0.0, 0.0, 0.0 };
	FVector2D ShiftGoal = FVector2D::ZeroVector;
	StrategyErrorCm = FVector2D::ZeroVector;
	const int32 Feet = Balance.bValid ? Balance.StandingFeet.Num() : 0;

	// Leaning into a held push: the centre of mass goes F h / (m g) toward the force, so that the centre of
	// pressure can stay in the middle of the feet. It follows the sustained force with the strategies' lag,
	// and lets go of the lean the same way.
	FVector2D LeanGoal2 = FVector2D::ZeroVector;
	if (bUpright && WeightN > 0.0)
	{
		LeanGoal2 = -SustainedMomentNCm / WeightN;
		if (LeanGoal2.Size() > Limits.LeanMaxCm)
		{
			LeanGoal2 = LeanGoal2.GetSafeNormal() * Limits.LeanMaxCm;
		}
	}
	{
		const double LeanFollow = Limits.ResponseS > 0.0 ? 1.0 - FMath::Exp(-DeltaTime / Limits.ResponseS) : 1.0;
		LeanCm += (LeanGoal2 - LeanCm) * LeanFollow;
		if (LeanGoal2.IsZero() && LeanCm.Size() < 1.0e-6)
		{
			LeanCm = FVector2D::ZeroVector;
		}
	}
	// The idle's sway is a lean like the one into a push, and goes the same way: anchors, reference and ankles.
	const FVector2D LeaningCm = IdleSwayCm.IsZero() ? LeanCm : LeanCm + IdleSwayCm;
	// The assist leans with the body rather than holding it upright against the push.
	for (FAssist& Assist : Assists)
	{
		if (Assist.Slot != EAssistSlot::Feet && Assist.AppliedLeanCm != LeaningCm)
		{
			Assist.AppliedLeanCm = LeaningCm;
			SetAssistTarget(Assist);
		}
	}

	if (bUpright && Feet > 0)
	{
		FVector SupportLocation;
		FQuat SupportRotation;
		SupportFrame(SupportLocation, SupportRotation);
		SupportForward = SupportRotation.RotateVector(FVector(0.0, 1.0, 0.0));
		SupportSide = SupportRotation.RotateVector(FVector(1.0, 0.0, 0.0));
		const FVector Reference = SupportLocation + SupportRotation.RotateVector(ComSupportLocation);
		// A weight shift moves the place the body is to stand over, like a lean. (Left out, the ankles brought the
		// body back toward the middle of its feet while the legs' frames took it to the stance foot: of a shift of
		// 19 cm forward out of a lunge the pelvis went 5.)
		StrategyErrorCm = Balance.CapturePoint - (FVector2D(Reference.X, Reference.Y) + LeaningCm + ShiftCm);
		// The error in the support's own axes: forward, sideways.
		const FVector2D Error(
			StrategyErrorCm.X * SupportForward.X + StrategyErrorCm.Y * SupportForward.Y,
			StrategyErrorCm.X * SupportSide.X + StrategyErrorCm.Y * SupportSide.Y);

		// Ankles: the centre of pressure moves toward the capture point by the gain, plus a slow part that takes
		// up a steady error. Pressing the toes down by a torque tau moves it by tau / weight; the legs' muscles
		// give that torque when their target turns by tau / stiffness.
		// Only while standing: in a flinch or a stagger the assist does not hold the pelvis over its anchor, and the
		// slow part wound up to its limit on an offset that was no posture's (5 cm after a push to the pelvis); back
		// to standing, it drove the body over the middle of its feet at 0.6 m/s and out the other side.
		if (Limits.AnkleIntegralS > 0.0)
		{
			if (State == ESandboxBodyState::Standing)
			{
				// The slow part keeps the pelvis over the place it stands on, which is where the assist's anchor
				// holds it: whatever steady offset the posture settles into under its weight is taken up by the
				// ankles instead of by the assist.
				FVector2D PelvisErrorCm = FVector2D::ZeroVector;
				if (const FAssist* Pelvis = Assists.FindByPredicate([](const FAssist& Assist) { return Assist.Slot == EAssistSlot::Pelvis; }))
				{
					const FBodyInstance* Body = Mesh->GetBodyInstance(Pelvis->Bone);
					if (Body && Body->IsValidBodyInstance())
					{
						const FVector Offset = Body->GetCOMPosition() - AssistGoal(*Pelvis).TransformPositionNoScale(Body->GetMassSpaceLocal().GetLocation());
						PelvisErrorCm = FVector2D(Offset.X * SupportForward.X + Offset.Y * SupportForward.Y, Offset.X * SupportSide.X + Offset.Y * SupportSide.Y);
					}
				}
				AnkleIntegralCm += PelvisErrorCm * (DeltaTime / Limits.AnkleIntegralS);
				if (AnkleIntegralCm.Size() > Limits.AnkleIntegralMaxCm)
				{
					AnkleIntegralCm = AnkleIntegralCm.GetSafeNormal() * Limits.AnkleIntegralMaxCm;
				}
			}
		}
		else
		{
			AnkleIntegralCm = FVector2D::ZeroVector;
		}
		const double StiffnessNmPerRad = Tuning.Legs.StiffnessNmPerRad * MuscleScale(ESandboxMuscleGroup::Legs) * Feet;
		if (StiffnessNmPerRad > 0.0)
		{
			const FVector2D ShiftM = (FVector2D(Error.X * Limits.AnkleGain, Error.Y * Limits.AnkleSideGain) + AnkleIntegralCm) * 0.01;
			// Leaning into a held push the body tilts about its ankles as a whole: their targets turn by the
			// lean's angle, so that the muscles hold the lean instead of pulling the body back upright (the
			// push's torque and the lean's weight cancel at the ankles). A lean forward turns the body away from
			// a centre of pressure behind it, hence the sign.
			const FVector2D Lean(LeaningCm.X * SupportForward.X + LeaningCm.Y * SupportForward.Y, LeaningCm.X * SupportSide.X + LeaningCm.Y * SupportSide.Y);
			const FVector2D TiltRad = -Lean / ComHeightCm;
			const double MaxRad = FMath::DegreesToRadians(Limits.AnkleMaxDeg);
			if (Tuning.Weight.AnkleAsTorque > 0.5 && IsPushByPlace())
			{
				// The shift as a place the floor pushes at (UpdateWeight); the targets turn by the lean alone.
				LeanGoal.X = FMath::Clamp(TiltRad.X, -MaxRad, MaxRad);
				LeanGoal.Y = FMath::Clamp(TiltRad.Y, -MaxRad, MaxRad);
				ShiftGoal = (FVector2D(SupportForward.X, SupportForward.Y) * ShiftM.X + FVector2D(SupportSide.X, SupportSide.Y) * ShiftM.Y) * 100.0;
			}
			else
			{
				LeanGoal.X = FMath::Clamp(ShiftM.X * WeightN / StiffnessNmPerRad + TiltRad.X, -MaxRad, MaxRad);
				LeanGoal.Y = FMath::Clamp(ShiftM.Y * WeightN / StiffnessNmPerRad + TiltRad.Y, -MaxRad, MaxRad);
			}
		}

		// Arms: thrown the way the body tips, in proportion to how far the capture point has got from where the body
		// stands (poses.balance_*). Nothing inside the dead zone, where quiet standing and the idle's sway stay.
		if (Limits.ArmFullCm > Limits.ArmDeadCm)
		{
			const auto Share = [&Limits](double Cm) { return FMath::Clamp((Cm - Limits.ArmDeadCm) / (Limits.ArmFullCm - Limits.ArmDeadCm), 0.0, 1.0); };
			ArmGoal[ArmForward] = Share(Error.X);
			ArmGoal[ArmBack] = Share(-Error.X);
			ArmGoal[ArmLeft] = Share(Error.Y);
			ArmGoal[ArmRight] = Share(-Error.Y);
		}

		// Hips: once the capture point is outside the support, where the ankles cannot help, the trunk bends
		// toward it, in proportion to how far out it is. Forward and back only: bending sideways is not how a
		// person catches a fall to the side.
		const double OutsideCm = FMath::Max(-LastDecisionMarginCm, 0.0);
		if (OutsideCm > 0.0 && Limits.HipDegPerCm > 0.0 && Error.X != 0.0)
		{
			const double BendRad = FMath::DegreesToRadians(FMath::Min(OutsideCm * Limits.HipDegPerCm, Limits.HipMaxDeg));
			HipGoal = FVector2D(FMath::Sign(Error.X) * BendRad, 0.0);
		}
	}
	else
	{
		AnkleIntegralCm = FVector2D::ZeroVector;
	}

	// Follow, never jump: a first-order lag toward the goal, also on the way out when the body falls.
	const double Follow = Limits.ResponseS > 0.0 ? 1.0 - FMath::Exp(-DeltaTime / Limits.ResponseS) : 1.0;
	AnkleLeanRad += (LeanGoal - AnkleLeanRad) * Follow;
	HipBendRad += (HipGoal - HipBendRad) * Follow;
	StrategyShiftCm += (ShiftGoal - StrategyShiftCm) * Follow;
	// A turn too small to matter is no turn: a body without strategies carries none, and its run is unchanged.
	constexpr double NegligibleRad = 1.0e-7;
	if (LeanGoal.IsZero() && AnkleLeanRad.Size() < NegligibleRad)
	{
		AnkleLeanRad = FVector2D::ZeroVector;
	}
	if (HipGoal.IsZero() && HipBendRad.Size() < NegligibleRad)
	{
		HipBendRad = FVector2D::ZeroVector;
	}
	bStrategyActive = !AnkleLeanRad.IsZero() || !HipBendRad.IsZero();

	// The arms follow the same way, with a lag of their own, and come down again as the balance comes back.
	const double ArmFollow = Limits.ArmResponseS > 0.0 ? 1.0 - FMath::Exp(-DeltaTime / Limits.ArmResponseS) : 1.0;
	for (int32 Way = 0; Way < ArmWayCount; ++Way)
	{
		ArmShare[Way] += (ArmGoal[Way] - ArmShare[Way]) * ArmFollow;
		if (ArmGoal[Way] == 0.0 && ArmShare[Way] < 1.0e-6)
		{
			ArmShare[Way] = 0.0;
		}
	}
}

void USandboxBodyController::AddArmTurns(TArray<FSandboxPoseTurn>& Turns) const
{
	if (ArmShare[ArmBack] == 0.0 && ArmShare[ArmForward] == 0.0 && ArmShare[ArmLeft] == 0.0 && ArmShare[ArmRight] == 0.0)
	{
		return;
	}
	const FSandboxPose ToRight = ArmShare[ArmRight] > 0.0 ? FSandboxPoseTuning::Mirrored(Tuning.Poses.BalanceSide) : FSandboxPose();
	const FSandboxPose* const Poses[ArmWayCount] = { &Tuning.Poses.BalanceBack, &Tuning.Poses.BalanceForward, &Tuning.Poses.BalanceSide, &ToRight };
	TArray<FSandboxPoseTurn> Added;
	for (int32 Way = 0; Way < ArmWayCount; ++Way)
	{
		if (ArmShare[Way] <= 0.0)
		{
			continue;
		}
		PoseRig.BuildTurns(*Poses[Way], ArmShare[Way], Added);
		for (const FSandboxPoseTurn& Turn : Added)
		{
			const int32 Bone = Turn.MeshBoneIndex;
			if (FSandboxPoseTurn* Existing = Turns.FindByPredicate([Bone](const FSandboxPoseTurn& Each) { return Each.MeshBoneIndex == Bone; }))
			{
				Existing->Turn = (Existing->Turn * Turn.Turn).GetNormalized();
			}
			else
			{
				Turns.Add(Turn);
			}
		}
	}
}

void USandboxBodyController::AddStrategyTurns(TArray<FSandboxPoseTurn>& Turns) const
{
	if (!bStrategyActive)
	{
		return;
	}
	// The turns are rotations of the world as the bodies are now, carried into each bone's own space.
	const auto Rotation = [](const FVector& Toward, double Angle)
	{
		// Turns the up direction toward Toward by Angle (radians).
		const FVector Axis = FVector::CrossProduct(FVector::UpVector, Toward).GetSafeNormal();
		return Axis.IsNearlyZero() || Angle == 0.0 ? FQuat::Identity : FQuat(Axis, Angle);
	};
	const auto AddTurn = [&Turns](int32 BoneIndex, const FQuat& Turn)
	{
		if (BoneIndex == INDEX_NONE || Turn.Equals(FQuat::Identity, 0.0))
		{
			return;
		}
		FSandboxPoseTurn* Existing = Turns.FindByPredicate([BoneIndex](const FSandboxPoseTurn& Each) { return Each.MeshBoneIndex == BoneIndex; });
		if (Existing)
		{
			Existing->Turn = (Existing->Turn * Turn).GetNormalized();
		}
		else
		{
			Turns.Add(FSandboxPoseTurn{ BoneIndex, Turn.GetNormalized() });
		}
	};

	// The ankles: the body above the feet leans away from where the centre of pressure is to go, forward
	// as one piece, sideways as a parallelogram (the legs lean, the pelvis stays level).
	const FVector LeanForward = SupportForward * AnkleLeanRad.X;
	const FVector LeanSide = SupportSide * AnkleLeanRad.Y;
	const FQuat Forward = Rotation(-LeanForward.GetSafeNormal(), LeanForward.Size());
	const FQuat Sideways = Rotation(-LeanSide.GetSafeNormal(), LeanSide.Size());
	const FQuat Legs = Sideways * Forward;
	// The hips: the trunk bends toward the capture point.
	const FVector Bend = SupportForward * HipBendRad.X + SupportSide * HipBendRad.Y;
	const FQuat Trunk = Rotation(Bend.GetSafeNormal(), Bend.Size());

	// Both legs turn alike. (Turning each in proportion to the load it carries was tried: the less loaded leg
	// turned less, the body rolled onto the other, and under a held push it rocked from foot to foot.)
	for (int32 Side = 0; Side < 2; ++Side)
	{
		const FBodyInstance* Foot = Mesh->GetBodyInstance(FootBoneName[Side]);
		const FBodyInstance* Thigh = Mesh->GetBodyInstance(ThighBoneName[Side]);
		if (!Foot || !Thigh || !Foot->IsValidBodyInstance() || !Thigh->IsValidBodyInstance())
		{
			continue;
		}
		// The foot stays on the floor while everything above it turns by Legs: relative to its calf it turns back.
		const FQuat FootWorld = Foot->GetUnrealWorldTransform().GetRotation();
		AddTurn(FootBoneIndex[Side], FootWorld.Inverse() * Legs.Inverse() * FootWorld);
		// The thigh stays while the trunk bends: relative to the pelvis it turns back.
		const FQuat ThighWorld = Thigh->GetUnrealWorldTransform().GetRotation();
		AddTurn(ThighBoneIndex[Side], ThighWorld.Inverse() * Trunk.Inverse() * ThighWorld);
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

double USandboxBodyController::GetUpEase() const
{
	if (State != ESandboxBodyState::GetUp || Tuning.GetUp.EaseS <= 0.0)
	{
		return 1.0;
	}
	return FMath::SmoothStep(0.0, 1.0, (ElapsedS - StateEnteredS) / Tuning.GetUp.EaseS);
}

bool USandboxBodyController::IsAtRest() const
{
	// As a falling body is judged (UpdateState): the root mean square over the bodies, weighted by mass.
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
	return MassKg > 0.0 && FMath::Sqrt(LinearSquared / MassKg) <= Tuning.Thresholds.RestSpeedMps &&
		FMath::Sqrt(AngularSquared / MassKg) <= Tuning.Thresholds.RestAngularSpeedRadS;
}

bool USandboxBodyController::StartGetUp(FString& OutError)
{
	FAssist* Pelvis = Assists.FindByPredicate([](const FAssist& Assist) { return Assist.Slot == EAssistSlot::Pelvis; });
	const FBodyInstance* Body = Pelvis ? Mesh->GetBodyInstance(Pelvis->Bone) : nullptr;
	if (!Body || !Body->IsValidBodyInstance())
	{
		OutError = TEXT("the pelvis has no body");
		return false;
	}
	const FTransform Now = Body->GetUnrealWorldTransform();
	// How the pelvis lies is where the world's up points in the pelvis's own space; each clip starts with a pelvis
	// that lies some way, and the clip that starts most like the body lies is the one to get up by.
	const FVector UpInPelvis = Now.GetRotation().UnrotateVector(FVector::UpVector);
	// N7 (getup.rise): from the back the body follows a get-up to a crouch, and rises from it by itself.
	const bool bRiseBack = Tuning.GetUp.Rise > 0.5 && !Tuning.GetUp.RiseClipBack.IsEmpty();
	const FString* const Paths[] = { bRiseBack ? &Tuning.GetUp.RiseClipBack : &Tuning.GetUp.ClipBack, &Tuning.GetUp.ClipFront, &Tuning.GetUp.ClipLeft, &Tuning.GetUp.ClipRight };
	const FSandboxClipPose* Best = nullptr;
	FString BestPath;
	double BestLikeness = -2.0;
	const FString* BestOf = nullptr;
	// After an attempt that was given up the body goes another way, if another clip starts at all like it lies:
	// the same clip from the same place fails the same way (six times in a row, on the stand).
	constexpr double OtherWayLikeness = 0.0;
	for (int32 Pass = 0; Pass < 2 && !Best; ++Pass)
	{
		for (const FString* Path : Paths)
		{
			if (Path->IsEmpty())
			{
				continue;
			}
			FString Kept;
			const FSandboxClipPose* Clip = FindOrReadClip(*Path, Kept, OutError);
			if (!Clip)
			{
				return false;
			}
			const FQuat Unturned = StartSpace.GetRotation() * Clip->PelvisAt(0.0).GetRotation();
			const double Likeness = FVector::DotProduct(Unturned.UnrotateVector(FVector::UpVector), UpInPelvis);
			if (Pass == 0 && (Kept == GivenUpClip || Likeness < OtherWayLikeness) && !GivenUpClip.IsEmpty())
			{
				continue;
			}
			if (Likeness > BestLikeness)
			{
				BestLikeness = Likeness;
				Best = Clip;
				BestPath = Kept;
				BestOf = Path;
			}
		}
	}
	if (!Best)
	{
		OutError = TEXT("no clip to get up by (getup.clip_*)");
		return false;
	}

	// The clip's root in the world: turned about the vertical to how the body lies, with the clip's pelvis over
	// the body's, on the floor the body last stood on.
	const FTransform First = Best->PelvisAt(0.0);
	FQuat Tilt, Yaw;
	(Now.GetRotation() * (StartSpace.GetRotation() * First.GetRotation()).Inverse()).ToSwingTwist(FVector::UpVector, Tilt, Yaw);
	FTransform Frame((Yaw * StartSpace.GetRotation()).GetNormalized(), FVector::ZeroVector);
	const FVector Offset = Frame.GetRotation().RotateVector(First.GetLocation());
	Frame.SetLocation(FVector(Now.GetLocation().X - Offset.X, Now.GetLocation().Y - Offset.Y, LastSupportHeightCm));

	if (!HoldClip(BestPath, 0.0, 0.0, OutError))
	{
		return false;
	}
	// At once and whole: it is the strength of the muscles that comes up from nothing, not the pose that comes in.
	ClipHold.Share = 1.0;
	ClipHold.CrossShare = 1.0;
	GetUpRun = FGetUpRun();
	GetUpRun.Path = BestPath;
	GetUpRun.Frame = Frame;
	if (bRiseBack && BestOf == &Tuning.GetUp.RiseClipBack)
	{
		GetUpRun.RiseAtS = FMath::Min(Tuning.GetUp.RiseAtBackS, Best->GetLengthS());
	}
	if (Tuning.GetUp.FastRise > 0.5)
	{
		// N7 (getup.fast_rise): the rise begins where the clip's hands leave the floor for good.
		static const FName HandBones[2] = { FName(TEXT("hand_l")), FName(TEXT("hand_r")) };
		const double ClipEndS = FMath::Min(Tuning.GetUp.EndS, Best->GetLengthS());
		constexpr double StepS = 1.0 / 60.0;
		for (int32 Index = 0; Index * StepS <= ClipEndS; ++Index)
		{
			const double TimeS = Index * StepS;
			for (const FName& Hand : HandBones)
			{
				FTransform At;
				if (Best->BodyAt(Hand, TimeS, At) && At.GetLocation().Z < Tuning.GetUp.FastHandCm)
				{
					GetUpRun.FastFromS = TimeS;
				}
			}
		}
		UE_LOG(LogSandbox, Display, TEXT("Body controller: the clip's hands leave the floor at %.2f s of it: the rise goes at the clip's rate from there"), GetUpRun.FastFromS);
	}
	Pelvis->Target = First * Frame;
	Pelvis->Target.SetScale3D(FVector::OneVector);
	SetAssistTarget(*Pelvis);
	++GetUpView.Attempts;
	GetUpView.Clip = BestPath;
	GetUpView.StartedS = ElapsedS;
	GetUpView.DurationS = -1.0;
	GetUpView.StallS = 0.0;
	GetUpView.PausedS = 0.0;
	GetUpView.PausedEaseS = 0.0;
	GetUpView.PausedBehindS = 0.0;
	GetUpView.PausedEndS = 0.0;
	GetUpView.BehindHeightS = 0.0;
	GetUpView.BehindSideS = 0.0;
	GetUpView.BehindFootS = 0.0;
	GetUpView.BehindHeldS = 0.0;
	GetUpView.PlankS = 0.0;
	GetUpView.PlankMaxCm = 0.0;
	GetUpView.PlankAfterS = 0.0;
	GetUpView.RiseFeetCm = -1.0;
	GetUpView.RiseTrunkDeg = -1.0;
	GetUpView.RiseBeganS = -1.0;
	GetUpView.RiseWaitS = 0.0;
	GetUpView.LayS = State == ESandboxBodyState::Lying ? ElapsedS - StateEnteredS : 0.0;
	EnterState(ESandboxBodyState::GetUp, FString::Printf(TEXT("lay still for %.2f s; by '%s' (likeness %.2f)"), GetUpRestS(), *FPaths::GetBaseFilename(BestPath), BestLikeness));
	return true;
}

void USandboxBodyController::MeasureGetUpShape(const FTransform& PelvisNow, double Ease, double DeltaTime)
{
	// The plank, measured (read only): the feet carry the body while its centre of mass is outside them (the world holds it up); and
	// the body's shape as the pelvis comes up through 70 cm. The body rose like a plank over feet left behind it (the owner's look of
	// 26 November).
	if (Ease >= 1.0 && Balance.bValid && Balance.IsSupported() && WeightN > 0.0 &&
		Balance.FootForceN[0] + Balance.FootForceN[1] >= 0.8 * WeightN && Balance.CenterOfMassMarginCm < -5.0)
	{
		GetUpView.PlankS += DeltaTime;
		GetUpView.PlankMaxCm = FMath::Max(GetUpView.PlankMaxCm, -Balance.CenterOfMassMarginCm);
	}
	if (GetUpView.RiseFeetCm < 0.0 && PelvisNow.GetLocation().Z - LastSupportHeightCm >= 70.0)
	{
		const FBodyInstance* Chest = Mesh->GetBodyInstance(Tuning.Rig.Chest);
		const FBodyInstance* Feet[2] = { Mesh->GetBodyInstance(Tuning.Rig.FootLeft), Mesh->GetBodyInstance(Tuning.Rig.FootRight) };
		if (Chest && Chest->IsValidBodyInstance() && Feet[0] && Feet[0]->IsValidBodyInstance() && Feet[1] && Feet[1]->IsValidBodyInstance())
		{
			const FVector Trunk = Chest->GetUnrealWorldTransform().GetLocation() - PelvisNow.GetLocation();
			GetUpView.RiseTrunkDeg = FMath::RadiansToDegrees(FMath::Atan2(Trunk.Size2D(), Trunk.Z));
			GetUpView.RiseFeetCm = FMath::Max(FVector::Dist2D(Feet[0]->GetUnrealWorldTransform().GetLocation(), PelvisNow.GetLocation()),
				FVector::Dist2D(Feet[1]->GetUnrealWorldTransform().GetLocation(), PelvisNow.GetLocation()));
		}
	}
}

void USandboxBodyController::UpdateRise(double DeltaTime, const FTransform& PelvisNow)
{
	const FSandboxGetUpTuning& Limits = Tuning.GetUp;
	FAssist* Pelvis = Assists.FindByPredicate([](const FAssist& Assist) { return Assist.Slot == EAssistSlot::Pelvis; });
	if (!Pelvis)
	{
		GiveUpGetUp(TEXT("the pelvis has no assist"));
		return;
	}
	// Where the feet stand: the middle of their bodies' centres. The centre of mass is kept over it.
	FVector FeetMid = FVector::ZeroVector;
	int32 FeetFound = 0;
	for (const FName Bone : { Tuning.Rig.FootLeft, Tuning.Rig.FootRight })
	{
		const FBodyInstance* Foot = Mesh->GetBodyInstance(Bone);
		if (Foot && Foot->IsValidBodyInstance())
		{
			FeetMid += Foot->GetCOMPosition();
			++FeetFound;
		}
	}
	FeetMid /= FMath::Max(FeetFound, 1);
	const bool bOnFeet = Balance.bValid && Balance.FootForceN[0] > 0.0 && Balance.FootForceN[1] > 0.0;
	const double Lift = FMath::SmoothStep(0.0, 1.0, GetUpRun.RiseShare);
	const double AimHeightCm = FMath::Lerp(GetUpRun.RiseFromHeightCm, StandPelvisHeightCm, Lift);
	const double HeightLagCm = AimHeightCm - (PelvisNow.GetLocation().Z - LastSupportHeightCm);
	const double PressedN = SustainedN.Size();
	const bool bHeldDown = Limits.HoldForceN > 0.0 && PressedN >= Limits.HoldForceN;
	const bool bOffFeet = Balance.bValid && Balance.IsSupported() && Balance.CenterOfMassMarginCm < -Limits.RiseComCm;
	const bool bWaits = HeightLagCm > Limits.LagCm || bOffFeet || bHeldDown || !bOnFeet;
	GetUpView.PressedMaxN = FMath::Max(GetUpView.PressedMaxN, PressedN);

	// The rise goes on while the body keeps up with it, and the clip's crouch fades into the stance with it.
	if (!bWaits)
	{
		GetUpRun.RiseShare = FMath::Min(GetUpRun.RiseShare + DeltaTime / Limits.RiseS, 1.0);
		GetUpRun.StallS = 0.0;
	}
	else
	{
		GetUpView.RiseWaitS += DeltaTime;
		GetUpRun.StallS += DeltaTime;
		GetUpRun.WaitedS += DeltaTime;
		GetUpView.StallS = GetUpRun.WaitedS;
		if (PressedN <= 0.0)
		{
			GetUpRun.FreeWaitS += DeltaTime;
		}
	}
	ClipHold.Rate = 0.0;
	ClipHold.ShareCap = 1.0 - GetUpRun.RiseShare;

	// The pelvis's aim: up to the standing height, upright, and over the floor where it takes the centre of mass over the feet (its
	// pull is capped, so this is a lean the muscles are helped into, not a place the body is carried to). It glides.
	const FVector2D Want = Balance.bValid ? FVector2D(FeetMid - Balance.CenterOfMass) : FVector2D::ZeroVector;
	if (Limits.RiseLeanS > 0.0 && Balance.bValid)
	{
		// The body's own part: everything above the feet leans toward where the centre of mass should be (UpdatePoseTurns).
		const double ComHeightAboveCm = FMath::Max(Balance.CenterOfMass.Z - LastSupportHeightCm, 30.0);
		RiseLeanRad = (RiseLeanRad + Want / ComHeightAboveCm * (DeltaTime / Limits.RiseLeanS)).GetClampedToMaxSize(FMath::DegreesToRadians(Limits.RiseLeanMaxDeg));
	}
	GetUpRun.RiseOffsetCm +=(Want - GetUpRun.RiseOffsetCm) * (Limits.FrameFollowS > 0.0 ? FMath::Min(DeltaTime / Limits.FrameFollowS, 1.0) : 1.0);
	const double LiftNow = FMath::SmoothStep(0.0, 1.0, GetUpRun.RiseShare);
	const FVector Aim(PelvisNow.GetLocation().X + GetUpRun.RiseOffsetCm.X, PelvisNow.GetLocation().Y + GetUpRun.RiseOffsetCm.Y,
		LastSupportHeightCm + FMath::Lerp(GetUpRun.RiseFromHeightCm, StandPelvisHeightCm, LiftNow));
	Pelvis->Target = FTransform(FQuat::Slerp(GetUpRun.RiseFromTurn, GetUpRun.RiseToTurn, LiftNow).GetNormalized(), Aim);
	SetAssistTarget(*Pelvis);

	MeasureGetUpShape(PelvisNow, 1.0, DeltaTime);
	if (FMath::FloorToInt32(ElapsedS * 4.0) != FMath::FloorToInt32((ElapsedS - DeltaTime) * 4.0))
	{
		UE_LOG(LogSandbox, Display, TEXT("Body controller: rising at %.2f s: %.0f%% of the way, pelvis %.0f cm up (aim %.0f), centre of mass %.1f cm inside the feet, floor on feet %.0f and %.0f N, aim %.0f cm aside, assist %.2f%s"),
			ElapsedS, 100.0 * GetUpRun.RiseShare, PelvisNow.GetLocation().Z - LastSupportHeightCm, AimHeightCm,
			Balance.bValid ? Balance.CenterOfMassMarginCm : 0.0, Balance.bValid ? Balance.FootForceN[0] : 0.0, Balance.bValid ? Balance.FootForceN[1] : 0.0,
			GetUpRun.RiseOffsetCm.Size(), AssistLoad.ForceShare, bWaits ? TEXT(", waiting") : TEXT(""));
	}

	if (FVector::Dist(PelvisNow.GetLocation(), Aim) > Limits.LostCm)
	{
		GiveUpGetUp(FString::Printf(TEXT("the pelvis is %.0f cm from where the rise has it"), FVector::Dist(PelvisNow.GetLocation(), Aim)));
		return;
	}
	if (GetUpRun.RiseShare >= 1.0)
	{
		const FVector Up = (PelvisNow.GetRotation() * GetUpRun.RiseToTurn.Inverse()).RotateVector(FVector::UpVector);
		const double TiltDeg = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(Up.Z, -1.0, 1.0)));
		if (!bWaits && TiltDeg <= Limits.UprightDeg)
		{
			++GetUpView.Successes;
			GetUpView.DurationS = ElapsedS - GetUpView.StartedS;
			GivenUpClip.Reset();
			ReleaseClipPose();
			EnterState(ESandboxBodyState::Standing, FString::Printf(TEXT("got up in %.2f s (rose by itself in %.2f s)"), GetUpView.DurationS, ElapsedS - GetUpView.RiseBeganS));
			return;
		}
	}
	if (GetUpRun.StallS > Limits.StallS)
	{
		GiveUpGetUp(bHeldDown ? FString::Printf(TEXT("held down with %.0f N for %.1f s while rising"), PressedN, GetUpRun.StallS)
			: FString::Printf(TEXT("the rise waited %.1f s at %.0f%% (pelvis %.0f cm below its aim, centre of mass %.1f cm inside the feet)"),
				GetUpRun.StallS, 100.0 * GetUpRun.RiseShare, HeightLagCm, Balance.bValid ? Balance.CenterOfMassMarginCm : 0.0));
		return;
	}
	if (Limits.WaitS > 0.0 && GetUpRun.FreeWaitS > Limits.WaitS)
	{
		GiveUpGetUp(FString::Printf(TEXT("the get-up waited %.1f s in all, the rise at %.0f%%"), GetUpRun.FreeWaitS, 100.0 * GetUpRun.RiseShare));
	}
}

double USandboxBodyController::GetUpRestS() const
{
	const FSandboxGetUpTuning& Limits = Tuning.GetUp;
	if (Limits.RestPerNsS <= 0.0)
	{
		return Limits.RestS;
	}
	// N7: a slip is got up from soon, a knockdown later.
	const double Rest = Limits.RestS + Limits.RestPerNsS * FellByNs;
	return Limits.RestMaxS > 0.0 ? FMath::Max(Limits.RestS, FMath::Min(Rest, Limits.RestMaxS)) : Rest;
}

void USandboxBodyController::GiveUpGetUp(const FString& Reason)
{
	if (State != ESandboxBodyState::GetUp)
	{
		return;
	}
	GetUpView.GivenUp = Reason;
	GivenUpClip = GetUpRun.Path;
	ReleaseClipPose();
	EnterState(ESandboxBodyState::Falling, FString::Printf(TEXT("the get-up is given up: %s"), *Reason));
}

void USandboxBodyController::UpdateGetUp(double DeltaTime)
{
	if (State != ESandboxBodyState::GetUp)
	{
		return;
	}
	FAssist* Pelvis = Assists.FindByPredicate([](const FAssist& Assist) { return Assist.Slot == EAssistSlot::Pelvis; });
	const FBodyInstance* Body = Pelvis ? Mesh->GetBodyInstance(Pelvis->Bone) : nullptr;
	const FSandboxClipPose* Clip = Clips.Find(GetUpRun.Path);
	if (!Body || !Body->IsValidBodyInstance() || !Clip || ClipHold.Path != GetUpRun.Path)
	{
		GiveUpGetUp(TEXT("its clip is gone"));
		return;
	}
	if (bHeldUp)
	{
		GiveUpGetUp(TEXT("the body is held up"));
		return;
	}
	const FSandboxGetUpTuning& Limits = Tuning.GetUp;
	// The muscles and the assist come up from nothing (MuscleScale, PushTuningToControls).
	const double Ease = GetUpEase();
	if (!GetUpRun.bEased)
	{
		PushTuningToControls();
		const bool bHitOver = FlinchStartedS < StateEnteredS || ElapsedS - FlinchStartedS >= Tuning.Reactions.FlinchRecoveryS;
		GetUpRun.bEased = Ease >= 1.0 && bHitOver;
	}
	if (GetUpRun.bRising)
	{
		UpdateRise(DeltaTime, Body->GetUnrealWorldTransform());
		return;
	}

	// The pelvis's assist is aimed at where the clip has the pelvis now, and the feet's at where it has the feet:
	// a clip brings the feet under the body while the hands carry it, and feet that the floor's friction kept where
	// they were left the body leaning like a plank from feet half a metre behind it.
	FTransform Goal = Clip->PelvisAt(ClipHold.TimeS) * GetUpRun.Frame;
	Goal.SetScale3D(FVector::OneVector);
	Pelvis->Target = Goal;
	SetAssistTarget(*Pelvis);
	for (FAssist& Assist : Assists)
	{
		FTransform InClip;
		if (Assist.Slot == EAssistSlot::Feet && (Limits.ReachedFeet > 0.5 ? Clip->ReachedBodyAt(Assist.Bone, ClipHold.TimeS, InClip)
			: Clip->BodyAt(Assist.Bone, ClipHold.TimeS, InClip)))
		{
			Assist.Target = InClip * GetUpRun.Frame;
			Assist.Target.SetScale3D(FVector::OneVector);
			SetAssistTarget(Assist);
		}
	}

	const FTransform Now = Body->GetUnrealWorldTransform();
	const double LagCm = FVector::Dist(Now.GetLocation(), Goal.GetLocation());
	if (LagCm > Limits.LostCm)
	{
		GiveUpGetUp(FString::Printf(TEXT("the pelvis is %.0f cm from where the clip has it"), LagCm));
		return;
	}
	// The feet against the clip. A foot off the floor has to come to where the clip has it, and the clip waits for
	// it. A foot the body stands on is where it is: the clip is laid over it instead, its root following the feet in
	// proportion to the weight they carry (getup.frame_follow_s).
	double FootLagCm[2] = { 0.0, 0.0 };
	double AirLagCm = 0.0;
	FVector2D CarriedOff = FVector2D::ZeroVector;
	double CarriedN = 0.0;
	for (const FAssist& Assist : Assists)
	{
		const FBodyInstance* Foot = Assist.Slot == EAssistSlot::Feet && Assist.Side >= 0 ? Mesh->GetBodyInstance(Assist.Bone) : nullptr;
		if (Foot && Foot->IsValidBodyInstance())
		{
			const FVector Off = Foot->GetUnrealWorldTransform().GetLocation() - Assist.Target.GetLocation();
			FootLagCm[Assist.Side] = Off.Size();
			const double LoadN = Balance.bValid ? Balance.FootForceN[Assist.Side] : 0.0;
			if (LoadN > 0.0)
			{
				CarriedOff += FVector2D(Off.X, Off.Y) * LoadN;
				CarriedN += LoadN;
			}
			else
			{
				AirLagCm = FMath::Max(AirLagCm, FootLagCm[Assist.Side]);
			}
		}
	}
	if (CarriedN > 0.0 && Limits.FrameFollowS > 0.0 && WeightN > 0.0)
	{
		const double Share = FMath::Clamp(CarriedN / WeightN, 0.0, 1.0);
		const FVector2D Shift = (CarriedOff / CarriedN) * FMath::Min(Share * DeltaTime / Limits.FrameFollowS, 1.0);
		GetUpRun.Frame.AddToTranslation(FVector(Shift.X, Shift.Y, 0.0));
	}
	// And over the body itself where the feet carry nothing: a body that turns into the clip's first pose, rolls onto
	// its side or comes onto its hands and knees moves over the floor, and where on the floor it gets up does not
	// matter. The clip's root follows the pelvis, by the share of the weight that is not on the feet
	// (getup.pelvis_follow_s). A blow moves the pelvis faster than that and is still told by the distance above.
	//
	// What presses on the body is the force felt in every frame of the last moments from other simulated bodies, as
	// for a held push (FeelFrame). Pressed down harder than it will push against (getup.hold_force_n), the body does
	// not go on: its clip waits, below, and the attempt is given up when it has waited too long. Pressed with less,
	// it gets up where the hand lets it: the clip goes on following the pelvis when the feet carry the body too, by
	// getup.pressed_follow. (A hand on the pelvis of a body in a crouch kept it 25 cm behind where the clip had it
	// over the feet, and one hand then stopped a get-up as surely as two.)
	const double PressedN = SustainedN.Size();
	const bool bHeldDown = Limits.HoldForceN > 0.0 && PressedN >= Limits.HoldForceN;
	const bool bPushesOn = Limits.HoldForceN > 0.0 && PressedN > 0.0 && !bHeldDown;
	GetUpView.PressedMaxN = FMath::Max(GetUpView.PressedMaxN, PressedN);
	if (Limits.PelvisFollowS > 0.0)
	{
		const double OnFeet = WeightN > 0.0 ? FMath::Clamp(CarriedN / WeightN, 0.0, 1.0) : 0.0;
		const FVector Off = Now.GetLocation() - Goal.GetLocation();
		const double Share = 1.0 - OnFeet * (1.0 - (bPushesOn ? Limits.PressedFollow : 0.0));
		const FVector2D Shift = FVector2D(Off.X, Off.Y) * (Share * FMath::Min(DeltaTime / Limits.PelvisFollowS, 1.0));
		GetUpRun.Frame.AddToTranslation(FVector(Shift.X, Shift.Y, 0.0));
	}
	MeasureGetUpShape(Now, Ease, DeltaTime);
	// A line of the log four times a second: where the body is against the clip.
	if (FMath::FloorToInt32(ElapsedS * 4.0) != FMath::FloorToInt32((ElapsedS - DeltaTime) * 4.0))
	{
		const FVector Off = Now.GetLocation() - Goal.GetLocation();
		const FVector Leans = (Now.GetRotation() * Goal.GetRotation().Inverse()).RotateVector(FVector::UpVector);
		UE_LOG(LogSandbox, Display, TEXT("Body controller: getting up at %.2f s: clip %.2f s, pelvis %.0f cm off (%.0f, %.0f, %.0f) at %.0f cm up, turned %.0f deg from the clip's, feet %.0f and %.0f cm off, floor on feet %.0f and %.0f N, assist %.2f"),
			ElapsedS, ClipHold.TimeS, LagCm, Off.X, Off.Y, Off.Z, Now.GetLocation().Z - LastSupportHeightCm,
			FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(Leans.Z, -1.0, 1.0))), FootLagCm[0], FootLagCm[1],
			Balance.bValid ? Balance.FootForceN[0] : 0.0, Balance.bValid ? Balance.FootForceN[1] : 0.0, AssistLoad.ForceShare);
	}
	// The clip's time waits for a body that does not keep up, and stops at the time by which the clip stands.
	// A foot in the air is waited for, but not without end (getup.foot_wait_s).
	const FVector PelvisOff = Now.GetLocation() - Goal.GetLocation();
	// A body that pushes on under a press sags under it: its muscles are springs, and a clip that waited at the usual
	// lag would never ask them for more (a steady 300 N on the pelvis left it 18 cm low in a crouch, 2 cm past the
	// lag, for as long as it pressed). The clip may then run further ahead in height (getup.pressed_lag_cm).
	const double HeightLagCm = bPushesOn ? FMath::Max(Limits.LagCm, Limits.PressedLagCm) : Limits.LagCm;
	bool bPelvisBehind = FMath::Abs(PelvisOff.Z) > HeightLagCm || PelvisOff.Size2D() > Limits.SideLagCm;
	GetUpRun.FootWaitS = AirLagCm > Limits.FootLagCm ? GetUpRun.FootWaitS + DeltaTime : 0.0;
	bool bFootWaited = AirLagCm > Limits.FootLagCm && GetUpRun.FootWaitS <= Limits.FootWaitS;
	if (Limits.ProgressCm > 0.0)
	{
		// N7: a wait goes on only while what is waited for comes closer. A foot the clip had put where the body cannot reach stayed
		// 18 cm off for the whole of foot_wait_s, and the clip stood still for it: a pause and nothing gained.
		const auto Progress = [this, DeltaTime, &Limits](bool bBeyond, double LagCm, double& CheckS, double& LagAtCheck, bool& bNotWaited)
		{
			if (!bBeyond)
			{
				CheckS = 0.0;
				LagAtCheck = LagCm;
				bNotWaited = false;
				return;
			}
			CheckS += DeltaTime;
			if (CheckS >= Limits.ProgressWindowS)
			{
				bNotWaited = bNotWaited || LagAtCheck - LagCm < Limits.ProgressCm;
				LagAtCheck = LagCm;
				CheckS = 0.0;
			}
		};
		Progress(bFootWaited, AirLagCm, GetUpRun.FootCheckS, GetUpRun.FootLagAtCheck, GetUpRun.bFootNotWaited);
		bFootWaited = bFootWaited && !GetUpRun.bFootNotWaited;
		if (Limits.ProgressPelvis > 0.5)
		{
			Progress(bPelvisBehind, PelvisOff.Size(), GetUpRun.PelvisCheckS, GetUpRun.PelvisLagAtCheck, GetUpRun.bPelvisNotWaited);
			bPelvisBehind = bPelvisBehind && !GetUpRun.bPelvisNotWaited;
		}
	}
	if (Limits.FastRise > 0.5 && GetUpRun.FastFromS >= 0.0)
	{
		// N7 (getup.fast_rise): in the rise the clip does not wait for the body, and the pelvis's pull is stronger. Waiting there, the body
		// stood in the forward lean that the recorded person passes in a moment, and the world held it like a plank.
		const bool bFast = Ease >= 1.0 && ClipHold.TimeS >= GetUpRun.FastFromS;
		if (bFast)
		{
			bPelvisBehind = false;
			bFootWaited = false;
			if (GetUpRun.FastShare <= 0.0)
			{
				UE_LOG(LogSandbox, Display, TEXT("Body controller: getting up at %.2f s: the rise, at the clip's rate, from %.2f s of it"), ElapsedS, ClipHold.TimeS);
			}
		}
		const double FastBefore = GetUpRun.FastShare;
		const double FastStep = Limits.FastEaseS > 0.0 ? DeltaTime / Limits.FastEaseS : 1.0;
		GetUpRun.FastShare = FMath::Clamp(GetUpRun.FastShare + (bFast ? FastStep : -FastStep), 0.0, 1.0);
		if (GetUpRun.FastShare != FastBefore)
		{
			PushTuningToControls();
		}
	}
	const bool bBehind = bHeldDown || bPelvisBehind || bFootWaited;
	// A clip the body rises from by itself (getup.rise) ends at its crouch.
	const bool bThrough = ClipHold.TimeS >= (GetUpRun.RiseAtS >= 0.0 ? GetUpRun.RiseAtS : FMath::Min(Limits.EndS, Clip->GetLengthS())) - KINDA_SMALL_NUMBER;
	double RateGoal = Ease < 1.0 || bBehind || bThrough ? 0.0 : Limits.Rate;
	if (Limits.SlowFrom > 0.0 && !bBehind && !bThrough)
	{
		// N7: the clip slows as the body falls behind, to a stop at the lags that stopped it before, and starts while the body
		// eases in. Run or stand, the clip read as separate motions with pauses between them (the owner's look of 26 November).
		const double FootShare = AirLagCm > 0.0 && GetUpRun.FootWaitS <= Limits.FootWaitS ? AirLagCm / Limits.FootLagCm : 0.0;
		const double Share = FMath::Max3(FMath::Abs(PelvisOff.Z) / HeightLagCm, PelvisOff.Size2D() / Limits.SideLagCm, FootShare);
		RateGoal = Limits.Rate * Ease * (1.0 - FMath::SmoothStep(Limits.SlowFrom, 1.0, Share));
	}
	ClipHold.Rate = Limits.RateFollowS > 0.0 ? ClipHold.Rate + (RateGoal - ClipHold.Rate) * (1.0 - FMath::Exp(-DeltaTime / Limits.RateFollowS)) : RateGoal;
	if (Limits.GatherS > 0.0 && Ease >= 1.0 && bBehind && !bThrough)
	{
		// N7, a wait that is not a freeze: the clip goes back a little and comes again while the body catches up, never past where
		// the wait began. Standing still, it read as a body that froze between clips (the owner's look of 26 November).
		if (GetUpRun.GatherFromS < 0.0)
		{
			GetUpRun.GatherFromS = ClipHold.TimeS;
			GetUpRun.GatherWay = -1.0;
		}
		if (ClipHold.TimeS <= FMath::Max(GetUpRun.GatherFromS - Limits.GatherS, 0.0) + KINDA_SMALL_NUMBER)
		{
			GetUpRun.GatherWay = 1.0;
		}
		else if (ClipHold.TimeS >= GetUpRun.GatherFromS)
		{
			GetUpRun.GatherWay = -1.0;
		}
		ClipHold.Rate = GetUpRun.GatherWay * Limits.GatherRate * Limits.Rate;
	}
	else if (!bBehind)
	{
		GetUpRun.GatherFromS = -1.0;
	}
	if (FMath::Abs(ClipHold.Rate) < 0.1 * Limits.Rate)
	{
		GetUpView.PausedS += DeltaTime;
		(Ease < 1.0 ? GetUpView.PausedEaseS : bThrough ? GetUpView.PausedEndS : GetUpView.PausedBehindS) += DeltaTime;
	}
	if (Ease >= 1.0 && bBehind && !bThrough)
	{
		GetUpView.BehindHeightS += bPelvisBehind && FMath::Abs(PelvisOff.Z) > HeightLagCm ? DeltaTime : 0.0;
		GetUpView.BehindSideS += bPelvisBehind && PelvisOff.Size2D() > Limits.SideLagCm ? DeltaTime : 0.0;
		GetUpView.BehindFootS += bFootWaited ? DeltaTime : 0.0;
		GetUpView.BehindHeldS += bHeldDown ? DeltaTime : 0.0;
	}
	if (Ease >= 1.0 && (bBehind || bThrough))
	{
		GetUpRun.StallS += DeltaTime;
		GetUpRun.WaitedS += DeltaTime;
		GetUpView.StallS = GetUpRun.WaitedS;
		if (PressedN <= 0.0)
		{
			GetUpRun.FreeWaitS += DeltaTime;
		}
	}
	else
	{
		GetUpRun.StallS = 0.0;
	}
	if (bThrough && GetUpRun.RiseAtS >= 0.0)
	{
		// N7 (getup.rise): at the clip's crouch, with the body caught up and on both feet, the body takes over and rises by itself.
		const bool bOnFeet = Balance.bValid && Balance.FootForceN[0] > 0.0 && Balance.FootForceN[1] > 0.0;
		if (!bBehind && bOnFeet)
		{
			GetUpRun.bRising = true;
			GetUpRun.RiseShare = 0.0;
			GetUpRun.RiseFromHeightCm = Goal.GetLocation().Z - LastSupportHeightCm;
			GetUpRun.RiseFromTurn = Goal.GetRotation();
			FQuat Tilt, Yaw;
			(Goal.GetRotation() * Pelvis->SupportRotation.Inverse()).ToSwingTwist(FVector::UpVector, Tilt, Yaw);
			GetUpRun.RiseToTurn = (Yaw * Pelvis->SupportRotation).GetNormalized();
			GetUpRun.RiseOffsetCm = FVector2D(Goal.GetLocation() - Now.GetLocation());
			GetUpRun.StallS = 0.0;
			for (FAssist& Assist : Assists)
			{
				if (Assist.Slot == EAssistSlot::Feet)
				{
					Assist.Target = FlatFootTransform(Assist);
					SetAssistTarget(Assist);
				}
			}
			ClipHold.Rate = 0.0;
			GetUpView.RiseBeganS = ElapsedS;
			ApplyPose();
			UE_LOG(LogSandbox, Display, TEXT("Body controller: getting up at %.2f s: the body rises by itself from the clip's crouch at %.2f s (pelvis %.0f cm up, to %.0f)"),
				ElapsedS, ClipHold.TimeS, GetUpRun.RiseFromHeightCm, StandPelvisHeightCm);
			return;
		}
	}
	else if (bThrough)
	{
		const FVector Up = (Now.GetRotation() * Goal.GetRotation().Inverse()).RotateVector(FVector::UpVector);
		const double TiltDeg = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(Up.Z, -1.0, 1.0)));
		const bool bOnFeet = Balance.bValid && Balance.FootForceN[0] > 0.0 && Balance.FootForceN[1] > 0.0;
		if (!bBehind && TiltDeg <= Limits.UprightDeg && bOnFeet)
		{
			++GetUpView.Successes;
			GetUpView.DurationS = ElapsedS - GetUpView.StartedS;
			GivenUpClip.Reset();
			ReleaseClipPose();
			EnterState(ESandboxBodyState::Standing, FString::Printf(TEXT("got up in %.2f s"), GetUpView.DurationS));
			return;
		}
	}
	if (GetUpRun.StallS > Limits.StallS)
	{
		if (bHeldDown)
		{
			GiveUpGetUp(FString::Printf(TEXT("held down with %.0f N for %.1f s, at %.2f s of the clip"), PressedN, GetUpRun.StallS, ClipHold.TimeS));
			return;
		}
		GiveUpGetUp(FString::Printf(TEXT("the clip stood at %.2f s for %.1f s waiting for the body (pelvis %.0f cm, feet %.0f and %.0f cm from it)"),
			ClipHold.TimeS, GetUpRun.StallS, LagCm, FootLagCm[0], FootLagCm[1]));
		return;
	}
	// A clip that moves on by a frame now and then never stands still that long, and the body holds one pose for as
	// long as it does (13 s, with its back under a table's edge). All its waiting with nothing pressing on the body is
	// bounded too (getup.wait_s); a body that a hand slows is not hurried by this.
	if (Limits.WaitS > 0.0 && GetUpRun.FreeWaitS > Limits.WaitS)
	{
		GiveUpGetUp(FString::Printf(TEXT("the clip waited %.1f s in all for the body and is at %.2f s (pelvis %.0f cm, feet %.0f and %.0f cm from it)"),
			GetUpRun.FreeWaitS, ClipHold.TimeS, LagCm, FootLagCm[0], FootLagCm[1]));
	}
}

double USandboxBodyController::MuscleScale(ESandboxMuscleGroup Group) const
{
	// A body that begins to get up was limp a moment ago: its muscles come up from nothing.
	if (State == ESandboxBodyState::GetUp)
	{
		// A hit taken while getting up weakens the struck part for a moment, as a flinch does.
		const bool bHit = Group == FlinchGroup && FlinchStartedS >= StateEnteredS && Tuning.Reactions.FlinchRecoveryS > 0.0;
		const double Recovered = bHit ? FMath::Clamp((ElapsedS - FlinchStartedS) / Tuning.Reactions.FlinchRecoveryS, 0.0, 1.0) : 1.0;
		return GetUpEase() * FMath::Lerp(Tuning.Reactions.FlinchWeakening, 1.0, Recovered);
	}
	// A flinch that turns into a stagger keeps its weakening: it fades by its own clock from the hit, so that the
	// change of state does not hand the hit muscles their full strength back in one step.
	if ((State != ESandboxBodyState::Flinch && State != ESandboxBodyState::Stagger) || Group != FlinchGroup || FlinchStartedS < 0.0 || FlinchStartedS > ElapsedS)
	{
		return 1.0;
	}
	// Weak at the moment of the hit, back to full strength in a straight line.
	const double Recovered = Tuning.Reactions.FlinchRecoveryS > 0.0 ? FMath::Clamp((ElapsedS - FlinchStartedS) / Tuning.Reactions.FlinchRecoveryS, 0.0, 1.0) : 1.0;
	return FMath::Lerp(Tuning.Reactions.FlinchWeakening, 1.0, Recovered);
}

void USandboxBodyController::TellEvent(const FSandboxBodyEvent& Event)
{
	Events.Add(Event);
	OnBodyEvent.Broadcast(Event);
}

void USandboxBodyController::TellHolds(TConstArrayView<FSandboxBodyHold> InHolds)
{
	const auto Has = [](TConstArrayView<FSandboxBodyHold> List, const FSandboxBodyHold& Hold)
	{
		return List.ContainsByPredicate([&Hold](const FSandboxBodyHold& Other) { return Other.Holder == Hold.Holder && Other.Bone == Hold.Bone; });
	};
	const auto Tell = [this](ESandboxBodyEventType Type, const FSandboxBodyHold& Hold)
	{
		FSandboxBodyEvent Event;
		Event.Type = Type;
		Event.TimeS = ElapsedS;
		Event.Bone = Hold.Bone;
		Event.Holder = Hold.Holder;
		Event.State = State;
		Event.PreviousState = State;
		TellEvent(Event);
	};
	// Those that ended first, then those that began: a hand that lets go of one part and takes another in
	// the same frame reads in that order.
	for (const FSandboxBodyHold& Old : Holds)
	{
		if (!Has(InHolds, Old))
		{
			Tell(ESandboxBodyEventType::Released, Old);
		}
	}
	for (const FSandboxBodyHold& New : InHolds)
	{
		if (!Has(Holds, New))
		{
			Tell(ESandboxBodyEventType::Grabbed, New);
		}
	}
	Holds = InHolds;
}

void USandboxBodyController::EnterState(ESandboxBodyState NewState, const FString& Reason)
{
	const ESandboxBodyState OldState = State;
	State = NewState;
	StateEnteredS = ElapsedS;
	RestS = 0.0;
	StateLog.Add(FSandboxStateChange{ ElapsedS, NewState, Reason });
	UE_LOG(LogSandbox, Display, TEXT("Body controller: %s at %.3f s (%s)"), FSandboxBodyTuning::StateName(NewState), ElapsedS, *Reason);
	{
		FSandboxBodyEvent Event;
		Event.Type = ESandboxBodyEventType::StateChanged;
		Event.TimeS = ElapsedS;
		Event.State = NewState;
		Event.PreviousState = OldState;
		Event.Reason = Reason;
		TellEvent(Event);
	}
	if (NewState == ESandboxBodyState::Falling)
	{
		FellByNs = RecentHitS >= 0.0 && ElapsedS - RecentHitS <= 1.0 ? RecentHitNs : 0.0;
		// The strengths the muscles have now, which go over to the fall's (balance.fall_ease_s): each muscle's own, as last
		// handed to its drive, the leg of a step under way and a flinch's weakening included. (Taken from the groups'
		// values, a fall during a swing dropped the swinging leg to half its stiffness in one step.)
		for (FMuscle& Muscle : Muscles)
		{
			Muscle.FallFromStiffness = Muscle.StiffnessNmPerRad;
			Muscle.FallFromDamping = Muscle.DampingNmsPerRad;
			Muscle.FallFromVelocityShare = Muscle.PoseVelocityShare;
		}
		bFallEaseFromSet = true;
		// The strategies' goals let go of as in any fall, not held to the end of a reflex's delay (N4).
		MechanicsUntilS = -1.0;
		// The pose of the fall is chosen once, as the fall starts (ApplyTuning below asks for it).
		ChooseFallWay();
		static const TCHAR* const WayNames[] = { TEXT("backward"), TEXT("forward"), TEXT("to the left"), TEXT("to the right") };
		UE_LOG(LogSandbox, Display, TEXT("Body controller: going down %s"), WayNames[static_cast<int32>(FallWay)]);
	}

	// Each state has its own values. The store has checked every state that sets any, so this reads.
	FSandboxBodyTuning NewTuning;
	FString Error;
	if (!ReadStateTuning(NewTuning, Error) || !ApplyTuning(NewTuning, Error))
	{
		UE_LOG(LogSandbox, Error, TEXT("Body controller keeps the previous state's tuning: %s"), *Error);
	}

	// A falling body has no use for a foot held at a point of the floor: its legs would keep reaching for it
	// as the body goes down, and feed it.
	if (NewState == ESandboxBodyState::Falling)
	{
		ReleaseFootTargets();
	}

	// Back from a stagger or a flinch (states without the assist's horizontal pull) the assist takes hold again,
	// at full strength. Its anchors of pelvis, chest and head start where those bodies are and glide to their
	// places over the support, so that the pull grows from nothing instead of arriving at once.
	if (OldState == ESandboxBodyState::GetUp && NewState == ESandboxBodyState::Standing)
	{
		// The feet stand where the get-up left them: brought back together like after a step.
		RegroupLeft = FMath::RoundToInt32(Tuning.Balance.RegroupMaxSteps);
	}
	if ((OldState == ESandboxBodyState::Stagger || OldState == ESandboxBodyState::Flinch || OldState == ESandboxBodyState::GetUp) && NewState == ESandboxBodyState::Standing)
	{
		// The feet are anchored where they stand: without the assist's pull they may have turned or slid a little.
		// (After a flinch too: a foot that had slid 7 cm under a push to the pelvis was pulled back by its spring, and
		// the pelvis's anchor was taken from where the feet had been.)
		{
			for (FAssist& Assist : Assists)
			{
				if (Assist.Slot == EAssistSlot::Feet)
				{
					Assist.Target = FlatFootTransform(Assist);
					SetAssistTarget(Assist);
				}
			}
		}
		FVector SupportLocation;
		FQuat SupportRotation;
		SupportFrame(SupportLocation, SupportRotation);
		for (FAssist& Assist : Assists)
		{
			const FBodyInstance* Body = Assist.Slot != EAssistSlot::Feet ? Mesh->GetBodyInstance(Assist.Bone) : nullptr;
			if (!Body || !Body->IsValidBodyInstance())
			{
				continue;
			}
			FTransform Now = Body->GetUnrealWorldTransform();
			Now.SetScale3D(FVector::OneVector);
			// The lean is added on top of the anchor (AssistGoal); the glide starts from the body less the lean.
			Now.AddToTranslation(-FVector(Assist.AppliedLeanCm * Assist.LeanFactor, 0.0));
			Assist.FollowTo = FTransform((SupportRotation * Assist.SupportRotation).GetNormalized(), SupportLocation + SupportRotation.RotateVector(Assist.SupportLocation));
			// Over the floor from where the body is, but at the standing height: the legs' frames are lowered from
			// it as far as the feet need (UpdateLowering), and an anchor at the height of a pelvis that the legs
			// already hold lower would lower them twice, at once (the knees gave, both feet left the floor).
			Now.SetLocation(FVector(Now.GetLocation().X, Now.GetLocation().Y, Assist.FollowTo.GetLocation().Z));
			Assist.FollowFrom = Now;
			Assist.FollowElapsedS = 0.0;
			Assist.FollowS = GlideSeconds(Assist.FollowFrom.GetLocation(), Assist.FollowTo.GetLocation());
			Assist.Target = Now;
			SetAssistTarget(Assist);
			if (Assist.Slot == EAssistSlot::Pelvis)
			{
				UE_LOG(LogSandbox, Display, TEXT("Body controller: the pelvis's anchor glides %.1f cm back over the support in %.2f s"),
					FVector::Dist2D(Assist.FollowFrom.GetLocation(), Assist.FollowTo.GetLocation()), Assist.FollowS);
			}
		}
		// What is left of a weight shift is where the body is now, and so in the anchors already: kept on top of
		// them, it moved the legs' upright frame 5 cm off the body, the far leg was asked for more than its length
		// and kicked the body off the floor (4800 N).
		ShiftCm = FVector2D::ZeroVector;
		ShiftGoalCm = FVector2D::ZeroVector;
		bShiftHeld = false;
		// The legs go over to the posture over the anchors, which starts where the body is, from the frame they were
		// solved in until now. (From a frame worked out anew, every frame moved with the anchors' jump to the body:
		// 16 cm after a step to the side, and the legs struck the floor with 5000 N each.)
		for (int32 Side = 0; Side < 2; ++Side)
		{
			FFootPlacement& Foot = Placement[Side];
			if (Foot.Mode != EPlacement::Planted)
			{
				continue;
			}
			// The leg's point goes over to where the foot is now, as the frame does. Kept where the foot was set
			// down, a foot that had slid 15 cm on its edge after a step across was pulled back toward it by its leg,
			// which pushed the body off its support and into the next stagger; moved there at once, the leg's reach
			// jumped past its length and the knee straightened in one step.
			const FVector Was = Foot.GlidedPoint();
			FVector Now = Was;
			for (const FAssist& Assist : Assists)
			{
				if (Assist.Slot == EAssistSlot::Feet && Assist.Side == Side)
				{
					Now = FVector(Assist.Target.GetLocation().X, Assist.Target.GetLocation().Y, Was.Z);
				}
			}
			PlaceFoot(Side, EPlacement::Planted, Now, EFrame::Anchor, Tuning.Balance.AnchorFollowS, nullptr, true);
			Foot.GlideFrom = Was;
			Foot.GlideElapsedS = 0.0;
			Foot.GlideS = Tuning.Balance.AnchorFollowS;
		}
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
	FeelImpulse(Bone, ImpulseNs, Body->GetCOMPosition());
}

bool USandboxBodyController::SetFootTarget(FName Foot, const FVector& WorldLocation, double Weight, FString& OutError, double BlendS)
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
	// An arm bends its elbow back and out, a leg its knee forward. (A knee's numbers are the ones the placement always had.)
	if (FSandboxBodyTuning::GroupOfBone(Foot) == ESandboxMuscleGroup::Arms)
	{
		const bool bLeft = Foot.ToString().EndsWith(TEXT("_l"));
		Animation->SetLimbBend(End, FVector(bLeft ? 0.4 : -0.4, -1.0, -0.3).GetSafeNormal(), FVector::XAxisVector);
		FSandboxJointLimits Shoulder;
		FSandboxJointLimits Elbow;
		if (FSandboxPoseRig::ReadJointLimits(Mesh, Skeleton.GetBoneName(Root), Shoulder) && FSandboxPoseRig::ReadJointLimits(Mesh, Skeleton.GetBoneName(Middle), Elbow))
		{
			Animation->SetLimbRanges(End, Shoulder, Elbow);
		}
	}
	Animation->SetLimbTarget(End, WorldLocation, Weight, BlendS >= 0.0 ? BlendS : Tuning.Poses.BlendS);
	PlacedFeet.Remove(End);
	if (Weight > 0.0)
	{
		PlacedFeet.Add(End, WorldLocation);
	}
	return true;
}

void USandboxBodyController::ReleaseFootTargets()
{
	USandboxAnimInstance* Animation = Mesh ? Cast<USandboxAnimInstance>(Mesh->GetAnimInstance()) : nullptr;
	if (Animation)
	{
		// Eased out like any placement: a leg let go in one step would snap back. As fast as the fall's pose comes in:
		// let go more slowly, the legs of a body that fell after a step were still straight when it met the floor.
		for (const TPair<int32, FVector>& Placed : PlacedFeet)
		{
			Animation->SetLimbTarget(Placed.Key, Placed.Value, 0.0, Tuning.Poses.FallBlendS);
		}
	}
	PlacedFeet.Reset();
	// The controller's own placements too, and a step under way ends where it is.
	for (int32 Side = 0; Side < 2; ++Side)
	{
		ReleaseFoot(Side, Tuning.Poses.FallBlendS);
	}
	Swing.bActive = false;
	Pending.bActive = false;
	bShiftHeld = false;
}

FTransform USandboxBodyController::FrameTransform(EFrame Frame, int32 Side, bool bLowered) const
{
	const FTransform Component = Mesh->GetComponentTransform();
	const FAssist* PelvisAssist = Assists.FindByPredicate([](const FAssist& Assist) { return Assist.Slot == EAssistSlot::Pelvis; });
	const FBodyInstance* Body = PelvisAssist ? Mesh->GetBodyInstance(PelvisAssist->Bone) : nullptr;
	if (Frame == EFrame::Actual || !Body || !Body->IsValidBodyInstance())
	{
		return Component;
	}
	// The component sits on the simulated pelvis: carry that relation over to a pelvis where it should be.
	FTransform PelvisNow = Body->GetUnrealWorldTransform();
	PelvisNow.SetScale3D(FVector::OneVector);
	const FTransform Relative = Component.GetRelativeTransform(PelvisNow);
	FTransform Pelvis = AssistGoal(*PelvisAssist);
	// A weight shift before a step moves the goal toward the stance foot.
	Pelvis.AddToTranslation(FVector(ShiftCm, 0.0));
	if (Frame == EFrame::Here || Frame == EFrame::Stance)
	{
		const FVector Goal = Pelvis.GetLocation();
		const FVector Now = PelvisNow.GetLocation();
		const double Follow = Frame == EFrame::Here ? 1.0 : Tuning.Balance.StanceFollow;
		// In a stagger the planted legs also brake the body: the pelvis is solved for behind where it would be by the
		// body's speed times balance.stagger_damping_s, so that the leg ahead pushes back while the body moves, and
		// only the pull toward the anchor is left once it stops. (Without it, a step that caught the capture point
		// 11 cm inside the new support did not stop the body: the weight stayed on the trailing leg, which pushed the
		// body on past the leading foot; a pull toward the anchor strong enough to stop it turned the body round and
		// sent it out the other side.)
		FVector2D Behind = FVector2D::ZeroVector;
		if (Frame == EFrame::Stance && Tuning.Balance.StaggerBrakeFollowS > 0.0)
		{
			// With the speed's own lag, in and out (UpdateStaggerBrake).
			Behind = StaggerBrakeCm;
		}
		else if (Frame == EFrame::Stance && State == ESandboxBodyState::Stagger)
		{
			Behind = FVector2D(Balance.Velocity.X, Balance.Velocity.Y) * Tuning.Balance.StaggerDampingS;
		}
		Pelvis.SetLocation(FVector(FMath::Lerp(Goal.X, Now.X, Follow) - Behind.X, FMath::Lerp(Goal.Y, Now.Y, Follow) - Behind.Y, Goal.Z));
	}
	FTransform Solved = Relative * Pelvis;

	// No higher than standing, but low enough for the planted legs to reach their feet (UpdateLowering).
	if (bLowered && LowerCm > 0.0)
	{
		Solved.AddToTranslation(FVector(0.0, 0.0, -LowerCm));
	}
	return Solved;
}

double USandboxBodyController::GlideSeconds(const FVector& From, const FVector& To) const
{
	const double FollowS = Tuning.Balance.AnchorFollowS;
	const FVector2D Way(To.X - From.X, To.Y - From.Y);
	const double DistanceCm = Way.Size();
	const double HeightCm = StandingComHeightM * 100.0;
	if (DistanceCm < KINDA_SMALL_NUMBER || Tuning.Balance.AnchorRoomShare <= 0.0 || HeightCm <= 0.0)
	{
		return FollowS;
	}
	// The support: both soles, each where it lies (a foot just set down may not be quite on the floor yet).
	TArray<FVector2D> Points;
	for (int32 Side = 0; Side < 2; ++Side)
	{
		TArray<FVector2D> Sole;
		FSandboxBodyProbe::SupportPolygon(Mesh, TArray<FName>{ FootBoneName[Side] }, Tuning.Balance.SoleToleranceCm, Sole);
		Points.Append(Sole);
	}
	TArray<FVector2D> Support;
	FSandboxBodyProbe::ConvexHull2D(Points, Support);
	// Standing, where the assist pulls the body along, no less than what the ankles bring back: a glide's end that
	// lies outside the support as the soles are now (one foot raised on its edge) has no room at all, and was given
	// the shortest glide instead of the longest (20 cm in 0.5 s, the body carried over the middle of its feet and out
	// the other side). In a stagger the assist does not pull the body (its anchor only sets the legs' frames), and
	// slower glides there held the legs to the old stance: more falls in the push matrix, one solver blow-up.
	double RoomCm = FSandboxBodyProbe::DistanceToEdgeAlong(Support, FVector2D(To.X, To.Y), Way);
	if (State == ESandboxBodyState::Standing)
	{
		RoomCm = FMath::Max(RoomCm, Tuning.Balance.RecoverableCm);
	}
	// A smooth step peaks at 1.5 times its mean speed; the capture point runs that speed / omega ahead.
	const double MaxSpeedCmS = Tuning.Balance.AnchorRoomShare * RoomCm * FMath::Sqrt(GravityCmS2 / HeightCm);
	return MaxSpeedCmS > 0.0 ? FMath::Max(FollowS, 1.5 * DistanceCm / MaxSpeedCmS) : FollowS;
}

double USandboxBodyController::LoweringFor(const FTransform& Frame, int32 Leg, const FVector& Foot) const
{
	if (LegLengthCm[Leg] <= 0.0)
	{
		return 0.0;
	}
	const FVector Hip = Frame.TransformPosition(HipReferenceCs[Leg]);
	const double ReachCm = Tuning.Balance.LegExtensionMax * LegLengthCm[Leg];
	const double AcrossSquared = FMath::Square(Hip.X - Foot.X) + FMath::Square(Hip.Y - Foot.Y);
	const double DownCm = FMath::Sqrt(FMath::Max(ReachCm * ReachCm - AcrossSquared, 0.0));
	return FMath::Max((Hip.Z - Foot.Z) - DownCm, 0.0);
}

void USandboxBodyController::UpdateLowering(double DeltaTime)
{
	// A leg asked to reach further than it can goes straight and lifts its foot off the floor (in a wide stance), so
	// the pelvis is held as low as the planted leg that needs it most. The same height for both legs, or they would
	// fight over it. While a foot swings, the swinging leg's reach to its target comes in as the foot travels over:
	// the stance knee bends before the foot comes down, so that the foot reaches the floor and the height does not
	// change when it lands. The height follows with a lag either way: a pelvis that drops at once unloads the
	// stance foot, and one that rises at once (a foot lifted for the next step) throws the body up.
	double GoalCm = 0.0;
	for (int32 Leg = 0; Leg < 2; ++Leg)
	{
		if (Placement[Leg].Mode == EPlacement::Planted)
		{
			double NeedCm = LoweringFor(FrameTransform(Placement[Leg].Frame, Leg, false), Leg, Placement[Leg].Point);
			// The foot a regrouping step is about to lift holds the pelvis down only by the share of the weight it still
			// carries: as the weight goes over to the stance foot the body rises on that leg, as a person comes up out
			// of a lunge. (Held down for a leg that carried nothing, the body sat 15 cm low on the bent stance leg with
			// its centre of mass over that foot's heel, and could not come further forward.)
			if (Pending.bActive && Pending.Plan.bRegroup && Leg == Pending.Plan.Side && WeightN > 0.0)
			{
				NeedCm *= FMath::Clamp(Balance.FootForceN[Leg] / (0.5 * WeightN), 0.0, 1.0);
			}
			GoalCm = FMath::Max(GoalCm, NeedCm);
		}
	}
	if (Swing.bActive)
	{
		GoalCm = FMath::Max(GoalCm, LoweringFor(FrameTransform(EFrame::Stance, Swing.Side, false), Swing.Side, Swing.Target) * Swing.Across);
	}
	if (Placement[0].Mode == EPlacement::None && Placement[1].Mode == EPlacement::None)
	{
		LowerCm = 0.0;
		return;
	}
	GoalCm = FMath::Min(GoalCm, Tuning.Balance.LowerMaxCm);
	const double Follow = Tuning.Balance.LowerS > 0.0 ? 1.0 - FMath::Exp(-DeltaTime / Tuning.Balance.LowerS) : 1.0;
	LowerCm += (GoalCm - LowerCm) * Follow;
}

void USandboxBodyController::PlaceFoot(int32 Side, EPlacement Mode, const FVector& Point, EFrame Frame, double BlendS, const EFrame* From, bool bFromLast)
{
	USandboxAnimInstance* Animation = Cast<USandboxAnimInstance>(Mesh->GetAnimInstance());
	if (!Animation || Side < 0 || Side > 1)
	{
		return;
	}
	FFootPlacement& Foot = Placement[Side];
	if (Foot.Mode == EPlacement::None)
	{
		// From the pose: the placement's weight eases in, in its frame from the start.
		Animation->SetLimbTarget(FootBoneIndex[Side], Point, 1.0, BlendS);
		Foot.FromFrame = Frame;
		Foot.FrameElapsedS = 0.0;
		Foot.FrameBlendS = 0.0;
		Foot.bFromLastFrame = false;
	}
	else if (Foot.Frame != Frame || From || bFromLast)
	{
		// From one frame to another: the frame itself goes over, so that the leg's target does not jump.
		Foot.FromFrame = From ? *From : Foot.Frame;
		Foot.FrameElapsedS = 0.0;
		Foot.FrameBlendS = BlendS;
		Foot.bFromLastFrame = bFromLast && Foot.bHasLastFrame;
		Foot.FromTransform = Foot.LastFrame;
	}
	// A foot that stays planted keeps settling (a new frame or point dropped what was left of it at once, and the
	// leg stamped the foot down by 4 cm in one frame).
	if (!(Foot.Mode == EPlacement::Planted && Mode == EPlacement::Planted))
	{
		Foot.SettleCm = 0.0;
		Foot.SettleElapsedS = 0.0;
		Foot.SettleS = 0.0;
		Foot.GlideS = 0.0;
	}
	Foot.Mode = Mode;
	Foot.Frame = Frame;
	Foot.Point = Point;
}

FVector USandboxBodyController::FlatFootPoint(int32 Side) const
{
	const FBodyInstance* Foot = Mesh->GetBodyInstance(FootBoneName[Side]);
	const FVector At = Foot && Foot->IsValidBodyInstance() ? Foot->GetUnrealWorldTransform().GetLocation() : Placement[Side].Point;
	return FVector(At.X, At.Y, Balance.SupportHeightCm + FootStandHeightCm[Side]);
}

FTransform USandboxBodyController::FlatFootTransform(const FAssist& Assist) const
{
	const FBodyInstance* Foot = Mesh->GetBodyInstance(Assist.Bone);
	if (!Foot || !Foot->IsValidBodyInstance() || Assist.Side < 0)
	{
		return Assist.Target;
	}
	const FTransform At = Foot->GetUnrealWorldTransform();
	FQuat Tilt, Yaw;
	(At.GetRotation() * Assist.StartRotation.Inverse()).ToSwingTwist(FVector::UpVector, Tilt, Yaw);
	// The floor is where the sole's lowest point is, whichever way the foot is tilted.
	TArray<FVector2D> Sole;
	const double SoleZ = FSandboxBodyProbe::SupportPolygon(Mesh, TArray<FName>{ Assist.Bone }, Tuning.Balance.SoleToleranceCm, Sole);
	const FVector Location = At.GetLocation();
	const double Z = Sole.Num() > 0 ? SoleZ + FootStandHeightCm[Assist.Side] : Location.Z;
	return FTransform((Yaw * Assist.StartRotation).GetNormalized(), FVector(Location.X, Location.Y, Z));
}

void USandboxBodyController::ReleaseFoot(int32 Side, double BlendS)
{
	FFootPlacement& Foot = Placement[Side];
	if (Foot.Mode == EPlacement::None)
	{
		return;
	}
	if (USandboxAnimInstance* Animation = Mesh ? Cast<USandboxAnimInstance>(Mesh->GetAnimInstance()) : nullptr)
	{
		// Let go from the leg as it is now, held in the frame of the body as it is now: a point fixed in the
		// world would have the leg reach for it while the body goes down, and feed the fall.
		const FBodyInstance* Body = Mesh->GetBodyInstance(FootBoneName[Side]);
		FVector Point = Body && Body->IsValidBodyInstance() ? Body->GetUnrealWorldTransform().GetLocation() : Foot.Point;
		const FTransform Frame = Mesh->GetComponentTransform();
		if (Tuning.Balance.ReleaseSmooth > 0.5 && Foot.bHasLastFrame)
		{
			// From the target the leg was solved for, held in the body: the target itself does not jump, and goes with the
			// body as it goes down.
			Point = Frame.TransformPosition(Foot.LastFrame.InverseTransformPosition(Foot.LastPoint));
			Animation->SetLimbTarget(FootBoneIndex[Side], Point, 0.0, BlendS);
			Animation->MoveLimbTarget(FootBoneIndex[Side], Point, &Frame, Foot.bHasLastEndFrame ? &Foot.LastEndFrame : nullptr, Foot.LastEndLimitWeight, Tuning.Balance.LegExtensionMax);
		}
		else
		{
			Animation->SetLimbTarget(FootBoneIndex[Side], Point, 0.0, BlendS);
			Animation->MoveLimbTarget(FootBoneIndex[Side], Point, &Frame);
		}
	}
	Foot = FFootPlacement();
}

void USandboxBodyController::UpdatePlacements(double DeltaTime)
{
	USandboxAnimInstance* Animation = Cast<USandboxAnimInstance>(Mesh->GetAnimInstance());
	if (!Animation)
	{
		return;
	}
	for (int32 Side = 0; Side < 2; ++Side)
	{
		FFootPlacement& Foot = Placement[Side];
		if (Foot.Mode == EPlacement::None)
		{
			continue;
		}
		Foot.FrameElapsedS += DeltaTime;
		Foot.SettleElapsedS += DeltaTime;
		Foot.GlideElapsedS += DeltaTime;
		const FVector Point = Foot.SettledPoint();
		const double Weight = Foot.FrameBlendS <= 0.0 || Foot.FrameElapsedS >= Foot.FrameBlendS ? 1.0
			: FMath::SmoothStep(0.0, 1.0, Foot.FrameElapsedS / Foot.FrameBlendS);
		// A foot in the air is kept level, as it would be under an upright pelvis, so that it lands flat.
		const FQuat Level = FrameTransform(EFrame::Here, -1).GetRotation();
		const FQuat* EndFrame = Foot.Mode == EPlacement::Swing ? &Level : nullptr;
		// Held to the ankle's range in the air, let go of it as the foot settles on the floor.
		double EndLimitWeight = Foot.Mode == EPlacement::Swing ? 1.0 : Foot.SettleLeft();
		FQuat EasedLevel = Level;
		if (Tuning.Balance.LiftSmooth > 0.5)
		{
			// Both come in over the lift, and the level goes out with the settling: whole from the first frame of a swing
			// they turned the ankle's target at once.
			const double Ease = Foot.Mode != EPlacement::Swing ? Foot.SettleLeft()
				: Tuning.Balance.StepEaseS > 0.0 ? FMath::SmoothStep(0.0, 1.0, FMath::Min(Swing.ElapsedS / Tuning.Balance.StepEaseS, 1.0)) : 1.0;
			EasedLevel = FQuat::Slerp(Mesh->GetComponentQuat(), Level, Ease).GetNormalized();
			EndFrame = Ease > 0.0 ? &EasedLevel : nullptr;
			EndLimitWeight = Ease;
		}
		Foot.LastPoint = Point;
		Foot.LastEndLimitWeight = EndLimitWeight;
		Foot.bHasLastEndFrame = EndFrame != nullptr;
		Foot.LastEndFrame = EndFrame ? *EndFrame : FQuat::Identity;
		if (Weight >= 1.0 && Foot.Frame == EFrame::Actual)
		{
			// The component as the animation sees it when it evaluates: the most recent there is.
			Animation->MoveLimbTarget(FootBoneIndex[Side], Point, nullptr, EndFrame, EndLimitWeight, Tuning.Balance.LegExtensionMax);
			Foot.LastFrame = Mesh->GetComponentTransform();
			Foot.bHasLastFrame = true;
			continue;
		}
		FTransform Frame = FrameTransform(Foot.Frame, Side);
		if (Weight < 1.0)
		{
			const FTransform From = Foot.bFromLastFrame ? Foot.FromTransform : FrameTransform(Foot.FromFrame, Side);
			Frame = FTransform(FQuat::Slerp(From.GetRotation(), Frame.GetRotation(), Weight).GetNormalized(),
				FMath::Lerp(From.GetLocation(), Frame.GetLocation(), Weight));
		}
		Animation->MoveLimbTarget(FootBoneIndex[Side], Point, &Frame, EndFrame, EndLimitWeight, Tuning.Balance.LegExtensionMax);
		Foot.LastFrame = Frame;
		Foot.bHasLastFrame = true;
	}
}

void USandboxBodyController::PlannedSupport(TArray<FVector2D>& OutPolygon) const
{
	OutPolygon.Reset();
	if (!Swing.bActive)
	{
		return;
	}
	TArray<FVector2D> Points;
	FSandboxBodyProbe::SupportPolygon(Mesh, TArray<FName>{ FootBoneName[1 - Swing.Side] }, Tuning.Balance.SoleToleranceCm, Points);
	Points.Append(Swing.TargetSole);
	FSandboxBodyProbe::ConvexHull2D(Points, OutPolygon);
}

bool USandboxBodyController::PlanStep(int32 Side, double TimeS, FSandboxStepRecord& OutPlan, TArray<FVector2D>& OutTargetSole,
	const TArray<FVector2D>* LiftSole, const FVector* LiftFrom, int32 KeepCross, const FVector2D* KeepAim) const
{
	const int32 Other = 1 - Side;
	const FBodyInstance* Foot = Mesh->GetBodyInstance(FootBoneName[Side]);
	const FBodyInstance* Stance = Mesh->GetBodyInstance(FootBoneName[Other]);
	if (!Foot || !Stance || !Foot->IsValidBodyInstance() || !Stance->IsValidBodyInstance() || !Balance.bValid)
	{
		return false;
	}
	const double Tolerance = Tuning.Balance.SoleToleranceCm;
	TArray<FVector2D> SwingSole, StanceSole;
	if (LiftSole)
	{
		SwingSole = *LiftSole;
	}
	else
	{
		FSandboxBodyProbe::SupportPolygon(Mesh, TArray<FName>{ FootBoneName[Side] }, Tolerance, SwingSole);
	}
	FSandboxBodyProbe::SupportPolygon(Mesh, TArray<FName>{ FootBoneName[Other] }, Tolerance, StanceSole);
	if (SwingSole.Num() == 0 || StanceSole.Num() == 0)
	{
		return false;
	}
	const FSandboxBalanceTuning& Limits = Tuning.Balance;

	// The capture point the step has to catch, and where it will be when the foot lands.
	const FVector2D Point = Balance.CapturePoint + (WeightN > 0.0 ? SustainedMomentNCm / WeightN : FVector2D::ZeroVector);
	const FVector2D Pivot = FSandboxBodyProbe::NearestPointOfPolygon(StanceSole, Point);
	const FVector2D Landing = CaptureAtLanding(Other, TimeS);
	FVector2D Way = (Landing - Pivot).GetSafeNormal();
	if (Way.IsNearlyZero())
	{
		Way = (Point - FVector2D(Balance.CenterOfMass.X, Balance.CenterOfMass.Y)).GetSafeNormal();
	}
	const FVector2D Wanted = Landing + Way * Limits.StepMarginCm;

	// The sole's middle goes there; the bone moves with it.
	FVector2D SoleMiddle = FVector2D::ZeroVector;
	for (const FVector2D& Corner : SwingSole)
	{
		SoleMiddle += Corner;
	}
	SoleMiddle /= SwingSole.Num();

	// A foot that steps to its own side of the other keeps a stance's width from it. One whose target lies past the
	// other foot's line crosses over (the owner's decision): it goes in front of the other foot, or behind it when
	// the step goes back, and lands at least balance.cross_offset_cm ahead or behind, since calves and feet collide.
	FVector SupportLocation;
	FQuat SupportRotation;
	SupportFrame(SupportLocation, SupportRotation);
	const FVector Left3 = SupportRotation.RotateVector(FVector(1.0, 0.0, 0.0));
	const FVector2D Outward = FVector2D(Left3.X, Left3.Y) * (Side == 0 ? 1.0 : -1.0);
	const FVector2D Forward = CrossForward(Other);
	const FVector From = LiftFrom ? *LiftFrom : Foot->GetUnrealWorldTransform().GetLocation();
	const FVector StanceBone = Stance->GetUnrealWorldTransform().GetLocation();
	const FVector2D FromToStance = FVector2D(From.X, From.Y) - FVector2D(StanceBone.X, StanceBone.Y);
	// In front or behind: the side that needs the smaller correction, in front when the two are near alike (a push
	// straight to the side). Chosen for the step as cut to the reach: chosen before, a foot far behind the other
	// was sent 60 cm forward to land in front of it.
	const auto Choose = [&Limits](double AheadCm)
	{
		const double FrontCm = FMath::Max(Limits.CrossOffsetCm - AheadCm, 0.0);
		const double BehindCm = FMath::Max(Limits.CrossOffsetCm + AheadCm, 0.0);
		return FrontCm <= BehindCm + 0.5 * Limits.CrossOffsetCm ? 1 : -1;
	};
	// The limits on a move: the reach, the stance's width on its own side, ahead of or behind the other foot on the
	// far side. Which side of the other foot the step lands on is decided once (Cross, -1 to 1, or anything else to
	// decide it here), when the foot lifts; a step aimed anew keeps it, or its swing's path would jump from one side
	// of the other foot to the other.
	const auto Constrain = [&](FVector2D Move, int32 InCross, FSandboxStepRecord& Flags)
	{
		Flags.bWidthLimited = false;
		Flags.bReachLimited = Move.Size() > Limits.StepReachCm;
		if (Flags.bReachLimited)
		{
			Move = Move.GetSafeNormal() * Limits.StepReachCm;
		}
		double Apart = FVector2D::DotProduct(FromToStance + Move, Outward);
		const bool bKept = InCross >= -1 && InCross <= 1;
		const bool bAcross = bKept ? InCross != 0 : Apart < 0.0;
		int32 Cross = 0;
		if (!bAcross)
		{
			if (Apart < Limits.StepWidthMinCm)
			{
				Flags.bWidthLimited = true;
				Move += Outward * (Limits.StepWidthMinCm - Apart);
			}
		}
		else
		{
			// Kept on the far side it was aimed at.
			if (Apart > 0.0)
			{
				Move -= Outward * Apart;
				Apart = 0.0;
			}
			// And no further past the other foot than balance.cross_past_cm: a leg reaches across the body only so far,
			// and the step across is the first of two (the other foot, unloaded by it, steps out next). Sent to catch the
			// capture point alone, it was cut to the reach in three of four long chains of steps and landed short.
			if (-Apart > Limits.CrossPastCm)
			{
				Move += Outward * (-Apart - Limits.CrossPastCm);
				Apart = -Limits.CrossPastCm;
				Flags.bReachLimited = true;
			}
			Cross = bKept ? InCross : Choose(FVector2D::DotProduct(FromToStance + Move, Forward));
			// Ahead of (or behind) the other foot by the offset, while it is not a stance's width to the side of it.
			const double Ahead = FVector2D::DotProduct(FromToStance + Move, Forward) * Cross;
			if (-Apart < Limits.StepWidthMinCm && Ahead < Limits.CrossOffsetCm)
			{
				Move += Forward * (Cross * (Limits.CrossOffsetCm - Ahead));
				Flags.bWidthLimited = true;
			}
		}
		Flags.Cross = Cross;
		// The swing goes round the other foot when it starts on one side of it and ends on the other: over to the far
		// side on the side it lands, back from the far side on the side the foot is.
		const double FromApart = FVector2D::DotProduct(FromToStance, Outward);
		Flags.Pass = 0;
		if ((FromApart < 0.0) != bAcross)
		{
			Flags.Pass = bAcross ? Cross : Choose(FVector2D::DotProduct(FromToStance, Forward));
		}
		// Within reach whatever the corrections asked for.
		if (Move.Size() > Limits.StepReachCm)
		{
			Move = Move.GetSafeNormal() * Limits.StepReachCm;
			Flags.bReachLimited = true;
		}
		return Move;
	};
	// How far inside the support a move would put the capture point at landing: the least over the way from where
	// it is now to where it is predicted. The prediction (running away from the stance sole's nearest point) is the
	// best the stance foot can do and can be far off in direction when the capture point is near that sole; scored
	// on the prediction alone, a capture point 5 cm ahead of both feet sent the stepping foot 8 cm back.
	const FVector2D Midway = 0.5 * (Point + Landing);
	const auto MarginOf = [&](const FVector2D& Move)
	{
		TArray<FVector2D> Points = StanceSole;
		for (const FVector2D& Corner : SwingSole)
		{
			Points.Add(Corner + Move);
		}
		TArray<FVector2D> Hull;
		FSandboxBodyProbe::ConvexHull2D(Points, Hull);
		if (Hull.Num() == 0)
		{
			return -1.0e6;
		}
		return FMath::Min3(FSandboxBodyProbe::MarginInsidePolygon(Hull, Landing), FSandboxBodyProbe::MarginInsidePolygon(Hull, Midway),
			FSandboxBodyProbe::MarginInsidePolygon(Hull, Point));
	};

	// The aimed point, and around it a few others along the capture point's way and across it: the one that leaves
	// the capture point at least balance.step_margin_goal_cm inside the new support, as near the aimed point as
	// can be. (Aimed at the point alone, a step to the side and back put the feet in a line along the way, the
	// capture point landed on the edge of their narrow hull, and the body stepped ten times before it stood.) A step
	// aimed anew keeps the offset it was given at the lift (KeepAim), so that its target does not hop between them.
	const FVector2D Across(-Way.Y, Way.X);
	FSandboxStepRecord Nominal;
	FVector2D Move = Constrain(Wanted - SoleMiddle, KeepCross, Nominal);
	const int32 Cross = Nominal.Cross;
	FVector2D BestAim = FVector2D::ZeroVector;
	if (KeepAim)
	{
		BestAim = *KeepAim;
		Move = Constrain(Wanted + Way * BestAim.X + Across * BestAim.Y - SoleMiddle, Cross, Nominal);
	}
	else if (Limits.StepMarginGoalCm > 0.0)
	{
		double BestScore = -1.0e9;
		static const double Offsets[] = { 0.0, -5.0, 5.0, -10.0, 10.0, -15.0, 15.0 };
		for (const double AlongCm : { 0.0, 5.0, 10.0, 15.0, -5.0 })
		{
			for (const double AcrossCm : Offsets)
			{
				FSandboxStepRecord Flags;
				const FVector2D Candidate = Constrain(Wanted + Way * AlongCm + Across * AcrossCm - SoleMiddle, Cross, Flags);
				// Of two offsets alike, the one toward the other foot: a rule that mirror-image pushes mirror.
				const double Inward = FVector2D::DotProduct(Across, Outward) > 0.0 ? -AcrossCm : AcrossCm;
				const double Score = FMath::Min(MarginOf(Candidate), Limits.StepMarginGoalCm) - 0.05 * (FMath::Abs(AlongCm) + FMath::Abs(AcrossCm))
					+ 0.001 * Inward;
				if (Score > BestScore + KINDA_SMALL_NUMBER)
				{
					BestScore = Score;
					BestAim = FVector2D(AlongCm, AcrossCm);
					Move = Candidate;
					Nominal = Flags;
				}
			}
		}
	}
	OutPlan.bWidthLimited = Nominal.bWidthLimited;
	OutPlan.bReachLimited = Nominal.bReachLimited;
	OutPlan.Cross = Nominal.Cross;
	OutPlan.Pass = Nominal.Pass;
	OutPlan.AimOffsetCm = BestAim;

	// Down at the height the foot's bone has standing flat on the floor the body stands on. (The height of the
	// foot's anchor was that of a foot planted on its toe, 4 cm up, and the step never reached the floor.)
	const double PlantedZ = Balance.SupportHeightCm + FootStandHeightCm[Side];
	OutPlan.Side = Side;
	OutPlan.Foot = FootBoneName[Side];
	OutPlan.From = From;
	OutPlan.Target = FVector(From.X + Move.X, From.Y + Move.Y, PlantedZ);
	OutPlan.CapturePoint = Point;
	OutPlan.CaptureAtLanding = Landing;

	// What the step would give: the stance sole and this one at its target, and the capture point in it.
	OutTargetSole = SwingSole;
	for (FVector2D& Corner : OutTargetSole)
	{
		Corner += Move;
	}
	FVector2D Unused;
	OutPlan.PlannedMarginCm = LandingMarginCm(Side, OutTargetSole, TimeS, Unused);
	return true;
}

FVector2D USandboxBodyController::CrossForward(int32 StanceSide) const
{
	// The foot's own turn, not the support's: after steps across, the two feet can point apart, and the calves to be
	// passed stand along the stance foot.
	const FAssist* FootAssist = Assists.FindByPredicate([StanceSide](const FAssist& Assist) { return Assist.Slot == EAssistSlot::Feet && Assist.Side == StanceSide; });
	const FBodyInstance* Foot = Mesh->GetBodyInstance(FootBoneName[StanceSide]);
	FQuat Yaw = FQuat::Identity;
	if (FootAssist && Foot && Foot->IsValidBodyInstance())
	{
		FQuat Tilt;
		(Foot->GetUnrealWorldTransform().GetRotation() * FootAssist->StartRotation.Inverse()).ToSwingTwist(FVector::UpVector, Tilt, Yaw);
	}
	// The character faces +y at Start.
	const FVector Forward3 = Yaw.RotateVector(FVector(0.0, 1.0, 0.0));
	return FVector2D(Forward3.X, Forward3.Y).GetSafeNormal();
}

FVector2D USandboxBodyController::CaptureAtLanding(int32 StanceSide, double TimeS) const
{
	// Moved by a held push as the decision moves it; it runs away from the stance sole (the centre of pressure as
	// far toward it as the foot reaches) by e^(omega t).
	const FVector2D Point = Balance.CapturePoint + (WeightN > 0.0 ? SustainedMomentNCm / WeightN : FVector2D::ZeroVector);
	TArray<FVector2D> StanceSole;
	FSandboxBodyProbe::SupportPolygon(Mesh, TArray<FName>{ FootBoneName[StanceSide] }, Tuning.Balance.SoleToleranceCm, StanceSole);
	if (StanceSole.Num() == 0 || !Balance.bValid)
	{
		return Point;
	}
	const FVector2D Pivot = FSandboxBodyProbe::NearestPointOfPolygon(StanceSole, Point);
	const double HeightCm = FMath::Max(Balance.CenterOfMass.Z - Balance.SupportHeightCm, 1.0);
	const double Growth = FMath::Exp(FMath::Sqrt(GravityCmS2 / HeightCm) * TimeS);
	return Pivot + (Point - Pivot) * Growth;
}

double USandboxBodyController::LandingMarginCm(int32 Side, const TArray<FVector2D>& TargetSole, double TimeS, FVector2D& OutLanding) const
{
	const int32 Other = 1 - Side;
	TArray<FVector2D> Points;
	FSandboxBodyProbe::SupportPolygon(Mesh, TArray<FName>{ FootBoneName[Other] }, Tuning.Balance.SoleToleranceCm, Points);
	Points.Append(TargetSole);
	TArray<FVector2D> Planned;
	FSandboxBodyProbe::ConvexHull2D(Points, Planned);
	OutLanding = CaptureAtLanding(Other, TimeS);
	return Planned.Num() > 0 ? FSandboxBodyProbe::MarginInsidePolygon(Planned, OutLanding) : 0.0;
}

void USandboxBodyController::UpdateShift(double DeltaTime)
{
	ShiftGoalCm = FVector2D::ZeroVector;
	if (Pending.bActive)
	{
		// From the middle of the two soles toward the middle of the stance sole: sideways when the feet stand side
		// by side, forward or back too when one stands ahead of the other.
		TArray<FVector2D> Soles[2];
		for (int32 Each = 0; Each < 2; ++Each)
		{
			FSandboxBodyProbe::SupportPolygon(Mesh, TArray<FName>{ FootBoneName[Each] }, Tuning.Balance.SoleToleranceCm, Soles[Each]);
		}
		if (Soles[0].Num() > 0 && Soles[1].Num() > 0)
		{
			const auto Middle = [](const TArray<FVector2D>& Sole)
			{
				FVector2D Sum = FVector2D::ZeroVector;
				for (const FVector2D& Corner : Sole)
				{
					Sum += Corner;
				}
				return Sum / Sole.Num();
			};
			const int32 Stance = 1 - Pending.Plan.Side;
			ShiftGoalCm = (Middle(Soles[Stance]) - Middle(Soles[Pending.Plan.Side])) * (0.5 * Tuning.Balance.StepShiftShare);
			if (Pending.Plan.bRegroup && Pending.TargetSole.Num() > 0)
			{
				// A regrouping step starts from wherever the recovery steps left the feet (a lunge of 50 cm), and ends
				// with the feet side by side. The body goes to the stance foot, short of its middle on the side the other
				// foot will come down on (balance.regroup_inside_share of the way to that foot's place): on one leg it
				// then leans toward the foot that is coming, as in a walking step, not away from it. The goal is moved
				// further for as long as the capture point is not there (RegroupLead).
				// The whole sole of the stance foot, also the part a raised heel has lifted off the floor.
				TArray<FVector2D> WholeSole;
				FSandboxBodyProbe::SupportPolygon(Mesh, TArray<FName>{ FootBoneName[Stance] }, WholeSoleToleranceCm, WholeSole);
				const FVector2D StanceMiddle = WholeSole.Num() > 0 ? Middle(WholeSole) : Middle(Soles[Stance]);
				const FVector2D Between = (StanceMiddle + Middle(Soles[Pending.Plan.Side])) * 0.5;
				RegroupPoint = FMath::Lerp(StanceMiddle, Middle(Pending.TargetSole), Tuning.Balance.RegroupInsideShare);
				RegroupBaseCm = RegroupPoint - Between;
				ShiftGoalCm = RegroupBaseCm + RegroupLead(DeltaTime);
			}
		}
	}
	else if (Swing.bActive && bShiftHeld && StepLog.Num() > 0 && StepLog.Last().bRegroup)
	{
		// A regrouping step ends with the feet side by side under the body: the body is held where the shift took it
		// while the foot swings, the same way. (Let go along the step like any other shift, the goal went back toward
		// the place the foot had left.)
		ShiftGoalCm = RegroupBaseCm + RegroupLead(DeltaTime);
	}
	else if (Swing.bActive && bShiftHeld)
	{
		// The weight stays on the stance foot while the other swings, and what of the shift lies along the step
		// goes over to the new foot as it comes down. (Let go along the step from the start, the body went with it,
		// and a prediction of where over 0.4 s of swing, grown by e^(omega t) = 3.7, put it 10 to 15 cm past the
		// new feet. Held whole, a step to the side landed 51 cm from the stance foot with the pelvis still over
		// it: the new leg could not reach its foot and pushed the body away.) A step forward or back from feet
		// side by side has no shift along it.
		const FVector2D Along = FVector2D(Swing.Target.X - Swing.From.X, Swing.Target.Y - Swing.From.Y).GetSafeNormal();
		const FVector2D AlongCm = Along * FVector2D::DotProduct(ShiftAtLiftCm, Along);
		ShiftGoalCm = ShiftAtLiftCm - AlongCm * Swing.Descent;
	}
	// Out of the shift as the anchors glide to the new support (balance.anchor_follow_s): let go sooner, the
	// pelvis's goal went back toward the old support first and the legs threw the body sideways.
	const double LagS = ShiftGoalCm.IsZero() ? Tuning.Balance.AnchorFollowS : Tuning.Balance.StepShiftS;
	const double Follow = LagS > 0.0 ? 1.0 - FMath::Exp(-DeltaTime / LagS) : 1.0;
	ShiftCm += (ShiftGoalCm - ShiftCm) * Follow;
	if (ShiftGoalCm.IsZero() && ShiftCm.Size() < 1.0e-4)
	{
		ShiftCm = FVector2D::ZeroVector;
	}
}

FVector2D USandboxBodyController::RegroupLead(double DeltaTime)
{
	// The pelvis's goal is moved for as long as the capture point is not where the body is to stand: the legs' frames
	// follow the body, and the leg left behind in a lunge holds it back, so a goal set once is not reached.
	// By the distance still to go, plus a part that grows while it stays (balance.regroup_shift_gain_per_s), no
	// further than balance.regroup_lead_max_cm: a goal far beyond the stance foot folds the leg under it.
	const FSandboxBalanceTuning& Limits = Tuning.Balance;
	const FVector2D ErrorCm = RegroupPoint - Balance.CapturePoint;
	RegroupIntegralCm += ErrorCm * (Limits.RegroupShiftGainPerS * DeltaTime);
	if (RegroupIntegralCm.Size() > Limits.RegroupLeadMaxCm)
	{
		RegroupIntegralCm = RegroupIntegralCm.GetSafeNormal() * Limits.RegroupLeadMaxCm;
	}
	FVector2D LeadCm = ErrorCm * Limits.RegroupLeadGain + RegroupIntegralCm;
	if (LeadCm.Size() > Limits.RegroupLeadMaxCm)
	{
		LeadCm = LeadCm.GetSafeNormal() * Limits.RegroupLeadMaxCm;
	}
	return LeadCm;
}

void USandboxBodyController::StartSwing(const FSandboxStepRecord& Plan, const TArray<FVector2D>& TargetSole, double DurationS, bool bReaim)
{
	const FSandboxBalanceTuning& Limits = Tuning.Balance;
	const int32 Side = Plan.Side;
	const int32 Other = 1 - Side;
	FSandboxStepRecord& Record = StepLog.Add_GetRef(Plan);
	Record.Number = ++StaggerSteps;
	Record.StartS = ElapsedS;
	Swing.bActive = true;
	Swing.Side = Side;
	Swing.ElapsedS = 0.0;
	Swing.From = Record.From;
	Swing.Target = Record.Target;
	// Where the leg was solved for until now. (Started from where the foot was, the target of a leg whose foot had been
	// carried 20 cm from its point while the body was off the floor jumped by that much, and the stiffened swing leg
	// snapped its calf at 31 to 48 rad/s.)
	Swing.StartOffset = Placement[Side].Mode != EPlacement::None ? Placement[Side].SettledPoint() - Record.From : FVector::ZeroVector;
	Swing.Point = Record.From + Swing.StartOffset;
	Swing.VelocityCmS = FVector::ZeroVector;
	Swing.Across = 0.0;
	Swing.Descent = 0.0;
	Swing.DurationS = DurationS;
	Swing.TargetSole = TargetSole;
	// The sole at the lift, which the aim moves: the target sole less the plan's move.
	const FVector2D Move(Record.Target.X - Record.From.X, Record.Target.Y - Record.From.Y);
	Swing.LiftSole = TargetSole;
	for (FVector2D& Corner : Swing.LiftSole)
	{
		Corner -= Move;
	}
	Swing.bReaim = bReaim;
	Swing.Cross = Record.Cross;
	Swing.Pass = Record.Pass;
	if (Swing.Pass != 0)
	{
		// The swing passes the other foot's bone this far ahead of it (or behind).
		const FVector StanceAt = Mesh->GetBodyInstance(FootBoneName[Other])->GetUnrealWorldTransform().GetLocation();
		Swing.PassPoint = FVector2D(StanceAt.X, StanceAt.Y) + CrossForward(Other) * (Swing.Pass * Limits.CrossOffsetCm);
	}
	PlaceFoot(Side, EPlacement::Swing, Swing.Point, EFrame::Actual, Limits.LiftFrameS > 0.0 ? Limits.LiftFrameS : Limits.StepEaseS, nullptr, Limits.LiftSmooth > 0.5);
	// The other leg carries the body while the foot is in the air.
	const FVector StancePoint = Placement[Other].Mode != EPlacement::None ? Placement[Other].Point : FlatFootPoint(Other);
	PlaceFoot(Other, EPlacement::Planted, StancePoint, EFrame::Stance, Limits.StepEaseS);
	PushTuningToControls();
	UE_LOG(LogSandbox, Display, TEXT("Body controller: step %d with %s at %.3f s, %.1f cm toward (%.1f, %.1f)%s%s"),
		Record.Number, *Record.Foot.ToString(), ElapsedS, (Record.Target - Record.From).Size2D(), Record.Target.X, Record.Target.Y,
		Record.bReachLimited ? TEXT(", cut to the reach") : TEXT(""),
		Record.Pass > 0 ? TEXT(", round the other foot in front") : Record.Pass < 0 ? TEXT(", round the other foot behind")
			: Record.Cross != 0 ? TEXT(", on the far side") : Record.bWidthLimited ? TEXT(", kept to its side") : TEXT(""));
}

bool USandboxBodyController::RequestStep(FName Foot, const FVector& WorldTarget, FString& OutError)
{
	const int32 Side = Foot == FootBoneName[0] ? 0 : Foot == FootBoneName[1] ? 1 : -1;
	if (!bStarted || !IsUpright() || IsStepUnderWay() || Side < 0 || !Cast<USandboxAnimInstance>(Mesh->GetAnimInstance()))
	{
		OutError = Side < 0 ? FString::Printf(TEXT("'%s' is not a foot of the rig"), *Foot.ToString())
			: TEXT("the body is not upright under control, or a step is under way");
		return false;
	}
	FSandboxStepRecord Plan;
	TArray<FVector2D> Sole;
	if (!PlanStep(Side, Tuning.Balance.StepTimeS, Plan, Sole))
	{
		OutError = TEXT("the feet have no soles to step with");
		return false;
	}
	// The plan's own target is replaced by the one asked for; its sole moves with it.
	const FVector2D Shift(WorldTarget.X - Plan.Target.X, WorldTarget.Y - Plan.Target.Y);
	for (FVector2D& Corner : Sole)
	{
		Corner += Shift;
	}
	Plan.Target = FVector(WorldTarget.X, WorldTarget.Y, Plan.Target.Z);
	Plan.bReachLimited = false;
	Plan.bWidthLimited = false;
	Plan.Cross = 0;
	Plan.Pass = 0;
	// A step is a step, asked for or not: the body is on one leg while it lasts, and stands again only once its
	// balance is back (the stagger's own way out).
	if (State != ESandboxBodyState::Stagger)
	{
		RecoveredS = 0.0;
		StaggerSteps = 0;
		EnterState(ESandboxBodyState::Stagger, FString::Printf(TEXT("a step with %s was asked for"), *Foot.ToString()));
	}
	// First the weight goes over onto the other foot (UpdateStep lifts this one when the step would hold): both
	// legs are solved over the pelvis's goal, which moves toward the stance foot.
	Pending.bActive = true;
	Pending.Plan = Plan;
	Pending.TargetSole = Sole;
	Pending.ElapsedS = 0.0;
	const FSandboxBalanceTuning& Limits = Tuning.Balance;
	for (int32 Each = 0; Each < 2; ++Each)
	{
		const FVector Point = Placement[Each].Mode != EPlacement::None ? Placement[Each].Point : FlatFootPoint(Each);
		PlaceFoot(Each, EPlacement::Planted, Point, EFrame::Stance, Limits.StepEaseS);
	}
	return true;
}

FVector USandboxBodyController::StanceError(FVector OutFootAt[2], FVector& OutStance) const
{
	FVector SupportLocation;
	FQuat SupportRotation;
	SupportFrame(SupportLocation, SupportRotation);
	OutFootAt[0] = SupportLocation;
	OutFootAt[1] = SupportLocation;
	for (const FAssist& Assist : Assists)
	{
		if (Assist.Slot == EAssistSlot::Feet && Assist.Side >= 0)
		{
			OutFootAt[Assist.Side] = Assist.Target.GetLocation();
		}
	}
	OutStance = SupportRotation.RotateVector(FVector(StanceRightFromLeft, 0.0));
	return (OutFootAt[1] - OutFootAt[0]) - OutStance;
}

double USandboxBodyController::GetStanceErrorCm() const
{
	FVector FootAt[2];
	FVector Stance;
	return bStarted ? StanceError(FootAt, Stance).Size2D() : 0.0;
}

void USandboxBodyController::UpdateRegroup(double DeltaTime)
{
	const FSandboxBalanceTuning& Limits = Tuning.Balance;
	// Only a body that stands quietly after steps of its own brings its feet together: both feet on the floor, at
	// rest, the capture point inside the support, and nothing pushing it (a body braced against a held push keeps
	// the stance it braced in).
	constexpr double BracedCm = 1.0;
	const bool bQuiet = State == ESandboxBodyState::Standing && RegroupLeft > 0 && Limits.RegroupWaitS > 0.0 && PlacedFeet.Num() == 0 &&
		LeanCm.Size() < BracedCm &&
		!IsStepUnderWay() && Balance.bValid && Balance.FootForceN[0] > 0.0 && Balance.FootForceN[1] > 0.0 &&
		Balance.Velocity.Size2D() <= Limits.RecoverSpeedMps * 100.0 && LastDecisionMarginCm > 0.0;
	RegroupQuietS = bQuiet ? RegroupQuietS + DeltaTime : 0.0;
	if (!bQuiet || RegroupQuietS < Limits.RegroupWaitS - KINDA_SMALL_NUMBER)
	{
		return;
	}
	RegroupQuietS = 0.0;
	FVector FootAt[2];
	FVector Stance;
	const FVector Off = StanceError(FootAt, Stance);
	if (Off.Size2D() <= Limits.RegroupToleranceCm)
	{
		RegroupLeft = 0;
		return;
	}
	// Either foot can close the stance. The one to move is the one whose move leaves the feet under the body: the
	// middle of the stance it would make is nearer the centre of mass.
	const FVector Targets[2] = { FootAt[1] - Stance, FootAt[0] + Stance };
	const FVector Middle[2] = { FootAt[1] - Stance * 0.5, FootAt[0] + Stance * 0.5 };
	const int32 Side = FVector::DistSquared2D(Middle[0], Balance.CenterOfMass) <= FVector::DistSquared2D(Middle[1], Balance.CenterOfMass) ? 0 : 1;
	--RegroupLeft;
	FString Error;
	if (RequestStep(FootBoneName[Side], Targets[Side], Error))
	{
		Pending.Plan.bRegroup = true;
		RegroupIntegralCm = FVector2D::ZeroVector;
		// The whole sole where it will stand: a foot left behind in a lunge stands on its toes, and the part of its
		// sole that touches the floor is not what it will stand on.
		TArray<FVector2D> WholeSole;
		FSandboxBodyProbe::SupportPolygon(Mesh, TArray<FName>{ FootBoneName[Side] }, WholeSoleToleranceCm, WholeSole);
		const FBodyInstance* Foot = Mesh->GetBodyInstance(FootBoneName[Side]);
		if (WholeSole.Num() > 0 && Foot && Foot->IsValidBodyInstance())
		{
			const FVector At = Foot->GetUnrealWorldTransform().GetLocation();
			const FVector2D Move(Targets[Side].X - At.X, Targets[Side].Y - At.Y);
			for (FVector2D& Corner : WholeSole)
			{
				Corner += Move;
			}
			Pending.TargetSole = WholeSole;
		}
		UE_LOG(LogSandbox, Display, TEXT("Body controller: the feet stand %.1f cm from their stance; %s steps back to its place"),
			Off.Size2D(), *FootBoneName[Side].ToString());
	}
}

void USandboxBodyController::UpdateStep(double DeltaTime)
{
	const FSandboxBalanceTuning& Limits = Tuning.Balance;
	if (!IsUpright())
	{
		// A body that falls has let go of its feet already (EnterState).
		Swing.bActive = false;
		return;
	}
	// Legs placed from outside (a scenario's foot_target) are not the controller's to step with.
	if (PlacedFeet.Num() > 0 || !Cast<USandboxAnimInstance>(Mesh->GetAnimInstance()))
	{
		return;
	}

	if (Pending.bActive)
	{
		// The foot lifts once the weight is over the stance foot (the capture point within
		// balance.step_shift_ready_cm of its sole: it can carry the body alone while the other swings), or when
		// the shift has taken long enough.
		Pending.ElapsedS += DeltaTime;
		FVector2D Landing;
		const double MarginCm = LandingMarginCm(Pending.Plan.Side, Pending.TargetSole, Limits.StepAskedTimeS, Landing);
		TArray<FVector2D> StanceSole;
		FSandboxBodyProbe::SupportPolygon(Mesh, TArray<FName>{ FootBoneName[1 - Pending.Plan.Side] }, Limits.SoleToleranceCm, StanceSole);
		const FVector2D Point = Balance.CapturePoint + (WeightN > 0.0 ? SustainedMomentNCm / WeightN : FVector2D::ZeroVector);
		bool bOver = StanceSole.Num() > 0 && FSandboxBodyProbe::MarginInsidePolygon(StanceSole, Point) >= -Limits.StepShiftReadyCm;
		if (Pending.Plan.bRegroup)
		{
			// A regrouping step is not needed for balance, so its foot lifts only when the step will hold: the capture
			// point, where it will be when the foot lands, inside the support the step gives by balance.regroup_margin_cm.
			// That is a body on its way onto the stance foot, as a person pushes off the foot left behind, not one that
			// has come to rest over it. (Waiting for the centre of mass to stand over the stance sole took 0.5 to 1.2 s
			// and a pelvis's goal far beyond that foot; out of a deep lunge the body did not get there at all.) A body
			// that does not get that far keeps its feet where they are.
			bOver = MarginCm >= Limits.RegroupMarginCm;
			if (!bOver)
			{
				if (Pending.ElapsedS >= Limits.RegroupShiftMaxS - KINDA_SMALL_NUMBER)
				{
					Pending.bActive = false;
					RegroupLeft = 0;
					UE_LOG(LogSandbox, Display, TEXT("Body controller: the weight did not come over the stance foot in %.2f s; the feet stay where they are (capture point (%.1f, %.1f), wanted at (%.1f, %.1f); at landing (%.1f, %.1f), %.1f cm inside the support to come; swing foot carries %.0f N; pelvis lowered %.1f cm)"),
						Pending.ElapsedS, Point.X, Point.Y, RegroupPoint.X, RegroupPoint.Y, Landing.X, Landing.Y, MarginCm, Balance.FootForceN[Pending.Plan.Side], LowerCm);
				}
				return;
			}
		}
		if (bOver || Pending.ElapsedS >= Limits.StepShiftMaxS - KINDA_SMALL_NUMBER)
		{
			Pending.bActive = false;
			Pending.Plan.CapturePoint = Balance.CapturePoint + (WeightN > 0.0 ? SustainedMomentNCm / WeightN : FVector2D::ZeroVector);
			Pending.Plan.CaptureAtLanding = Landing;
			Pending.Plan.PlannedMarginCm = MarginCm;
			Pending.Plan.From = Mesh->GetBodyInstance(FootBoneName[Pending.Plan.Side])->GetUnrealWorldTransform().GetLocation();
			UE_LOG(LogSandbox, Display, TEXT("Body controller: weight shifted for %.3f s%s"), Pending.ElapsedS,
				bOver ? TEXT("") : TEXT(", not yet over the stance foot; the foot lifts anyway"));
			bShiftHeld = true;
			ShiftAtLiftCm = ShiftCm;
			StartSwing(Pending.Plan, Pending.TargetSole, Limits.StepAskedTimeS, false);
		}
		else
		{
			return;
		}
	}

	if (!Swing.bActive)
	{
		// Recovery steps are the stagger's; a standing body steps only when asked to (RequestStep).
		if (State != ESandboxBodyState::Stagger || !bStepNeeded)
		{
			return;
		}
		FSandboxStepRecord Plans[2];
		TArray<FVector2D> Soles[2];
		// Only a foot whose other foot is on the floor can step: the body has to stand on something meanwhile.
		// (A blow that lifts one foot leaves that foot as the only one that can.)
		bool bPlanned[2] = { false, false };
		for (int32 Each = 0; Each < 2; ++Each)
		{
			bPlanned[Each] = Balance.FootForceN[1 - Each] > 0.0 && PlanStep(Each, Limits.StepTimeS, Plans[Each], Soles[Each]);
		}
		if (!bPlanned[0] && !bPlanned[1])
		{
			return;
		}
		int32 Side = 0;
		if (StaggerSteps > 0 && StepLog.Num() > 0 && bPlanned[1 - StepLog.Last().Side])
		{
			// Step after step, the feet take turns.
			Side = 1 - StepLog.Last().Side;
		}
		else if (bPlanned[0] && bPlanned[1])
		{
			// The first step is made by the foot that catches the capture point better, and of two that catch it
			// alike, by the one that has less far to go. Mirror-image pushes have to choose mirror-image feet, so
			// near-equal figures count as equal, and then the foot on the side the capture point runs to steps, or,
			// straight ahead or back, the one that carries less.
			constexpr double SameCm = 1.0;
			const double Way[2] = { (Plans[0].Target - Plans[0].From).Size2D(), (Plans[1].Target - Plans[1].From).Size2D() };
			// A step across the other foot is not held back: with the weight already on the near foot, lifting it
			// leaves the body on the far one, from which the capture point runs away much faster (40 cm out instead
			// of 15 after a push to the pelvis's side: to 81 cm by the landing instead of 51).
			if (FMath::Abs(Plans[0].PlannedMarginCm - Plans[1].PlannedMarginCm) > SameCm)
			{
				Side = Plans[0].PlannedMarginCm > Plans[1].PlannedMarginCm ? 0 : 1;
			}
			else if (FMath::Abs(Way[0] - Way[1]) > SameCm)
			{
				Side = Way[0] < Way[1] ? 0 : 1;
			}
			else
			{
				FVector SupportLocation;
				FQuat SupportRotation;
				SupportFrame(SupportLocation, SupportRotation);
				const FVector Left3 = SupportRotation.RotateVector(FVector(1.0, 0.0, 0.0));
				const FVector2D Runs = Plans[0].CaptureAtLanding - FVector2D(Balance.CenterOfMass.X, Balance.CenterOfMass.Y);
				const double Sideways = FVector2D::DotProduct(Runs.GetSafeNormal(), FVector2D(Left3.X, Left3.Y));
				constexpr double StraightCos = 0.1;
				Side = FMath::Abs(Sideways) > StraightCos ? (Sideways > 0.0 ? 0 : 1) : (Balance.FootForceN[0] <= Balance.FootForceN[1] ? 0 : 1);
			}
		}
		else
		{
			Side = bPlanned[0] ? 0 : 1;
		}
		const int32 Other = 1 - Side;
		UE_LOG(LogSandbox, Display, TEXT("Body controller: feet to step with: left %s (margin %.1f cm%s), right %s (margin %.1f cm%s)"),
			bPlanned[0] ? TEXT("can") : TEXT("cannot"), Plans[0].PlannedMarginCm, Plans[0].Cross != 0 ? TEXT(", across") : TEXT(""),
			bPlanned[1] ? TEXT("can") : TEXT("cannot"), Plans[1].PlannedMarginCm, Plans[1].Cross != 0 ? TEXT(", across") : TEXT(""));
		// The other foot has to be on the floor to carry the body, and after a step it has to have taken weight.
		const double OtherMinN = StaggerSteps > 0 ? Limits.FootContactMinN : 0.0;
		if (!bPlanned[Side] || Balance.FootForceN[Other] <= OtherMinN)
		{
			return;
		}

		StartSwing(Plans[Side], Soles[Side], Limits.StepTimeS, true);
	}

	// The swing: the foot's point travels to the target, eased in and out, over an arc, and ends a little below
	// the floor, so that the leg sets the foot down on it whatever the pelvis has done meanwhile.
	Swing.ElapsedS += DeltaTime;
	const double DurationS = FMath::Max(Swing.DurationS, DeltaTime);
	const double Progress = FMath::Clamp(Swing.ElapsedS / DurationS, 0.0, 1.0);
	// Over the target by StepOverShare of the swing: up in the first half of that, then across at the top, then
	// down to below the floor in the rest.
	const double Over = FMath::Clamp(Limits.StepOverShare, KINDA_SMALL_NUMBER, 1.0 - KINDA_SMALL_NUMBER);
	// A recovery step is aimed anew, over the time it has left, until its foot starts down. Aimed once at the lift,
	// the capture point was taken to run away from the stance foot, which a blow had lifted off the floor for the
	// first 0.15 s: the step went 17 cm to the side of where the capture point went.
	if (Swing.bReaim && Progress < Over)
	{
		FSandboxStepRecord Aim;
		TArray<FVector2D> AimSole;
		const FVector2D KeepAim = StepLog.Last().AimOffsetCm;
		if (PlanStep(Swing.Side, FMath::Max(DurationS - Swing.ElapsedS, 0.0), Aim, AimSole, &Swing.LiftSole, &Swing.From, Swing.Cross, &KeepAim))
		{
			Swing.Target = Aim.Target;
			Swing.TargetSole = AimSole;
			FSandboxStepRecord& Record = StepLog.Last();
			Record.Target = Aim.Target;
			Record.bReachLimited = Aim.bReachLimited;
			Record.bWidthLimited = Aim.bWidthLimited;
			Record.Cross = Aim.Cross;
		}
	}
	const double Across =FMath::SmoothStep(0.0, 1.0, FMath::Min(Progress / Over, 1.0));
	// Down from balance.step_descent_share of the swing, which may begin before the foot is over its target: down only
	// after it (in the last 20% of a 0.25 s swing), the foot met the floor at 1.7 m/s with 1400 N, the slanted leg threw
	// the pelvis back, and a 30 N*s push caught with 10 cm to spare took a second step out of the other side.
	const double DescentFrom = FMath::Clamp(Limits.StepDescentShare, KINDA_SMALL_NUMBER, 1.0 - KINDA_SMALL_NUMBER);
	const double Descent = Progress > DescentFrom ? FMath::SmoothStep(0.0, 1.0, (Progress - DescentFrom) / (1.0 - DescentFrom)) : 0.0;
	// The foot leaves the floor at rest and speeds up. (Raised along a sine, it started at its fastest, 1.8 m/s, and the
	// leg drove its toe into the floor as it lifted: up to 3800 N, the stance foot thrown off the floor.)
	const double Rise = FMath::SmoothStep(0.0, 1.0, FMath::Min(Progress / (0.5 * Over), 1.0));
	// The swing leg stiffens over balance.step_ease_s (PushTuningToControls), not at once.
	if (Swing.ElapsedS <= Limits.StepEaseS + DeltaTime)
	{
		PushTuningToControls();
	}
	const FVector LastPoint = Swing.Point;
	Swing.Across = Across;
	Swing.Descent = Descent;
	Swing.Point = FMath::Lerp(Swing.From, Swing.Target, Across) +
		FVector(0.0, 0.0, Limits.StepLiftCm * Rise * (1.0 - Descent) - Limits.StepPressCm * Descent);
	if (Swing.Pass != 0)
	{
		// Across the other foot: over the floor along a quadratic curve through the pass point (halfway), so that
		// the foot goes round the other foot instead of through it.
		const FVector2D From2(Swing.From.X, Swing.From.Y);
		const FVector2D Target2(Swing.Target.X, Swing.Target.Y);
		const FVector2D Control2 = 2.0 * Swing.PassPoint - 0.5 * (From2 + Target2);
		const FVector2D Curve = From2 * FMath::Square(1.0 - Across) + Control2 * (2.0 * Across * (1.0 - Across)) + Target2 * FMath::Square(Across);
		Swing.Point.X = Curve.X;
		Swing.Point.Y = Curve.Y;
	}
	Swing.Point += Swing.StartOffset * (1.0 - Across);
	Swing.VelocityCmS = DeltaTime > 0.0 ? (Swing.Point - LastPoint) / DeltaTime : FVector::ZeroVector;
	Placement[Swing.Side].Point = Swing.Point;
	// The assist's foot-planting control carries the foot along: its target is the swing's point, moving.
	for (const FAssist& Assist : Assists)
	{
		if (IsSwingFoot(Assist))
		{
			FPhysicsControlTarget Target;
			Target.TargetPosition = Swing.Point;
			Target.TargetVelocity = Swing.VelocityCmS;
			Target.TargetOrientation = Assist.Target.Rotator();
			Target.bApplyControlPointToTarget = true;
			Control->SetControlTarget(Assist.Control, Target);
		}
	}

	// Down once the foot touches the floor on its way down over the target, or when the swing has run twice its
	// time. A toe that brushes the floor on the way across is not a landing. Waiting for the swing's end instead,
	// a foot that touched early with its toe (a step back) or its edge (a step to the side) was driven on into the
	// floor by the stiffened swing leg: 2900 N, both feet off the floor, the foot thrown off again.
	const bool bTouched = Progress >= FMath::Min(Over, DescentFrom) && Balance.FootForceN[Swing.Side] > 0.0;
	if (bTouched || Swing.ElapsedS >= 2.0 * DurationS)
	{
		Swing.bActive = false;
		bShiftHeld = false;
		FSandboxStepRecord& Record = StepLog.Last();
		if (Record.bRegroup)
		{
			// The anchors glide to the new support from where the body is (FollowSupport), which is where the shift
			// took it: what is left of the shift would be counted twice.
			ShiftCm = FVector2D::ZeroVector;
		}
		const FBodyInstance* Landed = Mesh->GetBodyInstance(FootBoneName[Swing.Side]);
		Record.LandS = ElapsedS;
		Record.bTouched = bTouched;
		Record.Landed = Landed ? Landed->GetUnrealWorldTransform().GetLocation() : Swing.Target;
		Record.CaptureAtTouch = Balance.CapturePoint + (WeightN > 0.0 ? SustainedMomentNCm / WeightN : FVector2D::ZeroVector);
		Record.MarginAtTouchCm = LastDecisionMarginCm;
		// Both feet stand where they are now, and both legs are solved as the stance leg was: the pelvis upright
		// at its height, balance.stance_follow of the way from its anchor (which glides to the new support from
		// now on) to where it is. Fully at the anchor, a leg reaching back toward a foot behind a body that is
		// ahead of its anchor is asked for too short a reach and lifts that foot. Over AnchorFollowS, so that no
		// leg's target jumps.
		// A body that is standing (a step asked for) goes straight over to the posture over its anchors.
		const int32 Other = 1 - Swing.Side;
		const EFrame Planted = State == ESandboxBodyState::Stagger ? EFrame::Stance : EFrame::Anchor;
		// Where the other foot stands now: it may have turned or slid under the body while this one swung.
		const FBodyInstance* OtherFoot = Mesh->GetBodyInstance(FootBoneName[Other]);
		const FVector OtherAt = OtherFoot ? OtherFoot->GetUnrealWorldTransform().GetLocation() : Placement[Other].Point;
		// Both points go over from where the legs were solved for to where the feet are, over AnchorFollowS as the frames
		// do. (Moved at once, a stance foot that had been carried 20 cm off its point while the body was off the floor
		// had its leg's target jump by that much, and the next step lifted that leg from there.)
		const FVector WasPoint[2] = { Placement[0].GlidedPoint(), Placement[1].GlidedPoint() };
		const bool bWasPlaced[2] = { Placement[0].Mode != EPlacement::None, Placement[1].Mode != EPlacement::None };
		PlaceFoot(Swing.Side, EPlacement::Planted, FVector(Record.Landed.X, Record.Landed.Y, Swing.Target.Z), Planted, Limits.AnchorFollowS, nullptr, true);
		// From the height the bone touched at down to the planted one: the sole comes down onto the floor.
		Placement[Swing.Side].SettleCm = Limits.SettleSmooth > 0.5 ? WasPoint[Swing.Side].Z - Swing.Target.Z : FMath::Max(Record.Landed.Z - Swing.Target.Z, 0.0);
		Placement[Swing.Side].SettleS = Limits.StepSettleS;
		// The planned sole where the foot landed: what the decisions count as its support while it settles.
		LandedSole = Swing.TargetSole;
		for (FVector2D& Corner : LandedSole)
		{
			Corner += FVector2D(Record.Landed.X - Swing.Target.X, Record.Landed.Y - Swing.Target.Y);
		}
		LandedSoleSide = Swing.Side;
		LandedSoleUntilS = ElapsedS + Limits.StepSettleS;
		PlaceFoot(Other, EPlacement::Planted, FVector(OtherAt.X, OtherAt.Y, FlatFootPoint(Other).Z), Planted, Limits.AnchorFollowS, nullptr, true);
		for (int32 Each = 0; Each < 2; ++Each)
		{
			if (bWasPlaced[Each])
			{
				// The landed foot glides only across: its height is the settling's.
				FFootPlacement& Foot = Placement[Each];
				Foot.GlideFrom = Each == Swing.Side ? FVector(WasPoint[Each].X, WasPoint[Each].Y, Foot.Point.Z) : WasPoint[Each];
				Foot.GlideElapsedS = 0.0;
				Foot.GlideS = Limits.AnchorFollowS;
			}
		}
		// The assist's control of the landed foot was led along the swing, with the swing's velocity: it is planted
		// where the foot is now (FollowSupport may have done so as the foot touched, before the swing's last
		// guidance in the same tick overwrote it).
		for (FAssist& Assist : Assists)
		{
			if (Assist.Slot == EAssistSlot::Feet && Assist.Side == Swing.Side && Landed && Landed->IsValidBodyInstance())
			{
				Assist.Target = FlatFootTransform(Assist);
				SetAssistTarget(Assist);
			}
		}
		PushTuningToControls();
		UE_LOG(LogSandbox, Display, TEXT("Body controller: step %d landed at %.3f s, %.1f cm from its target%s"),
			Record.Number, ElapsedS, FVector::Dist2D(Record.Landed, Record.Target), bTouched ? TEXT("") : TEXT(" (never touched the floor)"));
	}
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
	// With reactions.thing_support, so is a thing with mass that pushes up on the body no harder in a substep
	// than standing or lying on it does (the owner's rule: a steady force from below is support, a sharp
	// impulse from the side or above is a hit). The body does not know what the thing is; a hand under a foot
	// reads the same.
	const bool bSimulated = OtherComponent->IsSimulatingPhysics(Hit.BoneName);
	const double SizeNs = ImpulseNs.Size();
	// What supports has a weight of its own and is not itself on its way up: a thing that stands, lies or is shoved aside
	// does, a hand has no weight (its gravity is off) and a thing thrown from below comes up at more than this. How hard
	// it pushes does not tell them apart: a fall onto a table is a hard push, and so is an uppercut.
	const bool bHasWeight = bSimulated && OtherComponent->IsGravityEnabled();
	const bool bThingSupports = bSimulated && Tuning.Reactions.ThingSupport > 0.5 && SizeNs > 0.0 && bHasWeight && ImpulseNs.Z >= 0.7 * SizeNs &&
		OtherComponent->GetPhysicsLinearVelocityAtPoint(Hit.ImpactPoint, Hit.BoneName).Z <= Tuning.Reactions.ThingSupportMaxUpMps * 100.0;
	if (!bSimulated || bThingSupports)
	{
		if (IsFootBone(Hit.MyBoneName))
		{
			const FVector ThingCmS = bThingSupports ? OtherComponent->GetPhysicsLinearVelocityAtPoint(Hit.ImpactPoint, Hit.BoneName) : FVector::ZeroVector;
			const double PushNs = FMath::Max(ImpulseNs.Z, 0.0);
			SupportVelocitySum += FVector2D(ThingCmS.X, ThingCmS.Y) * PushNs;
			SupportVelocityWeight += PushNs;
		}
		bWorldTouched = true;
		// The trunk or the head on the floor: what a fall ends with (UpdateState lets the muscles go after it).
		const ESandboxMuscleGroup Group = FSandboxBodyTuning::GroupOfBone(Hit.MyBoneName);
		bTrunkTouched = bTrunkTouched || Group == ESandboxMuscleGroup::Spine || Group == ESandboxMuscleGroup::Neck;
		if (Group == ESandboxMuscleGroup::Arms && bFallAiming)
		{
			const FString Bone = Hit.MyBoneName.ToString();
			if (Bone.StartsWith(TEXT("hand")) || Bone.StartsWith(TEXT("lowerarm")))
			{
				bHandTouchedFrame[Bone.EndsWith(TEXT("_r")) ? 1 : 0] = true;
			}
		}
		const FBodyInstance* Body = Mesh->GetBodyInstance(Hit.MyBoneName);
		if (Body && SupportImpulseNs.IsValidIndex(Body->InstanceBodyIndex))
		{
			SupportImpulseNs[Body->InstanceBodyIndex] += ImpulseNs;
		}
		return;
	}
	ContactImpulseNs += ImpulseNs.Size();
	++ContactReports;
	FeelImpulse(Hit.MyBoneName, ImpulseNs, Hit.ImpactPoint);
}

void USandboxBodyController::FeelImpulse(FName Bone, const FVector& ImpulseNs, const FVector& PointCm)
{
	FrameImpulseNs += ImpulseNs;
	FrameMomentNsCm += FVector2D(ImpulseNs.X, ImpulseNs.Y) * (PointCm.Z - LastSupportHeightCm);
	if (ImpulseNs.Size() > FrameBoneImpulseNs)
	{
		FrameBoneImpulseNs = ImpulseNs.Size();
		FrameBone = Bone;
		FramePointCm = PointCm;
	}
}

bool USandboxBodyController::IsIntoBelly(FName Bone, const FVector& PointCm, const FVector& HitNs) const
{
	// The trunk only (an arm that hangs in front of the belly is an arm), from the front, and between two
	// shares of the line from the pelvis's joint to the neck's: the belly and the solar plexus, not the chest.
	static const FName PelvisName(TEXT("pelvis"));
	static const FName NeckName(TEXT("neck_01"));
	if (Bone != PelvisName && FSandboxBodyTuning::GroupOfBone(Bone) != ESandboxMuscleGroup::Spine)
	{
		return false;
	}
	const FBodyInstance* Pelvis = Mesh->GetBodyInstance(PelvisName);
	const FBodyInstance* Neck = Mesh->GetBodyInstance(NeckName);
	if (!Pelvis || !Neck || !Pelvis->IsValidBodyInstance() || !Neck->IsValidBodyInstance())
	{
		return false;
	}
	const FVector Low = Pelvis->GetUnrealWorldTransform().GetLocation();
	const FVector Line = Neck->GetUnrealWorldTransform().GetLocation() - Low;
	if (Line.SizeSquared() < 1.0)
	{
		return false;
	}
	const double Along = FVector::DotProduct(PointCm - Low, Line) / Line.SizeSquared();
	if (Along < Tuning.Reactions.DoubleOverFrom || Along > Tuning.Reactions.DoubleOverTo)
	{
		return false;
	}
	// From the front: the trunk's own forward, which is across the line of the hips (left to right) and the
	// line of the trunk. A hit that pushes the trunk backward comes from the front.
	static const FName ThighLeftName(TEXT("thigh_l"));
	static const FName ThighRightName(TEXT("thigh_r"));
	const FBodyInstance* ThighLeft = Mesh->GetBodyInstance(ThighLeftName);
	const FBodyInstance* ThighRight = Mesh->GetBodyInstance(ThighRightName);
	if (!ThighLeft || !ThighRight || !ThighLeft->IsValidBodyInstance() || !ThighRight->IsValidBodyInstance())
	{
		return false;
	}
	const FVector Hips = ThighRight->GetUnrealWorldTransform().GetLocation() - ThighLeft->GetUnrealWorldTransform().GetLocation();
	const FVector Forward = FVector::CrossProduct(Hips, Line).GetSafeNormal();
	return FVector::DotProduct(HitNs.GetSafeNormal(), Forward) < -0.5;
}

void USandboxBodyController::FeelFrame(double DeltaTime)
{
	const FSandboxReactionTuning& Reactions = Tuning.Reactions;
	const FVector Felt = FrameImpulseNs;

	// The hit: what this frame brought beyond the pressure felt so far. A blow is all hit; a hand that keeps
	// pressing is hit only while its force rises, and its pressure alone makes nobody flinch.
	const FVector HitNs = Felt - PressureN * DeltaTime;
	// A blow: a frame that by itself brings what makes a standing body flinch.
	const bool bBlowFrame = !Felt.IsZero() && HitNs.Size() >= Tuning.Thresholds.FlinchImpulseNs && (HitNs | Felt) > 0.0;
	if (!Felt.IsZero())
	{
		// For whoever listens: a hit in any state, by the rule of the flinch below, on a sum of its own so that
		// telling changes nothing of what the body does.
		// One contact is one hit: a shove of a tenth of a second, or a hand that lands and keeps pressing, is
		// told once, when it has brought as much as makes a standing body flinch; a hand held against the body
		// presses now more and now less, and that is not a row of hits. A new hit is a contact after nothing was
		// felt for the flinch's window, or a blow that lands while something is pressing already: a frame that is
		// a hit by itself, after the force of the hit told before it has stopped rising.
		if (ElapsedS - EventFeltS > Tuning.Thresholds.FlinchWindowS)
		{
			EventHitNs = FVector::ZeroVector;
			bEventHitTold = false;
		}
		EventFeltS = ElapsedS;
		EventHitNs += HitNs;
		if (bEventHitTold && (HitNs | Felt) <= 0.0)
		{
			bEventRiseOver = true;
		}
		const bool bFirstOfContact = !bEventHitTold && EventHitNs.Size() >= Tuning.Thresholds.FlinchImpulseNs && (EventHitNs | Felt) > 0.0;
		const bool bBlowIntoContact = bEventHitTold && bBlowFrame && bEventRiseOver;
		// A hit braces the body after the reflex's delay (N3). The brace is a tone, so it comes and goes by the tone's own time.
		if (Tuning.Tone.BraceFromNs > 0.0 && (bBlowIntoContact ? HitNs : EventHitNs).Size() >= Tuning.Tone.BraceFromNs && (bFirstOfContact || bBlowIntoContact) &&
			State != ESandboxBodyState::Lying && State != ESandboxBodyState::Falling)
		{
			if (BraceUntilS < ElapsedS)
			{
				BraceFromS = ElapsedS + FMath::Max(Tuning.Tone.BraceDelayS, Tuning.Reactions.ReflexDelayS);
				++Braces;
			}
			BraceUntilS = ElapsedS + Tuning.Tone.BraceDelayS + Tuning.Tone.BraceHoldS;
		}
		if (bFirstOfContact || bBlowIntoContact)
		{
			LastHitS = ElapsedS;
			{
				// N7: what felled the body, for how long it lies (getup.rest_per_ns_s). Measuring only: nothing reads it otherwise.
				const double HitSizeNs = (bBlowIntoContact ? HitNs : EventHitNs).Size();
				RecentHitNs = ElapsedS - RecentHitS > 1.0 ? HitSizeNs : FMath::Max(RecentHitNs, HitSizeNs);
				RecentHitS = ElapsedS;
				if (State == ESandboxBodyState::Falling || State == ESandboxBodyState::Lying)
				{
					FellByNs = FMath::Max(FellByNs, HitSizeNs);
				}
			}
			FSandboxBodyEvent Event;
			Event.Type = ESandboxBodyEventType::Hit;
			Event.TimeS = ElapsedS;
			Event.Bone = FrameBone;
			Event.ImpulseNs = bBlowIntoContact ? HitNs : EventHitNs;
			Event.State = State;
			Event.PreviousState = State;
			bEventHitTold = true;
			bEventRiseOver = false;
			TellEvent(Event);
		}

		// Doubling over: a hit from the front into the belly or the solar plexus, as a sum over one contact of
		// its own (a punch with weight behind it brings its impulse over a tenth of a second and more). Only an
		// upright body doubles over; one that falls, lies or gets up feels the hit as it feels any other.
		if (ElapsedS - BellyFeltS > Tuning.Thresholds.FlinchWindowS)
		{
			BellyHitNs = 0.0;
			bBellyDoubled = false;
		}
		if ((HitNs | Felt) > 0.0 && IsIntoBelly(FrameBone, FramePointCm, HitNs))
		{
			BellyFeltS = ElapsedS;
			BellyHitNs += HitNs.Size();
			MaxBellyHitNs = FMath::Max(MaxBellyHitNs, BellyHitNs);
			const bool bUpright = State == ESandboxBodyState::Standing || State == ESandboxBodyState::Flinch || State == ESandboxBodyState::Stagger;
			if (Reactions.DoubleOverNs > 0.0 && !bBellyDoubled && bUpright && BellyHitNs >= Reactions.DoubleOverNs)
			{
				bBellyDoubled = true;
				// After the reflex's delay (N4), counted from the hit that started the clocks.
				DoubledFromS = Reactions.ReflexDelayS > 0.0 ? (InMechanics() ? MechanicsUntilS : ElapsedS + Reactions.ReflexDelayS) : ElapsedS;
				DoubledUntilS = DoubledFromS + Reactions.DoubleOverS;
				++DoubleOvers;
				UE_LOG(LogSandbox, Display, TEXT("Body controller: doubled over at %.3f s (hit of %.1f N*s into the belly, on %s)"), ElapsedS, BellyHitNs, *FrameBone.ToString());
				FSandboxBodyEvent Event;
				Event.Type = ESandboxBodyEventType::DoubledOver;
				Event.TimeS = ElapsedS;
				Event.Bone = FrameBone;
				Event.ImpulseNs = HitNs.GetSafeNormal() * BellyHitNs;
				Event.State = State;
				Event.PreviousState = State;
				TellEvent(Event);
			}
		}

		// A knockdown: a hit on the head, from any side, summed over one contact as the belly's is. A kick that
		// meets the head throws it back on its neck, and the legs go from under the body: it falls. (By momentum
		// alone a kick felled the body from three points of the head out of nine: the foot slides off.) Only an
		// upright body is knocked down.
		if (ElapsedS - HeadFeltS > Tuning.Thresholds.FlinchWindowS)
		{
			HeadHitNs = 0.0;
			bHeadFelled = false;
		}
		if ((HitNs | Felt) > 0.0 && FSandboxBodyTuning::GroupOfBone(FrameBone) == ESandboxMuscleGroup::Neck)
		{
			HeadFeltS = ElapsedS;
			HeadHitNs += HitNs.Size();
			MaxHeadHitNs = FMath::Max(MaxHeadHitNs, HeadHitNs);
			const bool bUpright = State == ESandboxBodyState::Standing || State == ESandboxBodyState::Flinch || State == ESandboxBodyState::Stagger;
			// A hit on the head below the knockdown dazes the body: legs and spine lose some of their tone for a while (N3).
			if (Tuning.Tone.DazeFromNs > 0.0 && bUpright && HeadHitNs >= Tuning.Tone.DazeFromNs)
			{
				const double Top = Reactions.KnockdownHeadNs > Tuning.Tone.DazeFromNs ? Reactions.KnockdownHeadNs : Tuning.Tone.DazeFromNs + 1.0;
				const double Depth = FMath::Clamp((HeadHitNs - Tuning.Tone.DazeFromNs) / (Top - Tuning.Tone.DazeFromNs), 0.0, 1.0);
				if (Depth > DazeDepth || DazedAtS < 0.0)
				{
					if (DazedAtS < 0.0 || ElapsedS - DazedAtS > Tuning.Tone.DazeS)
					{
						++Dazes;
					}
					DazeDepth = FMath::Max(Depth, DazeDepth);
					DazedAtS = ElapsedS;
				}
			}
			if (Reactions.KnockdownHeadNs > 0.0 && !bHeadFelled && bUpright && HeadHitNs >= Reactions.KnockdownHeadNs)
			{
				bHeadFelled = true;
				++Knockdowns;
				EnterState(ESandboxBodyState::Falling, FString::Printf(TEXT("knocked down by a hit of %.1f N*s on the head"), HeadHitNs));
			}
		}

		// What is felt is the hit as a whole: hits that follow each other closely are added up.
		if (ElapsedS - HitStartedS > Tuning.Thresholds.FlinchWindowS)
		{
			HitImpulseNs = FVector::ZeroVector;
			HitStartedS = ElapsedS;
		}
		HitImpulseNs += HitNs;
		// A pressure that falls away is no hit, however fast: only what pushes along with this frame counts.
		if (State == ESandboxBodyState::Standing && HitImpulseNs.Size() >= Tuning.Thresholds.FlinchImpulseNs && (HitImpulseNs | Felt) > 0.0)
		{
			FlinchGroup = FSandboxBodyTuning::GroupOfBone(FrameBone);
			FlinchBone = FrameBone;
			FlinchImpulseNs = HitImpulseNs;
			FlinchStartedS = ElapsedS;
			if (Reactions.ReflexDelayS > 0.0)
			{
				// Mechanics first (N4): the flinch's pose and weakening, new goals of the strategies and a decision to step wait
				// for the reflex. The state is a flinch at once, so that the assist lets go of the body as in any flinch: held
				// by it while the body waits, a blow was taken back and the step it called for was not needed.
				MechanicsUntilS = ElapsedS + Reactions.ReflexDelayS;
				FlinchStartedS = MechanicsUntilS;
				UE_LOG(LogSandbox, Display, TEXT("Body controller: hit of %.1f N*s on %s at %.3f s; mechanics alone until %.3f s"), HitImpulseNs.Size(), *FrameBone.ToString(), ElapsedS, MechanicsUntilS);
			}
			bFlinchWeakeningOver = false;
			EnterState(ESandboxBodyState::Flinch, FString::Printf(TEXT("hit of %.1f N*s on %s"), HitImpulseNs.Size(), *FrameBone.ToString()));
			HitImpulseNs = FVector::ZeroVector;
		}
		else if (State == ESandboxBodyState::GetUp && HitImpulseNs.Size() >= Tuning.Thresholds.FlinchImpulseNs && (HitImpulseNs | Felt) > 0.0)
		{
			// Getting up, a hit is felt as it is standing: the struck part gives way and its muscles are weak for a
			// moment (the flinch's pose offset and weakening, each by its own clock). The body does not leave the
			// get-up for it: it goes on if it can, and is given up if the hit has thrown it off (UpdateGetUp).
			FlinchGroup = FSandboxBodyTuning::GroupOfBone(FrameBone);
			FlinchBone = FrameBone;
			FlinchImpulseNs = HitImpulseNs;
			FlinchStartedS = ElapsedS;
			GetUpRun.bEased = false;
			++GetUpView.Hits;
			UE_LOG(LogSandbox, Display, TEXT("Body controller: hit of %.1f N*s on %s while getting up, at %.3f s"), HitImpulseNs.Size(), *FrameBone.ToString(), ElapsedS);
			HitImpulseNs = FVector::ZeroVector;
		}
	}

	// How much of the doubled-over pose is on: it comes in and goes out by its own clock, never in one step (a
	// target that jumps puts energy into the body). A body that lies or gets up lets it go.
	if (DeltaTime > 0.0)
	{
		const bool bHeld = ElapsedS < DoubledUntilS && ElapsedS >= DoubledFromS && State != ESandboxBodyState::Lying && State != ESandboxBodyState::GetUp;
		DoubledWeight = bHeld
			? FMath::Min(DoubledWeight + DeltaTime / Reactions.DoubleOverInS, 1.0)
			: FMath::Max(DoubledWeight - DeltaTime / Reactions.DoubleOverOutS, 0.0);
	}

	// The pressure follows the felt force.
	if (DeltaTime > 0.0)
	{
		const double Follow = Reactions.PressureS > 0.0 ? 1.0 - FMath::Exp(-DeltaTime / Reactions.PressureS) : 1.0;
		PressureN += (Felt / DeltaTime - PressureN) * Follow;
		if (PressureN.Size() < 1.0e-6)
		{
			PressureN = FVector::ZeroVector;
		}

		// The sustained force: the least of the last frames, so that only a push felt in all of them counts.
		const int32 Frames = FMath::Max(FMath::CeilToInt32(Reactions.HeldPushS / DeltaTime), 1);
		if (RecentForceN.Num() != Frames)
		{
			RecentForceN.Init(FVector::ZeroVector, Frames);
			RecentMomentNCm.Init(FVector2D::ZeroVector, Frames);
			RecentNext = 0;
		}
		RecentForceN[RecentNext] = Felt / DeltaTime;
		RecentMomentNCm[RecentNext] = FrameMomentNsCm / DeltaTime;
		if (Reactions.HoldFelt > 0.5)
		{
			// A hold reaches the body through a joint and no contact tells of it: what the hands pull with is told
			// (TellHolds) and counts as a push that is kept up, for the lean, for the step and for a get-up that is pressed.
			for (const FSandboxBodyHold& Hold : Holds)
			{
				RecentForceN[RecentNext] += Hold.PullN;
				RecentMomentNCm[RecentNext] += FVector2D(Hold.PullN.X, Hold.PullN.Y) * (Hold.PointCm.Z - LastSupportHeightCm);
			}
		}
		RecentNext = (RecentNext + 1) % Frames;
		int32 Least = 0;
		for (int32 Index = 1; Index < Frames; ++Index)
		{
			if (RecentForceN[Index].SizeSquared() < RecentForceN[Least].SizeSquared())
			{
				Least = Index;
			}
		}
		SustainedN = RecentForceN[Least];
		SustainedMomentNCm = RecentMomentNCm[Least];
	}

	FrameImpulseNs = FVector::ZeroVector;
	FrameMomentNsCm = FVector2D::ZeroVector;
	FrameBone = NAME_None;
	FrameBoneImpulseNs = 0.0;
}

void USandboxBodyController::UpdateState(float DeltaTime)
{
	// A body whose pose is asked for from outside (SetClipPose) does not judge its balance, by the capture point or
	// by its posture: it cannot step for it, the pose may be one of lying down, and whoever asks for it holds the body.
	// (Carried through a get-up by its pelvis, it took itself for fallen as the pelvis was laid down, and hung limp.)
	const bool bJudged = IsUpright() && !IsPosedFromOutside();
	if (bJudged)
	{
		// The capture point decides. Inside the support, or outside by no more than the ankles and the hips
		// can bring back, the body stands. Further out it needs a step, and staggers. Beyond a step's reach
		// balance is lost. A step being needed holds only after decision_s, so that a foot that loses its load
		// for one frame decides nothing.
		const FSandboxBalanceTuning& Limits = Tuning.Balance;
		LastDecisionMarginCm = DecisionMarginCm();
		// Where the capture point will be when a step started now has landed: it runs away from the support's
		// edge by e^(omega t) (linear inverted pendulum).
		// While a step is under way, the margin is to the support the step will give, and the capture point runs
		// away only for the rest of the swing.
		const double HeightCm = FMath::Max(Balance.CenterOfMass.Z - Balance.SupportHeightCm, 1.0);
		const double StepLeftS = Swing.bActive ? FMath::Max(Swing.DurationS - Swing.ElapsedS, 0.0) : Limits.StepTimeS;
		const double Growth = FMath::Exp(FMath::Sqrt(GravityCmS2 / HeightCm) * StepLeftS);
		const double AtLandingCm = -LastDecisionMarginCm * Growth;
		// A step being needed has to last a moment. Balance beyond a step's reach is lost at once: the capture
		// point is the whole body's momentum, which a blow gives it in one step, and waiting would only let
		// the assist take back what the body could not.
		StepNeededS = LastDecisionMarginCm < -Limits.RecoverableCm ? StepNeededS + DeltaTime : 0.0;
		LostS = AtLandingCm > Limits.StepReachCm ? LostS + DeltaTime : 0.0;
		// Beyond one step's reach the body does not wait either, and it does not give up: it steps as far as it can,
		// and again, and falls when its posture gives way (below), as a person does. (Deciding here that balance was
		// lost, it let go at once and went over like a plank, without having moved a foot.)
		const bool bCanStep = PlacedFeet.Num() == 0;
		bStepNeeded = StepNeededS >= Limits.DecisionS - KINDA_SMALL_NUMBER || (bCanStep && LostS > 0.0);
		// A staggering body that has come to rest off its support needs a step too. The allowance above
		// (balance.recoverable_cm, measured to the whole soles) is for a body in motion, which the ankles and the hips
		// bring back; at rest the capture point is the centre of mass, nothing brings it back, and a body whose centre
		// of mass is not over what touches the floor is kept up by the stagger's upright hold on the pelvis alone. Such
		// a body did not step (2.4 cm outside its whole soles, under the allowance) and did not stand (6 cm outside the
		// edges of the soles it stood on, so never "over its support"): after two steps across it stayed on crossed
		// legs, one foot before the other, in Stagger for good. In a recovery that goes well this lasts 0.09 s at most.
		const bool bRestsOff = State == ESandboxBodyState::Stagger && bCanStep && !IsStepUnderWay() && Balance.bValid && Balance.IsSupported() &&
			Balance.FootForceN[0] > 0.0 && Balance.FootForceN[1] > 0.0 &&
			Balance.Velocity.Size2D() <= Limits.RecoverSpeedMps * 100.0 &&
			Balance.CenterOfMassMarginCm <= 0.0 && Balance.CaptureMarginCm <= 0.0;
		const double RestedOffS = RestOffSupportS;
		RestOffSupportS = bRestsOff ? RestOffSupportS + DeltaTime : 0.0;
		if (Limits.RestStepS > 0.0 && RestOffSupportS >= Limits.RestStepS - KINDA_SMALL_NUMBER)
		{
			bStepNeeded = true;
			if (RestedOffS < Limits.RestStepS - KINDA_SMALL_NUMBER)
			{
				UE_LOG(LogSandbox, Display, TEXT("Body controller: at rest for %.2f s with the centre of mass %.1f cm outside the support (capture point %.1f cm %s the whole soles): a step is needed, at %.3f s"),
					RestOffSupportS, -Balance.CenterOfMassMarginCm, FMath::Abs(LastDecisionMarginCm), LastDecisionMarginCm >= 0.0 ? TEXT("inside") : TEXT("outside"), ElapsedS);
			}
		}
		if (LostS > 0.0 && !bCanStep)
		{
			bStepNeeded = false;
			EnterState(ESandboxBodyState::Falling, FString::Printf(TEXT("capture point %.1f cm outside the support, %.0f cm by the time a step lands: beyond a step"),
				-LastDecisionMarginCm, AtLandingCm));
		}
		else if (bStepNeeded && State != ESandboxBodyState::Stagger && PlacedFeet.Num() == 0 && !InMechanics())
		{
			// A body whose legs are placed from outside (a scenario's foot_target) cannot step: a step it needs is held
			// by the assist and reported, as before the stagger existed. (Staggering, it lost the assist's pull and fell
			// over the foot it had been told to hold up.)
			RecoveredS = 0.0;
			StaggerSteps = 0;
			RegroupLeft = FMath::RoundToInt32(Limits.RegroupMaxSteps);
			EnterState(ESandboxBodyState::Stagger, FString::Printf(TEXT("capture point %.1f cm outside the support: a step is needed"), -LastDecisionMarginCm));
		}
	}
	else
	{
		bStepNeeded = false;
		StepNeededS = 0.0;
		LostS = 0.0;
		RestOffSupportS = 0.0;
	}

	if (bJudged && IsUpright())
	{
		// Whatever the capture point says, the posture itself can give way: the legs fold (the pelvis sinks) or
		// the body bends over too far to stand.
		const FAssist* Pelvis = Assists.FindByPredicate([](const FAssist& Assist) { return Assist.Slot == EAssistSlot::Pelvis; });
		const FBodyInstance* Body = Pelvis ? Mesh->GetBodyInstance(Pelvis->Bone) : nullptr;
		if (Body && Body->IsValidBodyInstance())
		{
			const FTransform Now = Body->GetUnrealWorldTransform();
			// Below the height the legs' posture holds it at: in a wide stance after a step that is lower than
			// standing by LowerCm, on purpose (a lunge of 50 cm held the pelvis 20 cm down and was taken for a fall).
			const double DropCm = Pelvis->Target.GetLocation().Z - LowerCm - Now.GetLocation().Z;
			// How far the direction that pointed up when standing has turned away from up.
			const FVector Up = Now.GetRotation().RotateVector(Pelvis->Target.GetRotation().UnrotateVector(FVector::UpVector));
			const double TiltDeg = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(Up.Z, -1.0, 1.0)));

			const FSandboxThresholdTuning& Limits = Tuning.Thresholds;
			if (DropCm > Limits.FallPelvisDropCm)
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
		if (ElapsedS - FMath::Max(StateEnteredS, FlinchStartedS) >= Tuning.Reactions.FlinchRecoveryS)
		{
			bFlinchWeakeningOver = true;
			EnterState(ESandboxBodyState::Standing, TEXT("recovered from the hit"));
		}
		else
		{
			// The hit muscles regain strength every tick.
			PushTuningToControls();
		}
	}
	else if (State == ESandboxBodyState::Stagger)
	{
		// A flinch that turned into a stagger goes on weakening the hit muscles, by its own clock, until they are
		// back to full strength (the last push of them is at full strength).
		if (!bFlinchWeakeningOver)
		{
			PushTuningToControls();
			bFlinchWeakeningOver = ElapsedS - FlinchStartedS >= Tuning.Reactions.FlinchRecoveryS;
		}
		// Balance is back once the centre of mass is over the support and the capture point within what the
		// ankles and hips bring back, for a while.
		// And both feet are on the floor: the assist anchors them where they are when the body stands again, and a
		// foot that had bounced off the floor after its step was anchored 3 cm up in the air, tilted, and the
		// pelvis's anchor with it.
		// And the body has come to rest over its feet (balance.recover_speed_mps): the assist's springs take hold
		// again at full strength, and a body still on its way was thrown by them into the next stagger.
		const bool bFeetDown = Balance.FootForceN[0] > 0.0 && Balance.FootForceN[1] > 0.0;
		const bool bSlow = Balance.Velocity.Size2D() <= Tuning.Balance.RecoverSpeedMps * 100.0;
		const bool bRecovered = Balance.bValid && Balance.IsSupported() && Balance.CenterOfMassMarginCm > 0.0 &&
			LastDecisionMarginCm >= -Tuning.Balance.RecoverableCm && !bStepNeeded && !IsStepUnderWay() && bFeetDown && bSlow;
		RecoveredS = bRecovered ? RecoveredS + DeltaTime : 0.0;
		if (RecoveredS >= Tuning.Balance.RecoverS - KINDA_SMALL_NUMBER && bFlinchWeakeningOver)
		{
			EnterState(ESandboxBodyState::Standing, FString::Printf(TEXT("centre of mass over the support for %.2f s"), RecoveredS));
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
		// Or the fall is over because the trunk or the head has met the floor: the pose of the fall was for that
		// moment, and the muscles let go a little after it (thresholds.impact_let_go_s), at rest or not. (Held until
		// the body was at rest, the pose made a stiff shape of it: knocked over by the head, it stood on its head
		// with its legs in the air for a second.) A body that is held up has not fallen.
		if (bTrunkTouched && TrunkTouchedS < 0.0)
		{
			TrunkTouchedS = ElapsedS;
		}
		const bool bLetGo = Tuning.Thresholds.ImpactLetGoS > 0.0 && TrunkTouchedS >= 0.0 && !bHeldUp &&
			ElapsedS - TrunkTouchedS >= Tuning.Thresholds.ImpactLetGoS - KINDA_SMALL_NUMBER;
		if (bAtRest && RestS >= Tuning.Thresholds.RestTimeS)
		{
			EnterState(ESandboxBodyState::Lying, FString::Printf(TEXT("at rest for %.2f s"), RestS));
		}
		else if (bLetGo)
		{
			EnterState(ESandboxBodyState::Lying, FString::Printf(TEXT("the trunk met the floor %.2f s ago"), ElapsedS - TrunkTouchedS));
		}
	}
	else
	{
		TrunkTouchedS = -1.0;
	}
	bTrunkTouched = false;

	// A body that has lain still for a while gets up, unless something holds it up or it has been told to stay down.
	if (State == ESandboxBodyState::Lying)
	{
		LyingS = IsAtRest() && !bHeldUp ? LyingS + DeltaTime : 0.0;
		if (bGetUpAllowed && !bGetUpBroken && !IsPosedFromOutside() && Tuning.GetUp.RestS > 0.0 && LyingS >= GetUpRestS() - KINDA_SMALL_NUMBER)
		{
			LyingS = 0.0;
			FString Error;
			if (!StartGetUp(Error))
			{
				bGetUpBroken = true;
				UE_LOG(LogSandbox, Error, TEXT("Body controller: the body cannot get up and stays down: %s"), *Error);
			}
		}
	}
	else
	{
		LyingS = 0.0;
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
	if (!(NewTuning.Rig == TunedRig))
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

void USandboxBodyController::ApplySurface()
{
	if (bSurfaceApplied && AppliedSurface == Tuning.Surface)
	{
		return;
	}
	const auto Write = [this](TObjectPtr<UPhysicalMaterial>& Material, const TCHAR* Name, double Friction)
	{
		if (!Material)
		{
			Material = NewObject<UPhysicalMaterial>(this, MakeUniqueObjectName(this, UPhysicalMaterial::StaticClass(), Name), RF_Transient);
		}
		Material->Friction = static_cast<float>(Friction);
		Material->Restitution = static_cast<float>(Tuning.Surface.Restitution);
		Material->FrictionCombineMode = static_cast<EFrictionCombineMode::Type>(FMath::RoundToInt32(Tuning.Surface.FrictionCombine));
		Material->RestitutionCombineMode = static_cast<EFrictionCombineMode::Type>(FMath::RoundToInt32(Tuning.Surface.RestitutionCombine));
		Material->bOverrideFrictionCombineMode = true;
		Material->bOverrideRestitutionCombineMode = true;
		// The solver has its own copy of a material, made when the material is first asked for and brought up to
		// date only by this call (the editor makes it after a property was edited; nothing does at run time).
		FChaosEngineInterface::UpdateMaterial(Material->GetPhysicsMaterial(), Material);
	};
	Write(SurfaceMaterial, TEXT("SandboxBodySurface"), Tuning.Surface.Friction);
	Write(FeetSurfaceMaterial, TEXT("SandboxFeetSurface"), Tuning.Surface.FeetFriction);

	for (FBodyInstance* Body : Mesh->Bodies)
	{
		if (!Body || !Body->IsValidBodyInstance())
		{
			continue;
		}
		const FName Bone = Mesh->GetBoneName(Body->InstanceBoneIndex);
		UPhysicalMaterial* Material = IsFootBone(Bone) ? FeetSurfaceMaterial : SurfaceMaterial;
		if (Body->GetPhysMaterialOverride() != Material)
		{
			Body->SetPhysMaterialOverride(Material);
		}
	}
	AppliedSurface = Tuning.Surface;
	bSurfaceApplied = true;
}

void USandboxBodyController::StartCollectingPushes()
{
	const UWorld* World = GetWorld();
	FPhysScene* Scene = World ? World->GetPhysicsScene() : nullptr;
	if (Chaos::FPhysicsSolver* Solver = Scene && !PushHandle.IsValid() ? Scene->GetSolver() : nullptr)
	{
		PushHandle = Solver->AddPostAdvanceCallback(FSolverPostAdvance::FDelegate::CreateWeakLambda(this, [this](Chaos::FReal SubstepS) { AddPushes(SubstepS); }));
	}
}

void USandboxBodyController::StopCollectingPushes()
{
	const UWorld* World = GetWorld();
	FPhysScene* Scene = World ? World->GetPhysicsScene() : nullptr;
	if (Chaos::FPhysicsSolver* Solver = Scene && PushHandle.IsValid() ? Scene->GetSolver() : nullptr)
	{
		Solver->RemovePostAdvanceCallback(PushHandle);
	}
	PushHandle.Reset();
}

void USandboxBodyController::AddPushes(double SubstepS)
{
	// The solver's own contact points with their own impulses: a contact event has the nearest point of a contact with the
	// impulse of all of it, 8.6 cm from where the floor's push on a standing body goes through.
	const UWorld* World = GetWorld();
	FPhysScene* Scene = World ? World->GetPhysicsScene() : nullptr;
	Chaos::FPhysicsSolver* Solver = Scene ? Scene->GetSolver() : nullptr;
	if (!bStarted || !Mesh || !Solver || !Solver->GetEvolution() || SubstepS <= 0.0)
	{
		return;
	}
	PushImpulseNs.SetNumZeroed(Mesh->Bodies.Num());
	PushMomentNsCm.SetNumZeroed(Mesh->Bodies.Num());
	PushSizeNs.SetNumZeroed(Mesh->Bodies.Num());
	TMap<const Chaos::FGeometryParticleHandle*, int32, TInlineSetAllocator<64>> Own;
	for (int32 Index = 0; Index < Mesh->Bodies.Num(); ++Index)
	{
		const FBodyInstance* Body = Mesh->Bodies[Index];
		if (Body && Body->GetPhysicsActor())
		{
			Own.Add(Body->GetPhysicsActor()->GetHandle_LowLevel(), Index);
		}
	}
	for (const Chaos::FPBDCollisionConstraintHandle* Handle : Solver->GetEvolution()->GetCollisionConstraints().GetConstraintHandles())
	{
		const Chaos::FPBDCollisionConstraint& Contact = Handle->GetContact();
		const Chaos::FGeometryParticleHandle* First = Contact.GetParticle0();
		const Chaos::FGeometryParticleHandle* Second = Contact.GetParticle1();
		const int32* FirstIndex = Own.Find(First);
		const int32* SecondIndex = Own.Find(Second);
		if (!Contact.IsEnabled() || (FirstIndex != nullptr) == (SecondIndex != nullptr))
		{
			continue;
		}
		// The world: what does not move under physics.
		const Chaos::FConstGenericParticleHandle Other(FirstIndex ? Second : First);
		if (Other->IsDynamic())
		{
			continue;
		}
		const int32 Index = FirstIndex ? *FirstIndex : *SecondIndex;
		// Away from the other body, toward ours.
		const FVector Away = FVector(Contact.CalculateWorldContactNormal()) * (FirstIndex ? 1.0 : -1.0);
		for (int32 PointIndex = 0; PointIndex < Contact.NumManifoldPoints(); ++PointIndex)
		{
			if (!Contact.IsManifoldPointActive(PointIndex))
			{
				continue;
			}
			const Chaos::FManifoldPointResult& Result = Contact.GetManifoldPointResult(PointIndex);
			FVector ImpulseNs = (FVector(Result.NetImpulse) + FVector(Result.NetPushOut) / SubstepS) * 0.01;
			if ((ImpulseNs | Away) < 0.0)
			{
				ImpulseNs = -ImpulseNs;
			}
			const FVector Point = Contact.GetShapeWorldTransform0().TransformPositionNoScale(FVector(Contact.GetManifoldPoint(PointIndex).ContactPoint.ShapeContactPoints[0]));
			const double Size = ImpulseNs.Size();
			PushImpulseNs[Index] += ImpulseNs;
			PushMomentNsCm[Index] += Point * Size;
			PushSizeNs[Index] += Size;
		}
	}
}

void USandboxBodyController::UpdateWeight(double DeltaTime)
{
	bool bWanted = false;
	for (int32 Group = 0; Group < UE_ARRAY_COUNT(WeightShareNow); ++Group)
	{
		bWanted = bWanted || Tuning.Muscle(static_cast<ESandboxMuscleGroup>(Group)).WeightShare > 0.0;
	}
	bWanted = bWanted || Tuning.Weight.AnkleAsTorque > 0.5 || Tuning.Weight.InternalShare > 0.0;
	if (!bWanted && !bWeightOn)
	{
		return;
	}
	if (!bWeightOn)
	{
		bWeightOn = true;
		StartCollectingPushes();
		// The share of the weight held is the whole of it for a body that stands; a body in the air loses it as it falls.
		WeightHeld = Balance.IsSupported() ? 1.0 : 0.0;
		return; // nothing has been collected yet
	}
	const double Follow = Tuning.Weight.FollowS > 0.0 ? 1.0 - FMath::Exp(-DeltaTime / Tuning.Weight.FollowS) : 1.0;
	const bool bByPlace = Tuning.Weight.LegsBy > 0.5;
	bool bAny = false;
	for (int32 Group = 0; Group < UE_ARRAY_COUNT(WeightShareNow); ++Group)
	{
		double Goal = Tuning.Muscle(static_cast<ESandboxMuscleGroup>(Group)).WeightShare;
		// A body that is not upright is carried by what it lies or falls on, not by its muscles: the weight torques go, all of them.
		// (Kept for the arms, the spine and the neck, they held a fallen body's limbs up and kept it from rest for more than 3 s.)
		if (!IsUpright() && (bByPlace || static_cast<ESandboxMuscleGroup>(Group) != ESandboxMuscleGroup::Legs))
		{
			Goal = 0.0;
		}
		WeightShareNow[Group] += (Goal - WeightShareNow[Group]) * Follow;
		if (Goal == 0.0 && WeightShareNow[Group] < 1.0e-4)
		{
			WeightShareNow[Group] = 0.0;
		}
		bAny = bAny || WeightShareNow[Group] > 0.0;
	}

	// What the world pushed with over the last frame.
	double HeldN = 0.0;
	const double PerS = DeltaTime > 0.0 ? 1.0 / DeltaTime : 0.0;
	TArray<FSandboxWeightTorques::FBody> Bodies;
	Bodies.SetNum(WeightBodies.Num());
	for (int32 Index = 0; Index < WeightBodies.Num(); ++Index)
	{
		FWeightBody& Each = WeightBodies[Index];
		const FBodyInstance* Body = Mesh->GetBodyInstance(Each.Bone);
		if (!Body || !Body->IsValidBodyInstance())
		{
			return;
		}
		const int32 Instance = Body->InstanceBodyIndex;
		const bool bCollected = PushImpulseNs.IsValidIndex(Instance);
		const FVector Com = Body->GetCOMPosition();
		const FVector PushN = bCollected ? PushImpulseNs[Instance] * PerS : FVector::ZeroVector;
		const double SizeN = bCollected ? PushSizeNs[Instance] * PerS : 0.0;
		const FVector OffsetNcm = bCollected ? (PushMomentNsCm[Instance] - Com * PushSizeNs[Instance]) * PerS : FVector::ZeroVector;
		HeldN += PushN.Z;
		Each.PushN += (PushN - Each.PushN) * Follow;
		Each.PushSizeN += (SizeN - Each.PushSizeN) * Follow;
		Each.PushOffsetNcm += (OffsetNcm - Each.PushOffsetNcm) * Follow;

		FSandboxWeightTorques::FBody& Out = Bodies[Index];
		Out.Parent = Each.Parent;
		Out.JointCm = Body->GetUnrealWorldTransform().GetLocation();
		Out.CenterOfMassCm = Com;
		Out.MassKg = Body->GetBodyMass();
		if (Each.PushSizeN > 0.1)
		{
			Out.PushN = Each.PushN;
			Out.PushAtCm = Com + Each.PushOffsetNcm / Each.PushSizeN;
		}
	}
	const double HeldGoal = WeightN > 0.0 ? FMath::Clamp(HeldN / WeightN, 0.0, 1.0) : 0.0;
	WeightHeld += (HeldGoal - WeightHeld) * Follow;
	for (FVector& Each : PushImpulseNs) { Each = FVector::ZeroVector; }
	for (FVector& Each : PushMomentNsCm) { Each = FVector::ZeroVector; }
	for (double& Each : PushSizeNs) { Each = 0.0; }

	// The feet with the floor's push at a shifted place, and what the trunk gives of the assist; only for an upright body.
	TArray<FSandboxWeightTorques::FBody> Shifted;
	TMap<int32, FVector> TrunkNm;
	if (bByPlace)
	{
		// The weight shared between the feet that the floor touches, by where the centre of mass stands over their soles.
		TArray<FVector2D> Soles[2];
		for (int32 Side = 0; Side < 2; ++Side)
		{
			if (Balance.FootForceN[Side] > 0.0)
			{
				FSandboxBodyProbe::SupportPolygon(Mesh, TArray<FName>{ FootBoneName[Side] }, WholeSoleToleranceCm, Soles[Side]);
			}
		}
		double FirstShare = 0.0;
		FVector2D At[2];
		FSandboxWeightTorques::ShareBetweenFeet(Soles[0], Soles[1], FVector2D(Balance.CenterOfMass.X, Balance.CenterOfMass.Y), FirstShare, At);
		const double Goals[2] = { Soles[0].Num() > 0 ? FirstShare * WeightN * WeightHeld : 0.0, Soles[1].Num() > 0 ? (1.0 - FirstShare) * WeightN * WeightHeld : 0.0 };
		for (int32 Side = 0; Side < 2; ++Side)
		{
			if (Goals[Side] > 0.0)
			{
				FootPushAt[Side] = FootPushN[Side] > 1.0 ? FMath::Lerp(FootPushAt[Side], At[Side], Follow) : At[Side];
			}
			FootPushN[Side] += (Goals[Side] - FootPushN[Side]) * Follow;
			const int32 Index = WeightBodies.IndexOfByPredicate([this, Side](const FWeightBody& Body) { return Body.Bone == FootBoneName[Side]; });
			if (Index != INDEX_NONE)
			{
				Bodies[Index].PushN = FVector(0.0, 0.0, FootPushN[Side]);
				Bodies[Index].PushAtCm = FVector(FootPushAt[Side], Balance.SupportHeightCm);
			}
		}
		const double LoadN = FootPushN[0] + FootPushN[1];
		if (IsUpright() && LoadN > 0.2 * WeightN && (Tuning.Weight.InternalShare > 0.0 || Tuning.Weight.AnkleAsTorque > 0.5))
		{
			FVector ForceN = FVector::ZeroVector;
			FVector TorqueNm = FVector::ZeroVector;
			if (Tuning.Weight.InternalShare > 0.0)
			{
				InternalAssist(ForceN, TorqueNm, TrunkNm);
				// No more than the soles' friction carries.
				ForceN = ForceN.GetClampedToMaxSize2D(0.6 * LoadN);
			}
			// A push at a place d from the one that only carries the weight turns the body by d x F, so a torque T is a shift of
			// (-Ty, Tx) / F; and a horizontal force at the floor turns it by its height over the floor, which is shifted away.
			const FAssist* Pelvis = Assists.FindByPredicate([](const FAssist& Assist) { return Assist.Slot == EAssistSlot::Pelvis; });
			const FBodyInstance* PelvisBody = Pelvis ? Mesh->GetBodyInstance(Pelvis->Bone) : nullptr;
			const double HeightM = PelvisBody ? FMath::Max(PelvisBody->GetCOMPosition().Z - Balance.SupportHeightCm, 0.0) * 0.01 : 0.0;
			FVector2D ShiftCm2 = StrategyShiftCm;
			ShiftCm2 += FVector2D(-TorqueNm.Y, TorqueNm.X) * (100.0 / LoadN);
			ShiftCm2 -= FVector2D(ForceN.X, ForceN.Y) * (HeightM * 100.0 / LoadN);
			const FVector2D Place = (FootPushAt[0] * FootPushN[0] + FootPushAt[1] * FootPushN[1]) / LoadN + ShiftCm2;
			for (int32 Side = 0; Side < 2; ++Side)
			{
				if (FootPushN[Side] < 0.05 * LoadN)
				{
					Soles[Side].Reset();
				}
			}
			double ShiftedShare = 0.0;
			FVector2D ShiftedAt[2];
			FSandboxWeightTorques::ShareBetweenFeet(Soles[0], Soles[1], Place, ShiftedShare, ShiftedAt);
			Shifted = Bodies;
			for (int32 Side = 0; Side < 2; ++Side)
			{
				const int32 Index = WeightBodies.IndexOfByPredicate([this, Side](const FWeightBody& Body) { return Body.Bone == FootBoneName[Side]; });
				const double Share = Side == 0 ? ShiftedShare : 1.0 - ShiftedShare;
				if (Index != INDEX_NONE && Soles[Side].Num() > 0)
				{
					Shifted[Index].PushN = FVector(ForceN.X * Share, ForceN.Y * Share, LoadN * Share);
					Shifted[Index].PushAtCm = FVector(ShiftedAt[Side], Balance.SupportHeightCm);
				}
			}
		}
	}

	TArray<FVector> Torques;
	const double GravityMps2 = GravityCmS2 * 0.01;
	if (!FSandboxWeightTorques::Compute(Bodies, FVector(0.0, 0.0, -GravityMps2 * WeightHeld), Torques))
	{
		return;
	}
	TArray<FVector> ShiftedTorques;
	if (Shifted.Num() > 0)
	{
		FSandboxWeightTorques::Compute(Shifted, FVector(0.0, 0.0, -GravityMps2 * WeightHeld), ShiftedTorques);
	}
	for (int32 Index = 0; Index < WeightBodies.Num(); ++Index)
	{
		if (WeightBodies[Index].Muscle == INDEX_NONE)
		{
			continue;
		}
		FMuscle& Muscle = Muscles[WeightBodies[Index].Muscle];
		const double CapNm = Tuning.Muscle(Muscle.Group).MaxTorqueNm;
		FVector TorqueNm = Torques[Index] * WeightShareNow[static_cast<int32>(Muscle.Group)];
		if (ShiftedTorques.IsValidIndex(Index) && Muscle.Group == ESandboxMuscleGroup::Legs)
		{
			TorqueNm += ShiftedTorques[Index] - Torques[Index];
		}
		if (const FVector* Trunk = TrunkNm.Find(WeightBodies[Index].Muscle))
		{
			TorqueNm += *Trunk;
		}
		// The shock of a landing is not answered with more than the joint has.
		if (CapNm > 0.0)
		{
			TorqueNm = TorqueNm.GetClampedToMaxSize(CapNm);
		}
		const double StepNm = (TorqueNm - Muscle.WeightNm).Size();
		if (StepNm > WeightStepMaxNm && !Muscle.WeightNm.IsZero())
		{
			WeightStepMaxNm = StepNm;
			WeightStepBone = Muscle.ChildBone;
			WeightStepAtS = ElapsedS;
		}
		WeightMaxNm = FMath::Max(WeightMaxNm, TorqueNm.Size());
		const bool bHad = !Muscle.WeightNm.IsZero();
		Muscle.WeightNm = TorqueNm;
		if (bHad || !TorqueNm.IsZero())
		{
			GiveWeightTorque(Muscle);
		}
	}
	if (!bWanted && !bAny)
	{
		bWeightOn = false;
		StopCollectingPushes();
		for (FMuscle& Muscle : Muscles)
		{
			Muscle.WeightNm = FVector::ZeroVector;
			GiveWeightTorque(Muscle);
		}
	}
}

void USandboxBodyController::InternalAssist(FVector& OutForceN, FVector& OutTorqueNm, TMap<int32, FVector>& OutJointNm) const
{
	for (const FAssist& Assist : Assists)
	{
		const FBodyInstance* Body = Assist.Slot != EAssistSlot::Feet ? Mesh->GetBodyInstance(Assist.Bone) : nullptr;
		if (!Body || !Body->IsValidBodyInstance())
		{
			continue;
		}
		// The assist's own spring law (UpdateAssistLoad).
		const FSandboxAssistTuning& Slot = AssistTuning(Assist.Slot);
		const double Share = Tuning.Weight.InternalShare;
		if (Slot.HorizontalNPerM > 0.0)
		{
			const FVector ErrorM = (AssistGoal(Assist).TransformPositionNoScale(Body->GetMassSpaceLocal().GetLocation()) - Body->GetCOMPosition()) * 0.01;
			const FVector VelocityMps = Body->GetUnrealWorldVelocity() * 0.01 - FVector(SupportVelocityCmS, 0.0) * 0.01;
			FVector CommandN = (ErrorM - VelocityMps * Slot.DampingS) * Slot.HorizontalNPerM * Share;
			CommandN.Z = 0.0;
			if (Slot.MaxForceN > 0.0)
			{
				CommandN = CommandN.GetClampedToMaxSize(Slot.MaxForceN);
			}
			OutForceN += CommandN;
		}
		if (Slot.AngularNmPerRad > 0.0)
		{
			const FVector ErrorRad = (Assist.Target.GetRotation() * Body->GetUnrealWorldTransform().GetRotation().Inverse()).GetShortestArcWith(FQuat::Identity).ToRotationVector();
			FVector CommandNm = (ErrorRad - Body->GetUnrealWorldAngularVelocityInRadians() * Slot.DampingS) * Slot.AngularNmPerRad * Share;
			if (Slot.MaxTorqueNm > 0.0)
			{
				CommandNm = CommandNm.GetClampedToMaxSize(Slot.MaxTorqueNm);
			}
			// Tilting only: a turn about the vertical is the feet's friction's.
			CommandNm.Z = 0.0;
			OutTorqueNm += CommandNm;
			// Every joint between this body and the pelvis carries it.
			int32 Index = WeightBodies.IndexOfByPredicate([&Assist](const FWeightBody& Each) { return Each.Bone == Assist.Bone; });
			while (Index != INDEX_NONE && WeightBodies[Index].Muscle != INDEX_NONE)
			{
				OutJointNm.FindOrAdd(WeightBodies[Index].Muscle) += CommandNm;
				Index = WeightBodies[Index].Parent;
			}
		}
	}
}

void USandboxBodyController::RequestTone(ESandboxTone Level, double Seconds)
{
	AskedTone = Seconds > 0.0 ? static_cast<int32>(Level) : -1;
	AskedToneUntilS = ElapsedS + Seconds;
	UE_LOG(LogSandbox, Display, TEXT("Body controller: tone %d asked for %.2f s at %.3f s"), AskedTone, Seconds, ElapsedS);
}

void USandboxBodyController::UpdateTone(double DeltaTime)
{
	const FSandboxToneTuning& Tone = Tuning.Tone;
	if (AskedTone >= 0 && ElapsedS >= AskedToneUntilS)
	{
		AskedTone = -1;
	}
	bool bChanged = false;
	for (int32 Group = 0; Group < 5; ++Group)
	{
		// A level asked for from outside, or the standing level of the group. (The states' own strengths are the states'.)
		const bool bUprightState = State != ESandboxBodyState::Falling && State != ESandboxBodyState::Lying;
		const bool bBraced = bUprightState && ((BraceFromS >= 0.0 && ElapsedS >= BraceFromS && ElapsedS < BraceUntilS) ||
			(AnticipateFromS >= 0.0 && ElapsedS < AnticipateUntilS));
		const int32 Level = AskedTone >= 0 ? AskedTone : bBraced ? static_cast<int32>(ESandboxTone::Braced) : FMath::Clamp(FMath::RoundToInt32(Tone.Standing[Group]), 0, SandboxToneCount - 1);
		double GoalStiffness = Tone.Stiffness[Level][Group];
		double GoalDampingNow = Tone.Damping[Level][Group];
		// Reaching for the floor in a fall, the arms are tense; once a hand has touched they give way (fall.arm_*).
		if (bFallAiming && Group == static_cast<int32>(ESandboxMuscleGroup::Arms))
		{
			const bool bTouched = HandTouchedS[0] >= 0.0 || HandTouchedS[1] >= 0.0;
			GoalStiffness = bTouched ? Tuning.FallAim.YieldStiffness : Tuning.FallAim.ReachStiffness;
			GoalDampingNow = bTouched ? Tuning.FallAim.YieldDamping : Tuning.FallAim.ReachDamping;
		}
		// Dazed: the legs and the spine, deepest at the hit and back over tone.daze_s.
		if (DazedAtS >= 0.0 && Tone.DazeS > 0.0 && (Group == static_cast<int32>(ESandboxMuscleGroup::Legs) || Group == static_cast<int32>(ESandboxMuscleGroup::Spine)))
		{
			const double Left = FMath::Clamp(1.0 - (ElapsedS - DazedAtS) / Tone.DazeS, 0.0, 1.0);
			GoalStiffness *= FMath::Lerp(1.0, Tone.DazeScale, DazeDepth * Left);
		}
		const auto Follow = [&](double& Now, double Goal)
		{
			if (Now == Goal)
			{
				return;
			}
			const double TimeS = Goal > Now ? Tone.RiseS : Tone.FallS;
			const double Share = TimeS > 0.0 ? 1.0 - FMath::Exp(-DeltaTime / TimeS) : 1.0;
			Now += (Goal - Now) * Share;
			// Close enough is there: a tone that only creeps toward its goal would never let the controls rest.
			if (FMath::Abs(Goal - Now) < 1.0e-4 * FMath::Max(Goal, 1.0))
			{
				Now = Goal;
			}
			bChanged = true;
		};
		Follow(ToneStiffness[Group], GoalStiffness);
		Follow(ToneDamping[Group], GoalDampingNow);
	}
	if (DazedAtS >= 0.0 && Tone.DazeS > 0.0 && ElapsedS - DazedAtS > Tone.DazeS)
	{
		DazedAtS = -1.0;
		DazeDepth = 0.0;
	}
	if (bChanged || bToneMoving)
	{
		PushTuningToControls();
	}
	bToneMoving = bChanged;
}

void USandboxBodyController::UpdateFallAim(double DeltaTime)
{
	const FSandboxFallAimTuning& Aim = Tuning.FallAim;
	for (int32 Side = 0; Side < 2; ++Side)
	{
		if (bHandTouchedFrame[Side] && bFallAiming && HandTouchedS[Side] < 0.0)
		{
			HandTouchedS[Side] = ElapsedS;
			if (FallHandTouchS < 0.0)
			{
				FallHandTouchS = ElapsedS;
			}
		}
		bHandTouchedFrame[Side] = false;
	}
	const bool bWant = Aim.Aim > 0.5 && State == ESandboxBodyState::Falling && !bHeldUp && TrunkTouchedS < 0.0 && Balance.bValid &&
		(Aim.ReachBack > 0.5 || FallWay != EFallWay::Back);
	if (!bWant)
	{
		if (bFallAiming)
		{
			StopFallAim();
		}
		return;
	}
	// The way the body goes down, as ChooseFallWay takes it, but every tick and smoothed: a fall turns as it goes.
	const FAssist* Pelvis = Assists.FindByPredicate([](const FAssist& Assist) { return Assist.Slot == EAssistSlot::Pelvis; });
	const FBodyInstance* PelvisBody = Pelvis ? Mesh->GetBodyInstance(Pelvis->Bone) : nullptr;
	if (!PelvisBody || !PelvisBody->IsValidBodyInstance())
	{
		return;
	}
	const FQuat Turned = PelvisBody->GetUnrealWorldTransform().GetRotation() * Pelvis->Target.GetRotation().Inverse();
	const FVector Up = Turned.RotateVector(FVector::UpVector);
	constexpr double LeanPerMps = 0.5;
	FVector2D Way = FVector2D(Up.X, Up.Y) / LeanPerMps + FVector2D(Balance.Velocity.X, Balance.Velocity.Y) * 0.01;
	if (!Way.Normalize())
	{
		return;
	}
	const bool bStarting = !bFallAiming;
	if (bStarting)
	{
		FallSkipSide = GuardSide;
		FallWayNow = Way;
		FallFloorZ = LastSupportHeightCm;
		HandTouchedS[0] = HandTouchedS[1] = -1.0;
		FallHandTouchS = -1.0;
		++FallReaches;
		bFallAiming = true;
		UE_LOG(LogSandbox, Display, TEXT("Body controller: reaching for the floor at %.3f s (way %.2f, %.2f)"), ElapsedS, Way.X, Way.Y);
	}
	else
	{
		const double Share = Aim.WayFollowS > 0.0 ? 1.0 - FMath::Exp(-DeltaTime / Aim.WayFollowS) : 1.0;
		FallWayNow += (Way - FallWayNow) * Share;
		FallWayNow.Normalize();
	}
	static const FName Hands[2] = { FName(TEXT("hand_l")), FName(TEXT("hand_r")) };
	static const FName Shoulders[2] = { FName(TEXT("upperarm_l")), FName(TEXT("upperarm_r")) };
	USandboxAnimInstance* Animation = Cast<USandboxAnimInstance>(Mesh->GetAnimInstance());
	const FReferenceSkeleton& Skeleton = Mesh->GetSkeletalMeshAsset()->GetRefSkeleton();
	for (int32 Side = 0; Side < 2; ++Side)
	{
		const FBodyInstance* Shoulder = Mesh->GetBodyInstance(Shoulders[Side]);
		const int32 End = Skeleton.FindBoneIndex(Hands[Side]);
		if (!Animation || !Shoulder || End == INDEX_NONE || HandTouchedS[Side] >= 0.0 || Side == FallSkipSide || Side == GuardSide)
		{
			// A hand on the floor stays where its target was.
			continue;
		}
		const FVector From = Shoulder->GetUnrealWorldTransform().GetLocation();
		const FVector Point(From.X + FallWayNow.X * Aim.ReachAheadCm, From.Y + FallWayNow.Y * Aim.ReachAheadCm, FallFloorZ + Aim.HandHeightCm);
		if (bStarting || !PlacedFeet.Contains(End))
		{
			FString Error;
			SetFootTarget(Hands[Side], Point, 1.0, Error, Aim.ReachBlendS);
		}
		else
		{
			Animation->MoveLimbTarget(End, Point, nullptr, nullptr, 0.0, 0.0);
			PlacedFeet.Add(End, Point);
		}
	}
}

void USandboxBodyController::UpdatePerception(double DeltaTime)
{
	// It reads the bodies and what it is told, and writes only its own account: measuring changes nothing.
	const FSandboxPerceptionTuning& Look = Tuning.Perception;
	PerceptionView.bOn = true;
	++PerceptionView.TellFrames;
	const FBodyInstance* Head = Mesh->GetBodyInstance(Tuning.Rig.Head);
	const FBodyInstance* Trunk = Mesh->GetBodyInstance(Tuning.Rig.Chest);
	if (!Head || !Head->IsValidBodyInstance() || !Trunk || !Trunk->IsValidBodyInstance())
	{
		return;
	}
	const FTransform HeadNow = Head->GetUnrealWorldTransform();
	const FVector Facing = HeadNow.GetRotation().RotateVector(HeadForwardLocal);
	const double CosHalf = FMath::Cos(FMath::DegreesToRadians(Look.ViewHalfAngleDeg));
	const struct { const FBodyInstance* Body; FName Name; double RadiusCm; bool bHead; } Parts[] = {
		{ Head, Tuning.Rig.Head, Look.HeadRadiusCm, true }, { Trunk, Tuning.Rig.Chest, Look.TrunkRadiusCm, false } };
	TArray<FSeenTrack> Next;
	Next.Reserve(Seen.Num());
	bool bAnyInView = false;
	for (const FSandboxSeen& Thing : Seen)
	{
		if (Thing.Kind == ESandboxSeenKind::Viewer)
		{
			continue;
		}
		const FSeenTrack* Before = SeenTracks.FindByPredicate([&Thing](const FSeenTrack& Track) { return Track.Name == Thing.Name; });
		FSeenTrack Track = Before ? *Before : FSeenTrack{ Thing.Name };
		const FVector To = Thing.LocationCm - Head->GetCOMPosition();
		const bool bInView = !To.IsNearlyZero() && (To.GetSafeNormal() | Facing) >= CosHalf;
		bAnyInView = bAnyInView || bInView;
		Track.InViewS = bInView ? Track.InViewS + DeltaTime : 0.0;
		const bool bNoticed = bInView && Track.InViewS >= Look.NoticeS - KINDA_SMALL_NUMBER;
		if (bNoticed && !Track.bNoticed)
		{
			++PerceptionView.Noticed;
		}
		Track.bNoticed = Track.bNoticed || bNoticed;
		// A threat: closing on the head or the trunk fast, with little time left, or held near the face. Only what is noticed.
		bool bThreat = false;
		FSandboxThreatEvent Worst;
		Worst.TimeToContactS = TNumericLimits<double>::Max();
		for (const auto& Part : Parts)
		{
			const FVector Gap = Thing.LocationCm - Part.Body->GetCOMPosition();
			const double DistanceCm = Gap.Size();
			const double GapCm = DistanceCm - Part.RadiusCm - (Thing.RadiusCm > 0.0 ? Thing.RadiusCm : Look.ThingRadiusCm);
			const double ClosingCmS = DistanceCm > 1.0 ? -((Thing.VelocityCmS - Part.Body->GetUnrealWorldVelocity()) | Gap) / DistanceCm : 0.0;
			// At the speed it closes with now. (With the gain of that speed counted in, from one frame to the next, a hand that had just
			// started read 0.09 s for 0.22: the start's jolt is no measure of what follows.)
			const double TimeS = ClosingCmS > 0.0 ? FMath::Max(GapCm, 0.0) / ClosingCmS : TNumericLimits<double>::Max();
			// On a course that meets the part: the nearest the two come, at the speed they close with now, within their sizes. (Asked of
			// the closing speed alone, a hand that would pass half a metre from the head counted.)
			const FVector Relative = Thing.VelocityCmS - Part.Body->GetUnrealWorldVelocity();
			const double RelativeSq = Relative.SizeSquared();
			const double Along = RelativeSq > 1.0 ? FMath::Max(-(Gap | Relative) / RelativeSq, 0.0) : 0.0;
			const double MissCm = (Gap + Relative * Along).Size() - Part.RadiusCm - (Thing.RadiusCm > 0.0 ? Thing.RadiusCm : Look.ThingRadiusCm);
			const bool bOnCourse = MissCm <= 0.0;
			const bool bComing = bOnCourse && ClosingCmS >= Look.ThreatSpeedMps * 100.0 && TimeS <= Look.ThreatTimeS;
			// Once a threat, it stays one while it still closes in twice the time: a speed or a time on its threshold made one hand many threats.
			const bool bStill = Track.bThreat && bOnCourse && ClosingCmS > 0.0 && TimeS <= 2.0 * Look.ThreatTimeS;
			const bool bNear = Part.bHead && GapCm <= Look.ThreatNearCm;
			if (bNoticed && (bComing || bStill || bNear) && (!bThreat || TimeS < Worst.TimeToContactS))
			{
				bThreat = true;
				Worst.Part = Part.Name;
				Worst.TimeToContactS = TimeS;
				Worst.GapCm = GapCm;
				Worst.ClosingMps = ClosingCmS * 0.01;
				Worst.bNear = !bComing && !bStill;
			}
		}
		Track.TimeToContactS = bThreat ? Worst.TimeToContactS : TNumericLimits<double>::Max();
		if (bThreat && !Track.bThreat)
		{
			Worst.Name = Thing.Name;
			Worst.TimeS = ElapsedS;
			PerceptionView.Threats.Add(Worst);
			if (Look.Guard > 0.5 && Worst.Part == Tuning.Rig.Head && (Worst.bNear || Worst.TimeToContactS >= Look.GuardTimeS) && GuardSide < 0 &&
				Look.Anticipate > 0.5 && AnticipateDueS < 0.0 && ElapsedS >= AnticipateUntilS)
			{
				// Only with an anticipation that falls due: the guard rises with it, or not at all (left over, it rose later for another threat).
				// The near arm: the threat's side of the chest (the mesh's left is +x).
				const FVector Across = Trunk->GetUnrealWorldTransform().GetRotation().UnrotateVector(Thing.LocationCm - Trunk->GetCOMPosition());
				const FVector Side = ChestRefRotation.RotateVector(Across);
				GuardWantedSide = Side.X >= 0.0 ? 0 : 1;
				GuardWantedOn = Thing.Name;
			}
			if (Look.Anticipate > 0.5 && AnticipateDueS < 0.0 && ElapsedS >= AnticipateUntilS)
			{
				AnticipateDueS = ElapsedS + Look.ReactS;
			}
			else if (Look.Anticipate > 0.5 && AnticipateFromS >= 0.0 && ElapsedS < AnticipateUntilS)
			{
				// Another threat while it holds: it holds on, rather than letting go before that one lands.
				AnticipateUntilS = FMath::Max(AnticipateUntilS, ElapsedS + Look.ReactS + Look.AnticipateHoldS);
				AnticipatePoseUntilS = FMath::Max(AnticipatePoseUntilS, AnticipateUntilS);
			}
			UE_LOG(LogSandbox, Display, TEXT("Body controller: %s is a threat to %s at %.3f s (%.2f s to the contact, %.1f cm, %.2f m/s%s)"), *Thing.Name.ToString(), *Worst.Part.ToString(),
				ElapsedS, Worst.TimeToContactS < 1.0e6 ? Worst.TimeToContactS : -1.0, Worst.GapCm, Worst.ClosingMps, Worst.bNear ? TEXT(", near the face") : TEXT(""));
		}
		Track.bThreat = bThreat;
		Next.Add(Track);
	}
	SeenTracks = MoveTemp(Next);
	if (bAnyInView)
	{
		++PerceptionView.SeenFrames;
	}
	if (Look.Gaze > 0.5)
	{
		UpdateGaze(DeltaTime, Facing);
	}
}

void USandboxBodyController::UpdateGaze(double DeltaTime, const FVector& Facing)
{
	const FSandboxPerceptionTuning& Look = Tuning.Perception;
	const FBodyInstance* Head = Mesh->GetBodyInstance(Tuning.Rig.Head);
	const FBodyInstance* Chest = Mesh->GetBodyInstance(Tuning.Rig.Chest);
	if (!Head || !Chest || NeckMeshIndex == INDEX_NONE)
	{
		return;
	}
	const FVector Eye = Head->GetCOMPosition();
	// What it attends to: a threat first, the one nearest its contact; else the nearest thing it has noticed that moves. It stays on a
	// thing for gaze_dwell_s before another may take its place, unless that one is a threat.
	const FSandboxSeen* Threat = nullptr;
	const FSandboxSeen* Mover = nullptr;
	const FSandboxSeen* Current = nullptr;
	double MoverCm = TNumericLimits<double>::Max();
	double ThreatS = TNumericLimits<double>::Max();
	for (const FSandboxSeen& Thing : Seen)
	{
		const FSeenTrack* Track = SeenTracks.FindByPredicate([&Thing](const FSeenTrack& Each) { return Each.Name == Thing.Name; });
		if (!Track || Track->InViewS < Look.NoticeS - KINDA_SMALL_NUMBER)
		{
			continue;
		}
		if (Thing.Name == GazeOn)
		{
			Current = &Thing;
		}
		if (Track->bThreat && (!Threat || Track->TimeToContactS < ThreatS))
		{
			Threat = &Thing;
			ThreatS = Track->TimeToContactS;
		}
		const double DistanceCm = FVector::Dist(Thing.LocationCm, Eye);
		if (Thing.VelocityCmS.Size() >= Look.GazeMoveMps * 100.0 && DistanceCm < MoverCm)
		{
			Mover = &Thing;
			MoverCm = DistanceCm;
		}
	}
	const bool bDwelling = Current && ElapsedS - GazeOnSinceS < Look.GazeDwellS;
	// Only an upright body whose pose is its own looks about: falling, lying, getting up or posed from outside, the head goes back.
	const bool bLooks = IsUpright() && State != ESandboxBodyState::GetUp && !IsPosedFromOutside();
	// After a threat, the place it is held at (N5 step 3): felt, so looked at whether it was in view or not.
	const FSandboxBodyHold* Held = nullptr;
	if (bLooks && !Threat)
	{
		for (const FSandboxBodyHold& Hold : Holds)
		{
			// A hold on the head moves with the head: the neck would chase it. The strongest other is looked at.
			if (FSandboxBodyTuning::GroupOfBone(Hold.Bone) != ESandboxMuscleGroup::Neck && (!Held || Hold.ForceN > Held->ForceN))
			{
				Held = &Hold;
			}
		}
	}
	const FSandboxSeen* Target = !bLooks || Held ? nullptr : Threat ? Threat : bDwelling ? Current : Mover ? Mover : Current;
	const FName TargetName = Held ? Held->Holder : Target ? Target->Name : NAME_None;
	const FVector TargetCm = Held ? Held->PointCm : Target ? Target->LocationCm : FVector::ZeroVector;
	if (TargetName != GazeOn)
	{
		GazeOn = TargetName;
		GazeOnSinceS = ElapsedS;
		++PerceptionView.GazeShifts;
	}
	// The way to it, seen from the chest as the reference pose has it: yaw about up, pitch about the side.
	double GoalYawDeg = 0.0;
	double GoalPitchDeg = 0.0;
	if (Target || Held)
	{
		const FVector Way = ChestRefRotation.RotateVector(Chest->GetUnrealWorldTransform().GetRotation().UnrotateVector(TargetCm - Eye)).GetSafeNormal();
		// Straight above or below the yaw means nothing: the head keeps the yaw it has.
		GoalYawDeg = FVector2D(Way.X, Way.Y).Size() > 0.2 ? FMath::RadiansToDegrees(FMath::Atan2(-Way.X, Way.Y)) * Look.GazeHeadShare : GazeYawDeg;
		GoalPitchDeg = FMath::RadiansToDegrees(FMath::Atan2(Way.Z, FVector2D(Way.X, Way.Y).Size())) * Look.GazeHeadShare;
		const double ErrorDeg = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(Facing | (TargetCm - Eye).GetSafeNormal(), -1.0, 1.0)));
		++PerceptionView.GazeFrames;
		PerceptionView.GazeErrorSumDeg += ErrorDeg;
		if (Held)
		{
			++PerceptionView.GazeHoldFrames;
			PerceptionView.GazeHoldErrorSumDeg += ErrorDeg;
		}
	}
	const bool bAtLimit = FMath::Abs(GoalYawDeg) > Look.GazeMaxYawDeg || FMath::Abs(GoalPitchDeg) > Look.GazeMaxPitchDeg;
	PerceptionView.GazeAtLimitFrames += bAtLimit ? 1 : 0;
	GoalYawDeg = FMath::Clamp(GoalYawDeg, -Look.GazeMaxYawDeg, Look.GazeMaxYawDeg);
	GoalPitchDeg = FMath::Clamp(GoalPitchDeg, -Look.GazeMaxPitchDeg, Look.GazeMaxPitchDeg);
	StepGaze(DeltaTime, GoalYawDeg, GoalPitchDeg);
}

void USandboxBodyController::UpdateAnticipation(double DeltaTime)
{
	const FSandboxPerceptionTuning& Look = Tuning.Perception;
	// Due: unless the blow has landed already (too late), or the body is no longer upright, it braces and draws its head in.
	if (AnticipateDueS >= 0.0 && ElapsedS >= AnticipateDueS - KINDA_SMALL_NUMBER)
	{
		const double ThreatS = AnticipateDueS - Look.ReactS;
		AnticipateDueS = -1.0;
		if (LastHitS < ThreatS - KINDA_SMALL_NUMBER && IsUpright() && State != ESandboxBodyState::GetUp && bAnticipationJudged)
		{
			AnticipateFromS = ElapsedS;
			AnticipateUntilS = ElapsedS + Look.AnticipateHoldS;
			AnticipatePoseUntilS = AnticipateUntilS;
			bAnticipationJudged = false;
			++PerceptionView.Anticipations;
			if (GuardWantedSide >= 0 && GuardSide < 0)
			{
				GuardSide = GuardWantedSide;
				GuardOn = GuardWantedOn;
				GuardUntilS = ElapsedS + Look.GuardHoldS;
				GuardPointCm = FVector::ZeroVector;
				bGuardPlaced = false;
				++PerceptionView.Guards;
				UE_LOG(LogSandbox, Display, TEXT("Body controller: raises its %s arm against %s at %.3f s"), GuardSide == 0 ? TEXT("left") : TEXT("right"), *GuardOn.ToString(), ElapsedS);
			}
			UE_LOG(LogSandbox, Display, TEXT("Body controller: braces for what it sees coming at %.3f s"), ElapsedS);
		}
		// The arm a threat to the head asked for is raised now or not at all.
		GuardWantedSide = -1;
	}
	// What followed: a contact (and how long after the brace), or none within a second (a false alarm).
	if (!bAnticipationJudged && AnticipateFromS >= 0.0)
	{
		if (LastHitS >= AnticipateFromS - KINDA_SMALL_NUMBER)
		{
			PerceptionView.AnticipationLeadSumS += LastHitS - AnticipateFromS;
			++PerceptionView.AnticipationLeads;
			bAnticipationJudged = true;
			// The brace and the pose hold on through the contact; with the hit's flinch the pose is taken as the larger of the two (UpdatePoseTurns).
		}
		else if (ElapsedS - AnticipateFromS > 1.0)
		{
			++PerceptionView.FalseAlarms;
			bAnticipationJudged = true;
		}
	}
	if (GuardSide >= 0)
	{
		UpdateGuard();
	}
	// The pose: in over anticipate_pose_s while it holds, out again after; never in one step.
	const bool bHolding = AnticipateFromS >= 0.0 && ElapsedS < AnticipatePoseUntilS && IsUpright();
	const double StepShare = Look.AnticipatePoseS > 0.0 ? DeltaTime / Look.AnticipatePoseS : 1.0;
	AnticipateWeight = FMath::Clamp(AnticipateWeight + (bHolding ? StepShare : -StepShare), 0.0, 1.0);
	if (!bHolding && AnticipateWeight <= 0.0 && bAnticipationJudged && ElapsedS >= AnticipateUntilS)
	{
		AnticipateFromS = -1.0;
	}
}

bool USandboxBodyController::PlaceArm(FName Hand, const FVector& WorldLocation, double Weight, double BlendS, bool bElbowDown)
{
	USandboxAnimInstance* Animation = bStarted ? Cast<USandboxAnimInstance>(Mesh->GetAnimInstance()) : nullptr;
	const FReferenceSkeleton& Skeleton = Mesh->GetSkeletalMeshAsset()->GetRefSkeleton();
	const int32 End = Skeleton.FindBoneIndex(Hand);
	const int32 Middle = End != INDEX_NONE ? Skeleton.GetParentIndex(End) : INDEX_NONE;
	const int32 Root = Middle != INDEX_NONE ? Skeleton.GetParentIndex(Middle) : INDEX_NONE;
	if (!Animation || Root == INDEX_NONE)
	{
		return false;
	}
	const bool bLeft = Hand.ToString().EndsWith(TEXT("_l"));
	// A reach bends the elbow back and out; a guard keeps it down, so that the forearm stands before the face.
	Animation->SetLimbBend(End, bElbowDown ? FVector(bLeft ? 0.3 : -0.3, 0.2, -1.0).GetSafeNormal() : FVector(bLeft ? 0.4 : -0.4, -1.0, -0.3).GetSafeNormal(), FVector::XAxisVector,
		/*bEaseTwice*/ bElbowDown);
	FSandboxJointLimits Shoulder;
	FSandboxJointLimits Elbow;
	if (FSandboxPoseRig::ReadJointLimits(Mesh, Skeleton.GetBoneName(Root), Shoulder) && FSandboxPoseRig::ReadJointLimits(Mesh, Skeleton.GetBoneName(Middle), Elbow))
	{
		Animation->SetLimbRanges(End, Shoulder, Elbow);
	}
	Animation->SetLimbTarget(End, WorldLocation, Weight, BlendS);
	return true;
}

void USandboxBodyController::UpdateGuard()
{
	static const FName Hands[2] = { FName(TEXT("hand_l")), FName(TEXT("hand_r")) };
	const FSandboxPerceptionTuning& Look = Tuning.Perception;
	const FBodyInstance* Head = Mesh->GetBodyInstance(Tuning.Rig.Head);
	const FSandboxSeen* Thing = Seen.FindByPredicate([this](const FSandboxSeen& Each) { return Each.Name == GuardOn; });
	if (ElapsedS >= GuardUntilS || !IsUpright() || State == ESandboxBodyState::GetUp || IsPosedFromOutside() || !Head || (!Thing && !bGuardPlaced))
	{
		StopGuard();
		return;
	}
	// Toward the threat as it is now, guard_reach_cm from the face; where it was last if it is no longer told.
	const FVector Face = Head->GetCOMPosition();
	const FVector Point = Thing ? Face + (Thing->LocationCm - Face).GetSafeNormal() * Look.GuardReachCm : GuardPointCm;
	USandboxAnimInstance* Animation = Cast<USandboxAnimInstance>(Mesh->GetAnimInstance());
	const int32 End = Mesh->GetSkeletalMeshAsset()->GetRefSkeleton().FindBoneIndex(Hands[GuardSide]);
	if (!bGuardPlaced)
	{
		PlaceArm(Hands[GuardSide], Point, 1.0, Look.GuardBlendS, /*bElbowDown*/ true);
		bGuardPlaced = true;
	}
	else if (Animation && End != INDEX_NONE)
	{
		Animation->MoveLimbTarget(End, Point, nullptr, nullptr, 0.0, 0.0);
	}
	GuardPointCm = Point;
}

void USandboxBodyController::StopGuard()
{
	static const FName Hands[2] = { FName(TEXT("hand_l")), FName(TEXT("hand_r")) };
	if (GuardSide >= 0 && bGuardPlaced)
	{
		if (USandboxAnimInstance* Animation = Cast<USandboxAnimInstance>(Mesh->GetAnimInstance()))
		{
			const int32 End = Mesh->GetSkeletalMeshAsset()->GetRefSkeleton().FindBoneIndex(Hands[GuardSide]);
			if (End != INDEX_NONE)
			{
				Animation->SetLimbTarget(End, GuardPointCm, 0.0, Tuning.Perception.GuardBlendS);
			}
		}
	}
	GuardSide = -1;
	GuardOn = NAME_None;
	bGuardPlaced = false;
}

void USandboxBodyController::StepGaze(double DeltaTime, double GoalYawDeg, double GoalPitchDeg)
{
	// No faster than gaze_speed_dps, and up to that speed and down from it over gaze_rise_s: the neck's target never jumps, and
	// never starts or stops turning in one frame. It slows so as to arrive, not to overshoot.
	const FSandboxPerceptionTuning& Look = Tuning.Perception;
	const double MaxDps = Look.GazeSpeedDps;
	const double Accel = Look.GazeRiseS > 0.0 ? MaxDps / Look.GazeRiseS : TNumericLimits<double>::Max();
	const auto Axis = [&](double& Now, double& Rate, double Goal)
	{
		const double Left = Goal - Now;
		// The speed that stops on the goal with the acceleration there is.
		const double Arrive = FMath::Sign(Left) * FMath::Min(MaxDps, FMath::Sqrt(2.0 * Accel * FMath::Abs(Left)));
		const double Change = Accel * DeltaTime;
		Rate = FMath::Clamp(Arrive, Rate - Change, Rate + Change);
		double Step = Rate * DeltaTime;
		if ((Left >= 0.0 && Step > Left) || (Left <= 0.0 && Step < Left))
		{
			Step = Left;
			Rate = 0.0;
		}
		Now += Step;
	};
	Axis(GazeYawDeg, GazeYawRateDps, GoalYawDeg);
	Axis(GazePitchDeg, GazePitchRateDps, GoalPitchDeg);
}

void USandboxBodyController::StopFallAim()
{
	bFallAiming = false;
	static const FName Hands[2] = { FName(TEXT("hand_l")), FName(TEXT("hand_r")) };
	USandboxAnimInstance* Animation = Cast<USandboxAnimInstance>(Mesh->GetAnimInstance());
	const FReferenceSkeleton& Skeleton = Mesh->GetSkeletalMeshAsset()->GetRefSkeleton();
	for (int32 Side = 0; Side < 2; ++Side)
	{
		const int32 End = Skeleton.FindBoneIndex(Hands[Side]);
		if (const FVector* Placed = End != INDEX_NONE ? PlacedFeet.Find(End) : nullptr)
		{
			if (Animation)
			{
				Animation->SetLimbTarget(End, *Placed, 0.0, Tuning.Poses.FallBlendS);
			}
			PlacedFeet.Remove(End);
		}
	}
	UE_LOG(LogSandbox, Display, TEXT("Body controller: the reach for the floor ends at %.3f s (a hand touched at %.3f s)"), ElapsedS, FallHandTouchS);
}

void USandboxBodyController::GiveWeightTorque(const FMuscle& Muscle)
{
	// Through the muscle's own drive, as a turn of its target: the drive's torque is stiffness times the error, so a target
	// turned by torque / stiffness about the torque's axis is meant to be that torque on top of the spring, within the
	// joint's cap. It is not exact: on an arm held out it reached the shoulder by 100% held forward and by 65 to 75% held
	// out to the side, the same as a turning speed asked of the drive; the cause was not found. Put on the two bodies as a
	// torque pair instead it counted 1.6 to 1.7 times, which is the solver's inertia conditioning (shown by switching it
	// off: 0.5 N*m left of 8.3); switched off for the body's own bodies while it runs, the solver did not take the change.
	const FBodyInstance* Parent = Mesh->GetBodyInstance(Muscle.ParentBone);
	FPhysicsControlTarget Target;
	if (Parent && Parent->IsValidBodyInstance() && Muscle.StiffnessNmPerRad > 0.0 && !Muscle.WeightNm.IsZero())
	{
		const FQuat CachedParent = Control->GetCachedBoneTransform(Mesh, Muscle.ParentBone).GetRotation();
		const FQuat CachedChild = Control->GetCachedBoneTransform(Mesh, Muscle.ChildBone).GetRotation();
		const FQuat TargetWorld = (Parent->GetUnrealWorldTransform().GetRotation() * CachedParent.Inverse() * CachedChild).GetNormalized();
		// The record's orientation is taken in the space of the target from the pose.
		const FVector TurnRad = TargetWorld.UnrotateVector(Muscle.WeightNm / Muscle.StiffnessNmPerRad);
		Target.TargetOrientation = FQuat::MakeFromRotationVector(TurnRad.GetClampedToMaxSize(UE_DOUBLE_PI * 0.75)).Rotator();
	}
	Control->SetControlTarget(Muscle.Control, Target);
}

double USandboxBodyController::LandShare(FName ChildBone) const
{
	if (Tuning.Balance.LandEaseS <= 0.0 || !IsUpright())
	{
		return 0.0;
	}
	for (int32 Side = 0; Side < 2; ++Side)
	{
		if (ChildBone != ThighBoneName[Side] && ChildBone != CalfBoneName[Side] && ChildBone != FootBoneName[Side])
		{
			continue;
		}
		for (int32 Index = StepLog.Num() - 1; Index >= 0; --Index)
		{
			if (StepLog[Index].Side == Side && StepLog[Index].LandS >= 0.0)
			{
				return 1.0 - FMath::SmoothStep(0.0, 1.0, FMath::Clamp((ElapsedS - StepLog[Index].LandS) / Tuning.Balance.LandEaseS, 0.0, 1.0));
			}
		}
	}
	return 0.0;
}

double USandboxBodyController::FallEaseShare() const
{
	if (State != ESandboxBodyState::Falling || Tuning.Balance.FallEaseS <= 0.0)
	{
		return 1.0;
	}
	return FMath::SmoothStep(0.0, 1.0, FMath::Clamp((ElapsedS - StateEnteredS) / Tuning.Balance.FallEaseS, 0.0, 1.0));
}

void USandboxBodyController::UpdateStrengthEases()
{
	// The strengths that go over in time are handed to the controls while they do, and once more at the end.
	const auto Running = [this](double SinceS, double EaseS) { return EaseS > 0.0 && ElapsedS - SinceS <= EaseS + 0.02; };
	bool bPush = State == ESandboxBodyState::Falling && Running(StateEnteredS, Tuning.Balance.FallEaseS);
	for (int32 Index = FMath::Max(StepLog.Num() - 2, 0); Index < StepLog.Num() && !bPush; ++Index)
	{
		bPush = IsUpright() && StepLog[Index].LandS >= 0.0 && Running(StepLog[Index].LandS, Tuning.Balance.LandEaseS);
	}
	if (bPush)
	{
		PushTuningToControls();
	}
}

void USandboxBodyController::PushTuningToControls()
{
	ApplySurface();
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

	if (USandboxAnimInstance* Animation = Cast<USandboxAnimInstance>(Mesh->GetAnimInstance()))
	{
		Animation->SetReachWithWeight(Tuning.Balance.ReachSmooth > 0.5);
	}
	const double FallShare = FallEaseShare();
	for (FMuscle& Muscle : Muscles)
	{
		FSandboxMuscleTuning Group = Tuning.Muscle(Muscle.Group);
		// The leg of a step under way is stiffer, so that the foot keeps up with its point.
		const bool bSwingLeg = Swing.bActive &&
			(Muscle.ChildBone == ThighBoneName[Swing.Side] || Muscle.ChildBone == CalfBoneName[Swing.Side] || Muscle.ChildBone == FootBoneName[Swing.Side]);
		// Eased in from the lift over balance.step_ease_s: doubled at once, the error the leg had while it carried weight
		// was pushed twice as hard into the floor.
		const double SwingEase = Tuning.Balance.StepEaseS > 0.0 ? FMath::SmoothStep(0.0, 1.0, FMath::Min(Swing.ElapsedS / Tuning.Balance.StepEaseS, 1.0)) : 1.0;
		// And out again once it has landed, over balance.land_ease_s.
		const double SwingShare = bSwingLeg ? FMath::Max(SwingEase, LandShare(Muscle.ChildBone)) : LandShare(Muscle.ChildBone);
		const double SwingScale = FMath::Lerp(1.0, FMath::Max(Tuning.Balance.SwingStiffnessScale, 1.0), SwingShare);
		const double SwingDampingScale = FMath::Lerp(1.0, Tuning.Balance.SwingDampingScale, SwingShare);
		// The joint's own muscle and the body's tone (N3); 1 and ready are the group's muscle as it was.
		const FSandboxJointScale* Joint = Tuning.JointScale(Muscle.ChildBone);
		const int32 GroupIndex = static_cast<int32>(Muscle.Group);
		const double JointStiffness = (Joint ? Joint->StiffnessScale : 1.0) * ToneStiffness[GroupIndex];
		const double JointDamping = (Joint ? Joint->DampingScale : 1.0) * ToneDamping[GroupIndex];
		Group.MaxTorqueNm *= Joint ? Joint->MaxTorqueScale : 1.0;
		double Spring = Group.StiffnessNmPerRad * JointStiffness * MuscleScale(Muscle.Group) * SwingScale * TorqueToUnreal / ChaosAngularDriveScale;
		// The damping from the spring the muscle would have without the joint's and the tone's factor on stiffness, times theirs on
		// damping: worked out from the spring itself, a stiffness factor of 0 took the damping with it. (With both factors 1 the two
		// are the same number.)
		const double SpringBase = JointStiffness == 1.0 ? Spring : Group.StiffnessNmPerRad * MuscleScale(Muscle.Group) * SwingScale * TorqueToUnreal / ChaosAngularDriveScale;
		double ExtraDamping = JointStiffness == 1.0 && JointDamping == 1.0 ? Spring * Group.DampingS * SwingDampingScale
			: SpringBase * Group.DampingS * SwingDampingScale * JointDamping;
		double VelocityShare = FMath::Lerp(Group.PoseVelocityShare, Tuning.Balance.SwingPoseVelocityShare, SwingShare);
		if (FallShare < 1.0 && bFallEaseFromSet)
		{
			// Over to the fall's strengths from what this muscle had as the fall began.
			const double FromSpring = Muscle.FallFromStiffness * TorqueToUnreal / ChaosAngularDriveScale;
			const double FromDamping = Muscle.FallFromDamping * TorqueToUnreal / ChaosAngularDriveScale;
			Spring = FMath::Lerp(FromSpring, Spring, FallShare);
			ExtraDamping = FMath::Lerp(FromDamping, ExtraDamping, FallShare);
			VelocityShare = FMath::Lerp(Muscle.FallFromVelocityShare, VelocityShare, FallShare);
		}
		FPhysicsControlData Data;
		// Force mode: the spring is a real torque per radian. In the plugin's default (acceleration) mode
		// it would be scaled by the inertia of the two jointed bodies alone, which leaves an ankle or a hip
		// far too weak for the weight of everything above it.
		Data.bUseAccelerationDriveMode = false;
		Data.LinearStrength = 0.0f; // the joint itself holds the bodies together
		Data.AngularStrength = StrengthForSpring(Spring);
		Data.AngularDampingRatio = 0.0f;
		Data.AngularExtraDamping = static_cast<float>(ExtraDamping);
		Data.MaxTorque = static_cast<float>(Group.MaxTorqueNm * TorqueToUnreal);
		Data.AngularTargetVelocityMultiplier = static_cast<float>(VelocityShare);
		Data.bUseSkeletalAnimation = true;
		// The two bodies are jointed and must never collide, whatever this extra constraint says.
		Data.bDisableCollision = true;
		Control->SetControlData(Muscle.Control, Data);
		// The same in physical units, for whoever looks at the controls (GetControlView); nothing here reads it.
		Muscle.StiffnessNmPerRad = Spring * ChaosAngularDriveScale / TorqueToUnreal;
		const bool bAsBefore = JointStiffness == 1.0 && JointDamping == 1.0 && !(FallShare < 1.0 && bFallEaseFromSet);
		Muscle.DampingNmsPerRad = bAsBefore ? Muscle.StiffnessNmPerRad * Group.DampingS * SwingDampingScale : ExtraDamping * ChaosAngularDriveScale / TorqueToUnreal;
		Muscle.MaxTorqueNm = Group.MaxTorqueNm;
		Muscle.PoseVelocityShare = Data.AngularTargetVelocityMultiplier;
	}

	for (const FAssist& Assist : Assists)
	{
		FSandboxAssistTuning Slot = AssistTuning(Assist.Slot);
		if (IsSwingFoot(Assist))
		{
			// The foot of a step under way is guided along its swing (UpdateStep moves the target).
			Slot.HorizontalNPerM = Tuning.Balance.SwingFootNPerM;
			Slot.VerticalNPerM = Tuning.Balance.SwingFootNPerM;
		}
		if (State == ESandboxBodyState::GetUp)
		{
			// The pull that helps the body up comes up from nothing, as its muscles do.
			const double Ease = GetUpEase();
			Slot.HorizontalNPerM *= Ease;
			Slot.VerticalNPerM *= Ease;
			Slot.AngularNmPerRad *= Ease;
			ApplyFastRiseCaps(Assist, Slot);
		}
		{
			// What the legs and the trunk give of it the world does not.
			const double WorldShare = WorldShareOf(Assist.Slot);
			Slot.HorizontalNPerM *= WorldShare;
			Slot.VerticalNPerM *= WorldShare;
			Slot.AngularNmPerRad *= WorldShare;
		}
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
		Data.MaxTorque = static_cast<float>(Slot.MaxTorqueNm * TorqueToUnreal);
		// 1 N = 100 kg*cm/s^2.
		Data.MaxForce = static_cast<float>(Slot.MaxForceN * 100.0);
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
		LastSupportPolygon = Balance.SupportPolygon;
	}
	FollowSupport(DeltaTime);
	UpdateFingers(DeltaTime);
	if (bPerceptionAllowed && Tuning.Perception.Enabled > 0.5)
	{
		UpdatePerception(DeltaTime);
	}
	// A gaze switched off while the head is turned brings it back at the gaze's own pace, not at once and not never.
	if ((GazeYawDeg != 0.0 || GazePitchDeg != 0.0 || GazeYawRateDps != 0.0 || GazePitchRateDps != 0.0) &&
		!(bPerceptionAllowed && Tuning.Perception.Enabled > 0.5 && Tuning.Perception.Gaze > 0.5))
	{
		StepGaze(DeltaTime, 0.0, 0.0);
	}
	// What the body felt since the last tick: a hit may make it flinch, a held push makes it lean.
	FeelFrame(DeltaTime);
	if (AnticipateDueS >= 0.0 || AnticipateFromS >= 0.0 || AnticipateWeight > 0.0 || !bAnticipationJudged || GuardSide >= 0)
	{
		UpdateAnticipation(DeltaTime);
	}
	for (FVector& Impulse : SupportImpulseNs)
	{
		Impulse = FVector::ZeroVector;
	}
	SupportStepS = DeltaTime;

	Control->UpdateTargetCaches(DeltaTime);
	UpdateState(DeltaTime);
	UpdateHeld(DeltaTime);
	UpdateGetUp(DeltaTime);
	// The plank after a get-up, measured (read only): the same rule as while getting up, for 3 s after it ended standing.
	if (State != ESandboxBodyState::GetUp && GetUpView.DurationS >= 0.0 && ElapsedS - (GetUpView.StartedS + GetUpView.DurationS) <= 3.0 &&
		Balance.bValid && Balance.IsSupported() && WeightN > 0.0 &&
		Balance.FootForceN[0] + Balance.FootForceN[1] >= 0.8 * WeightN && Balance.CenterOfMassMarginCm < -5.0)
	{
		GetUpView.PlankAfterS += DeltaTime;
	}
	if (!RiseLeanRad.IsZero() && !(State == ESandboxBodyState::GetUp && GetUpRun.bRising))
	{
		// A rise's lean fades by its own clock once the rise is over, however it ended: a turn dropped at once would snap the body.
		RiseLeanRad *= Tuning.Poses.BlendS > 0.0 ? FMath::Exp(-DeltaTime / Tuning.Poses.BlendS) : 0.0;
		if (RiseLeanRad.Size() < 1.0e-4)
		{
			RiseLeanRad = FVector2D::ZeroVector;
		}
	}
	UpdateRegroup(DeltaTime);
	UpdateStep(DeltaTime);
	UpdateStrengthEases();
	if (Tuning.FallAim.Aim > 0.5 || bFallAiming)
	{
		UpdateFallAim(DeltaTime);
	}
	UpdateTone(DeltaTime);
	if (Tuning.Balance.StaggerBrakeFollowS > 0.0)
	{
		const FVector2D Goal = State == ESandboxBodyState::Stagger ? FVector2D(Balance.Velocity.X, Balance.Velocity.Y) * Tuning.Balance.StaggerDampingS : FVector2D::ZeroVector;
		StaggerBrakeCm += (Goal - StaggerBrakeCm) * (1.0 - FMath::Exp(-DeltaTime / Tuning.Balance.StaggerBrakeFollowS));
	}
	UpdateWeight(DeltaTime);
	UpdateShift(DeltaTime);
	UpdateLowering(DeltaTime);
	UpdateIdle(DeltaTime);
	UpdateStrategies(DeltaTime);
	UpdatePlacements(DeltaTime);
	UpdateClipPose(DeltaTime);
	UpdatePoseTurns();
	UpdateAssistLoad();
	Control->UpdateControls(DeltaTime);

	// Advanced last: during a tick this is the time at which the coming physics step begins.
	ElapsedS += DeltaTime;
}

double USandboxBodyController::DecisionMarginCm() const
{
	if (!Balance.bValid)
	{
		return 0.0;
	}
	// The support a decision is made on: every foot the floor touches, whatever its load, and those it touched
	// until a moment ago. A foot that carries less than the standing threshold (balance.foot_contact_min_n)
	// is still under the body and takes weight the moment the body sways onto it; a foot knocked off the floor
	// comes back down by itself, where it is, within balance.lift_grace_s. Only a foot that has been off the
	// floor longer no longer counts. (AirS is kept by FollowSupport, which runs first.)
	TArray<FName, TInlineAllocator<2>> Planted;
	for (const FAssist& Assist : Assists)
	{
		// A foot the pose holds where it was set down after a step is support too: if the body leans off it and
		// it loses the floor for a moment, the leg still puts it there.
		const bool bHeld = Assist.Side >= 0 && Placement[Assist.Side].Mode == EPlacement::Planted;
		if (Assist.Slot == EAssistSlot::Feet && (Assist.AirS < Tuning.Balance.LiftGraceS - KINDA_SMALL_NUMBER || bHeld))
		{
			Planted.Add(Assist.Bone);
		}
	}
	// Under a held push the centre of pressure has to be F h / (m g) further along the force than the capture
	// point alone says: that is the point that must stay inside the support.
	const FVector2D Shift = WeightN > 0.0 ? SustainedMomentNCm / WeightN : FVector2D::ZeroVector;
	const FVector2D Point = Balance.CapturePoint + Shift;
	// While a foot swings, the support is the one the step will give: the stance sole and the swinging sole at
	// its target.
	if (Swing.bActive)
	{
		TArray<FVector2D> Planned;
		PlannedSupport(Planned);
		if (Planned.Num() > 0)
		{
			return FSandboxBodyProbe::MarginInsidePolygon(Planned, Point);
		}
	}
	// A foot just set down is support with its whole sole, flat where it landed, while it settles (balance.step_settle_s):
	// it comes down on its heel or edge, and the few centimetres of sole that touch at first put a capture point that the
	// step had caught with 10 cm to spare 4 cm outside, and a second step followed.
	if (LandedSoleUntilS > ElapsedS && LandedSole.Num() > 0 && LandedSoleSide >= 0)
	{
		TArray<FVector2D> Points = LandedSole;
		TArray<FName, TInlineAllocator<2>> Others = Planted;
		Others.Remove(FootBoneName[LandedSoleSide]);
		if (Others.Num() > 0)
		{
			TArray<FVector2D> OtherSoles;
			FSandboxBodyProbe::SupportPolygon(Mesh, Others, Tuning.Balance.SoleToleranceCm, OtherSoles);
			Points.Append(OtherSoles);
		}
		TArray<FVector2D> Hull;
		FSandboxBodyProbe::ConvexHull2D(Points, Hull);
		if (Hull.Num() > 0)
		{
			return FSandboxBodyProbe::MarginInsidePolygon(Hull, Point);
		}
	}
	// The whole sole of every such foot, also the part that is off the floor: a body that rises on its toes or rocks
	// back on its heels still has its feet under it, and the rest of the sole comes down the moment the body goes
	// that way. (Judged against the part that touched, a body thrown onto its toes by a blow on the back of the head
	// had its capture point "4.7 cm outside the support", under its own raised heels, and stepped back twice.)
	if (Planted.Num() > 0)
	{
		TArray<FVector2D> Polygon;
		FSandboxBodyProbe::SupportPolygon(Mesh, Planted, WholeSoleToleranceCm, Polygon);
		if (Polygon.Num() > 0)
		{
			return FSandboxBodyProbe::MarginInsidePolygon(Polygon, Point);
		}
	}
	// No foot stands this instant (both lifted by a blow, or one frame without load): judged against where
	// the body stood last.
	return LastSupportPolygon.Num() > 0 ? FSandboxBodyProbe::MarginInsidePolygon(LastSupportPolygon, Point) : 0.0;
}

void USandboxBodyController::SupportFrame(FVector& OutLocation, FQuat& OutRotation) const
{
	FVector Sum = FVector::ZeroVector;
	double YawSum = 0.0;
	int32 Feet = 0;
	for (const FAssist& Assist : Assists)
	{
		if (Assist.Slot != EAssistSlot::Feet)
		{
			continue;
		}
		Sum += Assist.Target.GetLocation();
		// The foot's turn about the vertical since Start.
		FQuat Tilt, Twist;
		(Assist.Target.GetRotation() * Assist.StartRotation.Inverse()).ToSwingTwist(FVector::UpVector, Tilt, Twist);
		YawSum += Twist.GetTwistAngle(FVector::UpVector);
		++Feet;
	}
	OutLocation = Feet > 0 ? Sum / Feet : FVector::ZeroVector;
	OutRotation = Feet > 0 ? FQuat(FVector::UpVector, YawSum / Feet) : FQuat::Identity;
}

FTransform USandboxBodyController::AssistGoal(const FAssist& Assist) const
{
	if (Assist.AppliedLeanCm.IsZero())
	{
		return Assist.Target;
	}
	FTransform Goal = Assist.Target;
	Goal.AddToTranslation(FVector(Assist.AppliedLeanCm * Assist.LeanFactor, 0.0));
	return Goal;
}

void USandboxBodyController::SetAssistTarget(const FAssist& Assist)
{
	const FTransform Goal = AssistGoal(Assist);
	FPhysicsControlTarget Target;
	Target.TargetPosition = Goal.GetLocation();
	Target.TargetOrientation = Goal.Rotator();
	Target.bApplyControlPointToTarget = true;
	Control->SetControlTarget(Assist.Control, Target);
}

void USandboxBodyController::CarrySupport(double DeltaTime)
{
	// What carried the feet in the step that has just ended, its velocity weighted by how hard each foot was pushed up.
	const FVector2D Now = SupportVelocityWeight > 0.0 ? SupportVelocitySum / SupportVelocityWeight : FVector2D::ZeroVector;
	SupportVelocitySum = FVector2D::ZeroVector;
	SupportVelocityWeight = 0.0;
	constexpr double StillCmS = 2.0;
	const FVector2D Wanted = Now.Size() > StillCmS ? Now : FVector2D::ZeroVector;
	if (Wanted.IsZero() && SupportVelocityCmS.Size() < 0.5)
	{
		SupportVelocityCmS = FVector2D::ZeroVector;
		return;
	}
	SupportVelocityCmS += (Wanted - SupportVelocityCmS) * FMath::Min(DeltaTime / 0.05, 1.0);
	if (!IsUpright())
	{
		return;
	}
	const FVector Shift(SupportVelocityCmS * DeltaTime, 0.0);
	for (FAssist& Assist : Assists)
	{
		Assist.Target.AddToTranslation(Shift);
		Assist.FollowFrom.AddToTranslation(Shift);
		Assist.FollowTo.AddToTranslation(Shift);
		SetAssistTarget(Assist);
	}
	for (FFootPlacement& Foot : Placement)
	{
		Foot.Point += Shift;
		Foot.GlideFrom += Shift;
		Foot.LastFrame.AddToTranslation(Shift);
		Foot.FromTransform.AddToTranslation(Shift);
	}
	if (Swing.bActive)
	{
		Swing.From += Shift;
		Swing.Target += Shift;
		Swing.Point += Shift;
		Swing.PassPoint += FVector2D(Shift);
		for (TArray<FVector2D>* Sole : { &Swing.TargetSole, &Swing.LiftSole, &LandedSole })
		{
			for (FVector2D& Each : *Sole)
			{
				Each += FVector2D(Shift);
			}
		}
	}
	for (FVector2D& Each : LastSupportPolygon)
	{
		Each += FVector2D(Shift);
	}
	RegroupPoint += FVector2D(Shift);
}

void USandboxBodyController::FollowSupport(double DeltaTime)
{
	CarrySupport(DeltaTime);
	// Only an upright body plants its feet; a falling or lying one has no assist to move.
	const bool bUpright = IsUpright();
	bool bSupportMoved = false;
	for (FAssist& Assist : Assists)
	{
		if (Assist.Slot != EAssistSlot::Feet || !Balance.bValid)
		{
			continue;
		}
		// In the air is untouched by the floor: a foot that carries less than the standing threshold but still
		// rests on the floor is not in the air, and takes weight again the moment the body sways onto it.
		const bool bStanding = Assist.Side >= 0 && Balance.FootForceN[Assist.Side] > 0.0;
		if (!bStanding)
		{
			Assist.AirS += DeltaTime;
		}
		else
		{
			// Landed after a real lift, not a frame without load: the foot is planted where it is now.
			if (!Assist.bStanding && Assist.AirS >= Tuning.Balance.ReplantAirS - KINDA_SMALL_NUMBER && bUpright)
			{
				const FBodyInstance* Foot = Mesh->GetBodyInstance(Assist.Bone);
				if (Foot && Foot->IsValidBodyInstance())
				{
					// Flat on the floor: a foot that came down on its toe was planted 4 cm up and tilted, and the
					// next step of it was aimed that high and never reached the floor.
					Assist.Target = FlatFootTransform(Assist);
					SetAssistTarget(Assist);
					bSupportMoved = true;
					++ReplantCount;
				}
			}
			Assist.AirS = 0.0;
		}
		Assist.bStanding = bStanding;
	}

	if (bSupportMoved)
	{
		FVector SupportLocation;
		FQuat SupportRotation;
		SupportFrame(SupportLocation, SupportRotation);
		for (FAssist& Assist : Assists)
		{
			if (Assist.Slot == EAssistSlot::Feet)
			{
				continue;
			}
			Assist.FollowFrom = Assist.Target;
			Assist.FollowTo = FTransform((SupportRotation * Assist.SupportRotation).GetNormalized(), SupportLocation + SupportRotation.RotateVector(Assist.SupportLocation));
			// In a stagger the anchors set the legs' frames only, and a step has just carried the body: the glide starts
			// over the body, not at the old support. (From the old support, the planted legs pulled the body back
			// toward it, 40% of the way, and a 30 N*s push caught by a step with 10 cm to spare rebounded out of the
			// other side of the new support and took a second step.) The legs' frames go over smoothly (UpdateStep).
			const FBodyInstance* Body = State == ESandboxBodyState::Stagger ? Mesh->GetBodyInstance(Assist.Bone) : nullptr;
			if (Body && Body->IsValidBodyInstance())
			{
				const FVector At = Body->GetUnrealWorldTransform().GetLocation() - FVector(Assist.AppliedLeanCm * Assist.LeanFactor, 0.0);
				Assist.FollowFrom.SetLocation(FVector(At.X, At.Y, Assist.FollowTo.GetLocation().Z));
			}
			Assist.FollowElapsedS = 0.0;
			Assist.FollowS = GlideSeconds(Assist.FollowFrom.GetLocation(), Assist.FollowTo.GetLocation());
		}
	}

	// The anchors on their way to a new support, eased in and out like a pose.
	for (FAssist& Assist : Assists)
	{
		if (Assist.FollowElapsedS < 0.0)
		{
			continue;
		}
		Assist.FollowElapsedS += DeltaTime;
		const double FollowS = Assist.FollowS;
		if (FollowS <= 0.0 || Assist.FollowElapsedS >= FollowS)
		{
			Assist.Target = Assist.FollowTo;
			Assist.FollowElapsedS = -1.0;
		}
		else
		{
			const double Weight = FMath::SmoothStep(0.0, 1.0, Assist.FollowElapsedS / FollowS);
			Assist.Target = FTransform(
				FQuat::Slerp(Assist.FollowFrom.GetRotation(), Assist.FollowTo.GetRotation(), Weight).GetNormalized(),
				FMath::Lerp(Assist.FollowFrom.GetLocation(), Assist.FollowTo.GetLocation(), Weight));
		}
		SetAssistTarget(Assist);
	}
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
			double PushNs = SupportImpulseNs[Foot->InstanceBodyIndex].Z;
			if (!ToeBoneName[Side].IsNone())
			{
				const FBodyInstance* Toes = Mesh->GetBodyInstance(ToeBoneName[Side]);
				PushNs += Toes && SupportImpulseNs.IsValidIndex(Toes->InstanceBodyIndex) ? SupportImpulseNs[Toes->InstanceBodyIndex].Z : 0.0;
			}
			OutView.FootForceN[Side] = PushNs / SupportStepS;
			if (OutView.FootForceN[Side] >= Tuning.Balance.FootContactMinN)
			{
				OutView.StandingFeet.Add(Feet[Side]);
			}
		}
	}

	const double SoleHeightCm = FSandboxBodyProbe::SupportPolygon(Mesh, OutView.StandingFeet, Tuning.Balance.SoleToleranceCm, OutView.SupportPolygon);
	OutView.SupportHeightCm = OutView.IsSupported() ? SoleHeightCm : LastSupportHeightCm;
	// The velocity that counts is the one over what carries the body (0 over a floor that is still).
	OutView.Velocity -= FVector(SupportVelocityCmS, 0.0);
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
		View.TargetRelative = Relative;
		View.StiffnessNmPerRad = Muscle.StiffnessNmPerRad;
		View.DampingNmsPerRad = Muscle.DampingNmsPerRad;
		View.MaxTorqueNm = Muscle.MaxTorqueNm;
		View.PoseVelocityShare = Muscle.PoseVelocityShare;
	}

	for (const FAssist& Assist : Assists)
	{
		const FSandboxAssistTuning& Slot = AssistTuning(Assist.Slot);
		FSandboxAssistView& View = OutView.Assists.AddDefaulted_GetRef();
		View.Bone = Assist.Bone;
		View.Target = AssistGoal(Assist);
		View.bActive = Slot.HorizontalNPerM > 0.0 || Slot.VerticalNPerM > 0.0 || Slot.AngularNmPerRad > 0.0;
	}
}

void USandboxBodyController::ApplyFastRiseCaps(const FAssist& Assist, FSandboxAssistTuning& Slot) const
{
	// N7 (getup.fast_rise): the rise's stronger pull on the pelvis, eased in and out.
	if (State == ESandboxBodyState::GetUp && Assist.Slot == EAssistSlot::Pelvis && GetUpRun.FastShare > 0.0)
	{
		const double Fast = FMath::SmoothStep(0.0, 1.0, GetUpRun.FastShare);
		Slot.MaxForceN = FMath::Lerp(Slot.MaxForceN, Tuning.GetUp.FastForceN, Fast);
		Slot.MaxTorqueNm = FMath::Lerp(Slot.MaxTorqueNm, Tuning.GetUp.FastTorqueNm, Fast);
	}
}

void USandboxBodyController::UpdateAssistLoad()
{
	// The solver does not report drive forces, so the load is rebuilt from the spring law: stiffness times
	// error minus damping times velocity, for the state at the start of this step. The solver integrates
	// the springs implicitly, so this is a close estimate rather than a reading.
	double ForceN = 0.0;
	double TorqueNm = 0.0;
	double HorizontalN[FSandboxAssistLoad::SlotCount] = { 0.0, 0.0, 0.0, 0.0 };
	double VerticalN[FSandboxAssistLoad::SlotCount] = { 0.0, 0.0, 0.0, 0.0 };
	for (const FAssist& Assist : Assists)
	{
		const FBodyInstance* Body = Mesh->GetBodyInstance(Assist.Bone);
		if (!Body || !Body->IsValidBodyInstance())
		{
			continue;
		}
		FSandboxAssistTuning Slot = AssistTuning(Assist.Slot);
		FTransform Goal = AssistGoal(Assist);
		FVector GoalVelocityMps = FVector::ZeroVector;
		if (IsSwingFoot(Assist))
		{
			Slot.HorizontalNPerM = Tuning.Balance.SwingFootNPerM;
			Slot.VerticalNPerM = Tuning.Balance.SwingFootNPerM;
			Goal.SetLocation(Swing.Point);
			GoalVelocityMps = Swing.VelocityCmS * 0.01;
		}

		// Centimetres to metres.
		const FVector TargetCom = Goal.TransformPositionNoScale(Body->GetMassSpaceLocal().GetLocation());
		const FVector ErrorM = (TargetCom - Body->GetCOMPosition()) * 0.01;
		const FVector VelocityMps = Body->GetUnrealWorldVelocity() * 0.01 - GoalVelocityMps;
		if (State == ESandboxBodyState::GetUp)
		{
			const double Ease = GetUpEase();
			Slot.HorizontalNPerM *= Ease;
			Slot.VerticalNPerM *= Ease;
			Slot.AngularNmPerRad *= Ease;
			ApplyFastRiseCaps(Assist, Slot);
		}
		{
			const double WorldShare = WorldShareOf(Assist.Slot);
			Slot.HorizontalNPerM *= WorldShare;
			Slot.VerticalNPerM *= WorldShare;
			Slot.AngularNmPerRad *= WorldShare;
		}
		const FVector Stiffness(Slot.HorizontalNPerM, Slot.HorizontalNPerM, Slot.VerticalNPerM);
		FVector CommandN = Stiffness * (ErrorM - VelocityMps * Slot.DampingS);
		if (Slot.MaxForceN > 0.0)
		{
			// The engine caps a drive's force axis by axis.
			CommandN = CommandN.BoundToCube(Slot.MaxForceN);
		}
		ForceN += CommandN.Size();
		const int32 SlotIndex = static_cast<int32>(Assist.Slot);
		HorizontalN[SlotIndex] += CommandN.Size2D();
		VerticalN[SlotIndex] += FMath::Abs(CommandN.Z);

		const FQuat Delta = (Assist.Target.GetRotation() * Body->GetUnrealWorldTransform().GetRotation().Inverse()).GetShortestArcWith(FQuat::Identity);
		const FVector ErrorRad = Delta.ToRotationVector();
		const double SlotTorqueNm = Slot.AngularNmPerRad * (ErrorRad - Body->GetUnrealWorldAngularVelocityInRadians() * Slot.DampingS).Size();
		TorqueNm += Slot.MaxTorqueNm > 0.0 ? FMath::Min(SlotTorqueNm, Slot.MaxTorqueNm) : SlotTorqueNm;
	}

	AssistLoad.ForceShare = WeightN > 0.0 ? ForceN / WeightN : 0.0;
	AssistLoad.TorqueShare = WeightN > 0.0 ? TorqueNm / (WeightN * StandingComHeightM) : 0.0;
	for (int32 SlotIndex = 0; SlotIndex < FSandboxAssistLoad::SlotCount; ++SlotIndex)
	{
		AssistLoad.HorizontalShare[SlotIndex] = WeightN > 0.0 ? HorizontalN[SlotIndex] / WeightN : 0.0;
		AssistLoad.VerticalShare[SlotIndex] = WeightN > 0.0 ? VerticalN[SlotIndex] / WeightN : 0.0;
	}
}

const TCHAR* FSandboxAssistLoad::SlotName(int32 Slot)
{
	switch (Slot)
	{
	case 1: return TEXT("chest");
	case 2: return TEXT("head");
	case 3: return TEXT("feet");
	default: return TEXT("pelvis");
	}
}
