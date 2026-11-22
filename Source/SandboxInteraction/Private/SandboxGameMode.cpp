#include "SandboxGameMode.h"

#include "SandboxPawn.h"
#include "SandboxPlayerController.h"

ASandboxGameMode::ASandboxGameMode()
{
	DefaultPawnClass = ASandboxPawn::StaticClass();
	PlayerControllerClass = ASandboxPlayerController::StaticClass();
	HUDClass = ASandboxHUD::StaticClass();
}
