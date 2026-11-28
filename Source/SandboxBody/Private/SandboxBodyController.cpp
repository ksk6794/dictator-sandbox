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
	const bool bDown = (State == ESandboxBodyState::Falling || State == ESandboxBodyState::Lying || State == ESandboxBodyState::GetUp) && !bHeldUp;
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
		// R3, item d (fall.curl): a fall with no time to catch is curled up while it falls; lying, it takes the fall's pose again.
		if (State == ESandboxBodyState::Falling && FallRun.bCurl && FallRun.bActive && Tuning.FallAim.Curl > 0.5)
		{
			Pose = &Tuning.Poses.FallCurl;
		}
		// R10, step 5 (behaviour.stay_down): lying, or going down from an attempt it gave up afraid, while it stays down: the head covered.
		if (bProtectPose)
		{
			Pose = ProtectPosePlace == ECoverPlace::Trunk ? &Tuning.Poses.ProtectTrunk : ProtectPosePlace == ECoverPlace::Groin ? &Tuning.Poses.ProtectGroin :
				ProtectPosePlace == ECoverPlace::Leg ? &Tuning.Poses.ProtectLeg : &Tuning.Poses.ProtectHead;
		}
	}
	// R2, step 9 (getup.roll): a body that rolls before its clip holds the roll's pose, written for its left side down, mirrored for
	// the right; it comes in as a pose does, not as a fall's.
	const bool bRoll = State == ESandboxBodyState::GetUp && GetUpRun.bRolling && !bHeldUp;
	if (bRoll)
	{
		FSandboxPose Roll = GetUpRun.bRollToFront ? Tuning.Poses.RollToFront : Tuning.Poses.RollToBack;
		// Once it has come in, on toward getup.roll_push times its angles (UpdateRoll).
		if (GetUpRun.bRollPushed)
		{
			for (FSandboxPoseAngles& Angles : Roll.Joints)
			{
				Angles.Swing1Deg *= Tuning.GetUp.RollPush;
				Angles.Swing2Deg *= Tuning.GetUp.RollPush;
				Angles.TwistDeg *= Tuning.GetUp.RollPush;
			}
		}
		Mirror = GetUpRun.bRollOnLeft ? Roll : FSandboxPoseTuning::Mirrored(Roll);
		Pose = &Mirror;
	}
	TArray<FSandboxPoseBone> Bones;
	FString Error;
	if (!PoseRig.Build(*Pose, Bones, Error))
	{
		UE_LOG(LogSandbox, Error, TEXT("Body controller keeps its pose: %s"), *Error);
		return;
	}
	const double RollBlendS = GetUpRun.bRollPushed ? FMath::Max(Tuning.GetUp.RollS - Tuning.GetUp.RollBlendS, 0.1) : Tuning.GetUp.RollBlendS;
	// The protective pose comes and goes over its own blend (PoseBlendOverrideS, R10): over the fall's 0.15 s it would be a jump.
	Animation->SetTargetPose(Bones, PoseBlendOverrideS >= 0.0 ? PoseBlendOverrideS : bRoll ? RollBlendS : bDown ? Tuning.Poses.FallBlendS : Tuning.Poses.BlendS);
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

void USandboxBodyController::SenseFall()
{
	// R3, step 1 (docs/recon/r3_step1_plan.md): read only, the bodies and one sweep of the scene; nothing acts on it yet. A body held
	// up off the floor is not falling to it.
	if (State != ESandboxBodyState::Falling || bHeldUp)
	{
		FallSense = FSandboxFallSense();
		return;
	}
	const FAssist* Pelvis = Assists.FindByPredicate([](const FAssist& Assist) { return Assist.Slot == EAssistSlot::Pelvis; });
	const FBodyInstance* PelvisBody = Pelvis ? Mesh->GetBodyInstance(Pelvis->Bone) : nullptr;
	const FBodyInstance* Chest = Mesh->GetBodyInstance(Tuning.Rig.Chest);
	if (!PelvisBody || !PelvisBody->IsValidBodyInstance() || !Chest || !Chest->IsValidBodyInstance() || !Balance.bValid)
	{
		FallSense = FSandboxFallSense();
		return;
	}
	// The trunk taken as balls of this radius about the pelvis's, the chest's and the head's centres of mass, for the floor; the chest's
	// for what is in its way. Whichever part comes first is the trunk's meeting (the trunk's touch counts any of them).
	constexpr double TrunkRadiusCm = 12.0;
	constexpr double LongestS = 10.0;
	// The way, weighed as ChooseFallWay weighs it: a lean of 30 degrees counts as much as 1 m/s.
	const FQuat Turned = PelvisBody->GetUnrealWorldTransform().GetRotation() * Pelvis->Target.GetRotation().Inverse();
	const FVector Up = Turned.RotateVector(FVector::UpVector);
	FVector2D Way = FVector2D(Up.X, Up.Y) / 0.5 + FVector2D(Balance.Velocity.X, Balance.Velocity.Y) * 0.01;
	FallSense.Way = Way.GetSafeNormal();
	// Each part of the trunk: the time its ball meets the floor under free fall, and what it meets on its way there first. The way is
	// its free-fall path to 2 cm above the floor (so that the floor itself is not found) as two chords, through the middle of the time
	// (one chord sagged below the arc by up to a quarter of the fall's height and met low furniture the arc clears), swept against the
	// static world and things that move or simulate (a table's top, a wall, furniture with mass, a hand), the character itself left
	// out; a hit at a share of a chord is taken as that share of its time. The first part's is the trunk's.
	FCollisionQueryParams Query(SCENE_QUERY_STAT(SandboxFallSense), false, GetOwner());
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(ECC_WorldStatic);
	Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
	Objects.AddObjectTypesToQuery(ECC_PhysicsBody);
	FallSense.TimeToImpactS = LongestS;
	FallSense.bOther = false;
	for (const FBodyInstance* Part : { PelvisBody, Chest, static_cast<const FBodyInstance*>(Mesh->GetBodyInstance(Tuning.Rig.Head)) })
	{
		if (!Part || !Part->IsValidBodyInstance())
		{
			continue;
		}
		const FVector AtCm = Part->GetCOMPosition();
		const FVector GoingCmS = Part->GetUnrealWorldVelocity();
		const double HeightCm = AtCm.Z - LastSupportHeightCm;
		const double FloorS = FMath::Min(FSandboxBodyProbe::TimeToFloorS(HeightCm, GoingCmS.Z, GravityCmS2, TrunkRadiusCm), LongestS);
		if (FloorS < FallSense.TimeToImpactS)
		{
			FallSense.TimeToImpactS = FloorS;
			FallSense.bOther = false;
		}
		const double ShortS = FMath::Min(FSandboxBodyProbe::TimeToFloorS(HeightCm, GoingCmS.Z, GravityCmS2, TrunkRadiusCm + 2.0), LongestS);
		const auto AtTime = [&](double S) { return AtCm + GoingCmS * S - FVector(0.0, 0.0, 0.5 * GravityCmS2 * S * S); };
		const double HalfS = 0.5 * ShortS;
		for (int32 Chord = 0; Chord < 2 && ShortS > 0.0 && GetWorld(); ++Chord)
		{
			const double FromS = Chord * HalfS;
			FHitResult Hit;
			if (GetWorld()->SweepSingleByObjectType(Hit, AtTime(FromS), AtTime(FromS + HalfS), FQuat::Identity, Objects, FCollisionShape::MakeSphere(TrunkRadiusCm), Query))
			{
				if (FromS + Hit.Time * HalfS < FallSense.TimeToImpactS)
				{
					FallSense.TimeToImpactS = FromS + Hit.Time * HalfS;
					FallSense.bOther = true;
				}
				break;
			}
		}
	}
	FallSense.bValid = true;
}

bool USandboxBodyController::FallBlendTargets(double OutWeights[4]) const
{
	if (!FallSense.bValid)
	{
		return false;
	}
	return FallWayWeights(FallSense.Way, OutWeights);
}

bool USandboxBodyController::FallWayWeights(const FVector2D& WayOverFloor, double OutWeights[4]) const
{
	// The body's own forward and left over the floor, as ChooseFallWay takes them.
	const FAssist* Pelvis = Assists.FindByPredicate([](const FAssist& Assist) { return Assist.Slot == EAssistSlot::Pelvis; });
	const FBodyInstance* Body = Pelvis ? Mesh->GetBodyInstance(Pelvis->Bone) : nullptr;
	if (!Body || !Body->IsValidBodyInstance())
	{
		return false;
	}
	const FQuat Turned = Body->GetUnrealWorldTransform().GetRotation() * Pelvis->Target.GetRotation().Inverse();
	FVector SupportLocation;
	FQuat SupportRotation;
	SupportFrame(SupportLocation, SupportRotation);
	FVector Forward = Turned.RotateVector(SupportRotation.RotateVector(FVector(0.0, 1.0, 0.0)));
	Forward.Z = 0.0;
	if (!Forward.Normalize())
	{
		return false;
	}
	const FVector Left(Forward.Y, -Forward.X, 0.0);
	const FVector Way(WayOverFloor.X, WayOverFloor.Y, 0.0);
	const double Ahead = FVector::DotProduct(Way, Forward);
	const double ToLeft = FVector::DotProduct(Way, Left);
	// The squares of the way's parts: a way straight ahead is all forward, one at 45 degrees half forward and half to a side.
	OutWeights[0] = Ahead > 0.0 ? Ahead * Ahead : 0.0;
	OutWeights[1] = Ahead < 0.0 ? Ahead * Ahead : 0.0;
	OutWeights[2] = ToLeft > 0.0 ? ToLeft * ToLeft : 0.0;
	OutWeights[3] = ToLeft < 0.0 ? ToLeft * ToLeft : 0.0;
	const double Sum = OutWeights[0] + OutWeights[1] + OutWeights[2] + OutWeights[3];
	if (Sum <= KINDA_SMALL_NUMBER)
	{
		return false;
	}
	for (int32 Index = 0; Index < 4; ++Index)
	{
		OutWeights[Index] /= Sum;
	}
	return true;
}

void USandboxBodyController::UpdateFallBlend(double DeltaTime)
{
	// R3, step 2 (docs/recon/r3_step2_plan.md), behind fall.blend: the layer the get-up's clip uses holds, while the body falls, a
	// blend of the sample's reaching poses by the way it goes, for the arms only, over the written fall pose (the sample's legs are
	// those of a body in the air: laid over a lying one they held a leg up). A get-up's clip takes the layer over; anything else
	// that ends the blend (the body lies, is held up, the switch goes to 0) lets it go over the fall pose's blend time, as the base
	// pose goes (a target dropped in one step puts energy into a falling body: UpdatePoseTurns).
	const FSandboxFallAimTuning& Fall = Tuning.FallAim;
	USandboxAnimInstance* Animation = Cast<USandboxAnimInstance>(Mesh->GetAnimInstance());
	if (!Animation || !ClipHold.Path.IsEmpty())
	{
		bFallBlending = false;
		return;
	}
	// Not for a body the run put down at the controller's first tick (body.laid_deg), as R3's run is not: laid over a laid body, the
	// reaching poses changed how getup_roll_right_front rolled.
	const bool bFalling = State == ESandboxBodyState::Falling && StateEnteredS > 0.0 && !bHeldUp && Fall.Blend > 0.5 && !bFallBlendFailed;
	if (!bFalling)
	{
		if (!bFallBlending)
		{
			return;
		}
		FallBlendIn = Tuning.Poses.FallBlendS > 0.0 ? FMath::Max(FallBlendIn - DeltaTime / Tuning.Poses.FallBlendS, 0.0) : 0.0;
		if (FallBlendIn <= 0.0)
		{
			Animation->SetLayer(TArray<FSandboxPoseBone>(), 0.0);
			bFallBlending = false;
		}
		else
		{
			Animation->SetLayer(FallBlendLast, Fall.BlendShare * FMath::SmoothStep(0.0, 1.0, FallBlendIn));
		}
		return;
	}
	if (!bFallBlendRead)
	{
		bFallBlendRead = true;
		const FString* Paths[4] = { &Fall.ClipReachForward, &Fall.ClipReachBack, &Fall.ClipReachLeft, &Fall.ClipReachRight };
		for (int32 Index = 0; Index < 4 && !bFallBlendFailed; ++Index)
		{
			FString Path;
			FString Error;
			const FSandboxClipPose* Clip = FindOrReadClip(*Paths[Index], Path, Error);
			if (!Clip)
			{
				UE_LOG(LogSandbox, Error, TEXT("Body controller: the fall's blend is off for this body: %s"), *Error);
				bFallBlendFailed = true;
				break;
			}
			Clip->Evaluate(FMath::Min(Fall.BlendClipS, Clip->GetLengthS()), FallBlendPoses[Index]);
			if (Index > 0 && FallBlendPoses[Index].Num() != FallBlendPoses[0].Num())
			{
				UE_LOG(LogSandbox, Error, TEXT("Body controller: the fall's blend is off for this body: '%s' has %d joints, not %d"), **Paths[Index], FallBlendPoses[Index].Num(), FallBlendPoses[0].Num());
				bFallBlendFailed = true;
			}
		}
		if (bFallBlendFailed)
		{
			return;
		}
		// The arms only: the collarbones, the upper and lower arms and the hands.
		for (int32 Index = 0; Index < 4; ++Index)
		{
			FallBlendPoses[Index].RemoveAll([this](const FSandboxPoseBone& Bone)
			{
				const FString Name = Mesh->GetBoneName(Bone.MeshBoneIndex).ToString();
				return !(Name.Contains(TEXT("clavicle")) || Name.Contains(TEXT("upperarm")) || Name.Contains(TEXT("lowerarm")) || Name.StartsWith(TEXT("hand_")));
			});
		}
		UE_LOG(LogSandbox, Display, TEXT("Body controller: the fall's blend reads the reaching poses at %.2f s, %d joints of the arms"), Fall.BlendClipS, FallBlendPoses[0].Num());
	}
	double Targets[4];
	const bool bWay = FallBlendTargets(Targets);
	if (!bFallBlending)
	{
		// A fall begins: the weights start where the way is (or straight back, without one), the layer from nothing.
		bFallBlending = true;
		FallBlendIn = 0.0;
		for (int32 Index = 0; Index < 4; ++Index)
		{
			FallBlendWeights[Index] = bWay ? Targets[Index] : (Index == 1 ? 1.0 : 0.0);
		}
		UE_LOG(LogSandbox, Display, TEXT("Body controller: the fall's blend at %.2f s: forward %.2f, back %.2f, left %.2f, right %.2f"),
			ElapsedS, FallBlendWeights[0], FallBlendWeights[1], FallBlendWeights[2], FallBlendWeights[3]);
	}
	else if (bWay)
	{
		const double Follow = Fall.BlendFollowS > 0.0 ? FMath::Min(DeltaTime / Fall.BlendFollowS, 1.0) : 1.0;
		for (int32 Index = 0; Index < 4; ++Index)
		{
			FallBlendWeights[Index] += (Targets[Index] - FallBlendWeights[Index]) * Follow;
		}
	}
	FallBlendIn = Tuning.Poses.FallBlendS > 0.0 ? FMath::Min(FallBlendIn + DeltaTime / Tuning.Poses.FallBlendS, 1.0) : 1.0;

	// Each joint's rotation: the weighted sum of the four on one side of the quaternions' double cover, normalised.
	TArray<FSandboxPoseBone> Bones = FallBlendPoses[0];
	for (int32 Bone = 0; Bone < Bones.Num(); ++Bone)
	{
		const FQuat First = FallBlendPoses[0][Bone].Rotation;
		FQuat Sum(0.0, 0.0, 0.0, 0.0);
		for (int32 Index = 0; Index < 4; ++Index)
		{
			FQuat Q = FallBlendPoses[Index][Bone].Rotation;
			if ((Q | First) < 0.0)
			{
				Q = -Q;
			}
			Sum = Sum + Q * FallBlendWeights[Index];
		}
		Bones[Bone].Rotation = Sum.GetNormalized();
	}
	Animation->SetLayer(Bones, Fall.BlendShare * FMath::SmoothStep(0.0, 1.0, FallBlendIn));
	FallBlendLast = MoveTemp(Bones);
}

