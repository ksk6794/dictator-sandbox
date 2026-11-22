#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "GameFramework/PlayerController.h"
#include "SandboxPlayerController.generated.h"

/**
 * The player's controller. It differs from the engine's in one thing: it keeps working while the game is
 * paused. The engine otherwise stops updating the camera of a paused game, and the sandbox pauses to look
 * around a frozen scene.
 */
UCLASS()
class SANDBOXINTERACTION_API ASandboxPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	ASandboxPlayerController();
};

/**
 * The desktop HUD: a crosshair where the tools aim, and the lines ASandboxPawn::GetHudLines gives. Drawn on
 * the engine's canvas; there are no widget assets. Draws nothing for any other pawn.
 */
UCLASS()
class SANDBOXINTERACTION_API ASandboxHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;
};
