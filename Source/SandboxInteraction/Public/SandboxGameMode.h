#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "SandboxGameMode.generated.h"

/**
 * Project game mode, set in Config/DefaultEngine.ini.
 * M0: a free-flying spectator with no collision and no mesh, so the player cannot disturb a scenario.
 * The player pawn with physical effectors replaces it in M2.
 */
UCLASS()
class SANDBOXINTERACTION_API ASandboxGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ASandboxGameMode();
};