void USandboxBodyController::UpdateFallRun(double DeltaTime)
{
	// R3 (docs/recon/r3_plan.md): every item behind a switch of its own (fall.arm_curve, catch, head, curl, sit, turn, palms); with all of
	// them 0 nothing here reaches the body: the run is begun only by one of them.
	const FSandboxFallAimTuning& Fall = Tuning.FallAim;
	for (int32 Side = 0; Side < 2; ++Side)
	{
		if (FallRun.bTouchFrame[Side] && FallRun.bActive && FallRun.TouchS[Side] < 0.0)
		{
			FallRun.TouchS[Side] = ElapsedS;
			// Item f, measured (read only): how the palm meets the floor, 0 flat on it.
			SetPalmAxes();
			const FBodyInstance* Hand = HandMeshIndex[Side] != INDEX_NONE ? Mesh->GetBodyInstance(Mesh->GetBoneName(HandMeshIndex[Side])) : nullptr;
			if (Hand && Hand->IsValidBodyInstance() && !PalmLocal[Side].IsNearlyZero())
			{
				const FVector Palm = Hand->GetUnrealWorldTransform().GetRotation().RotateVector(PalmLocal[Side]);
				FallRun.PalmToFloorDeg[Side] = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(Palm | -FVector::UpVector, -1.0, 1.0)));
			}
			UE_LOG(LogSandbox, Display, TEXT("Body controller: the %s hand meets the world at %.3f s, %.3f s into the fall, its palm %.0f degrees from the floor's"),
				Side == 0 ? TEXT("left") : TEXT("right"), ElapsedS, ElapsedS - FallRun.StartS, FallRun.PalmToFloorDeg[Side]);
		}
		FallRun.bTouchFrame[Side] = false;
	}
	// An arm being let go is carried with the chest until its weight has gone, unless another placement has taken its hand (its weight
	// rises again).
	if (FallRun.bReleasing[0] || FallRun.bReleasing[1])
	{
		static const FName ReleasedHands[2] = { FName(TEXT("hand_l")), FName(TEXT("hand_r")) };
		USandboxAnimInstance* Released = Cast<USandboxAnimInstance>(Mesh->GetAnimInstance());
		const FBodyInstance* Carrier = Mesh->GetBodyInstance(Tuning.Rig.Chest);
		for (int32 Side = 0; Side < 2; ++Side)
		{
			const int32 End = Mesh->GetSkeletalMeshAsset()->GetRefSkeleton().FindBoneIndex(ReleasedHands[Side]);
			const double WeightNow = Released && End != INDEX_NONE ? Released->GetLimbWeight(End) : 0.0;
			if (!FallRun.bReleasing[Side])
			{
				continue;
			}
			// Another placement has taken the hand when its goal is a weight again, or (taken at a weight held flat) the weight rose.
			if (WeightNow <= 0.0 || WeightNow > FallRun.ReleaseWeight[Side] + 1.0e-6 || Released->GetLimbGoalWeight(End) > 0.0 || !Carrier ||
				!Carrier->IsValidBodyInstance())
			{
				FallRun.bReleasing[Side] = false;
				continue;
			}
			FallRun.ReleaseWeight[Side] = WeightNow;
			FallRun.CatchPoint[Side] = Carrier->GetUnrealWorldTransform().TransformPositionNoScale(FallRun.CatchLocal[Side]);
			Released->MoveLimbTarget(End, FallRun.CatchPoint[Side], nullptr, nullptr, 0.0, 0.0);
		}
	}
	const bool bAny = Fall.ArmCurve > 0.5 || Fall.Catch > 0.5 || Fall.Head > 0.5 || Fall.Curl > 0.5 || Fall.Sit > 0.5 || Fall.Turn > 0.5;
	// A fall that begins at the controller's very first tick is a body the run put down (body.laid_deg) or spawned in the air: it did
	// not fall from anything it stood on, and nothing of R3 is done for it (with R3 on, the laid bodies of getup_roll_* reached out).
	const bool bFalling = bAny && State == ESandboxBodyState::Falling && StateEnteredS > 0.0 && !bHeldUp && !IsPosedFromOutside() && Balance.bValid;
	if (!bFalling)
	{
		if (FallRun.bActive)
		{
			EndFallRun();
		}
		// What is laid on the pose goes over the fall pose's blend, never in a step.
		const double OutStep = Tuning.Poses.FallBlendS > 0.0 ? DeltaTime / Tuning.Poses.FallBlendS : 1.0;
		FallRun.HeadShare = FMath::Max(FallRun.HeadShare - OutStep, 0.0);
		FallRun.SitShare = FMath::Max(FallRun.SitShare - OutStep, 0.0);
		FallRun.TurnShare = FMath::Max(FallRun.TurnShare - OutStep, 0.0);
		FallRun.PalmIn[0] = FMath::Max(FallRun.PalmIn[0] - OutStep, 0.0);
		FallRun.PalmIn[1] = FMath::Max(FallRun.PalmIn[1] - OutStep, 0.0);
		return;
	}
	if (!FallRun.bActive)
	{
		// The run begins at the fall's first sensed tick: the time to impact then decides the curl and the turn, once.
		if (!FallSense.bValid)
		{
			return;
		}
		FallRun.bActive = true;
		FallRun.StartS = ElapsedS;
		FallRun.StartTimeToImpactS = FallSense.TimeToImpactS;
		FallRun.Way = FallSense.Way;
		FallRun.bAimHeld = false;
		for (int32 Side = 0; Side < 2; ++Side)
		{
			FallRun.TouchS[Side] = -1.0;
			FallRun.bCatching[Side] = false;
			FallRun.bCatchDropped[Side] = false;
			FallRun.PalmToFloorDeg[Side] = -1.0;
		}
		// The palms' axes before the first catch turns a hand (item f) or a touch is measured.
		SetPalmAxes();
		++FallRun.Count;
		// The arm the guard holds as the fall begins stays the guard's (and goes with the guard's own release) for the whole fall, as N6's
		// reach left it; taken by the catch a tick later, its point and its elbow's pole jumped.
		FallRun.SkipSide = GuardSide;
		// A run that begins again while what the last one laid on the pose still shows (a body held up for a moment) takes the way's
		// weights and the turn's side as they are: begun anew, the head's turn and the twist jumped.
		const bool bShowing = FallRun.HeadShare > 0.0 || FallRun.TurnShare > 0.0;
		double Start[4];
		const bool bWeights = FallWayWeights(FallRun.Way, Start);
		for (int32 Index = 0; Index < 4 && !bShowing; ++Index)
		{
			FallRun.Weights[Index] = bWeights ? Start[Index] : (Index == 1 ? 1.0 : 0.0);
		}
		// No time to catch, or (fall.curl_from_ns) a hit hard enough that a person would not try: the hit that felled it (FellByNs, the
		// struck part's sum within the second before the fall).
		FallRun.bCurl = Fall.Curl > 0.5 && (FallSense.TimeToImpactS < Fall.CurlBelowS || (Fall.CurlFromNs > 0.0 && FellByNs >= Fall.CurlFromNs));
		// A fall backward or forward with the time turns toward the side its way leans to (none for a way straight back or ahead, or
		// leaning to a side by less than fall.turn_side_min of the way's weight).
		if (FallRun.TurnShare <= 0.0)
		{
			FallRun.TurnSide = 0;
			if (Fall.Turn > 0.5 && !FallRun.bCurl && FallSense.TimeToImpactS >= Fall.TurnMinS && FallRun.Weights[0] + FallRun.Weights[1] >= 0.5 &&
				FMath::Abs(FallRun.Weights[2] - FallRun.Weights[3]) >= Fall.TurnSideMin)
			{
				FallRun.TurnSide = FallRun.Weights[2] > FallRun.Weights[3] ? 1 : -1;
				FallRun.bTurnForward = FallRun.Weights[0] > FallRun.Weights[1];
			}
		}
		UE_LOG(LogSandbox, Display, TEXT("Body controller: R3's fall at %.3f s, %.3f s to the impact (forward %.2f, back %.2f, left %.2f, right %.2f)%s%s"), ElapsedS,
			FallSense.TimeToImpactS, FallRun.Weights[0], FallRun.Weights[1], FallRun.Weights[2], FallRun.Weights[3], FallRun.bCurl ? TEXT(", curls up") : TEXT(""),
			FallRun.TurnSide > 0 ? TEXT(", turns to its left") : FallRun.TurnSide < 0 ? TEXT(", turns to its right") : TEXT(""));
		// The fall's pose asked for again whenever the curl is on, so that a run begun anew that does not curl lets the last one's curl go
		// (the same pose asked again changes nothing: SetTargetPose).
		if (Fall.Curl > 0.5)
		{
			ApplyPose();
		}
	}
	else if (FallSense.bValid && !FallRun.bAimHeld)
	{
		// The way the catch is aimed by follows the sensed way until it is held.
		const double Follow = Fall.CatchFollowS > 0.0 ? 1.0 - FMath::Exp(-DeltaTime / Fall.CatchFollowS) : 1.0;
		const FVector2D Next = (FallRun.Way + (FallSense.Way - FallRun.Way) * Follow).GetSafeNormal();
		if (!Next.IsNearlyZero())
		{
			FallRun.Way = Next;
		}
	}
	const bool bTouched = FallRun.TouchS[0] >= 0.0 || FallRun.TouchS[1] >= 0.0 || TrunkTouchedS >= 0.0;
	FallRun.bAimHeld = FallRun.bAimHeld || bTouched || (FallSense.bValid && FallSense.TimeToImpactS < Fall.CatchHoldS);
	// A catch begins only while its placement can still come in before the impact (catch_blend_s; begun later, in a fall whose way
	// turned from backward to a side 0.26 s in, the arm reached under a body already landing and its target turned over).
	const bool bTimeToCatch = !FallRun.bAimHeld && FallSense.bValid && FallSense.TimeToImpactS >= Fall.CatchBlendS;
	double Goal[4];
	if (FallWayWeights(FallRun.Way, Goal))
	{
		const double Follow = Fall.HeadFollowS > 0.0 ? 1.0 - FMath::Exp(-DeltaTime / Fall.HeadFollowS) : 1.0;
		for (int32 Index = 0; Index < 4; ++Index)
		{
			FallRun.Weights[Index] += (Goal[Index] - FallRun.Weights[Index]) * Follow;
		}
	}
	// The head's turn, the sit and the turn come in over their own times (in, never in a step).
	const auto Ease = [DeltaTime](double& Share, bool bIn, double InS)
	{
		const double Step = InS > 0.0 ? DeltaTime / InS : 1.0;
		Share = FMath::Clamp(Share + (bIn ? Step : -Step), 0.0, 1.0);
	};
	Ease(FallRun.HeadShare, Fall.Head > 0.5, Tuning.Poses.FallBlendS);
	Ease(FallRun.SitShare, Fall.Sit > 0.5 && !FallRun.bCurl && FallRun.Weights[1] >= Fall.SitFrom, Fall.SitBlendS);
	Ease(FallRun.TurnShare, Fall.Turn > 0.5 && FallRun.TurnSide != 0, Fall.TurnBlendS);

	// Item b, the catch: each arm placed in the chest's frame toward where the floor will be. N6's reach (fall.aim) and the catch do not
	// share the hands: with fall.aim on there is no catch.
	static const FName Hands[2] = { FName(TEXT("hand_l")), FName(TEXT("hand_r")) };
	USandboxAnimInstance* Animation = Cast<USandboxAnimInstance>(Mesh->GetAnimInstance());
	const FBodyInstance* Chest = Mesh->GetBodyInstance(Tuning.Rig.Chest);
	const bool bCatch = Fall.Catch > 0.5 && Fall.Aim < 0.5 && !FallRun.bCurl && !bFallAiming && Animation && Chest && Chest->IsValidBodyInstance() &&
		!(Fall.CatchLetGoAtTrunk > 0.5 && TrunkTouchedS >= 0.0);
	// Whether a catch may begin (a fall backward has the written pose's arms unless catch_back) is not whether one goes on: a catch under
	// way goes on, or is let go (the way turned round, the catch switched off), never left on a point of the world. (A fall forward left
	// to the written pose's arms too was measured and taken out: docs/testing.md, "R3", the forward catch.)
	const bool bMayBegin = Fall.CatchBack > 0.5 || FallRun.Weights[1] < 0.5;
	bool bAnyCatching = false;
	if (!bCatch)
	{
		for (int32 Side = 0; Side < 2; ++Side)
		{
			if (FallRun.bCatching[Side])
			{
				// From where the target is now in the chest's frame (the point last put was a tick old), or that point without a chest.
				const bool bChest = Chest && Chest->IsValidBodyInstance();
				ReleaseCatch(Side, bChest ? Chest->GetUnrealWorldTransform().TransformPositionNoScale(FallRun.CatchLocal[Side]) : FallRun.CatchPoint[Side]);
				FallRun.bCatching[Side] = false;
				FallRun.bCatchDropped[Side] = true;
			}
		}
	}
	else
	{
		const FReferenceSkeleton& Skeleton = Mesh->GetSkeletalMeshAsset()->GetRefSkeleton();
		const FTransform ChestNow = Chest->GetUnrealWorldTransform();
		const FVector UpLocal = ChestRefRotation.UnrotateVector(FVector::UpVector);
		const FVector FloorLocal = FSandboxBodyProbe::CatchDirectionLocal(ChestNow.GetRotation(), UpLocal, FallRun.Way, Fall.CatchTiltDeg, Fall.CatchAhead);
		for (int32 Side = 0; Side < 2; ++Side)
		{
			const int32 End = Skeleton.FindBoneIndex(Hands[Side]);
			const int32 Middle = End != INDEX_NONE ? Skeleton.GetParentIndex(End) : INDEX_NONE;
			const int32 Root = Middle != INDEX_NONE ? Skeleton.GetParentIndex(Middle) : INDEX_NONE;
			const FBodyInstance* Upper = Root != INDEX_NONE ? Mesh->GetBodyInstance(Skeleton.GetBoneName(Root)) : nullptr;
			if (Root == INDEX_NONE || !Upper || !Upper->IsValidBodyInstance() || Side == FallRun.SkipSide)
			{
				continue;
			}
			if (ArmLengthCm[Side] <= 0.0)
			{
				const FVector Shoulder = FAnimationRuntime::GetComponentSpaceTransformRefPose(Skeleton, Root).GetLocation();
				const FVector Elbow = FAnimationRuntime::GetComponentSpaceTransformRefPose(Skeleton, Middle).GetLocation();
				const FVector Wrist = FAnimationRuntime::GetComponentSpaceTransformRefPose(Skeleton, End).GetLocation();
				ArmLengthCm[Side] = FVector::Dist(Shoulder, Elbow) + FVector::Dist(Elbow, Wrist);
			}
			// The far arm of a fall to a side: the floor's direction from it crosses the trunk, and it keeps the fall pose's arm. And a catch
			// begins only with the time for it: under catch_hold_s its target would be held at once (a body pressed down as it got up,
			// dropped head first: the arm swung to a target it could not reach and its elbow's target turned over).
			// Nor on a hand whose limb another placement still holds (the guard's, R2's held arm, the last catch's, easing out): taken over,
			// its point jumped.
			const FVector Outward = ChestRefRotation.UnrotateVector(FVector(Side == 0 ? 1.0 : -1.0, 0.0, 0.0));
			if (!FallRun.bCatching[Side] && (!bMayBegin || (FloorLocal | Outward) < -0.3 || !bTimeToCatch || FallRun.bCatchDropped[Side] ||
				Animation->GetLimbWeight(End) > 0.0))
			{
				continue;
			}
			// A fall that turns its way round (a body that sinks forward and then tips back): the arm aimed the first way is let go to the
			// fall's pose for the rest of this fall. (Followed round, its target swept past the shoulder and the elbow's turned over.)
			if (FallRun.bCatching[Side] && FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector2D::DotProduct(FallRun.Way, FallRun.CatchWay[Side]), -1.0, 1.0))) > Fall.CatchTurnDeg)
			{
				ReleaseCatch(Side, ChestNow.TransformPositionNoScale(FallRun.CatchLocal[Side]));
				FallRun.bCatching[Side] = false;
				FallRun.bCatchDropped[Side] = true;
				UE_LOG(LogSandbox, Display, TEXT("Body controller: the %s arm lets the catch go at %.3f s: the fall's way turned more than %.0f degrees"),
					Side == 0 ? TEXT("left") : TEXT("right"), ElapsedS, Fall.CatchTurnDeg);
				continue;
			}
			// Once a hand has touched, or the impact is near, its target stays where it is in the chest's frame.
			if (!FallRun.bCatching[Side] || (!FallRun.bAimHeld && FallRun.TouchS[Side] < 0.0))
			{
				const FVector ShoulderLocal = ChestNow.InverseTransformPositionNoScale(Upper->GetUnrealWorldTransform().GetLocation());
				FallRun.CatchLocal[Side] = ShoulderLocal + FloorLocal * (Fall.CatchReach * ArmLengthCm[Side]);
			}
			const FVector Point = ChestNow.TransformPositionNoScale(FallRun.CatchLocal[Side]);
			FallRun.CatchPoint[Side] = Point;
			if (!FallRun.bCatching[Side])
			{
				// Where the elbow goes (the mesh's space): back and out, as a reach's (for a hand reaching forward, its part across the arm is
				// out and down), unless that lies nearly along the line from the shoulder to the target; then out and down, or out and up,
				// whichever lies more across it; only the part across the line. (Back and out for a hand reaching back and out lay along the
				// arm: the plane of the bend was all but undefined and the upper arm's target turned over by up to 172 degrees in a frame in
				// 22 of 96 falls.)
				const bool bLeft = Side == 0;
				const FVector Reach = Mesh->GetComponentTransform().InverseTransformVectorNoScale(Point - Upper->GetUnrealWorldTransform().GetLocation()).GetSafeNormal();
				const FVector Ways[3] = { FVector(bLeft ? 0.4 : -0.4, -1.0, -0.3).GetSafeNormal(), FVector(bLeft ? 0.6 : -0.6, 0.0, -1.0).GetSafeNormal(),
					FVector(bLeft ? 1.0 : -1.0, 0.0, 0.3).GetSafeNormal() };
				constexpr double AlongArm = 0.8;
				FVector Across = Ways[0];
				if (FMath::Abs(Ways[0] | Reach) >= AlongArm)
				{
					Across = FMath::Abs(Ways[1] | Reach) <= FMath::Abs(Ways[2] | Reach) ? Ways[1] : Ways[2];
				}
				FVector Pole = (Across - Reach * (Across | Reach)).GetSafeNormal();
				Pole = Pole.IsNearlyZero() ? Across : Pole;
				// Blended by the joints' turns, as the guard's arm (FSandboxLimbTarget::bJointBlend), coming in and going: blended by where
				// the hand is, the hand passed near its shoulder and the elbow's target turned over, up to 175 degrees in a frame, in 3 of 174
				// scenarios (the head struck in fewer falls by hand that way: docs/testing.md, "R3").
				FallRun.bCatchJoint[Side] = Fall.CatchJointBlend > 0.5;
				// The elbow's anatomical hinge for a joint-blended arm only: blended by where the hand is, at a weight near 0 the solution
				// has to give back the pose, and its hinge before and after would differ by the pose's own bend (third reading).
				Animation->SetLimbBend(End, Pole, FVector::XAxisVector, /*bEaseTwice*/ FallRun.bCatchJoint[Side], /*bAnatomicHinge*/ FallRun.bCatchJoint[Side]);
				FallRun.CatchPole[Side] = Pole;
				++FallRun.ArmsCaught;
				FSandboxJointLimits ShoulderLimits;
				FSandboxJointLimits ElbowLimits;
				if (FSandboxPoseRig::ReadJointLimits(Mesh, Skeleton.GetBoneName(Root), ShoulderLimits) && FSandboxPoseRig::ReadJointLimits(Mesh, Skeleton.GetBoneName(Middle), ElbowLimits))
				{
					Animation->SetLimbRanges(End, ShoulderLimits, ElbowLimits);
				}
				Animation->SetLimbTarget(End, Point, 1.0, Fall.CatchBlendS);
				FallRun.bCatching[Side] = true;
				FallRun.CatchWay[Side] = FallRun.Way;
				UE_LOG(LogSandbox, Display, TEXT("Body controller: the %s arm catches at %.3f s (the floor at %.2f, %.2f, %.2f in the chest's frame)"),
					bLeft ? TEXT("left") : TEXT("right"), ElapsedS, FloorLocal.X, FloorLocal.Y, FloorLocal.Z);
			}
			else
			{
				Animation->MoveLimbTarget(End, Point, nullptr, nullptr, 0.0, 0.0);
			}
			bAnyCatching = true;
		}
	}
	// C26, measured (read only): what reached the body while an arm reached for the floor (caught, and neither a hand nor the trunk
	// had met it yet); kept for the whole Falling state (EnterState), so that a run begun again after a hold does not forget it.
	if (bAnyCatching && !bTouched)
	{
		if (LastHitS >= 0.0 && LastHitS != FallRun.LastHitSeenS && LastHitS >= FallRun.StartS)
		{
			++FallRun.HitsWhileCatching;
		}
		FallRun.bHeldWhileCatching = FallRun.bHeldWhileCatching || Holds.Num() > 0;
	}
	FallRun.LastHitSeenS = LastHitS;
	// Item f: the palm's turn comes in with each catching arm, and goes once it does not catch.
	for (int32 Side = 0; Side < 2; ++Side)
	{
		const bool bPalm = Fall.Palms > 0.5 && FallRun.bCatching[Side];
		const double Step = bPalm ? (Fall.CatchBlendS > 0.0 ? DeltaTime / Fall.CatchBlendS : 1.0) : (Tuning.Poses.FallBlendS > 0.0 ? -DeltaTime / Tuning.Poses.FallBlendS : -1.0);
		FallRun.PalmIn[Side] = FMath::Clamp(FallRun.PalmIn[Side] + Step, 0.0, 1.0);
	}
	const double CapScale = bAnyCatching ? Fall.CatchMaxTorqueScale : 1.0;
	if (CapScale != FallArmCapScale)
	{
		FallArmCapScale = CapScale;
		PushTuningToControls();
	}
}

void USandboxBodyController::ReleaseCatch(int32 Side, const FVector& Point)
{
	static const FName Hands[2] = { FName(TEXT("hand_l")), FName(TEXT("hand_r")) };
	USandboxAnimInstance* Animation = Cast<USandboxAnimInstance>(Mesh->GetAnimInstance());
	const int32 End = Mesh->GetSkeletalMeshAsset()->GetRefSkeleton().FindBoneIndex(Hands[Side]);
	if (!Animation || End == INDEX_NONE)
	{
		return;
	}
	// Let go by the joints' turns, as the guard's arm is (FSandboxLimbTarget::bJointBlend): blended by where the hand is, the hand passed
	// near its shoulder on its way back to the fall's pose and the elbow's target turned over, 170 to 178 degrees in a frame. The blend
	// and the hinge stay as the catch began (a change of fall.catch_joint_blend in the middle of one does not change them half way through
	// the weight), and the weight goes down from where it is (1 once the catch has come in, less when it is let go sooner).
	Animation->SetLimbBend(End, FallRun.CatchPole[Side], FVector::XAxisVector, /*bEaseTwice*/ FallRun.bCatchJoint[Side], /*bAnatomicHinge*/ FallRun.bCatchJoint[Side]);
	Animation->SetLimbTarget(End, Point, 0.0, Tuning.Poses.FallBlendS);
	FallRun.bReleasing[Side] = true;
	FallRun.ReleaseWeight[Side] = Animation->GetLimbWeight(End);
}

void USandboxBodyController::SetPalmAxes()
{
	if (bPalmAxesSet)
	{
		return;
	}
	bPalmAxesSet = true;
	// As the finger rig takes them: along the hand toward the middle finger, across the knuckles toward the thumb's side, the back of the
	// hand from the two (on the right the line across is the other way round), the palm the other way.
	const FReferenceSkeleton& Skeleton = Mesh->GetSkeletalMeshAsset()->GetRefSkeleton();
	for (int32 Side = 0; Side < 2; ++Side)
	{
		const TCHAR* Suffix = Side == 0 ? TEXT("_l") : TEXT("_r");
		const int32 Hand = Skeleton.FindBoneIndex(FName(*FString::Printf(TEXT("hand%s"), Suffix)));
		const int32 Middle = Skeleton.FindBoneIndex(FName(*FString::Printf(TEXT("middle_01%s"), Suffix)));
		const int32 Index = Skeleton.FindBoneIndex(FName(*FString::Printf(TEXT("index_01%s"), Suffix)));
		const int32 Pinky = Skeleton.FindBoneIndex(FName(*FString::Printf(TEXT("pinky_01%s"), Suffix)));
		if (Hand == INDEX_NONE || Middle == INDEX_NONE || Index == INDEX_NONE || Pinky == INDEX_NONE)
		{
			continue;
		}
		const FTransform HandRef = FAnimationRuntime::GetComponentSpaceTransformRefPose(Skeleton, Hand);
		const FVector Along = (FAnimationRuntime::GetComponentSpaceTransformRefPose(Skeleton, Middle).GetLocation() - HandRef.GetLocation()).GetSafeNormal();
		const FVector Across = (FAnimationRuntime::GetComponentSpaceTransformRefPose(Skeleton, Index).GetLocation() -
			FAnimationRuntime::GetComponentSpaceTransformRefPose(Skeleton, Pinky).GetLocation()).GetSafeNormal() * (Side == 0 ? 1.0 : -1.0);
		const FVector Back = FVector::CrossProduct(Along, Across).GetSafeNormal();
		PalmLocal[Side] = HandRef.GetRotation().UnrotateVector(-Back);
		HandAlongLocal[Side] = HandRef.GetRotation().UnrotateVector(Along);
		HandMeshIndex[Side] = Hand;
	}
}

void USandboxBodyController::EndFallRun()
{
	static const FName Hands[2] = { FName(TEXT("hand_l")), FName(TEXT("hand_r")) };
	USandboxAnimInstance* Animation = Cast<USandboxAnimInstance>(Mesh->GetAnimInstance());
	const FBodyInstance* Chest = Mesh->GetBodyInstance(Tuning.Rig.Chest);
	const FReferenceSkeleton& Skeleton = Mesh->GetSkeletalMeshAsset()->GetRefSkeleton();
	for (int32 Side = 0; Side < 2; ++Side)
	{
		const int32 End = Skeleton.FindBoneIndex(Hands[Side]);
		if (FallRun.bCatching[Side] && Animation && End != INDEX_NONE)
		{
			// Let go over the fall pose's blend, from where the target is now (where it was last put, without a chest to carry it). (Let go
			// more slowly as the body lies, the slow tips after blows of 160 N*s rested later still: docs/testing.md, "R3".)
			const FVector Point = Chest && Chest->IsValidBodyInstance() ? Chest->GetUnrealWorldTransform().TransformPositionNoScale(FallRun.CatchLocal[Side]) : FallRun.CatchPoint[Side];
			ReleaseCatch(Side, Point);
		}
		FallRun.bCatching[Side] = false;
	}
	FallRun.bActive = false;
	// A run that ends while the body still falls (held up, posed from outside, the switches turned off) lets its curl go: the fall's pose.
	if (FallRun.bCurl && State == ESandboxBodyState::Falling)
	{
		ApplyPose();
	}
	if (FallArmCapScale != 1.0)
	{
		FallArmCapScale = 1.0;
		PushTuningToControls();
	}
	UE_LOG(LogSandbox, Display, TEXT("Body controller: R3's fall ends at %.3f s (hands at %.3f and %.3f s)"), ElapsedS, FallRun.TouchS[0], FallRun.TouchS[1]);
}

