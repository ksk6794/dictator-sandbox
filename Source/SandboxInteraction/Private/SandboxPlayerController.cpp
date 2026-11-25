#include "SandboxPlayerController.h"

#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "SandboxPawn.h"

ASandboxPlayerController::ASandboxPlayerController()
{
	bShouldPerformFullTickWhenPaused = true;
}

void ASandboxHUD::DrawHUD()
{
	Super::DrawHUD();

	const ASandboxPawn* Pawn = Cast<ASandboxPawn>(GetOwningPawn());
	if (!Pawn || !Canvas)
	{
		return;
	}
	// In a headset the canvas is laid across both eyes: the left eye saw the lines' beginnings, the right eye
	// their ends, and nothing could be read (the owner, in the first session). Nothing is drawn there; what a
	// session has to say is printed when it ends.
	if (Pawn->IsInHeadset())
	{
		return;
	}

	// The crosshair: the line of sight, which is where the impulse tool and the drag handle aim.
	const float CenterX = Canvas->ClipX * 0.5f;
	const float CenterY = Canvas->ClipY * 0.5f;
	const float Gap = 4.0f;
	const float Length = 10.0f;
	const FLinearColor Mark(1.0f, 1.0f, 1.0f, 0.9f);
	DrawLine(CenterX - Gap - Length, CenterY, CenterX - Gap, CenterY, Mark, 2.0f);
	DrawLine(CenterX + Gap, CenterY, CenterX + Gap + Length, CenterY, Mark, 2.0f);
	DrawLine(CenterX, CenterY - Gap - Length, CenterX, CenterY - Gap, Mark, 2.0f);
	DrawLine(CenterX, CenterY + Gap, CenterX, CenterY + Gap + Length, Mark, 2.0f);

	TArray<FString> Lines;
	Pawn->GetHudLines(Lines);
	UFont* Font = GEngine ? GEngine->GetMediumFont() : nullptr;
	float Y = 24.0f;
	for (const FString& Line : Lines)
	{
		// A dark copy underneath keeps the text readable on a bright wall.
		DrawText(Line, FLinearColor::Black, 25.0f, Y + 1.0f, Font);
		DrawText(Line, FLinearColor::White, 24.0f, Y, Font);
		Y += 20.0f;
	}
}
