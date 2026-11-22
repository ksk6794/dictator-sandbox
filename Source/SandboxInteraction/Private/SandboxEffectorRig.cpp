#include "SandboxEffectorRig.h"

#include "Components/SceneComponent.h"
#include "SandboxCore.h"
#include "SandboxEffectorComponent.h"
#include "SandboxParamStore.h"

ASandboxEffectorRig::ASandboxEffectorRig()
{
	PrimaryActorTick.bCanEverTick = false;
	// The effectors are bodies of their own in the world; the root only gives the actor a place.
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
}

bool ASandboxEffectorRig::Start(const FSandboxParamStore& InParams, FString& OutError)
{
	if (!FSandboxInteractionTuning::FromParams(InParams, Tuning, OutError))
	{
		return false;
	}
	Params = &InParams;
	ParamsRevision = InParams.GetRevision();
	return true;
}

USandboxEffectorComponent* ASandboxEffectorRig::AddEffector(FName Id, const FTransform& Pose, FString& OutError)
{
	const FSandboxEffectorTuning* EffectorTuning = SandboxEffectors::IsKnown(Id) ? Tuning.Effector(SandboxEffectors::KindOf(Id)) : nullptr;
	if (!Params || !EffectorTuning)
	{
		OutError = Params ? FString::Printf(TEXT("'%s' is not an effector (hand_left, hand_right)"), *Id.ToString()) : FString(TEXT("The effector rig is not started"));
		return nullptr;
	}
	if (FindEffector(Id))
	{
		OutError = FString::Printf(TEXT("Effector '%s' exists already"), *Id.ToString());
		return nullptr;
	}

	USandboxEffectorComponent* Effector = NewObject<USandboxEffectorComponent>(this, Id);
	Effector->SetWorldTransform(Pose);
	Effector->RegisterComponent();
	if (!Effector->Start(Id, *EffectorTuning, Pose, OutError))
	{
		Effector->DestroyComponent();
		return nullptr;
	}
	Effectors.Add(Effector);
	return Effector;
}

USandboxEffectorComponent* ASandboxEffectorRig::FindEffector(FName Id) const
{
	const TObjectPtr<USandboxEffectorComponent>* Found = Effectors.FindByPredicate([Id](const USandboxEffectorComponent* Effector) { return Effector && Effector->GetId() == Id; });
	return Found ? Found->Get() : nullptr;
}

void ASandboxEffectorRig::UpdateTargets(float DeltaTime)
{
	// One integer compared per frame: nothing else happens here while the tuning stays as it is.
	if (Params && Params->GetRevision() != ParamsRevision)
	{
		ParamsRevision = Params->GetRevision();
		FSandboxInteractionTuning NewTuning;
		FString Error;
		if (!FSandboxInteractionTuning::FromParams(*Params, NewTuning, Error))
		{
			// The store has put the new set through this module's checks already, so this is not expected.
			UE_LOG(LogSandbox, Error, TEXT("Effectors keep their tuning: %s"), *Error);
		}
		else if (!NewTuning.HasSameValues(Tuning))
		{
			Tuning = NewTuning;
			++TuningChangeCount;
			for (USandboxEffectorComponent* Effector : Effectors)
			{
				if (const FSandboxEffectorTuning* EffectorTuning = Tuning.Effector(SandboxEffectors::KindOf(Effector->GetId())))
				{
					Effector->ApplyTuning(*EffectorTuning);
				}
			}
			UE_LOG(LogSandbox, Display, TEXT("Effectors: new tuning values applied (change %d)"), TuningChangeCount);
		}
	}

	for (USandboxEffectorComponent* Effector : Effectors)
	{
		FSandboxEffectorTarget Target;
		if (Source && Source->GetTarget(Effector->GetId(), Target))
		{
			Effector->SetTarget(Target);
		}
		Effector->Update(DeltaTime);
	}
}