void USandboxBodyController::AddFallTurns(TArray<FSandboxPoseTurn>& Turns) const
{
	const FSandboxFallAimTuning& Fall = Tuning.FallAim;
	const auto Merge = [&Turns](const FSandboxPoseTurn& Each)
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
	};
	// Item c: the head's own joint turned away from the floor by the way, in the component's frame made one of the head's own (as the gaze).
	if (FallRun.HeadShare > 0.0 && NeckMeshIndex != INDEX_NONE)
	{
		const double Share = FMath::SmoothStep(0.0, 1.0, FallRun.HeadShare);
		// Pitch up is the head back (as the gaze's); a roll about the front tips the head's top toward the body's left.
		const double PitchDeg = (Fall.HeadForwardDeg * FallRun.Weights[0] - Fall.HeadBackDeg * FallRun.Weights[1]) * Share;
		const double RollDeg = Fall.HeadSideDeg * (FallRun.Weights[3] - FallRun.Weights[2]) * Share;
		const FQuat InComponent = (FQuat(FVector::XAxisVector, FMath::DegreesToRadians(PitchDeg)) * FQuat(FVector::YAxisVector, FMath::DegreesToRadians(RollDeg))).GetNormalized();
		Merge(FSandboxPoseTurn{ NeckMeshIndex, (NeckRefRotation.Inverse() * InComponent * NeckRefRotation).GetNormalized() });
	}
	// Item f: each catching hand bent back at the wrist, the palm turned from where it faces toward where the fingers point, so that
	// with the arm pointing at the floor the palm faces it (fall.palm_share of a right angle).
	for (int32 Side = 0; Side < 2; ++Side)
	{
		if (FallRun.PalmIn[Side] > 0.0 && HandMeshIndex[Side] != INDEX_NONE)
		{
			const FVector Axis = FVector::CrossProduct(PalmLocal[Side], HandAlongLocal[Side]).GetSafeNormal();
			if (!Axis.IsNearlyZero())
			{
				const double Angle = UE_DOUBLE_HALF_PI * Fall.PalmShare * FMath::SmoothStep(0.0, 1.0, FallRun.PalmIn[Side]);
				Merge(FSandboxPoseTurn{ HandMeshIndex[Side], FQuat(Axis, Angle) });
			}
		}
	}
	// Item e: sitting down going backward, and the turn toward a side. (BuildTurns empties what it is handed.)
	TArray<FSandboxPoseTurn> Drawn;
	if (FallRun.SitShare > 0.0)
	{
		PoseRig.BuildTurns(Tuning.Poses.FallSit, FMath::SmoothStep(0.0, 1.0, FallRun.SitShare), Drawn);
		for (const FSandboxPoseTurn& Each : Drawn)
		{
			Merge(Each);
		}
	}
	if (FallRun.TurnShare > 0.0 && FallRun.TurnSide != 0)
	{
		// To land on the side the way leans to, the chest twists that side's shoulder along the way: back going backward, forward going
		// forward, the opposite twists (poses.fall_turn is written for a fall backward that leans to the left).
		const FSandboxPose Mirror = FSandboxPoseTuning::Mirrored(Tuning.Poses.FallTurn);
		const int32 Twist = FallRun.bTurnForward ? -FallRun.TurnSide : FallRun.TurnSide;
		PoseRig.BuildTurns(Twist > 0 ? Tuning.Poses.FallTurn : Mirror, FMath::SmoothStep(0.0, 1.0, FallRun.TurnShare), Drawn);
		for (const FSandboxPoseTurn& Each : Drawn)
		{
			Merge(Each);
		}
	}
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
	// R3 (items c and e): the head kept off the floor, the sit and the turn, as much of each as is on now.
	if (FallRun.HeadShare > 0.0 || FallRun.SitShare > 0.0 || FallRun.TurnShare > 0.0 || FallRun.PalmIn[0] > 0.0 || FallRun.PalmIn[1] > 0.0)
	{
		AddFallTurns(Turns);
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
		GiveUpGetUp(ESandboxGiveUp::PosedFromOutside, TEXT("a pose was asked for from outside"));
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
	// "/Game/Folder/Clip" is the object "/Game/Folder/Clip.Clip". A path that ends in the mirror's mark ("|mirror", R2's step 8) is the
	// clip read as its mirror image, kept by the path with the mark.
	FString Path = ClipPath;
	const bool bMirror = Path.EndsWith(FSandboxMotionLibrary::MirrorMark);
	if (bMirror)
	{
		Path.LeftChopInline(FCString::Strlen(FSandboxMotionLibrary::MirrorMark));
	}
	int32 Slash = INDEX_NONE;
	if (!Path.Contains(TEXT(".")) && Path.FindLastChar(TEXT('/'), Slash))
	{
		Path += TEXT(".") + Path.Mid(Slash + 1);
	}
	const FString Key = bMirror ? Path + FSandboxMotionLibrary::MirrorMark : Path;
	if (!Clips.Contains(Key))
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
		if (!Clip.Load(Sequence, Mesh, Tuning.Rig.Pelvis, Joints, OutError, bMirror))
		{
			return nullptr;
		}
		UE_LOG(LogSandbox, Display, TEXT("Body controller: clip '%s' read, %d keys over %.2f s"), *Key, Clip.GetKeyCount(), Clip.GetLengthS());
		Clips.Add(Key, MoveTemp(Clip));
	}
	OutPath = Key;
	return Clips.Find(Key);
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
	ClipHold.CrossS = 0.0;
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
	ClipHold.Share = FMath::Clamp(ClipHold.Share + (ClipHold.bReleased ? -Step : Step), 0.0, 1.0);
	ClipHold.CrossShare = FMath::Min(ClipHold.CrossShare + (ClipHold.CrossS > 0.0 ? DeltaTime / ClipHold.CrossS : Step), 1.0);
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
	// R2, step 9 (getup.roll, the owner's decision 50): a body on its side rolls onto its back or its front by its own muscles first,
	// and the clip begins from there. The matcher then leaves out the get-ups that begin on a side.
	if (Tuning.GetUp.Roll > 0.5)
	{
		double SideDeg = 0.0;
		bool bOnLeft = false;
		bool bFacingDown = false;
		if (ReadLyingSide(SideDeg, bOnLeft, bFacingDown) && SideDeg >= Tuning.GetUp.RollSideDeg)
		{
			return StartRoll(SideDeg, bOnLeft, bFacingDown, OutError);
		}
	}
	return StartClip(true, OutError);
}

bool USandboxBodyController::ReadLyingSide(double& OutSideDeg, bool& bOutOnLeft, bool& bOutFacingDown) const
{
	static const FName Hips[2] = { TEXT("thigh_l"), TEXT("thigh_r") };
	const FBodyInstance* Pelvis = Mesh->GetBodyInstance(Tuning.Rig.Pelvis);
	const FBodyInstance* Chest = Mesh->GetBodyInstance(Tuning.Rig.Chest);
	if (Mesh->GetBoneIndex(Hips[0]) == INDEX_NONE || Mesh->GetBoneIndex(Hips[1]) == INDEX_NONE || !Pelvis || !Pelvis->IsValidBodyInstance() ||
		!Chest || !Chest->IsValidBodyInstance())
	{
		return false;
	}
	// The hip joints are the thighs' bone origins, as the mesh has them from the simulated bodies.
	return FSandboxBodyProbe::LyingSide(Mesh->GetBoneLocation(Hips[0]), Mesh->GetBoneLocation(Hips[1]), Pelvis->GetUnrealWorldTransform().GetLocation(),
		Chest->GetUnrealWorldTransform().GetLocation(), OutSideDeg, bOutOnLeft, bOutFacingDown);
}

void USandboxBodyController::BeginAttemptView()
{
	if (GetUpView.FirstStartedS < 0.0)
	{
		GetUpView.FirstStartedS = ElapsedS;
	}
	++GetUpView.Attempts;
	GetUpView.StartedS = ElapsedS;
	GetUpView.DurationS = -1.0;
	GetUpView.StallS = 0.0;
	GetUpView.PausedS = 0.0;
	GetUpView.PausedEaseS = 0.0;
	GetUpView.PausedBehindS = 0.0;
	GetUpView.PausedEndS = 0.0;
	GetUpView.BehindHeldS = 0.0;
	GetUpView.PlankS = 0.0;
	GetUpView.PlankMaxCm = 0.0;
	GetUpView.PlankAfterS = 0.0;
	GetUpView.RiseFeetCm = -1.0;
	GetUpView.RiseTrunkDeg = -1.0;
	GetUpView.RiseClipTrunkDeg = -1.0;
	GetUpView.RiseClipFeetCm = -1.0;
	GetUpView.LayS = State == ESandboxBodyState::Lying ? ElapsedS - StateEnteredS : 0.0;
}

bool USandboxBodyController::StartRoll(double SideDeg, bool bOnLeft, bool bFacingDown, FString& OutError)
{
	if (!Mesh->GetBodyInstance(Tuning.Rig.Pelvis))
	{
		OutError = TEXT("the pelvis has no body");
		return false;
	}
	// Which way: by where the front looks, or as getup.roll_way says; after an attempt with a roll that was given up, the other way
	// (the same roll from the same place fails the same way).
	const int32 Way = FMath::RoundToInt32(Tuning.GetUp.RollWay);
	bool bToFront = Way == 1 ? true : Way == 2 ? false : bFacingDown;
	if (GivenUpRollWay != INDEX_NONE)
	{
		bToFront = GivenUpRollWay == 0;
	}
	GetUpRun = FGetUpRun();
	GetUpRun.bRolling = true;
	GetUpRun.bRollOnLeft = bOnLeft;
	GetUpRun.bRollToFront = bToFront;
	FSandboxGetUpRoll Roll;
	Roll.AtS = ElapsedS;
	Roll.bOnLeft = bOnLeft;
	Roll.bToFront = bToFront;
	Roll.FromDeg = SideDeg;
	Roll.bFacingDownFrom = bFacingDown;
	GetUpView.Rolls.Add(Roll);
	HoldPace = 1.0;
	HitFree = 1.0;
	for (double& Kept : CarryBodyFree)
	{
		Kept = 1.0;
	}
	BeginAttemptView();
	GetUpView.Clip.Reset();
	// The pose is the roll's (ApplyPose, through the state's tuning), and the muscles come up from nothing as for any get-up.
	EnterState(ESandboxBodyState::GetUp, FString::Printf(TEXT("lay still for %.2f s; rolls onto its %s from its %s side (%.0f deg)"), GetUpRestS(),
		bToFront ? TEXT("front") : TEXT("back"), bOnLeft ? TEXT("left") : TEXT("right"), SideDeg));
	return true;
}

void USandboxBodyController::UpdateRoll(double DeltaTime)
{
	if (bHeldUp)
	{
		GiveUpGetUp(ESandboxGiveUp::HeldUp, TEXT("the body is held up"));
		return;
	}
	// The muscles come up from nothing, as in a clip's ease (UpdateGetUp).
	if (!GetUpRun.bEased)
	{
		PushTuningToControls();
		const bool bHitOver = FlinchStartedS < StateEnteredS || ElapsedS - FlinchStartedS >= Tuning.Reactions.FlinchRecoveryS;
		GetUpRun.bEased = GetUpEase() >= 1.0 && bHitOver;
	}
	// Nothing of the world pulls on a body that rolls: a hit while it rolls has no authority to take, and is not judged again by the clip.
	CarryYieldSeenS = FMath::Max(CarryYieldSeenS, FlinchStartedS);
	// How long and how hard a hand held the body while it rolled (read only), as while it follows a clip.
	if (Holds.Num() > 0)
	{
		GetUpView.HeldS += DeltaTime;
		for (const FSandboxBodyHold& Hold : Holds)
		{
			GetUpView.HoldMaxN = FMath::Max(GetUpView.HoldMaxN, Hold.ForceN);
		}
	}
	GetUpView.PressedMaxN = FMath::Max(GetUpView.PressedMaxN, SustainedN.Size());
	double SideDeg = 90.0;
	bool bOnLeft = false;
	bool bFacingDown = false;
	const double TookS = ElapsedS - StateEnteredS;
	// Hips that give no line (a degenerate pose) are read as still on the side: the roll then ends by roll_s, never not at all.
	const bool bRead = ReadLyingSide(SideDeg, bOnLeft, bFacingDown);
	FSandboxGetUpRoll& Roll = GetUpView.Rolls.Last();
	if (bRead)
	{
		Roll.ToDeg = SideDeg;
		Roll.bFacingDownTo = bFacingDown;
	}
	else
	{
		SideDeg = 90.0;
	}
	// The pose has come in: on toward getup.roll_push times it until roll_s, so that the target never stands still (C19).
	if (!GetUpRun.bRollPushed && TookS >= Tuning.GetUp.RollBlendS && Tuning.GetUp.RollPush != 1.0)
	{
		GetUpRun.bRollPushed = true;
		ApplyPose();
	}
	const bool bDone = SideDeg <= Tuning.GetUp.RollDoneDeg;
	if (!bDone && TookS < Tuning.GetUp.RollS)
	{
		return;
	}
	Roll.DurationS = TookS;
	Roll.bDone = bDone;
	Roll.Ended = bDone ? TEXT("rolled") : TEXT("roll_s");
	GetUpRun.bRolling = false;
	GetUpRun.bRolled = true;
	UE_LOG(LogSandbox, Display, TEXT("Body controller: the roll is over at %.2f s after %.2f s: the hips %.0f deg from level (%s)"), ElapsedS, TookS, SideDeg,
		bDone ? TEXT("rolled") : TEXT("its time ran out"));
	// Under the clip the body keeps the pose it lay in, as without a roll.
	ApplyPose();
	FString Error;
	if (!StartClip(false, Error))
	{
		bGetUpBroken = true;
		UE_LOG(LogSandbox, Error, TEXT("Body controller: the body cannot get up and stays down: %s"), *Error);
		GiveUpGetUp(ESandboxGiveUp::NoClip, FString::Printf(TEXT("no clip after the roll: %s"), *Error));
	}
}

double USandboxBodyController::CarryEase() const
{
	if (!GetUpRun.bRolled)
	{
		return GetUpEase();
	}
	if (State != ESandboxBodyState::GetUp || Tuning.GetUp.EaseS <= 0.0)
	{
		return 1.0;
	}
	return FMath::SmoothStep(0.0, 1.0, (ElapsedS - GetUpRun.ClipStartedS) / Tuning.GetUp.EaseS);
}

bool USandboxBodyController::StartClip(bool bNewAttempt, FString& OutError)
{
	FAssist* Pelvis = Assists.FindByPredicate([](const FAssist& Assist) { return Assist.Slot == EAssistSlot::Pelvis; });
	const FBodyInstance* Body = Pelvis ? Mesh->GetBodyInstance(Pelvis->Bone) : nullptr;
	if (!Body || !Body->IsValidBodyInstance())
	{
		OutError = TEXT("the pelvis has no body");
		return false;
	}
	const FSandboxClipPose* Best = nullptr;
	FString BestPath;
	// The time in the clip the attempt begins from, and the clip's root in the world.
	double StartTimeS = 0.0;
	FTransform Frame;
	FSandboxGetUpStart Start;
	FString How;
	// R2, step 10 (getup.fit_surface): the clip is laid on what the pelvis lies on, not on the floor last stood on.
	bChoosingFloorSet = false;
	if (Tuning.GetUp.FitSurface > 0.5)
	{
		bChoosingFloorSet = SurfaceUnderPelvis(ChoosingFloorZ);
	}
	// R2, step 8: the motion library's matcher chooses the clip (a get-up or its mirror image), the time and where it is laid (since
	// R2's rest the only choice: the rule by the pelvis's lean and getup.clip_* went).
	if (!ChooseByMatch(BestPath, StartTimeS, Frame, Start, OutError))
	{
		bChoosingFloorSet = false;
		return false;
	}
	Best = Clips.Find(BestPath);
	if (!Best)
	{
		bChoosingFloorSet = false;
		OutError = FString::Printf(TEXT("the matcher chose '%s', which is not read"), *BestPath);
		return false;
	}
	// The variant's name says whether it is a mirror image (a key's mark would be taken for an extension by GetBaseFilename).
	const int32 Chosen = MotionLibrary.FindVariant(BestPath);
	How = FString::Printf(TEXT("by '%s' from %.2f s (cost %.1f cm: places %.1f cm, lean %.1f deg; next '%s' %.1f cm)"),
		Chosen != INDEX_NONE ? *MotionLibrary.GetVariants()[Chosen].Name : *BestPath, StartTimeS, Start.CostCm, Start.PlacesCm, Start.UpDeg,
		*Start.Next, Start.NextCostCm);
	const FTransform First = Best->PelvisAt(StartTimeS);
	// How far the clip as laid stands from the body (read only), before anything of the attempt moves.
	Start.AtS = ElapsedS;
	Start.Clip = BestPath;
	Start.bMirrored = BestPath.EndsWith(FSandboxMotionLibrary::MirrorMark);
	Start.TimeS = StartTimeS;
	MeasureStart(*Best, StartTimeS, Frame, Start);
	const bool bFloorSet = bChoosingFloorSet;
	const double FloorZ = ChoosingFloorZ;
	bChoosingFloorSet = false;
	Start.FloorOverCm = bFloorSet ? FloorZ - LastSupportHeightCm : 0.0;

	if (!HoldClip(BestPath, StartTimeS, 0.0, OutError))
	{
		return false;
	}
	GetUpView.Starts.Add(Start);
	// At once and whole: it is the strength of the muscles that comes up from nothing, not the pose that comes in.
	ClipHold.Share = 1.0;
	ClipHold.CrossShare = 1.0;
	// After a roll the attempt goes on: what is known of its roll stays.
	const FGetUpRun Before = GetUpRun;
	GetUpRun = FGetUpRun();
	if (!bNewAttempt)
	{
		// Not bEased: the clip's own ease (the assist's, CarryEase) begins now, and is pushed until it is up.
		GetUpRun.bRolled = true;
		GetUpRun.bRollOnLeft = Before.bRollOnLeft;
		GetUpRun.bRollToFront = Before.bRollToFront;
	}
	GetUpRun.ClipStartedS = ElapsedS;
	GetUpRun.StartIndex = GetUpView.Starts.Num() - 1;
	GetUpRun.bFloorSet = bFloorSet;
	GetUpRun.FloorZ = FloorZ;
	GetUpRun.Variant = MotionLibrary.IsBuilt() ? MotionLibrary.FindVariant(BestPath) : INDEX_NONE;
	GetUpRun.Path = BestPath;
	GetUpRun.Frame = Frame;
	// Every clip is led (the clip-following path went with R2's rest); decision 17 still counts each attempt as led or not.
	GetUpRun.bCarryAsked = true;
	// Decision 22 (getup.start_blend_s): the start goes from where the body is. The clip layer crosses from the body's own pose (each
	// bone's local rotation as the simulation has it now) to the clip's first pose over that time, and the carry's targets from where
	// each body lies (UpdateCarry). Read only of the body.
	if (Tuning.GetUp.StartBlendS > 0.0)
	{
		TArray<FSandboxPoseBone> FromBones;
		Best->Evaluate(0.0, FromBones);
		const TArray<FTransform>& Space = Mesh->GetComponentSpaceTransforms();
		const FReferenceSkeleton& Skeleton = Mesh->GetSkeletalMeshAsset()->GetRefSkeleton();
		bool bAll = FromBones.Num() > 0;
		for (FSandboxPoseBone& Bone : FromBones)
		{
			if (!Space.IsValidIndex(Bone.MeshBoneIndex))
			{
				bAll = false;
				break;
			}
			const int32 Parent = Skeleton.GetParentIndex(Bone.MeshBoneIndex);
			const FQuat ParentRotation = Space.IsValidIndex(Parent) ? Space[Parent].GetRotation() : FQuat::Identity;
			Bone.Rotation = (ParentRotation.Inverse() * Space[Bone.MeshBoneIndex].GetRotation()).GetNormalized();
		}
		if (!bAll)
		{
			UE_LOG(LogSandbox, Warning, TEXT("Body controller: the get-up's start is not blended (getup.start_blend_s): the body's own pose could not be read; the clip's first pose is taken whole"));
		}
		else
		{
			ClipHold.From = MoveTemp(FromBones);
			ClipHold.FromPelvis = First;
			ClipHold.CrossShare = 0.0;
			ClipHold.CrossS = Tuning.GetUp.StartBlendS;
			GetUpRun.bStartBlend = true;
			for (const FBodyInstance* Instance : Mesh->Bodies)
			{
				if (Instance && Instance->IsValidBodyInstance())
				{
					FTransform Lay = Instance->GetUnrealWorldTransform();
					Lay.SetScale3D(FVector::OneVector);
					GetUpRun.StartBodies.Add(Mesh->GetBoneName(Instance->InstanceBoneIndex), Lay);
				}
			}
		}
	}
	HoldPace = 1.0;
	HitFree = 1.0;
	// What holds left of each body's springs in an attempt given up is not carried into the next: a hold still there takes its
	// chain down again in the first frame (UpdateCarry). (Kept, a chain whose share had not come back when the carry's share reached
	// 0 began the next attempt partly unled.)
	for (double& Kept : CarryBodyFree)
	{
		Kept = 1.0;
	}
	Pelvis->Target = First * Frame;
	Pelvis->Target.SetScale3D(FVector::OneVector);
	SetAssistTarget(*Pelvis);
	if (!bNewAttempt)
	{
		// The attempt began with its roll: its time, its count and the state are the roll's.
		GetUpView.Clip = BestPath;
		UE_LOG(LogSandbox, Display, TEXT("Body controller: after the roll, %s"), *How);
		return true;
	}
	BeginAttemptView();
	GetUpView.Clip = BestPath;
	EnterState(ESandboxBodyState::GetUp, FString::Printf(TEXT("lay still for %.2f s; %s"), GetUpRestS(), *How));
	return true;
}

