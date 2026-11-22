#include "SandboxGameMode.h"

#include "GameFramework/SpectatorPawn.h"

ASandboxGameMode::ASandboxGameMode()
{
	DefaultPawnClass = ASpectatorPawn::StaticClass();
}
