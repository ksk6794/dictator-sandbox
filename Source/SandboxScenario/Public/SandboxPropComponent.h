#pragma once

#include "CoreMinimal.h"
#include "Components/PrimitiveComponent.h"
#include "SandboxPropComponent.generated.h"

class UBodySetup;

/**
 * The body of a scenario's thing that is made of several boxes (a table: its top and four legs): one rigid body with
 * the boxes as its collision shapes, whose origin is the thing's own. It has no look of its own (the stage hangs a
 * cube mesh on it for each box). A thing of a single box is a UBoxComponent as before.
 */
UCLASS()
class SANDBOXSCENARIO_API USandboxPropComponent : public UPrimitiveComponent
{
	GENERATED_BODY()

public:
	/** Before the component is registered: the boxes' middles and edges, cm, from the component's origin. */
	void SetBoxes(const TArray<TPair<FVector, FVector>>& InBoxes);

	virtual UBodySetup* GetBodySetup() override { return Setup; }
	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;

private:
	UPROPERTY(Transient)
	TObjectPtr<UBodySetup> Setup;

	FBox LocalBox = FBox(ForceInit);
};