bool USandboxBodyController::ReadMotionSpec(FString& OutError)
{
	if (!bMotionSpecTried)
	{
		bMotionSpecTried = true;
		const FString File = FPaths::IsRelative(Tuning.Motion.Library) ? FPaths::Combine(FPaths::ProjectDir(), Tuning.Motion.Library) : Tuning.Motion.Library;
		bMotionSpecRead = FSandboxMotionSpec::Read(File, MotionSpec, MotionLibraryError);
		if (!bMotionSpecRead)
		{
			UE_LOG(LogSandbox, Warning, TEXT("Body controller: %s"), *MotionLibraryError);
		}
	}
	if (!bMotionSpecRead)
	{
		OutError = MotionLibraryError;
	}
	return bMotionSpecRead;
}

FSandboxMatchSettings USandboxBodyController::MatchSettings() const
{
	const FSandboxGetUpTuning& Limits = Tuning.GetUp;
	const double ByGroup[FSandboxMotionSpec::GroupCount] = { Limits.MatchPelvis, Limits.MatchTrunk, Limits.MatchHands, Limits.MatchElbows, Limits.MatchFeet, Limits.MatchKnees };
	FSandboxMatchSettings Settings;
	for (const int32 Group : MotionSpec.FeatureGroups)
	{
		Settings.Weights.Add(ByGroup[Group]);
	}
	Settings.UpCmPerDeg = Limits.MatchUpCmPerDeg;
	return Settings;
}

bool USandboxBodyController::BuildMotionLibrary(FString& OutError)
{
	if (MotionLibrary.IsBuilt())
	{
		return true;
	}
	if (bMotionLibraryFailed)
	{
		OutError = MotionLibraryError;
		return false;
	}
	if (!ReadMotionSpec(OutError))
	{
		bMotionLibraryFailed = true;
		return false;
	}
	FSandboxMotionLibrary::FBuildSettings Settings;
	Settings.Pelvis = Tuning.Rig.Pelvis;
	Settings.Chest = Tuning.Rig.Chest;
	Settings.ContactCm = Tuning.Motion.ContactCm;
	Settings.ContactMps = Tuning.Motion.ContactMps;
	Settings.WindowS = Tuning.GetUp.MatchWindowS;
	Settings.Match = MatchSettings();
	if (!MotionLibrary.Build(MotionSpec, Mesh, Settings, [this](const FString& Key, FString& OutKey, FString& OutReadError) { return FindOrReadClip(Key, OutKey, OutReadError); }, MotionLibraryError))
	{
		bMotionLibraryFailed = true;
		MotionLibraryError = FString::Printf(TEXT("the motion library cannot be built: %s"), *MotionLibraryError);
		OutError = MotionLibraryError;
		return false;
	}
	UE_LOG(LogSandbox, Display, TEXT("Body controller: the motion library holds %d motions from %d clips, %d of them for the matcher; mirror images within %.2f cm"),
		MotionLibrary.GetVariants().Num(), MotionSpec.Clips.Num(),
		MotionLibrary.GetVariants().FilterByPredicate([](const FSandboxMotionVariant& Variant) { return Variant.bMatch; }).Num(), MotionLibrary.MirrorErrorMaxCm());
	return true;
}

void USandboxBodyController::MatchBody(FSandboxMatchBody& OutBody) const
{
	OutBody = FSandboxMatchBody();
	for (const FName Bone : MotionSpec.FeatureBones)
	{
		const FBodyInstance* Instance = Mesh->GetBodyInstance(Bone);
		OutBody.Places.Add(Instance && Instance->IsValidBodyInstance() ? Instance->GetUnrealWorldTransform().GetLocation() : FVector::ZeroVector);
	}
	const FBodyInstance* Pelvis = Mesh->GetBodyInstance(Tuning.Rig.Pelvis);
	const FBodyInstance* Chest = Mesh->GetBodyInstance(Tuning.Rig.Chest);
	if (Pelvis && Pelvis->IsValidBodyInstance())
	{
		OutBody.PelvisUp = Pelvis->GetUnrealWorldTransform().GetRotation().UnrotateVector(FVector::UpVector);
	}
	if (Chest && Chest->IsValidBodyInstance())
	{
		OutBody.ChestUp = Chest->GetUnrealWorldTransform().GetRotation().UnrotateVector(FVector::UpVector);
	}
	OutBody.SupportZ = bChoosingFloorSet ? ChoosingFloorZ : LastSupportHeightCm;
}

bool USandboxBodyController::SurfaceUnderPelvis(double& OutZ) const
{
	const FBodyInstance* Pelvis = Mesh->GetBodyInstance(Tuning.Rig.Pelvis);
	if (!Pelvis || !Pelvis->IsValidBodyInstance() || !GetWorld())
	{
		return false;
	}
	// Read only, as SenseFall's sweeps: the static world and things that move or simulate (a table's top, furniture with mass), the
	// character itself left out. A ball of a few cm, so that a gap between two boards is not taken for the floor under them.
	FCollisionQueryParams Query(SCENE_QUERY_STAT(SandboxGetUpSurface), false, GetOwner());
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(ECC_WorldStatic);
	Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
	Objects.AddObjectTypesToQuery(ECC_PhysicsBody);
	const FVector From = Pelvis->GetCOMPosition();
	FHitResult Hit;
	if (!GetWorld()->SweepSingleByObjectType(Hit, From, From - FVector(0.0, 0.0, 250.0), FQuat::Identity, Objects, FCollisionShape::MakeSphere(3.0), Query))
	{
		return false;
	}
	OutZ = Hit.ImpactPoint.Z;
	return true;
}

double USandboxBodyController::FitAtCm(double TimeS) const
{
	const TArray<FSandboxMotionVariant>& Variants = MotionLibrary.GetVariants();
	if (!Variants.IsValidIndex(GetUpRun.Variant))
	{
		return 0.0;
	}
	const FSandboxMotionVariant& Variant = Variants[GetUpRun.Variant];
	if (Variant.FitCm.Num() == 0 || Variant.KeyS <= 0.0)
	{
		return 0.0;
	}
	const double Key = FMath::Clamp(TimeS / Variant.KeyS, 0.0, static_cast<double>(Variant.FitCm.Num() - 1));
	const int32 Before = FMath::FloorToInt32(Key);
	const int32 After = FMath::Min(Before + 1, Variant.FitCm.Num() - 1);
	return FMath::Lerp(Variant.FitCm[Before], Variant.FitCm[After], Key - Before);
}

void USandboxBodyController::FreeVerticals(TArray<double>& OutShares) const
{
	OutShares.Init(1.0, Carry.Num());
	const TArray<FSandboxMotionVariant>& Variants = MotionLibrary.GetVariants();
	if (!Variants.IsValidIndex(GetUpRun.Variant))
	{
		return;
	}
	const FSandboxMotionVariant& Variant = Variants[GetUpRun.Variant];
	if (Variant.Touches.Num() == 0 || Variant.KeyS <= 0.0)
	{
		return;
	}
	const int32 Key = FMath::Clamp(FMath::RoundToInt32(ClipHold.TimeS / Variant.KeyS), 0, Variant.Touches.Num() - 1);
	for (int32 Feature = 0; Feature < MotionSpec.FeatureBones.Num(); ++Feature)
	{
		if ((Variant.Touches[Key] & (1u << Feature)) == 0)
		{
			continue;
		}
		const int32 Index = Carry.IndexOf(MotionSpec.FeatureBones[Feature]);
		const FBodyInstance* Body = Mesh->GetBodyInstance(MotionSpec.FeatureBones[Feature]);
		if (Index != INDEX_NONE && Body && TouchedWorld.IsValidIndex(Body->InstanceBodyIndex) && TouchedWorld[Body->InstanceBodyIndex])
		{
			OutShares[Index] = 0.0;
		}
	}
}

bool USandboxBodyController::ChooseByMatch(FString& OutKey, double& OutTimeS, FTransform& OutFrame, FSandboxGetUpStart& InOutStart, FString& OutError)
{
	if (!BuildMotionLibrary(OutError))
	{
		return false;
	}
	const TArray<FSandboxMotionVariant>& Variants = MotionLibrary.GetVariants();
	FSandboxMatchBody Body;
	MatchBody(Body);
	FSandboxMatchSettings Settings = MatchSettings();
	// The same clip from the same place fails the same way: the variant just given up costs more, and so does its mirror image (on the
	// stand a body by the table went from the clip from the front to its mirror and back four times, losing the pelvis each time).
	const int32 GivenUp = GivenUpClip.IsEmpty() ? INDEX_NONE : MotionLibrary.FindVariant(GivenUpClip);
	if (GivenUp != INDEX_NONE)
	{
		Settings.PenalizedVariants.Add(GivenUp);
		if (MotionLibrary.CounterpartOf(GivenUp) != INDEX_NONE)
		{
			Settings.PenalizedVariants.Add(MotionLibrary.CounterpartOf(GivenUp));
		}
	}
	Settings.PenaltyCm = Tuning.GetUp.MatchGivenUpCm;
	Settings.LateCmPerS = Tuning.GetUp.MatchLateCmPerS;
	TArray<FSandboxMatchSample> Samples;
	MotionLibrary.Samples(StartSpace.GetRotation(), Tuning.GetUp.MatchWindowS, Samples);
	TArray<FSandboxMatch> Best;
	FSandboxMotionMatcher::BestByVariant(Body, Samples, Variants.Num(), Settings, Best);
	TArray<bool> Allowed;
	TArray<bool> GetUps;
	TArray<bool> Rolls;
	// With the roll (getup.roll, decision 50) a body gets up by the clips from the back and the front only: one on its side rolls first.
	const bool bNoSides = Tuning.GetUp.Roll > 0.5;
	for (const FSandboxMotionVariant& Variant : Variants)
	{
		Allowed.Add(Variant.bMatch && !(bNoSides && (Variant.Lies == TEXT("left") || Variant.Lies == TEXT("right"))));
		GetUps.Add(Variant.Kind == TEXT("getup"));
		Rolls.Add(Variant.Kind == TEXT("roll"));
	}
	FSandboxMatch Chosen;
	FSandboxMatch Next;
	FSandboxMotionMatcher::Choose(Best, Allowed, Chosen, Next);
	if (!Chosen.bValid)
	{
		OutError = TEXT("the motion library has no get-up the matcher may choose ('match' in motion.library)");
		return false;
	}
	OutKey = Variants[Chosen.Variant].Key;
	OutTimeS = Chosen.TimeS;
	OutFrame = FSandboxMotionMatcher::Frame(Chosen, StartSpace.GetRotation(), Body.SupportZ);
	InOutStart.bMatched = true;
	InOutStart.CostCm = Chosen.CostCm;
	InOutStart.PlacesCm = Chosen.PlacesCm;
	InOutStart.UpDeg = Chosen.UpDeg;
	InOutStart.YawDeg = FMath::RadiansToDegrees(Chosen.YawRad);
	if (Next.bValid)
	{
		InOutStart.Next = Variants[Next.Variant].Name;
		InOutStart.NextCostCm = Next.CostCm;
	}
	FSandboxMatch Any;
	FSandboxMatch Unused;
	FSandboxMotionMatcher::Choose(Best, GetUps, Any, Unused);
	if (Any.bValid)
	{
		InOutStart.BestAny = Variants[Any.Variant].Name;
		InOutStart.BestAnyCostCm = Any.CostCm;
	}
	FSandboxMatch Roll;
	FSandboxMotionMatcher::Choose(Best, Rolls, Roll, Unused);
	if (Roll.bValid)
	{
		InOutStart.BestRoll = Variants[Roll.Variant].Name;
		InOutStart.BestRollCostCm = Roll.CostCm;
	}
	return true;
}

