#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "GenerateAssetsCommandlet.generated.h"

/**
 * Regenerates Content/Generated from the text specs in Specs/.
 *
 *   UnrealEditor-Cmd.exe DictatorSandbox.uproject -run=GenerateAssets [-Result=<path to result.json>]
 *
 * Always writes a result JSON, because a commandlet's exit code alone is not a reliable success signal.
 * Generators: physics assets from Specs/physics/*.json (FPhysicsAssetGenerator).
 */
UCLASS()
class SANDBOXEDITOR_API UGenerateAssetsCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	virtual int32 Main(const FString& Params) override;
};
