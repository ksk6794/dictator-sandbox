#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "SandboxGameMode.generated.h"

/**
 * Project game mode, set in Config/DefaultEngine.ini. The player is ASandboxPawn: a free camera with two
 * physical hands and the desktop test tools, with a controller that keeps working while paused and a HUD
 * drawn in code.
 *
 * A scenario run replaces the pawn with a spectator that has no body (see UScenarioRunnerSubsystem), so
 * that nothing but the scenario's own contents is in its world.
 */
UCLASS()
class SANDBOXINTERACTION_API ASandboxGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ASandboxGameMode();
};