void USandboxBodyController::MeasureStart(const FSandboxClipPose& Clip, double TimeS, const FTransform& Frame, FSandboxGetUpStart& InOutStart)
{
	// Read only: the bodies' transforms, the mesh's pose, the clip.
	const FBodyInstance* PelvisBody = Mesh->GetBodyInstance(Tuning.Rig.Pelvis);
	FTransform InClip;
	if (PelvisBody && PelvisBody->IsValidBodyInstance() && Clip.ReachedBodyAt(Tuning.Rig.Pelvis, TimeS, InClip))
	{
		InOutStart.PelvisOffCm = FVector::Dist((InClip * Frame).GetLocation(), PelvisBody->GetUnrealWorldTransform().GetLocation());
	}
	FString Ignored;
	if (ReadMotionSpec(Ignored))
	{
		FSandboxMatchBody Body;
		MatchBody(Body);
		const FSandboxMatchSettings Settings = MatchSettings();
		double Total = 0.0;
		double Squared = 0.0;
		bool bAll = true;
		for (int32 Index = 0; Index < MotionSpec.FeatureBones.Num(); ++Index)
		{
			if (!Clip.ReachedBodyAt(MotionSpec.FeatureBones[Index], TimeS, InClip))
			{
				bAll = false;
				break;
			}
			Total += Settings.Weights[Index];
			Squared += Settings.Weights[Index] * FVector::DistSquared((InClip * Frame).GetLocation(), Body.Places[Index]);
		}
		InOutStart.OffCm = bAll && Total > 0.0 ? FMath::Sqrt(Squared / Total) : -1.0;
	}
	// The pose: each joint's bone as the body has it now against the clip's, both relative to the bone's parent.
	TArray<FSandboxPoseBone> Bones;
	Clip.Evaluate(TimeS, Bones);
	const TArray<FTransform>& Space = Mesh->GetComponentSpaceTransforms();
	const FReferenceSkeleton& Skeleton = Mesh->GetSkeletalMeshAsset()->GetRefSkeleton();
	double SumDeg = 0.0;
	int32 Counted = 0;
	for (const FSandboxPoseBone& Bone : Bones)
	{
		if (!Space.IsValidIndex(Bone.MeshBoneIndex))
		{
			continue;
		}
		const int32 Parent = Skeleton.GetParentIndex(Bone.MeshBoneIndex);
		const FQuat ParentRotation = Space.IsValidIndex(Parent) ? Space[Parent].GetRotation() : FQuat::Identity;
		const FQuat Own = (ParentRotation.Inverse() * Space[Bone.MeshBoneIndex].GetRotation()).GetNormalized();
		SumDeg += FMath::RadiansToDegrees(Own.AngularDistance(Bone.Rotation));
		++Counted;
	}
	InOutStart.PoseDeg = Counted > 0 ? SumDeg / Counted : -1.0;
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
	if (GetUpView.RiseFeetCm < 0.0 && PelvisNow.GetLocation().Z - GetUpFloorZ() >= 70.0)
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

const TCHAR* GiveUpName(ESandboxGiveUp Kind)
{
	switch (Kind)
	{
	case ESandboxGiveUp::PosedFromOutside: return TEXT("posed_from_outside");
	case ESandboxGiveUp::HeldUp: return TEXT("held_up");
	case ESandboxGiveUp::NoClip: return TEXT("no_clip");
	case ESandboxGiveUp::Hit: return TEXT("hit");
	case ESandboxGiveUp::Lost: return TEXT("lost");
	case ESandboxGiveUp::HeldDown: return TEXT("held_down");
	case ESandboxGiveUp::Waited: return TEXT("waited");
	case ESandboxGiveUp::Dragged: return TEXT("dragged");
	case ESandboxGiveUp::NotLed: return TEXT("not_led");
	case ESandboxGiveUp::Afraid: return TEXT("afraid");
	case ESandboxGiveUp::Protect: return TEXT("protect");
	default: return TEXT("other");
	}
}

bool USandboxBodyController::CanBeginGetUp(FString& OutWhy) const
{
	// Decision 18 (getup.drag): not while dragged, nor until the body has been still of it for getup.drag_free_s.
	if (Tuning.GetUp.Drag > 0.5 && DraggedLastS >= 0.0 && ElapsedS - DraggedLastS < Tuning.GetUp.DragFreeS)
	{
		OutWhy = FString::Printf(TEXT("it is dragged (%.2f s ago)"), ElapsedS - DraggedLastS);
		return false;
	}
	// R10, step 6 in part (behaviour.protect): not while it covers its head after a hit.
	if (Tuning.Behaviour.Protect > 0.5 && ElapsedS < ProtectUntilS)
	{
		OutWhy = FString::Printf(TEXT("it covers where it was hit (%.2f s more)"), ProtectUntilS - ElapsedS);
		return false;
	}
	// R10: nor while the protective pose's muscles are still let go (begun then, the get-up's muscles would be blended toward them for
	// protect_blend_s; the independent reading's find). 0 whenever every behaviour's switch is 0.
	if (ProtectShare > 0.0)
	{
		OutWhy = FString::Printf(TEXT("its cover is being let go (%.2f)"), ProtectShare);
		return false;
	}
	// R10, step 5 (behaviour.stay_down; decision 41): not while it is afraid and the player stands near, nor for getup.rest_s after that.
	if (Tuning.Behaviour.StayDown > 0.5)
	{
		if (bStayDownWanted)
		{
			OutWhy = FString::Printf(TEXT("it is afraid (%.2f) and the player is %.0f cm away"), BehaviourView.Fear, BehaviourView.ViewerLastCm);
			return false;
		}
		if (StayDownReleasedS >= 0.0 && ElapsedS - StayDownReleasedS < Tuning.GetUp.RestS && !bGetUpCommanded)
		{
			OutWhy = FString::Printf(TEXT("the player went away or its fear went down %.2f s ago"), ElapsedS - StayDownReleasedS);
			return false;
		}
	}
	return true;
}

void USandboxBodyController::StartCover(ECoverPlace Place, double HitNs, FName Bone)
{
	static const TCHAR* const PlaceNames[] = { TEXT("head"), TEXT("trunk"), TEXT("groin"), TEXT("leg") };
	FSandboxBehaviourView::FCover& Cover = BehaviourView.Covers.AddDefaulted_GetRef();
	Cover.Place = PlaceNames[static_cast<int32>(Place)];
	Cover.HitS = ElapsedS;
	Cover.HitNs = HitNs;
	CoverPlace = Place;
	CoverBone = Bone;
	ProtectUntilS = ElapsedS + Tuning.Behaviour.ProtectHoldS;
	UE_LOG(LogSandbox, Display, TEXT("Body controller: hit of %.1f N*s on the %s (%s) at %.2f s while %s: it covers it until %.2f s"),
		HitNs, *Cover.Place, *Bone.ToString(), ElapsedS, FSandboxBodyTuning::StateName(State), ProtectUntilS);
	// A get_up in force is interrupted: it protects, then goes on, at most behaviour.command_tries times (an attempt given up for it is
	// counted in GiveUpGetUp).
	if (State != ESandboxBodyState::GetUp)
	{
		InterruptCommand(TEXT("hit"));
	}
	if (State == ESandboxBodyState::GetUp)
	{
		Cover.bGaveUp = true;
		GiveUpGetUp(ESandboxGiveUp::Protect, FString::Printf(TEXT("hit on the %s with %.1f N*s"), *Cover.Place, HitNs));
		bProtectGivenUp = true;
	}
}

FVector USandboxBodyController::TrunkForward() const
{
	// The trunk's own forward: across the line of the hips (left to right) and the line of the trunk (the belly's rule, IsIntoBelly).
	static const FName PelvisName(TEXT("pelvis"));
	static const FName NeckName(TEXT("neck_01"));
	static const FName ThighLeftName(TEXT("thigh_l"));
	static const FName ThighRightName(TEXT("thigh_r"));
	const FBodyInstance* Pelvis = Mesh->GetBodyInstance(PelvisName);
	const FBodyInstance* Neck = Mesh->GetBodyInstance(NeckName);
	const FBodyInstance* ThighLeft = Mesh->GetBodyInstance(ThighLeftName);
	const FBodyInstance* ThighRight = Mesh->GetBodyInstance(ThighRightName);
	if (!Pelvis || !Neck || !ThighLeft || !ThighRight || !Pelvis->IsValidBodyInstance() || !Neck->IsValidBodyInstance() ||
		!ThighLeft->IsValidBodyInstance() || !ThighRight->IsValidBodyInstance())
	{
		return FVector::ZeroVector;
	}
	const FVector Line = Neck->GetUnrealWorldTransform().GetLocation() - Pelvis->GetUnrealWorldTransform().GetLocation();
	const FVector Hips = ThighRight->GetUnrealWorldTransform().GetLocation() - ThighLeft->GetUnrealWorldTransform().GetLocation();
	return FVector::CrossProduct(Hips, Line).GetSafeNormal();
}

bool USandboxBodyController::IsFromFront(const FVector& HitNs) const
{
	const FVector Forward = TrunkForward();
	return !Forward.IsZero() && FVector::DotProduct(HitNs.GetSafeNormal(), Forward) < -0.5;
}

void USandboxBodyController::MeasureCover()
{
	// C34 (read only): the gap from the nearer of each side's forearm and hand to the place covered after a hit, while the cover is held
	// (the place's points below); a limb is taken as 4 cm thick.
	if (BehaviourView.Covers.Num() == 0 || ElapsedS > ProtectUntilS)
	{
		return;
	}
	static const FName Limbs[2][2] = { { FName(TEXT("lowerarm_l")), FName(TEXT("hand_l")) }, { FName(TEXT("lowerarm_r")), FName(TEXT("hand_r")) } };
	static const FName Thighs[2] = { FName(TEXT("thigh_l")), FName(TEXT("thigh_r")) };
	static const FName Calves[2] = { FName(TEXT("calf_l")), FName(TEXT("calf_r")) };
	const FBodyInstance* HeadBody = Mesh->GetBodyInstance(Tuning.Rig.Head);
	const FBodyInstance* ChestBody = Mesh->GetBodyInstance(Tuning.Rig.Chest);
	if (!HeadBody || !HeadBody->IsValidBodyInstance() || !ChestBody || !ChestBody->IsValidBodyInstance())
	{
		return;
	}
	// The place as points with a radius: the head's middle; the chest's; the groin, between the hip joints (the thighs' origins), 8 cm;
	// a leg, the nearer of the struck side's thigh and calf, 7 cm.
	TArray<FVector, TInlineAllocator<2>> Points;
	double PlaceRadiusCm = 7.0;
	if (CoverPlace == ECoverPlace::Head)
	{
		Points.Add(HeadBody->GetCOMPosition());
		PlaceRadiusCm = Tuning.Perception.HeadRadiusCm;
	}
	else if (CoverPlace == ECoverPlace::Trunk)
	{
		Points.Add(ChestBody->GetCOMPosition());
		PlaceRadiusCm = Tuning.Perception.TrunkRadiusCm;
	}
	else if (CoverPlace == ECoverPlace::Groin)
	{
		// The front of the groin: 8 cm forward of the point between the hip joints, which lies deep in the pelvis (measured there, a hand
		// on the groin read 14 to 16 cm off).
		Points.Add((Mesh->GetBoneLocation(Thighs[0]) + Mesh->GetBoneLocation(Thighs[1])) * 0.5 + TrunkForward() * 8.0);
		PlaceRadiusCm = 5.0;
	}
	else
	{
		const int32 Side = CoverBone.ToString().EndsWith(TEXT("_r")) ? 1 : 0;
		for (const FName& Segment : { Thighs[Side], Calves[Side] })
		{
			const FBodyInstance* SegmentBody = Mesh->GetBodyInstance(Segment);
			if (SegmentBody && SegmentBody->IsValidBodyInstance())
			{
				Points.Add(SegmentBody->GetCOMPosition());
			}
		}
	}
	constexpr double LimbRadiusCm = 4.0;
	FSandboxBehaviourView::FCover& Cover = BehaviourView.Covers.Last();
	double Gaps[2] = { TNumericLimits<double>::Max(), TNumericLimits<double>::Max() };
	for (int32 Side = 0; Side < 2; ++Side)
	{
		for (const FName& Limb : Limbs[Side])
		{
			const FBodyInstance* LimbBody = Mesh->GetBodyInstance(Limb);
			if (LimbBody && LimbBody->IsValidBodyInstance())
			{
				for (const FVector& Point : Points)
				{
					Gaps[Side] = FMath::Min(Gaps[Side], FVector::Dist(LimbBody->GetCOMPosition(), Point) - PlaceRadiusCm - LimbRadiusCm);
				}
			}
		}
		Cover.GapCm[Side] = FMath::Min(Cover.GapCm[Side], Gaps[Side]);
	}
	if (Cover.OneWithinS < 0.0 && FMath::Min(Gaps[0], Gaps[1]) <= 10.0)
	{
		Cover.OneWithinS = ElapsedS - Cover.HitS;
	}
	if (Cover.BothWithinS < 0.0 && FMath::Max(Gaps[0], Gaps[1]) <= 15.0)
	{
		Cover.BothWithinS = ElapsedS - Cover.HitS;
	}
	// The head's fastest way down from 0.1 s after the hit (the hit's own throw left out) while covered: C16's limit at the floor.
	if (ElapsedS - Cover.HitS >= 0.1)
	{
		Cover.HeadDownMps = FMath::Max(Cover.HeadDownMps, -HeadBody->GetUnrealWorldVelocity().Z * 0.01);
	}
}

void USandboxBodyController::TellCommand(ESandboxCommand Command)
{
	FSandboxBehaviourView::FCommand& Record = BehaviourView.Commands.AddDefaulted_GetRef();
	Record.Name = Command == ESandboxCommand::GetUp ? TEXT("get_up") : TEXT("stop");
	Record.TellS = ElapsedS;
	const int32 Index = BehaviourView.Commands.Num() - 1;
	const auto Answer = [this, Index](const FString& Text)
	{
		BehaviourView.Commands[Index].Answer = Text;
		BehaviourView.Commands[Index].AnsweredS = ElapsedS;
		UE_LOG(LogSandbox, Display, TEXT("Body controller: at %.2f s the command %s is %s"), ElapsedS, *BehaviourView.Commands[Index].Name, *Text);
	};
	if (Tuning.Behaviour.Commands <= 0.5)
	{
		Answer(TEXT("refused: off"));
		return;
	}
	if (Command == ESandboxCommand::Stop)
	{
		if (bGetUpCommanded)
		{
			EndCommand(TEXT("stopped"));
		}
		Answer(TEXT("done"));
		return;
	}
	// get_up: from the floor only, and not while something keeps it there (decision 41's cases: held by two, pressed, held up).
	const FString Why = IsUpright() ? TEXT("standing") : State == ESandboxBodyState::Falling ? TEXT("falling") : Holds.Num() >= 2 ? TEXT("held") :
		(Tuning.GetUp.HoldForceN > 0.0 && SustainedN.Size() >= Tuning.GetUp.HoldForceN) ? TEXT("pressed") : bHeldUp || !bGetUpAllowed || bGetUpBroken ? TEXT("unable") :
		FString();
	if (!Why.IsEmpty())
	{
		Answer(FString(TEXT("refused: ")) + Why);
		return;
	}
	if (bGetUpCommanded)
	{
		EndCommand(TEXT("replaced"));
	}
	bGetUpCommanded = true;
	CommandRecord = Index;
	CommandInterruptedS = -1.0e9;
	// C39: the reaction is timed from the command; a body that had lain still for long would otherwise begin in the next tick (the
	// independent reading of 28 November: 0.011 s, under a person's 0.2).
	LyingS = 0.0;
	BehaviourView.Commands[Index].Answer = TEXT("accepted");
	UE_LOG(LogSandbox, Display, TEXT("Body controller: at %.2f s the command get_up is accepted"), ElapsedS);
	// Already getting up: that is its motion. The disposition set aside: a get-up turned back goes on up.
	if (State == ESandboxBodyState::GetUp)
	{
		BehaviourView.Commands[Index].FirstMotionS = ElapsedS;
	}
	if (State == ESandboxBodyState::GetUp && GetUpRun.bLowering)
	{
		GetUpRun.bLowering = false;
		// Turned up again as it was turned back: over lower_turn_s, never in one frame (found by the independent reading).
		GetUpRun.RaiseFromS = ElapsedS;
		GetUpRun.RaiseFromRate = ClipHold.Rate;
		if (BehaviourView.Lowerings.Num() > 0 && BehaviourView.Lowerings.Last().EndedS < 0.0)
		{
			BehaviourView.Lowerings.Last().EndedS = ElapsedS;
			BehaviourView.Lowerings.Last().Ended = TEXT("commanded up");
		}
	}
}

void USandboxBodyController::InterruptCommand(const TCHAR* What)
{
	// One blow that covers two places, or gives an attempt up and then covers, interrupts once (the independent reading of the fixes).
	if (bGetUpCommanded && BehaviourView.Commands.IsValidIndex(CommandRecord) && ElapsedS - CommandInterruptedS > Tuning.Perception.ReactS)
	{
		CommandInterruptedS = ElapsedS;
		const int32 Times = ++BehaviourView.Commands[CommandRecord].Interrupted;
		if (Times > FMath::RoundToInt32(Tuning.Behaviour.CommandTries))
		{
			EndCommand(FString::Printf(TEXT("given up: interrupted %d times, the last by a %s"), Times, What));
		}
	}
}

void USandboxBodyController::EndCommand(const FString& Answer)
{
	if (bGetUpCommanded && BehaviourView.Commands.IsValidIndex(CommandRecord))
	{
		BehaviourView.Commands[CommandRecord].Answer = Answer;
		BehaviourView.Commands[CommandRecord].AnsweredS = ElapsedS;
		UE_LOG(LogSandbox, Display, TEXT("Body controller: at %.2f s the command get_up is %s"), ElapsedS, *Answer);
	}
	bGetUpCommanded = false;
	CommandRecord = INDEX_NONE;
}

bool USandboxBodyController::IsAfraid() const
{
	return Tuning.Behaviour.Fear > 0.5 && BehaviourView.Fear > 0.0 && BehaviourView.Fear >= Tuning.Behaviour.FearStayDown;
}

void USandboxBodyController::AddFear(double Amount, bool bHit)
{
	// From what is left of it now, to at most 1; it falls from here over fear_fade_s (UpdateFear).
	UpdateFear();
	FearAtLast = FMath::Min(BehaviourView.Fear + Amount, 1.0);
	FearLastS = ElapsedS;
	BehaviourView.Fear = FearAtLast;
	BehaviourView.FearMax = FMath::Max(BehaviourView.FearMax, FearAtLast);
	// Hits are counted by contact where they are felt (FeelFrame); a threat here.
	if (!bHit)
	{
		++BehaviourView.FearThreats;
	}
}

void USandboxBodyController::UpdateFear()
{
	// Read only of its own clock: the fear at its last rise, gone in a straight line over fear_fade_s from then.
	const FSandboxBehaviourTuning& Limits = Tuning.Behaviour;
	if (FearLastS < 0.0)
	{
		BehaviourView.Fear = 0.0;
		return;
	}
	const double Since = ElapsedS - FearLastS;
	const double Left = Limits.FearFadeS > 0.0 ? FMath::Clamp(1.0 - Since / Limits.FearFadeS, 0.0, 1.0) : (Since <= KINDA_SMALL_NUMBER ? 1.0 : 0.0);
	BehaviourView.Fear = Limits.Fear > 0.5 ? FearAtLast * Left : 0.0;
	if (BehaviourView.Fear > 0.0 && ElapsedS >= FearSeriesNextS - KINDA_SMALL_NUMBER)
	{
		BehaviourView.FearSeries.Add(FVector2D(ElapsedS, BehaviourView.Fear));
		FearSeriesNextS = ElapsedS + 0.25;
	}
}

void USandboxBodyController::UpdateStayDown(double DeltaTime)
{
	const FSandboxBehaviourTuning& Limits = Tuning.Behaviour;
	// Near, and with the margin once near (so that the line is not crossed back and forth); measured by MeasureViewer this tick.
	const double ViewerCm = BehaviourView.ViewerLastCm;
	bViewerNear = ViewerCm >= 0.0 && ViewerCm < Limits.NearCm + (bViewerNear ? Limits.NearMarginCm : 0.0);
	// get_up (step 8 in part) sets the disposition aside.
	const bool bWanted = Limits.StayDown > 0.5 && IsAfraid() && bViewerNear && !bGetUpCommanded;
	if (bStayDownWanted && !bWanted)
	{
		StayDownReleasedS = ElapsedS;
	}
	bStayDownWanted = bWanted;
	if (State == ESandboxBodyState::GetUp || IsUpright())
	{
		bAfraidGivenUp = false;
		bProtectGivenUp = false;
	}
	MeasureCover();
	if (bWanted && State == ESandboxBodyState::Lying)
	{
		BehaviourView.HeldBackS += DeltaTime;
	}

	// A get-up under way when the player comes near the afraid body: turned back after the reaction to what it sees (perception.react_s).
	if (bWanted && State == ESandboxBodyState::GetUp && !GetUpRun.bLowering)
	{
		if (StayDownNearSinceS < 0.0)
		{
			StayDownNearSinceS = ElapsedS;
		}
		if (ElapsedS - StayDownNearSinceS >= Tuning.Perception.ReactS - KINDA_SMALL_NUMBER)
		{
			FSandboxBehaviourView::FLowering& Lowering = BehaviourView.Lowerings.AddDefaulted_GetRef();
			Lowering.NearS = StayDownNearSinceS;
			Lowering.DecidedS = ElapsedS;
			Lowering.FromClipS = ClipHold.TimeS;
			// Back to where the attempt began, the pose it lay in. (Back to the clip's own start instead, a body that began sitting against
			// a chair, 0.97 s into its clip, was pulled toward where the clip lies, 40 cm off, lost and dropped: its head met the floor at
			// 3.1 m/s. Where it began sitting, the world's pull is let go slowly after it, behaviour.lower_release_s.)
			Lowering.ToClipS = GetUpView.Starts.IsValidIndex(GetUpRun.StartIndex) ? GetUpView.Starts[GetUpRun.StartIndex].TimeS : 0.0;
			if (const FBodyInstance* PelvisNow = Mesh->GetBodyInstance(Tuning.Rig.Pelvis))
			{
				Lowering.FromPelvisCm = PelvisNow->IsValidBodyInstance() ? PelvisNow->GetCOMPosition().Z - GetUpFloorZ() : 0.0;
			}
			StayDownNearSinceS = -1.0;
			if (Limits.LowerRate <= 0.0 || GetUpRun.bRolling || GetUpRun.Path.IsEmpty() || ClipHold.Path != GetUpRun.Path || ClipHold.TimeS <= Lowering.ToClipS + KINDA_SMALL_NUMBER)
			{
				// At once: in a roll (no clip to run back), or with no way to lower; the world's pull goes over getup.carry_release_s.
				GiveUpGetUp(ESandboxGiveUp::Afraid, FString::Printf(TEXT("afraid (%.2f), the player %.0f cm away"), BehaviourView.Fear, ViewerCm));
			}
			else
			{
				GetUpRun.bLowering = true;
				GetUpRun.LowerToS = Lowering.ToClipS;
				GetUpRun.LowerFromRate = ClipHold.Rate;
				UE_LOG(LogSandbox, Display, TEXT("Body controller: at %.2f s the get-up is turned back (afraid %.2f, the player %.0f cm away): the clip runs back from %.2f s to %.2f s"),
					ElapsedS, BehaviourView.Fear, ViewerCm, ClipHold.TimeS, GetUpRun.LowerToS);
			}
		}
	}
	else if (State != ESandboxBodyState::GetUp || !bWanted)
	{
		StayDownNearSinceS = -1.0;
	}

	// How fast the pelvis and the head come down while lowered and for a second after (read only).
	if (BehaviourView.Lowerings.Num() > 0 && ((State == ESandboxBodyState::GetUp && GetUpRun.bLowering) || ElapsedS <= LoweringWatchUntilS))
	{
		FSandboxBehaviourView::FLowering& Lowering = BehaviourView.Lowerings.Last();
		const FBodyInstance* PelvisBody = Mesh->GetBodyInstance(Tuning.Rig.Pelvis);
		const FBodyInstance* HeadBody = Mesh->GetBodyInstance(Tuning.Rig.Head);
		if (PelvisBody && PelvisBody->IsValidBodyInstance() && -PelvisBody->GetUnrealWorldVelocity().Z * 0.01 > Lowering.PelvisDownMps)
		{
			Lowering.PelvisDownMps = -PelvisBody->GetUnrealWorldVelocity().Z * 0.01;
			Lowering.PelvisDownAtS = ElapsedS;
		}
		if (HeadBody && HeadBody->IsValidBodyInstance() && -HeadBody->GetUnrealWorldVelocity().Z * 0.01 > Lowering.HeadDownMps)
		{
			Lowering.HeadDownMps = -HeadBody->GetUnrealWorldVelocity().Z * 0.01;
			Lowering.HeadDownAtS = ElapsedS;
		}
	}

	// The protective pose: lying (or going down from an attempt it gave up afraid or for a hit on the head) while it stays down, or for
	// protect_hold_s after a hit on the head (step 6 in part).
	const bool bDownNow = State == ESandboxBodyState::Lying || (State == ESandboxBodyState::Falling && (bAfraidGivenUp || bProtectGivenUp));
	const bool bCovering = Limits.Protect > 0.5 && ElapsedS < ProtectUntilS;
	const bool bPose = (bWanted || bCovering) && bDownNow && !bHeldUp;
	// Covering after a hit, the struck place's pose; staying down, the head's.
	const ECoverPlace WantedPlace = bCovering ? CoverPlace : ECoverPlace::Head;
	if (bPose != bProtectPose || (bPose && WantedPlace != ProtectPosePlace))
	{
		bProtectPose = bPose;
		ProtectPosePlace = WantedPlace;
		UE_LOG(LogSandbox, Display, TEXT("Body controller: at %.2f s %s"), ElapsedS, bPose ? TEXT("the body covers itself") : TEXT("the body lets its cover go"));
		PoseBlendOverrideS = Limits.ProtectBlendS;
		ApplyPose();
		PoseBlendOverrideS = -1.0;
	}
	if (bPose)
	{
		BehaviourView.ProtectS += DeltaTime;
	}
	// Its muscles come up to hold the pose and go down again over the pose's blend (exact: nothing while the share is 0).
	const double WantedShare = bPose ? 1.0 : 0.0;
	if (ProtectShare != WantedShare)
	{
		const double Step = Limits.ProtectBlendS > 0.0 ? DeltaTime / Limits.ProtectBlendS : 1.0;
		ProtectShare = WantedShare > ProtectShare ? FMath::Min(ProtectShare + Step, 1.0) : FMath::Max(ProtectShare - Step, 0.0);
		PushTuningToControls();
	}
}

FVector USandboxBodyController::HoldsPullN() const
{
	FVector SumN = FVector::ZeroVector;
	for (const FSandboxBodyHold& Hold : Holds)
	{
		SumN += Hold.PullN;
	}
	return SumN;
}

void USandboxBodyController::UpdateDrag(double DeltaTime)
{
	// Read only of the holds and the pelvis. A drag: holds that together pull with drag_force_n, or that move the pelvis along their
	// pull faster than drag_mps (a lighter pull of a body that slides).
	const FSandboxGetUpTuning& Limits = Tuning.GetUp;
	// Over the floor only (decision 18 is a drag along the floor; an independent reader's find): hands that pull a rising body up by its
	// wrists see its pelvis rise along their pull, and a lift is a hold of another kind (held up, FR-47).
	const FVector PullN = HoldsPullN() * FVector(1.0, 1.0, 0.0);
	const FBodyInstance* Pelvis = Mesh->GetBodyInstance(Tuning.Rig.Pelvis);
	const double AlongMps = Pelvis && Pelvis->IsValidBodyInstance() && !PullN.IsNearlyZero() ?
		FVector::DotProduct(Pelvis->GetUnrealWorldVelocity() * FVector(1.0, 1.0, 0.0), PullN.GetSafeNormal()) * 0.01 : 0.0;
	// The pelvis's speed counts only for two holds or more (decision 18 is two hands): one hand whose pull a yielding arm lets the body
	// follow (getup.arm_answer) moves the pelvis along it too, and is a hindrance, not a drag.
	const bool bDragging = Holds.Num() > 0 && (PullN.Size() >= Limits.DragForceN || (Holds.Num() >= 2 && Limits.DragMps > 0.0 && AlongMps >= Limits.DragMps));
	DragS = bDragging ? DragS + DeltaTime : 0.0;
	if (bDragging && DragS >= Limits.DragS)
	{
		DraggedLastS = ElapsedS;
		GetUpView.DraggedS += DeltaTime;
	}
	// What the carry gives against the holds while they pull this hard (read only; the measure of decision 18's "no force against the
	// hands").
	if (Holds.Num() > 0 && PullN.Size() >= Limits.DragForceN && Carry.IsCreated() && Carry.GetShare() > 0.0)
	{
		const FSandboxCarrySettings Settings = CarrySettings();
		double AgainstN = 0.0;
		for (int32 Index = 0; Index < Carry.Num(); ++Index)
		{
			AgainstN += FMath::Max(-FVector::DotProduct(Carry.ForceOn(Index, Settings, Carry.GetShare()), PullN.GetSafeNormal()), 0.0);
		}
		GetUpView.CarryAgainstHoldsMaxN = FMath::Max(GetUpView.CarryAgainstHoldsMaxN, AgainstN);
		GetUpView.CarryAgainstHoldsNs += AgainstN * DeltaTime;
	}
	if (State == ESandboxBodyState::GetUp && bDragging && DragS >= Limits.DragS)
	{
		GiveUpGetUp(ESandboxGiveUp::Dragged, FString::Printf(TEXT("dragged with %.0f N, the pelvis at %.2f m/s along the pull"), PullN.Size(), AlongMps));
		// The world lets go of the body at once (decision 18), not over the release: it goes down on its muscles and is dragged.
		CarryShare = 0.0;
		Carry.Apply(CarrySettings(), 0.0);
	}
}

bool USandboxBodyController::IsBraced() const
{
	return AskedTone == static_cast<int32>(ESandboxTone::Braced) || (BraceFromS >= 0.0 && ElapsedS >= BraceFromS && ElapsedS < BraceUntilS) ||
		(AnticipateFromS >= 0.0 && ElapsedS < AnticipateUntilS);
}

void USandboxBodyController::UpdateHeldArms(double DeltaTime)
{
	static const FName Hands[2] = { FName(TEXT("hand_l")), FName(TEXT("hand_r")) };
	static const FName Chain[2][3] = { { FName(TEXT("hand_l")), FName(TEXT("lowerarm_l")), FName(TEXT("upperarm_l")) },
		{ FName(TEXT("hand_r")), FName(TEXT("lowerarm_r")), FName(TEXT("upperarm_r")) } };
	const FSandboxGetUpTuning& Limits = Tuning.GetUp;
	// Only while a clip leads the body (not in a roll, not standing: R5 and R6 take the standing and the lying body). Nor while a get-up
	// turned back is lowered (R10, step 5): the arm that yielded to a hand there had its upper arm's target turn over by 159 degrees in a
	// frame as the body went down under it (protect_phase_lowering_hold); the held arm goes with the body until the limbs' arbiter (R10,
	// step 3) gives it to one of the two.
	const bool bGettingUp = State == ESandboxBodyState::GetUp && !GetUpRun.bRolling && !GetUpRun.Path.IsEmpty() && !GetUpRun.bLowering;
	bool bChanged = false;
	for (int32 Side = 0; Side < 2; ++Side)
	{
		const FSandboxBodyHold* Hold = nullptr;
		if (bGettingUp)
		{
			for (const FSandboxBodyHold& Each : Holds)
			{
				if (Each.Bone == Chain[Side][0] || Each.Bone == Chain[Side][1] || Each.Bone == Chain[Side][2])
				{
					Hold = &Each;
					break;
				}
			}
		}
		const int32 Mode = FMath::RoundToInt32(Limits.ArmAnswer);
		// Mode 0 (switched off while an arm is held): the arm is let go as for a hold that ends.
		const int32 Answer = !Hold || Mode <= 0 ? 0 : Mode == 2 ? 1 : Mode == 3 ? 2 : IsBraced() ? 2 : 1;
		const double Scale = Answer == 1 ? Limits.ArmYieldScale : Answer == 2 ? Limits.ArmPullScale : 1.0;
		if (Answer != HeldArmAnswer[Side] || Scale != HeldArmScale[Side])
		{
			bChanged = true;
			UE_LOG(LogSandbox, Display, TEXT("Body controller: at %.2f s the %s arm %s"), ElapsedS, Side == 0 ? TEXT("left") : TEXT("right"),
				Answer == 1 ? TEXT("yields to the hand that holds it") : Answer == 2 ? TEXT("is pulled in against the hand that holds it") : TEXT("is its own again"));
		}
		if (Answer == 0)
		{
			if (HeldArmAnswer[Side] != 0)
			{
				// Let go over a pose's blend: the clip's arm comes back, never in a step.
				const FBodyInstance* HandBody = Mesh->GetBodyInstance(Hands[Side]);
				PlaceArm(Hands[Side], HandBody && HandBody->IsValidBodyInstance() ? HandBody->GetCOMPosition() : FVector::ZeroVector, 0.0, Tuning.Poses.BlendS, false);
			}
		}
		else
		{
			FVector Target = Hold->PointCm;
			if (Answer == 1)
			{
				// Yield: after the hand that holds it, along its pull.
				Target += Hold->PullN.GetSafeNormal() * Limits.ArmReachCm;
				GetUpView.ArmYieldS += DeltaTime;
			}
			else
			{
				// Pull in: the hand toward its shoulder, the elbow bent; where, is worked out once as the pull-in begins and then held in
				// the chest's space (worked out anew each tick, the way from the shoulder to the holding point turned round as that point
				// passed the shoulder, and the arm's target jumped 160 degrees in a frame).
				const FBodyInstance* Chest = Mesh->GetBodyInstance(Tuning.Rig.Chest);
				const FTransform ChestNow = Chest && Chest->IsValidBodyInstance() ? Chest->GetUnrealWorldTransform() : FTransform::Identity;
				if (Answer != HeldArmAnswer[Side])
				{
					const FBodyInstance* Shoulder = Mesh->GetBodyInstance(Chain[Side][2]);
					const FVector From = Shoulder && Shoulder->IsValidBodyInstance() ? Mesh->GetBoneLocation(Chain[Side][2]) : Hold->PointCm;
					HeldArmInChest[Side] = ChestNow.InverseTransformPositionNoScale(From + (Hold->PointCm - From).GetSafeNormal() * Limits.ArmPullInCm);
				}
				Target = ChestNow.TransformPositionNoScale(HeldArmInChest[Side]);
				GetUpView.ArmPullS += DeltaTime;
			}
			// The placed point follows the answer's point over arm_follow_s from where the hand was when the hold began, so that neither
			// the answer's start nor a change of answer (by state, as the body braces or not) moves the target in a step.
			if (HeldArmAnswer[Side] == 0)
			{
				const FBodyInstance* HandBody = Mesh->GetBodyInstance(Hands[Side]);
				HeldArmPlaced[Side] = HandBody && HandBody->IsValidBodyInstance() ? HandBody->GetCOMPosition() : Target;
			}
			const double Follow = Limits.ArmFollowS > 0.0 ? FMath::Min(DeltaTime / Limits.ArmFollowS, 1.0) : 1.0;
			HeldArmPlaced[Side] += (Target - HeldArmPlaced[Side]) * Follow;
			if (HeldArmAnswer[Side] == 0)
			{
				// Taken in over a pose's blend, then moved where it follows (the blend is not begun again each tick).
				PlaceArm(Hands[Side], HeldArmPlaced[Side], 1.0, Tuning.Poses.BlendS, false);
			}
			else if (USandboxAnimInstance* Animation = Cast<USandboxAnimInstance>(Mesh->GetAnimInstance()))
			{
				Animation->MoveLimbTarget(Mesh->GetBoneIndex(Hands[Side]), HeldArmPlaced[Side], nullptr, nullptr, 0.0, 0.0);
			}
		}
		HeldArmAnswer[Side] = Answer;
		HeldArmScale[Side] = Scale;
	}
	if (bChanged)
	{
		PushTuningToControls();
	}
}

void USandboxBodyController::GiveUpGetUp(ESandboxGiveUp Kind, const FString& Reason)
{
	if (State != ESandboxBodyState::GetUp)
	{
		return;
	}
	++GetUpView.GivenUpBy[static_cast<int32>(Kind)];
	GetUpView.GivenUp = Reason;
	// R10, step 8 in part: an attempt made for get_up and given up interrupts the command.
	InterruptCommand(TEXT("get-up given up"));
	// R10, step 5: a turned-back attempt ends here, lowered or not; its fall is watched for a second more, and an attempt given up afraid
	// is protected while it goes down.
	if (Kind == ESandboxGiveUp::Afraid || GetUpRun.bLowering)
	{
		if (BehaviourView.Lowerings.Num() > 0 && BehaviourView.Lowerings.Last().EndedS < 0.0)
		{
			BehaviourView.Lowerings.Last().EndedS = ElapsedS;
			BehaviourView.Lowerings.Last().Ended = Kind == ESandboxGiveUp::Afraid ? (GetUpRun.bLowering ? TEXT("lowered") : TEXT("at_once")) : Reason;
		}
		LoweringWatchUntilS = ElapsedS + 1.0;
		bAfraidGivenUp = Kind == ESandboxGiveUp::Afraid;
		GetUpRun.bLowering = false;
	}
	if (GetUpView.Starts.IsValidIndex(GetUpRun.StartIndex))
	{
		GetUpView.Starts[GetUpRun.StartIndex].EndedS = ElapsedS;
		GetUpView.Starts[GetUpRun.StartIndex].Ended = Reason;
	}
	// A roll given up is the roll's end too; the next roll goes the other way (StartRoll).
	if (GetUpRun.bRolling && GetUpView.Rolls.Num() > 0)
	{
		GetUpView.Rolls.Last().DurationS = ElapsedS - StateEnteredS;
		GetUpView.Rolls.Last().Ended = Reason;
	}
	// R10: an attempt given up afraid or for a hit on the head is no failure of its clip or its roll: the next attempt is not pushed off
	// them (found by the independent reading of 28 November: the matcher charged the clip that fit, and the next roll went the other way).
	const bool bClipFailed = Kind != ESandboxGiveUp::Afraid && Kind != ESandboxGiveUp::Protect;
	if (bClipFailed)
	{
		GivenUpRollWay = GetUpRun.bRolling || GetUpRun.bRolled ? (GetUpRun.bRollToFront ? 1 : 0) : INDEX_NONE;
	}
	GetUpRun.bRolling = false;
	// An attempt given up in its roll had no clip: the clip given up before it keeps its penalty.
	if (!GetUpRun.Path.IsEmpty() && bClipFailed)
	{
		GivenUpClip = GetUpRun.Path;
	}
	ReleaseClipPose();
	EnterState(ESandboxBodyState::Falling, FString::Printf(TEXT("the get-up is given up: %s"), *Reason));
}

void USandboxBodyController::UpdateGetUp(double DeltaTime)
{
	if (State != ESandboxBodyState::GetUp)
	{
		return;
	}
	if (GetUpRun.bRolling)
	{
		UpdateRoll(DeltaTime);
		return;
	}
	FAssist* Pelvis = Assists.FindByPredicate([](const FAssist& Assist) { return Assist.Slot == EAssistSlot::Pelvis; });
	const FBodyInstance* Body = Pelvis ? Mesh->GetBodyInstance(Pelvis->Bone) : nullptr;
	const FSandboxClipPose* Clip = Clips.Find(GetUpRun.Path);
	if (!Body || !Body->IsValidBodyInstance() || !Clip || ClipHold.Path != GetUpRun.Path)
	{
		GiveUpGetUp(ESandboxGiveUp::NoClip, TEXT("its clip is gone"));
		return;
	}
	if (bHeldUp)
	{
		// R10, step 5: a body lowered by its clip run back that touches nothing hangs on the world's pull near the floor at the lowering's
		// end (the clip's start laid a few centimetres up by the fit to the floor): it is let down there, as lowered. One a hand holds up
		// is held up, as any get-up.
		if (GetUpRun.bLowering && Holds.Num() == 0)
		{
			GiveUpGetUp(ESandboxGiveUp::Afraid, FString::Printf(TEXT("lowered back to %.2f s of its clip, where it hung on the world's pull"), ClipHold.TimeS));
			return;
		}
		GiveUpGetUp(ESandboxGiveUp::HeldUp, TEXT("the body is held up"));
		return;
	}
	// Every get-up is led (the clip-following path went with R2's rest): one whose carry could not be made is given up, and the
	// body stays down (decision 17 fails the run that asked for it).
	if (bCarryBroken)
	{
		// Counted here too: a carry that broke in a roll given up before its clip leaves the next attempt to be given up before
		// UpdateCarry sees it in this state.
		CountLed(false);
		bGetUpBroken = true;
		UE_LOG(LogSandbox, Error, TEXT("Body controller: the body cannot get up and stays down: %s"), *CarryBrokenReason);
		GiveUpGetUp(ESandboxGiveUp::NotLed, CarryBrokenReason);
		return;
	}
	const FSandboxGetUpTuning& Limits = Tuning.GetUp;
	// Led, the world pulls every body toward the clip, and what it gives it would give against a blow too: a hit that is felt this hard
	// takes the pull (getup.hit_by_impulse) or lets the body go at once. Each hit is judged once.
	const bool bLed = IsLed();
	if (bLed && Limits.CarryYieldNs > 0.0 && FlinchStartedS >= StateEnteredS && FlinchStartedS > CarryYieldSeenS)
	{
		CarryYieldSeenS = FlinchStartedS;
		// How far the blow moves the struck body from where the carry has it, for a second (read only, in every mode).
		HitWatchBone = FlinchBone;
		HitWatchUntilS = ElapsedS + 1.0;
		if (Limits.HitByImpulse > 0.5)
		{
			// Decision 20: the hit takes authority in proportion to its impulse, all of it at carry_yield_ns, and the parts of a blow
			// add up; it comes back by getup.hit_back_s (UpdateCarry). No attempt is given up for it: the blow moves the body, and
			// the get-up's own rules decide what follows (thrown off its clip by lost_cm, standing still too long).
			// With getup.hit_overflow 1, what a hit takes beyond the whole share is owed: the share stays at 0 until its clock has
			// given that much back (a blow of 200 N*s keeps the world off for 2.3 times hit_back_s). At most ten shares are owed.
			const double Floor = Limits.HitOverflow > 0.5 ? -10.0 : 0.0;
			HitFree = FMath::Max(HitFree - FlinchImpulseNs.Size() / Limits.CarryYieldNs, Floor);
			GetUpView.HitFreeMin = FMath::Min(GetUpView.HitFreeMin, FMath::Max(HitFree, 0.0));
			UE_LOG(LogSandbox, Display, TEXT("Body controller: a hit of %.1f N*s while led leaves %.2f of the world's pull (%.2f)"), FlinchImpulseNs.Size(), FMath::Max(HitFree, 0.0), HitFree);
		}
		else if (FlinchImpulseNs.Size() >= Limits.CarryYieldNs)
		{
			CarryShare = 0.0;
			Carry.Apply(CarrySettings(), 0.0);
			GiveUpGetUp(ESandboxGiveUp::Hit, FString::Printf(TEXT("hit with %.0f N*s while it was led"), FlinchImpulseNs.Size()));
			return;
		}
	}
	// The muscles and the assist come up from nothing (MuscleScale, PushTuningToControls).
	const double Ease = GetUpEase();
	if (!GetUpRun.bEased)
	{
		PushTuningToControls();
		const bool bHitOver = FlinchStartedS < StateEnteredS || ElapsedS - FlinchStartedS >= Tuning.Reactions.FlinchRecoveryS;
		// After a roll the assist's ease runs from the clip's start (CarryEase), after the muscles' (an independent reader's find:
		// pushed until the muscles were up, the assist of the clip-following path stayed where the roll had left it, at 0).
		// Without a roll the two are the same number.
		GetUpRun.bEased = Ease >= 1.0 && CarryEase() >= 1.0 && bHitOver;
	}

	// The pelvis's assist is aimed at where the clip has the pelvis now, and the feet's at where it has the feet:
	// a clip brings the feet under the body while the hands carry it, and feet that the floor's friction kept where
	// they were left the body leaning like a plank from feet half a metre behind it.
	FTransform Goal = Clip->PelvisAt(ClipHold.TimeS) * GetUpRun.Frame;
	Goal.SetScale3D(FVector::OneVector);
	// R2, step 10 (getup.fit_floor): the reference raised or lowered so that what the recording has on its floor lies on it with this
	// body's shapes, followed over fit_ease_s.
	if (Limits.FitFloor > 0.0)
	{
		const double Wanted = Limits.FitFloor * FitAtCm(ClipHold.TimeS);
		GetUpRun.FitCm = Limits.FitEaseS > 0.0 ? GetUpRun.FitCm + (Wanted - GetUpRun.FitCm) * FMath::Min(DeltaTime / Limits.FitEaseS, 1.0) : Wanted;
		Goal.AddToTranslation(FVector(0.0, 0.0, GetUpRun.FitCm));
		if (GetUpView.Starts.IsValidIndex(GetUpRun.StartIndex))
		{
			FSandboxGetUpStart& Start = GetUpView.Starts[GetUpRun.StartIndex];
			Start.FitMinCm = FMath::Min(Start.FitMinCm, GetUpRun.FitCm);
			Start.FitMaxCm = FMath::Max(Start.FitMaxCm, GetUpRun.FitCm);
		}
	}
	Pelvis->Target = Goal;
	SetAssistTarget(*Pelvis);
	for (FAssist& Assist : Assists)
	{
		FTransform InClip;
		if (Assist.Slot == EAssistSlot::Feet && Clip->BodyAt(Assist.Bone, ClipHold.TimeS, InClip))
		{
			Assist.Target = InClip * GetUpRun.Frame;
			if (Limits.FitFloor > 0.0)
			{
				Assist.Target.AddToTranslation(FVector(0.0, 0.0, GetUpRun.FitCm));
			}
			Assist.Target.SetScale3D(FVector::OneVector);
			SetAssistTarget(Assist);
		}
	}

	const FTransform Now = Body->GetUnrealWorldTransform();
	const double LagCm = FVector::Dist(Now.GetLocation(), Goal.GetLocation());
	if (LagCm > Limits.LostCm)
	{
		GiveUpGetUp(ESandboxGiveUp::Lost, FString::Printf(TEXT("the pelvis is %.0f cm from where the clip has it"), LagCm));
		return;
	}
	// R10, step 5 (behaviour.stay_down): turned back, the clip runs back to where the attempt began at lower_rate of its pace, led as it
	// rose (the body is lowered, not dropped), and the attempt is then given up. A hit acts on it as on any get-up above (decision 20, and
	// the lost pelvis); a press or a hold takes the world's pull away in proportion (UpdateCarry) but does not slow the clip's way back,
	// and a hold that drags gives it up (UpdateDrag). Bounded by getup.wait_s as a waiting clip is.
	if (GetUpRun.bLowering)
	{
		if (BehaviourView.Lowerings.Num() > 0 && Limits.WaitS > 0.0 && ElapsedS - BehaviourView.Lowerings.Last().DecidedS > Limits.WaitS)
		{
			GiveUpGetUp(ESandboxGiveUp::Afraid, FString::Printf(TEXT("lowered for %.1f s without reaching where it began"), ElapsedS - BehaviourView.Lowerings.Last().DecidedS));
			return;
		}
		// From the pace it had to its pace back, over lower_turn_s (the reference slows, stops and turns, never reversed in one frame).
		const double TurnShare = Tuning.Behaviour.LowerTurnS > 0.0 && BehaviourView.Lowerings.Num() > 0 ?
			FMath::SmoothStep(0.0, 1.0, FMath::Clamp((ElapsedS - BehaviourView.Lowerings.Last().DecidedS) / Tuning.Behaviour.LowerTurnS, 0.0, 1.0)) : 1.0;
		// The pace back is bounded so that the clip's pelvis comes down no faster than lower_max_mps (where the recording rose fast).
		double BackRate = Limits.Rate * Tuning.Behaviour.LowerRate;
		if (Tuning.Behaviour.LowerMaxMps > 0.0 && ClipHold.TimeS > 0.05)
		{
			const double ClipRiseCmPerS = (Clip->PelvisAt(ClipHold.TimeS).GetLocation().Z - Clip->PelvisAt(ClipHold.TimeS - 0.05).GetLocation().Z) / 0.05;
			if (ClipRiseCmPerS > 1.0)
			{
				BackRate = FMath::Min(BackRate, Tuning.Behaviour.LowerMaxMps * 100.0 / ClipRiseCmPerS);
			}
		}
		ClipHold.Rate = FMath::Lerp(GetUpRun.LowerFromRate, -BackRate, TurnShare);
		// Down to the clip's start, or until the chest is lower_end_cm over the floor (the trunk laid back down; the clip's first frames,
		// run back, swing the legs up and hold the body off the floor on the world's pull): from there the muscles take it down. (Ended by
		// the pelvis's height instead, a body let go kneeling dropped its trunk and its head met the floor at 2.0 to 2.9 m/s.)
		const FBodyInstance* ChestNow = Mesh->GetBodyInstance(Tuning.Rig.Chest);
		const double ChestUpCm = ChestNow && ChestNow->IsValidBodyInstance() ? ChestNow->GetCOMPosition().Z - GetUpFloorZ() : TNumericLimits<double>::Max();
		if (ClipHold.TimeS <= GetUpRun.LowerToS + KINDA_SMALL_NUMBER || (Tuning.Behaviour.LowerEndCm > 0.0 && ChestUpCm <= Tuning.Behaviour.LowerEndCm))
		{
			GiveUpGetUp(ESandboxGiveUp::Afraid, FString::Printf(TEXT("lowered back to %.2f s of its clip, the chest %.0f cm up, afraid (%.2f) with the player %.0f cm away"),
				ClipHold.TimeS, ChestUpCm, BehaviourView.Fear, BehaviourView.ViewerLastCm));
		}
		return;
	}
	// How far each foot is from where the clip has it (for the log and the reasons; a led body is brought to the clip, feet and all,
	// and the clip is not laid over where the body happens to be).
	double FootLagCm[2] = { 0.0, 0.0 };
	for (const FAssist& Assist : Assists)
	{
		const FBodyInstance* Foot = Assist.Slot == EAssistSlot::Feet && Assist.Side >= 0 ? Mesh->GetBodyInstance(Assist.Bone) : nullptr;
		if (Foot && Foot->IsValidBodyInstance())
		{
			FootLagCm[Assist.Side] = (Foot->GetUnrealWorldTransform().GetLocation() - Assist.Target.GetLocation()).Size();
		}
	}
	// What presses on the body is the force felt in every frame of the last moments from other simulated bodies, as for a held push
	// (FeelFrame). Pressed down harder than it will push against (getup.hold_force_n), the body does not go on: its clip waits,
	// below, and the attempt is given up when it has waited too long.
	const double PressedN = SustainedN.Size();
	// Decision 14 (getup.hold_by_body): led, the clip's pace goes down in proportion to the press and the holds together, to a stop at
	// hold_force_n; down at once, back by the release's clock, so that a hand that pulls is felt in the frame it pulls in.
	const bool bPaceByForce = bLed && Limits.HoldByBody > 0.5;
	const double PaceForceN = bPaceByForce ? HoldPaceForceN() : 0.0;
	if (bPaceByForce)
	{
		const double Span = Limits.HoldForceN - Limits.HoldSlowFromN;
		const double Wanted = Limits.HoldForceN > 0.0 && Span > 0.0 ? 1.0 - FMath::Clamp((PaceForceN - Limits.HoldSlowFromN) / Span, 0.0, 1.0) : 1.0;
		HoldPace = Wanted < HoldPace || Limits.CarryReleaseS <= 0.0 ? Wanted : FMath::Min(Wanted, HoldPace + DeltaTime / Limits.CarryReleaseS);
		GetUpView.PaceMin = FMath::Min(GetUpView.PaceMin, HoldPace);
	}
	// A press of hold_force_n holds the body down whatever share of it slows the clip (getup.hold_press_pace), as before.
	const bool bHeldDown = bPaceByForce
		? Limits.HoldForceN > 0.0 && (HoldPace <= 0.0 || SustainedContactN.Size() >= Limits.HoldForceN)
		: Limits.HoldForceN > 0.0 && PressedN >= Limits.HoldForceN;
	GetUpView.PressedMaxN = FMath::Max(GetUpView.PressedMaxN, PressedN);
	// How long and how hard a hand held the body while it got up (read only).
	if (Holds.Num() > 0)
	{
		GetUpView.HeldS += DeltaTime;
		for (const FSandboxBodyHold& Hold : Holds)
		{
			GetUpView.HoldMaxN = FMath::Max(GetUpView.HoldMaxN, Hold.ForceN);
		}
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
		if (bLed && Carry.GetShare() > 0.0)
		{
			FVector NetN;
			const double SumN = Carry.LoadN(CarrySettings(), Carry.GetShare(), &NetN);
			UE_LOG(LogSandbox, Display, TEXT("Body controller: led at %.2f s: the world gives %.0f N in all, net (%.0f, %.0f, %.0f) of a weight of %.0f N; hardest: %s"),
				ElapsedS, SumN, NetN.X, NetN.Y, NetN.Z, WeightN, *Carry.Describe(CarrySettings(), Carry.GetShare(), 4));
		}
	}
	// Led, the clip runs at its own rate and stops at the time by which it stands: the body is brought along, and a body that cannot
	// be (something is in the way) is told by the pelvis's distance above. Only a press that holds it down stops the clip.
	const bool bBehind = bHeldDown;
	const bool bThrough = ClipHold.TimeS >= FMath::Min(Limits.EndS, Clip->GetLengthS()) - KINDA_SMALL_NUMBER;
	ClipHold.Rate = Ease < 1.0 || bBehind || bThrough ? 0.0 : bPaceByForce ? Limits.Rate * HoldPace : Limits.Rate;
	// R10, step 8 in part: a lowering ended by get_up turns up again over lower_turn_s; a clip held still (pressed down, through its end)
	// stays still, and the way back never goes below where the attempt began (the independent reading of the fixes).
	if (GetUpRun.RaiseFromS >= 0.0)
	{
		if (ClipHold.Rate <= 0.0)
		{
			GetUpRun.RaiseFromS = -1.0;
		}
		else
		{
			const double RaiseShare = Tuning.Behaviour.LowerTurnS > 0.0 ? FMath::Clamp((ElapsedS - GetUpRun.RaiseFromS) / Tuning.Behaviour.LowerTurnS, 0.0, 1.0) : 1.0;
			ClipHold.Rate = FMath::Lerp(GetUpRun.RaiseFromRate, ClipHold.Rate, FMath::SmoothStep(0.0, 1.0, RaiseShare));
			if (ClipHold.Rate < 0.0 && ClipHold.TimeS <= GetUpRun.LowerToS + KINDA_SMALL_NUMBER)
			{
				ClipHold.Rate = 0.0;
			}
			if (RaiseShare >= 1.0)
			{
				GetUpRun.RaiseFromS = -1.0;
			}
		}
	}
	if (FMath::Abs(ClipHold.Rate) < 0.1 * Limits.Rate)
	{
		GetUpView.PausedS += DeltaTime;
		(Ease < 1.0 ? GetUpView.PausedEaseS : bThrough ? GetUpView.PausedEndS : GetUpView.PausedBehindS) += DeltaTime;
	}
	// A clip slowed by a hold to under a tenth of its pace (getup.hold_by_body) reads as a pause and is bounded as one: held this
	// slow for stall_s, the attempt is given up as one held down. (Without it a pace just above 0 crept on for tens of seconds,
	// neither stalled nor waiting.)
	const bool bCrawling = bPaceByForce && !bThrough && Ease >= 1.0 && HoldPace < 0.1;
	if (Ease >= 1.0 && (bBehind || bCrawling) && !bThrough)
	{
		GetUpView.BehindHeldS += bHeldDown || bCrawling ? DeltaTime : 0.0;
	}
	if (Ease >= 1.0 && (bBehind || bThrough || bCrawling))
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
	if (bThrough)
	{
		const FVector Up = (Now.GetRotation() * Goal.GetRotation().Inverse()).RotateVector(FVector::UpVector);
		const double TiltDeg = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(Up.Z, -1.0, 1.0)));
		const bool bOnFeet = Balance.bValid && Balance.FootForceN[0] > 0.0 && Balance.FootForceN[1] > 0.0;
		if (!bBehind && TiltDeg <= Limits.UprightDeg && bOnFeet)
		{
			++GetUpView.Successes;
			GetUpView.DurationS = ElapsedS - GetUpView.StartedS;
			if (GetUpView.Starts.IsValidIndex(GetUpRun.StartIndex))
			{
				GetUpView.Starts[GetUpRun.StartIndex].EndedS = ElapsedS;
				GetUpView.Starts[GetUpRun.StartIndex].Ended = TEXT("stood");
			}
			GivenUpClip.Reset();
			GivenUpRollWay = INDEX_NONE;
			ReleaseClipPose();
			EnterState(ESandboxBodyState::Standing, FString::Printf(TEXT("got up in %.2f s"), GetUpView.DurationS));
			return;
		}
	}
	if (GetUpRun.StallS > Limits.StallS)
	{
		if (bHeldDown || bCrawling)
		{
			GiveUpGetUp(ESandboxGiveUp::HeldDown, FString::Printf(TEXT("held down with %.0f N for %.1f s, at %.2f s of the clip"), bPaceByForce ? FMath::Max(PaceForceN, SustainedContactN.Size()) : PressedN, GetUpRun.StallS, ClipHold.TimeS));
			return;
		}
		GiveUpGetUp(ESandboxGiveUp::Waited, FString::Printf(TEXT("the clip stood at %.2f s for %.1f s waiting for the body (pelvis %.0f cm, feet %.0f and %.0f cm from it)"),
			ClipHold.TimeS, GetUpRun.StallS, LagCm, FootLagCm[0], FootLagCm[1]));
		return;
	}
	// A clip that moves on by a frame now and then never stands still that long, and the body holds one pose for as
	// long as it does (13 s, with its back under a table's edge). All its waiting with nothing pressing on the body is
	// bounded too (getup.wait_s); a body that a hand slows is not hurried by this.
	if (Limits.WaitS > 0.0 && GetUpRun.FreeWaitS > Limits.WaitS)
	{
		GiveUpGetUp(ESandboxGiveUp::Waited, FString::Printf(TEXT("the clip waited %.1f s in all for the body and is at %.2f s (pelvis %.0f cm, feet %.0f and %.0f cm from it)"),
			GetUpRun.FreeWaitS, ClipHold.TimeS, LagCm, FootLagCm[0], FootLagCm[1]));
	}
}

FSandboxCarrySettings USandboxBodyController::CarrySettings() const
{
	FSandboxCarrySettings Settings;
	Settings.LinearNPerMPerKg = Tuning.GetUp.CarryNPerMPerKg;
	Settings.AngularNmPerRadPerKg = Tuning.GetUp.CarryNmPerRadPerKg;
	Settings.DampingS = Tuning.GetUp.CarryDampingS;
	Settings.MaxWeights = Tuning.GetUp.CarryMaxWeights;
	Settings.MaxUpWeights = Tuning.GetUp.CarryMaxUpWeights;
	Settings.MaxNmPerKg = Tuning.GetUp.CarryMaxNmPerKg;
	// The caps stay whole under a press (CapShare 1); only the springs' share goes down with it. With the caps taken down as well,
	// 2 bodies of 50 stood under a press of 300 N (15 without, 13 before the led get-up) and one hand still did not hold.
	return Settings;
}

void USandboxBodyController::CountLed(bool bLed)
{
	// Decision 17: every attempt that asked for the led get-up is counted, once, as led or not, with the reason. Read only.
	if (!GetUpRun.bCarryAsked || GetUpRun.bLedCounted)
	{
		return;
	}
	GetUpRun.bLedCounted = true;
	if (bLed)
	{
		++GetUpView.LedAttempts;
		return;
	}
	++GetUpView.NotLedAttempts;
	GetUpView.NotLed = CarryBrokenReason;
	UE_LOG(LogSandbox, Warning, TEXT("Body controller: the get-up at %.2f s cannot be led: %s; it is given up and the body stays down"),
		ElapsedS, *GetUpView.NotLed);
}

void USandboxBodyController::UpdateCarry(double DeltaTime)
{
	bool bLed = IsLed();
	if (bLed && !Carry.IsCreated())
	{
		FString Error;
		if (!Carry.Create(Control, Mesh, Error))
		{
			bCarryBroken = true;
			CarryBrokenReason = FString::Printf(TEXT("the carry's springs could not be made: %s"), *Error);
			UE_LOG(LogSandbox, Warning, TEXT("Body controller: the get-up is not led: %s"), *Error);
			bLed = false;
		}
	}
	if (State == ESandboxBodyState::GetUp)
	{
		CountLed(bLed);
	}
	if (!bLed && CarryShare <= 0.0)
	{
		// Nothing is carried and nothing fades: a run that never got up does nothing here.
		bCarryAimed = false;
		return;
	}
	if (bCarryBroken)
	{
		return;
	}
	const FSandboxClipPose* Clip = bLed ? Clips.Find(GetUpRun.Path) : nullptr;
	if (Clip)
	{
		if (!bCarryAimed)
		{
			Carry.ForgetTargets();
		}
		// Decision 22 (getup.start_blend_s): from where each body lay to where the clip has it, by the clip layer's own cross.
		const double StartOver = GetUpRun.bStartBlend && ClipHold.CrossShare < 1.0 ? FMath::SmoothStep(0.0, 1.0, ClipHold.CrossShare) : 1.0;
		for (int32 Index = 0; Index < Carry.Num(); ++Index)
		{
			FTransform InClip;
			if (Clip->ReachedBodyAt(Carry.BoneAt(Index), ClipHold.TimeS, InClip))
			{
				FTransform Goal = InClip * GetUpRun.Frame;
				if (Tuning.GetUp.FitFloor > 0.0)
				{
					Goal.AddToTranslation(FVector(0.0, 0.0, GetUpRun.FitCm));
				}
				const FTransform* Lay = StartOver < 1.0 ? GetUpRun.StartBodies.Find(Carry.BoneAt(Index)) : nullptr;
				if (Lay)
				{
					Goal = FTransform(FQuat::Slerp(Lay->GetRotation(), Goal.GetRotation(), StartOver).GetNormalized(),
						FMath::Lerp(Lay->GetLocation(), Goal.GetLocation(), StartOver));
				}
				Carry.SetTarget(Index, Goal, DeltaTime);
			}
		}
		// C27 of the reference itself, as the body's own is measured (MeasureGetUpShape): read only.
		if (GetUpView.RiseClipTrunkDeg < 0.0)
		{
			FTransform Pelvis;
			FTransform Chest;
			FTransform Feet[2];
			if (Clip->ReachedBodyAt(Tuning.Rig.Pelvis, ClipHold.TimeS, Pelvis) && Clip->ReachedBodyAt(Tuning.Rig.Chest, ClipHold.TimeS, Chest) &&
				Clip->ReachedBodyAt(Tuning.Rig.FootLeft, ClipHold.TimeS, Feet[0]) && Clip->ReachedBodyAt(Tuning.Rig.FootRight, ClipHold.TimeS, Feet[1]))
			{
				const FVector PelvisCm = (Pelvis * GetUpRun.Frame).GetLocation();
				const double FitCm = Tuning.GetUp.FitFloor > 0.0 ? GetUpRun.FitCm : 0.0;
				if (PelvisCm.Z + FitCm - GetUpFloorZ() >= 70.0)
				{
					const FVector Trunk = (Chest * GetUpRun.Frame).GetLocation() - PelvisCm;
					GetUpView.RiseClipTrunkDeg = FMath::RadiansToDegrees(FMath::Atan2(Trunk.Size2D(), Trunk.Z));
					GetUpView.RiseClipFeetCm = FMath::Max(FVector::Dist2D((Feet[0] * GetUpRun.Frame).GetLocation(), PelvisCm),
						FVector::Dist2D((Feet[1] * GetUpRun.Frame).GetLocation(), PelvisCm));
				}
			}
		}
		// Up with the get-up's ease (after a roll, from when the clip began); a share left from a get-up that has just ended is not
		// taken down by a new one.
		CarryShare = FMath::Max(CarryShare, CarryEase());
	}
	else
	{
		// Down by its own clock, the last targets kept: let go in one step, the body would drop what the world still held.
		if (bCarryAimed)
		{
			Carry.ForgetTargets();
		}
		// R10, step 5: after an attempt given up afraid the world's pull goes over behaviour.lower_release_s, so that a body lowered to a
		// pose it began from sitting sinks the rest of the way instead of dropping (exact otherwise: the get-up's own release).
		// (Not after a hit on the head, behaviour.protect: a hit takes authority away, and the get-up's own release is kept; the independent
		// reading's find.)
		const double ReleaseS = bAfraidGivenUp && Tuning.Behaviour.LowerReleaseS > 0.0 ? Tuning.Behaviour.LowerReleaseS : Tuning.GetUp.CarryReleaseS;
		CarryShare = ReleaseS > 0.0 ? FMath::Max(CarryShare - DeltaTime / ReleaseS, 0.0) : 0.0;
	}
	bCarryAimed = Clip != nullptr;
	// A press or a hold from outside takes the world's pull away, in proportion: what a hand does to a body getting up is for the
	// body's own muscles to answer. At the force that holds a get-up down (getup.hold_force_n) nothing is left of it. It goes at
	// once and comes back by the release's clock. (Without this, one hand that held the chest was torn off and two that pressed
	// did not slow the body: the world got it up against them.)
	//
	// With getup.hold_by_body (decision 14) a hold is not part of that press: it takes the springs off its own chain instead, the held
	// body and the bodies between it and the pelvis, at once; the rest of the body is led as before.
	const FSandboxGetUpTuning& Limits = Tuning.GetUp;
	const bool bByBody = Limits.HoldByBody > 0.5;
	const FVector& PressN = bByBody ? SustainedContactN : SustainedN;
	const double Pressed = Limits.HoldForceN > 0.0 ? FMath::Clamp(PressN.Size() / Limits.HoldForceN, 0.0, 1.0) : 0.0;
	const double Free = 1.0 - Pressed;
	// With the hold's or the hit's rule, nothing of the pull comes back once the attempt has ended and the share only goes down (a
	// hand still holding a body that gave up, or a blow that felled it, was pulled at again as the shares recovered over the fade;
	// found in the review of the whole night, 27 November 2026).
	const bool bFading = !bLed && (bByBody || Limits.HitByImpulse > 0.5);
	CarryFree = Free < CarryFree || Limits.CarryReleaseS <= 0.0 ? Free : bFading ? CarryFree : FMath::Min(Free, CarryFree + DeltaTime / Limits.CarryReleaseS);
	// How far a blow moves the struck body from where the carry has it, for a second after it (read only, in every mode).
	if (Clip && State == ESandboxBodyState::GetUp && HitWatchUntilS >= ElapsedS)
	{
		const int32 Struck = Carry.IndexOf(HitWatchBone);
		FVector TargetComCm;
		FVector ComCm;
		if (Struck != INDEX_NONE && Carry.ComOf(Struck, TargetComCm, ComCm))
		{
			GetUpView.HitOffCm = FMath::Max(GetUpView.HitOffCm, FVector::Dist(TargetComCm, ComCm));
		}
	}
	// Decision 20 (getup.hit_by_impulse): what the hits left of the share, which then comes back by its own clock.
	double Share = FMath::SmoothStep(0.0, 1.0, CarryShare) * CarryFree;
	if (Limits.HitByImpulse > 0.5)
	{
		Share *= FMath::Clamp(HitFree, 0.0, 1.0);
		if (!bFading)
		{
			HitFree = Limits.HitBackS > 0.0 ? FMath::Min(HitFree + DeltaTime / Limits.HitBackS, 1.0) : 1.0;
		}
	}
	// R2, step 10 (getup.fit_free_z): a body the clip has on its floor that touches the world is carried by the floor, not pulled up or
	// down by the world.
	TArray<double> Verticals;
	if (Limits.FitFreeZ > 0.5)
	{
		// Without a clip (the fade after an attempt) every body has its vertical spring back.
		if (Clip)
		{
			FreeVerticals(Verticals);
		}
		else
		{
			Verticals.Init(1.0, Carry.Num());
		}
	}
	if (!bByBody)
	{
		Carry.Apply(CarrySettings(), Share, TConstArrayView<double>(), Verticals);
		return;
	}
	if (CarryBodyFree.Num() != Carry.Num())
	{
		CarryBodyFree.Init(1.0, Carry.Num());
	}
	TArray<double, TInlineAllocator<32>> Held;
	Held.Init(0.0, Carry.Num());
	double HeldN = 0.0;
	// Counted while any pull is left, the fade after an attempt included.
	for (const FSandboxBodyHold& Hold : Holds)
	{
		const int32 Index = Carry.IndexOf(Hold.Bone);
		if (Index != INDEX_NONE && Limits.HoldChainFullN > 0.0)
		{
			FSandboxCarry::ChainHolds(Carry.GetParents(), Index, FMath::RoundToInt32(Limits.HoldChain), Limits.HoldChainFalloff,
				Hold.ForceN / Limits.HoldChainFullN, Held);
			HeldN += Hold.ForceN;
		}
	}
	// What every body keeps at most, in a chain or not (getup.hold_rest_full_n; 0: the rest is led whole).
	const double RestKept = Limits.HoldRestFullN > 0.0 ? 1.0 - FMath::Clamp(HeldN / Limits.HoldRestFullN, 0.0, 1.0) : 1.0;
	for (int32 Index = 0; Index < Carry.Num(); ++Index)
	{
		const double Wanted = FMath::Min(1.0 - (1.0 - Limits.HoldChainShare) * Held[Index], RestKept);
		double& Kept = CarryBodyFree[Index];
		Kept = Wanted < Kept || Limits.CarryReleaseS <= 0.0 ? Wanted : bFading ? Kept : FMath::Min(Wanted, Kept + DeltaTime / Limits.CarryReleaseS);
	}
	Carry.Apply(CarrySettings(), Share, CarryBodyFree, Verticals);
}

double USandboxBodyController::HoldPaceForceN() const
{
	// The press from contacts and what the hands pull with (decision 14, getup.hold_sum): their sum as vectors, as SustainedN adds them,
	// or the sum of their sizes, so that two hands pulling apart count twice.
	// The press counts at getup.hold_press_pace of its force (1: as a hold).
	const double PressShare = Tuning.GetUp.HoldPressPace;
	if (Tuning.GetUp.HoldSum > 0.5)
	{
		double SizeN = SustainedContactN.Size() * PressShare;
		for (const FSandboxBodyHold& Hold : Holds)
		{
			SizeN += Hold.ForceN;
		}
		return SizeN;
	}
	FVector SumN = SustainedContactN * PressShare;
	for (const FSandboxBodyHold& Hold : Holds)
	{
		SumN += Hold.PullN;
	}
	return SumN.Size();
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
	// R10, step 8 in part: a get_up in force is done when the body stands from its get-up.
	if (bGetUpCommanded && NewState == ESandboxBodyState::Standing && OldState == ESandboxBodyState::GetUp)
	{
		EndCommand(TEXT("done"));
	}
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
		// R3's measures of the fall (read only): what reached it while an arm reached for the floor, and how many arms caught.
		FallRun.HitsWhileCatching = 0;
		FallRun.bHeldWhileCatching = false;
		FallRun.LastHitSeenS = LastHitS;
		FallRun.ArmsCaught = 0;
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
		if (Frame == EFrame::Stance && State == ESandboxBodyState::Stagger)
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
	PlaceFoot(Side, EPlacement::Swing, Swing.Point, EFrame::Actual, Limits.StepEaseS, nullptr, Limits.LiftSmooth > 0.5);
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
	// The swinging foot's assist follows the swing's point, moving: the world gives it no pull (PushTuningToControls), only its turn.
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
		// R3: a hand or a forearm that meets the world while the body falls (kept apart from N6's, which only its reach reads).
		if (Group == ESandboxMuscleGroup::Arms && FallRun.bActive)
		{
			const FString Bone = Hit.MyBoneName.ToString();
			if (Bone.StartsWith(TEXT("hand")) || Bone.StartsWith(TEXT("lowerarm")))
			{
				FallRun.bTouchFrame[Bone.EndsWith(TEXT("_r")) ? 1 : 0] = true;
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
			FearContactNs = 0.0;
		}
		EventFeltS = ElapsedS;
		EventHitNs += HitNs;
		// R10, step 4 (behaviour.fear): a contact that is a hit (as large as makes a standing body flinch) raises fear by its whole hit,
		// counted once as its sum grows. The static world never reaches here (OnBodyHit), so the body's own landing is no such hit.
		if (Tuning.Behaviour.Fear > 0.5 && EventHitNs.Size() >= Tuning.Thresholds.FlinchImpulseNs && EventHitNs.Size() > FearContactNs)
		{
			const double NewNs = EventHitNs.Size() - FearContactNs;
			BehaviourView.FearHits += FearContactNs <= 0.0 ? 1 : 0;
			FearContactNs = EventHitNs.Size();
			BehaviourView.FearHitNs += NewNs;
			AddFear(NewNs * Tuning.Behaviour.FearPerNs, true);
		}
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
			bHeadCoverCounted = false;
		}
		if ((HitNs | Felt) > 0.0 && FSandboxBodyTuning::GroupOfBone(FrameBone) == ESandboxMuscleGroup::Neck)
		{
			HeadFeltS = ElapsedS;
			HeadHitNs += HitNs.Size();
			MaxHeadHitNs = FMath::Max(MaxHeadHitNs, HeadHitNs);
			// R10, step 6 in part (behaviour.protect; decision 45): a hit on the head of protect_from_head_ns or more in one contact, while
			// the body lies or gets up (or goes down from an attempt it gave up so), gives the attempt up and covers the head.
			const bool bDownOrRising = State == ESandboxBodyState::Lying || State == ESandboxBodyState::GetUp ||
				(State == ESandboxBodyState::Falling && (bProtectGivenUp || bAfraidGivenUp));
			if (Tuning.Behaviour.Protect > 0.5 && !bHeadCoverCounted && bDownOrRising && HeadHitNs >= Tuning.Behaviour.ProtectFromHeadNs)
			{
				bHeadCoverCounted = true;
				StartCover(ECoverPlace::Head, HeadHitNs, FrameBone);
			}
			const bool bUpright = State == ESandboxBodyState::Standing || State == ESandboxBodyState::Flinch || State == ESandboxBodyState::Stagger;
			if (Reactions.KnockdownHeadNs > 0.0 && !bHeadFelled && bUpright && HeadHitNs >= Reactions.KnockdownHeadNs)
			{
				bHeadFelled = true;
				++Knockdowns;
				EnterState(ESandboxBodyState::Falling, FString::Printf(TEXT("knocked down by a hit of %.1f N*s on the head"), HeadHitNs));
			}
		}

		// R10, step 6 (behaviour.protect; decision 45): the trunk, the groin (the pelvis struck from the front) and the legs, each a sum of
		// one contact as the head's is, from protect_from_ns; while lying or getting up the struck place is covered.
		if (Tuning.Behaviour.Protect > 0.5)
		{
			static const FName PelvisName(TEXT("pelvis"));
			for (int32 Place = 0; Place < 3; ++Place)
			{
				if (ElapsedS - PlaceFeltS[Place] > Tuning.Thresholds.FlinchWindowS)
				{
					PlaceHitNs[Place] = 0.0;
					bPlaceCounted[Place] = false;
				}
			}
			const ESandboxMuscleGroup Group = FSandboxBodyTuning::GroupOfBone(FrameBone);
			const int32 Place = FrameBone == PelvisName ? (IsFromFront(HitNs) ? 1 : 0) : Group == ESandboxMuscleGroup::Spine ? 0 :
				Group == ESandboxMuscleGroup::Legs ? 2 : INDEX_NONE;
			if (Place != INDEX_NONE && (HitNs | Felt) > 0.0)
			{
				PlaceFeltS[Place] = ElapsedS;
				PlaceHitNs[Place] += HitNs.Size();
				const bool bDownOrRising = State == ESandboxBodyState::Lying || State == ESandboxBodyState::GetUp ||
					(State == ESandboxBodyState::Falling && (bProtectGivenUp || bAfraidGivenUp));
				if (!bPlaceCounted[Place] && bDownOrRising && PlaceHitNs[Place] >= Tuning.Behaviour.ProtectFromNs)
				{
					bPlaceCounted[Place] = true;
					StartCover(Place == 0 ? ECoverPlace::Trunk : Place == 1 ? ECoverPlace::Groin : ECoverPlace::Leg, PlaceHitNs[Place], FrameBone);
				}
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
			RecentContactN.Init(FVector::ZeroVector, Frames);
			RecentNext = 0;
		}
		RecentForceN[RecentNext] = Felt / DeltaTime;
		RecentMomentNCm[RecentNext] = FrameMomentNsCm / DeltaTime;
		// The press from contacts alone, apart from the holds (read only; getup.hold_by_body takes it).
		RecentContactN[RecentNext] = Felt / DeltaTime;
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
		int32 LeastContact = 0;
		for (int32 Index = 1; Index < Frames; ++Index)
		{
			if (RecentForceN[Index].SizeSquared() < RecentForceN[Least].SizeSquared())
			{
				Least = Index;
			}
			if (RecentContactN[Index].SizeSquared() < RecentContactN[LeastContact].SizeSquared())
			{
				LeastContact = Index;
			}
		}
		SustainedN = RecentForceN[Least];
		SustainedMomentNCm = RecentMomentNCm[Least];
		SustainedContactN = RecentContactN[LeastContact];
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
	// R10, step 8 in part: a get_up in force ends when the body can no longer get up (its get-up broken where the attempt was given up),
	// or when commands are switched off.
	if (bGetUpCommanded && (bGetUpBroken || Tuning.Behaviour.Commands <= 0.5))
	{
		EndCommand(bGetUpBroken ? TEXT("given up: unable") : TEXT("ended: commands switched off"));
	}
	if (State == ESandboxBodyState::Lying)
	{
		// R10, step 8 in part: told to get up, before the first attempt for the command it waits behaviour.command_react_s from the command
		// (C39) and lies still for perception.react_s (begun while it still moved after its cover was let go, the largest target step went from
		// 44 to 99 degrees); after an attempt given up it rests as always (with the short rest every time, a body by the chair tried seven
		// times in quick succession and never stood: the stand of 150 with a command, 28 November).
		LyingS = IsAtRest() && !bHeldUp ? LyingS + DeltaTime : 0.0;
		const bool bQuickRest = bGetUpCommanded && BehaviourView.Commands.IsValidIndex(CommandRecord) && BehaviourView.Commands[CommandRecord].FirstMotionS < 0.0;
		const bool bRested = bQuickRest
			? LyingS >= FMath::Min(Tuning.Perception.ReactS, GetUpRestS()) - KINDA_SMALL_NUMBER &&
				ElapsedS - BehaviourView.Commands[CommandRecord].TellS >= Tuning.Behaviour.CommandReactS - KINDA_SMALL_NUMBER
			: LyingS >= GetUpRestS() - KINDA_SMALL_NUMBER;
		FString Why;
		if (bGetUpAllowed && !bGetUpBroken && !IsPosedFromOutside() && Tuning.GetUp.RestS > 0.0 && bRested &&
			!CanBeginGetUp(Why))
		{
			// Held back (decision 18; R10 adds its reasons in CanBeginGetUp): it begins as soon as it may, its rest already lain.
			if (HeldBackWhy.IsEmpty())
			{
				UE_LOG(LogSandbox, Display, TEXT("Body controller: at %.2f s the get-up waits: %s"), ElapsedS, *Why);
			}
			HeldBackWhy = Why;
		}
		else if (bGetUpAllowed && !bGetUpBroken && !IsPosedFromOutside() && Tuning.GetUp.RestS > 0.0 && bRested)
		{
			// R10, C33 (read only, whenever fear is on): an attempt begun while afraid with the player near (not one it was told to make).
			if (Tuning.Behaviour.Fear > 0.5 && IsAfraid() && bViewerNear && !bGetUpCommanded)
			{
				++BehaviourView.BegunNearAfraid;
			}
			HeldBackWhy.Reset();
			LyingS = 0.0;
			FString Error;
			if (!StartGetUp(Error))
			{
				bGetUpBroken = true;
				UE_LOG(LogSandbox, Error, TEXT("Body controller: the body cannot get up and stays down: %s"), *Error);
				if (bGetUpCommanded)
				{
					EndCommand(TEXT("given up: unable"));
				}
			}
			else if (bGetUpCommanded && BehaviourView.Commands.IsValidIndex(CommandRecord) && BehaviourView.Commands[CommandRecord].FirstMotionS < 0.0)
			{
				// C39: the first motion for the command.
				BehaviourView.Commands[CommandRecord].FirstMotionS = ElapsedS;
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
		// R3, item a (fall.arm_curve): falling, the arms' tone is a curve of the sensed time to impact: loose far from the floor, braced as
		// it comes, and giving way once a hand, a forearm or the trunk has met the world.
		else if (FallRun.bActive && Tuning.FallAim.ArmCurve > 0.5 && Group == static_cast<int32>(ESandboxMuscleGroup::Arms))
		{
			const FSandboxFallAimTuning& Fall = Tuning.FallAim;
			const bool bTouched = FallRun.TouchS[0] >= 0.0 || FallRun.TouchS[1] >= 0.0 || TrunkTouchedS >= 0.0;
			const bool bNear = FallSense.bValid && FallSense.TimeToImpactS <= Fall.ArmBraceFromS;
			GoalStiffness = bTouched ? Fall.ArmTouchStiffness : bNear ? Fall.ArmBraceStiffness : Fall.ArmFarStiffness;
			GoalDampingNow = bTouched ? Fall.ArmTouchDamping : bNear ? Fall.ArmBraceDamping : Fall.ArmFarDamping;
		}
		// R3, item e (fall.sit): sitting down going backward, the legs give under the weight as the sit comes in.
		if (FallRun.bActive && Tuning.FallAim.Sit > 0.5 && FallRun.SitShare > 0.0 && Group == static_cast<int32>(ESandboxMuscleGroup::Legs))
		{
			GoalStiffness = FMath::Lerp(GoalStiffness, Tuning.FallAim.SitLegStiffness, FMath::SmoothStep(0.0, 1.0, FallRun.SitShare));
		}
		// R3, item c (fall.head): falling, the neck is braced.
		if (FallRun.bActive && Tuning.FallAim.Head > 0.5 && Group == static_cast<int32>(ESandboxMuscleGroup::Neck))
		{
			GoalStiffness = Tuning.FallAim.HeadStiffness;
			GoalDampingNow = Tuning.FallAim.HeadDamping;
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
			// R10, step 4 (behaviour.fear): each threat seen raises fear.
			if (Tuning.Behaviour.Fear > 0.5)
			{
				AddFear(Tuning.Behaviour.FearPerThreat, false);
			}
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

double USandboxBodyController::NearestViewerCm() const
{
	// Over the floor, to the nearest body: a player who stands 60 cm from a body that lies is 60 cm from it, whatever the head's height.
	double BestCm = -1.0;
	for (const FSandboxSeen& Thing : Seen)
	{
		if (Thing.Kind != ESandboxSeenKind::Viewer || !Mesh)
		{
			continue;
		}
		for (const FBodyInstance* Body : Mesh->Bodies)
		{
			if (Body && Body->IsValidBodyInstance())
			{
				const double DistanceCm = FVector::Dist2D(Thing.LocationCm, Body->GetCOMPosition());
				BestCm = BestCm < 0.0 ? DistanceCm : FMath::Min(BestCm, DistanceCm);
			}
		}
	}
	return BestCm;
}

void USandboxBodyController::MeasureViewer()
{
	// Read only: what it was told and where its bodies are.
	const double ViewerCm = NearestViewerCm();
	BehaviourView.ViewerLastCm = ViewerCm;
	if (ViewerCm < 0.0)
	{
		return;
	}
	++BehaviourView.ViewerFrames;
	BehaviourView.ViewerMinCm = BehaviourView.ViewerMinCm < 0.0 ? ViewerCm : FMath::Min(BehaviourView.ViewerMinCm, ViewerCm);
	if (ElapsedS >= ViewerSeriesNextS - KINDA_SMALL_NUMBER)
	{
		BehaviourView.ViewerSeries.Add(FVector2D(ElapsedS, ViewerCm));
		ViewerSeriesNextS = ElapsedS + 0.25;
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
		// Straight above or below the yaw means nothing: the head keeps the yaw it has. Behind the shoulders the angle meets its seam
		// at 180 degrees: a target there is taken whichever way round the head is nearer to, or one straight behind would swing the
		// head from one limit to the other as it crossed the seam (found in the review of 27 November 2026).
		const double YawDeg = FSandboxBodyProbe::GazeYawDeg(FMath::RadiansToDegrees(FMath::Atan2(-Way.X, Way.Y)), GazeYawDeg, Look.GazeHeadShare);
		GoalYawDeg = FVector2D(Way.X, Way.Y).Size() > 0.2 ? YawDeg * Look.GazeHeadShare : GazeYawDeg;
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

	const double FallShare = FallEaseShare();
	static const FName HeldArmBones[2][3] = { { FName(TEXT("hand_l")), FName(TEXT("lowerarm_l")), FName(TEXT("upperarm_l")) },
		{ FName(TEXT("hand_r")), FName(TEXT("lowerarm_r")), FName(TEXT("upperarm_r")) } };
	for (FMuscle& Muscle : Muscles)
	{
		FSandboxMuscleTuning Group = Tuning.Muscle(Muscle.Group);
		// R10, step 5: a body that covers itself lying holds the pose with the protect_* muscles, in by its share (none at 0: exact).
		if (ProtectShare > 0.0)
		{
			const FSandboxBehaviourTuning& Behaviour = Tuning.Behaviour;
			const double Wanted = Muscle.Group == ESandboxMuscleGroup::Spine ? Behaviour.ProtectSpineNmPerRad : Muscle.Group == ESandboxMuscleGroup::Neck ? Behaviour.ProtectNeckNmPerRad :
				Muscle.Group == ESandboxMuscleGroup::Arms ? Behaviour.ProtectArmsNmPerRad : Muscle.Group == ESandboxMuscleGroup::Legs ? Behaviour.ProtectLegsNmPerRad : -1.0;
			if (Wanted >= 0.0)
			{
				Group.StiffnessNmPerRad = FMath::Lerp(Group.StiffnessNmPerRad, Wanted, ProtectShare);
				Group.DampingS = FMath::Lerp(Group.DampingS, Behaviour.ProtectDampingS, ProtectShare);
			}
		}
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
		// R3, item b (fall.catch_max_torque_scale): the catching arms' caps (1 otherwise, and then left alone: exact).
		if (FallArmCapScale != 1.0 && Muscle.Group == ESandboxMuscleGroup::Arms)
		{
			Group.MaxTorqueNm *= FallArmCapScale;
		}
		// R2, step 12 (getup.arm_answer): a held arm that yields is softer, one pulled in stronger (1 for every other muscle: exact).
		double HeldArm = 1.0;
		for (int32 Side = 0; Side < 2; ++Side)
		{
			if (HeldArmScale[Side] != 1.0 && (Muscle.ChildBone == HeldArmBones[Side][0] || Muscle.ChildBone == HeldArmBones[Side][1] || Muscle.ChildBone == HeldArmBones[Side][2]))
			{
				HeldArm = HeldArmScale[Side];
			}
		}
		double Spring = Group.StiffnessNmPerRad * JointStiffness * MuscleScale(Muscle.Group) * HeldArm * SwingScale * TorqueToUnreal / ChaosAngularDriveScale;
		// The damping from the spring the muscle would have without the joint's and the tone's factor on stiffness, times theirs on
		// damping: worked out from the spring itself, a stiffness factor of 0 took the damping with it. (With both factors 1 the two
		// are the same number.)
		const double SpringBase = JointStiffness == 1.0 ? Spring : Group.StiffnessNmPerRad * MuscleScale(Muscle.Group) * HeldArm * SwingScale * TorqueToUnreal / ChaosAngularDriveScale;
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
			// The foot of a step under way is the leg's alone: the world does not pull it along its swing (no force from the world on an
			// upright body beyond the assist's recorded exception; docs/active-ragdoll.md, section 5). Its turn keeps the feet's angular spring.
			Slot.HorizontalNPerM = 0.0;
			Slot.VerticalNPerM = 0.0;
		}
		if (State == ESandboxBodyState::GetUp)
		{
			// The pull that helps the body up comes up from nothing, as its muscles do. Led, the carry pulls instead (FSandboxCarry); a
			// body that rolls (getup.roll) is pulled by nothing.
			const double Ease = IsLed() || GetUpRun.bRolling ? 0.0 : CarryEase();
			Slot.HorizontalNPerM *= Ease;
			Slot.VerticalNPerM *= Ease;
			Slot.AngularNmPerRad *= Ease;
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
	// R10, step 1: the player as a viewer, measured (read only; nothing reads it with every behaviour's switch at 0).
	MeasureViewer();
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
	// R2, step 10 (getup.fit_free_z): which bodies touched the world in the last step, kept for the carry before the impulses are cleared.
	if (Tuning.GetUp.FitFreeZ > 0.5)
	{
		TouchedWorld.SetNum(SupportImpulseNs.Num());
		for (int32 Index = 0; Index < SupportImpulseNs.Num(); ++Index)
		{
			TouchedWorld[Index] = !SupportImpulseNs[Index].IsZero();
		}
	}
	for (FVector& Impulse : SupportImpulseNs)
	{
		Impulse = FVector::ZeroVector;
	}
	SupportStepS = DeltaTime;

	Control->UpdateTargetCaches(DeltaTime);
	// Decision 18 (getup.drag): a drag ends a get-up and keeps the next from beginning (read only of the holds; gives up at most).
	if (Tuning.GetUp.Drag > 0.5)
	{
		UpdateDrag(DeltaTime);
	}
	// R10, steps 4 and 5: fear, and a body that stays down or lowers itself while the player is near (nothing at all with both at 0).
	if (Tuning.Behaviour.Fear > 0.5 || Tuning.Behaviour.StayDown > 0.5 || Tuning.Behaviour.Protect > 0.5 || FearLastS >= 0.0 || bProtectPose || ProtectShare > 0.0)
	{
		UpdateFear();
		UpdateStayDown(DeltaTime);
	}
	UpdateState(DeltaTime);
	UpdateHeld(DeltaTime);
	SenseFall();
	if (Tuning.FallAim.Blend > 0.5 || bFallBlending)
	{
		UpdateFallBlend(DeltaTime);
	}
	UpdateFallRun(DeltaTime);
	UpdateGetUp(DeltaTime);
	UpdateCarry(DeltaTime);
	// R2, step 12 (getup.arm_answer): an arm held while getting up yields or is pulled in; and is let go once it is not.
	if (Tuning.GetUp.ArmAnswer > 0.5 || HeldArmAnswer[0] != 0 || HeldArmAnswer[1] != 0)
	{
		UpdateHeldArms(DeltaTime);
	}
	// The plank after a get-up, measured (read only): the same rule as while getting up, for 3 s after it ended standing.
	if (State != ESandboxBodyState::GetUp && GetUpView.DurationS >= 0.0 && ElapsedS - (GetUpView.StartedS + GetUpView.DurationS) <= 3.0 &&
		Balance.bValid && Balance.IsSupported() && WeightN > 0.0 &&
		Balance.FootForceN[0] + Balance.FootForceN[1] >= 0.8 * WeightN && Balance.CenterOfMassMarginCm < -5.0)
	{
		GetUpView.PlankAfterS += DeltaTime;
	}
	UpdateRegroup(DeltaTime);
	UpdateStep(DeltaTime);
	UpdateStrengthEases();
	if (Tuning.FallAim.Aim > 0.5 || bFallAiming)
	{
		UpdateFallAim(DeltaTime);
	}
	UpdateTone(DeltaTime);
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
	double FirstYaw = 0.0;
	int32 Feet = 0;
	for (const FAssist& Assist : Assists)
	{
		if (Assist.Slot != EAssistSlot::Feet)
		{
			continue;
		}
		Sum += Assist.Target.GetLocation();
		// The foot's turn about the vertical since Start, from -180 to 180 degrees. A body turned about half round has one foot
		// on each side of that seam (175 and -175): each later foot is taken on the first one's side of it, or the mean would
		// be 0 and the support frame turned half round from the feet (found on 27 November 2026: a body that got up facing
		// away from where it started had its feet 65 cm from their stance, stepped to close them and fell).
		FQuat Tilt, Twist;
		(Assist.Target.GetRotation() * Assist.StartRotation.Inverse()).ToSwingTwist(FVector::UpVector, Tilt, Twist);
		const double Yaw = Twist.GetTwistAngle(FVector::UpVector);
		if (Feet == 0)
		{
			FirstYaw = Yaw;
		}
		YawSum += FSandboxBodyProbe::OnSideOf(Yaw, FirstYaw);
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
		// Active as the springs are handed to the solver in this state (PushTuningToControls): getting up, the old assist is 0 and
		// the carry pulls instead.
		FSandboxAssistTuning Slot = AssistTuning(Assist.Slot);
		if (IsSwingFoot(Assist))
		{
			Slot.HorizontalNPerM = 0.0;
			Slot.VerticalNPerM = 0.0;
		}
		const double Share = (State == ESandboxBodyState::GetUp ? (IsLed() || GetUpRun.bRolling ? 0.0 : CarryEase()) : 1.0) * WorldShareOf(Assist.Slot);
		FSandboxAssistView& View = OutView.Assists.AddDefaulted_GetRef();
		View.Bone = Assist.Bone;
		View.Target = AssistGoal(Assist);
		View.bActive = Share > 0.0 && (Slot.HorizontalNPerM > 0.0 || Slot.VerticalNPerM > 0.0 || Slot.AngularNmPerRad > 0.0);
	}

	OutView.Carry.Reset();
	if (Carry.GetShare() > 0.0)
	{
		const FSandboxCarrySettings Settings = CarrySettings();
		for (int32 Index = 0; Index < Carry.Num(); ++Index)
		{
			FSandboxCarryView View;
			View.Bone = Carry.BoneAt(Index);
			if (Carry.ComOf(Index, View.TargetComCm, View.ComCm))
			{
				View.ForceN = Carry.ForceOn(Index, Settings, Carry.GetShare());
				OutView.Carry.Add(View);
			}
		}
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
	// The same forces body by body, for control.authority (decision 15): read only.
	Authority.Entries.Reset();
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
			Slot.HorizontalNPerM = 0.0;
			Slot.VerticalNPerM = 0.0;
			Goal.SetLocation(Swing.Point);
			GoalVelocityMps = Swing.VelocityCmS * 0.01;
		}

		// Centimetres to metres.
		const FVector TargetCom = Goal.TransformPositionNoScale(Body->GetMassSpaceLocal().GetLocation());
		const FVector ErrorM = (TargetCom - Body->GetCOMPosition()) * 0.01;
		const FVector VelocityMps = Body->GetUnrealWorldVelocity() * 0.01 - GoalVelocityMps;
		if (State == ESandboxBodyState::GetUp)
		{
			const double Ease = IsLed() || GetUpRun.bRolling ? 0.0 : CarryEase();
			Slot.HorizontalNPerM *= Ease;
			Slot.VerticalNPerM *= Ease;
			Slot.AngularNmPerRad *= Ease;
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

		FVector EntryTorqueNm = Slot.AngularNmPerRad * (ErrorRad - Body->GetUnrealWorldAngularVelocityInRadians() * Slot.DampingS);
		if (Slot.MaxTorqueNm > 0.0)
		{
			EntryTorqueNm = EntryTorqueNm.GetClampedToMaxSize(Slot.MaxTorqueNm);
		}
		if (!CommandN.IsNearlyZero(1.0e-9) || !EntryTorqueNm.IsNearlyZero(1.0e-9))
		{
			FSandboxAuthorityEntry& Entry = Authority.Entries.AddDefaulted_GetRef();
			Entry.Bone = Assist.Bone;
			Entry.Source = FSandboxAuthorityEntry::ESource::Assist;
			Entry.ForceN = CommandN;
			Entry.TorqueNm = EntryTorqueNm;
		}
	}

	// What the carry asks of the world is assist like any other.
	if (Carry.GetShare() > 0.0)
	{
		const FSandboxCarrySettings Settings = CarrySettings();
		ForceN += Carry.LoadN(Settings, Carry.GetShare());
		for (int32 Index = 0; Index < Carry.Num(); ++Index)
		{
			const FVector CarryN = Carry.ForceOn(Index, Settings, Carry.GetShare());
			const FVector CarryNm = Carry.TorqueOn(Index, Settings, Carry.GetShare());
			if (!CarryN.IsNearlyZero(1.0e-9) || !CarryNm.IsNearlyZero(1.0e-9))
			{
				FSandboxAuthorityEntry& Entry = Authority.Entries.AddDefaulted_GetRef();
				Entry.Bone = Carry.BoneAt(Index);
				Entry.Source = FSandboxAuthorityEntry::ESource::Carry;
				Entry.ForceN = CarryN;
				Entry.TorqueNm = CarryNm;
			}
		}
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
