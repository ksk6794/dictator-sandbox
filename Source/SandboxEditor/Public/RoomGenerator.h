#pragma once

#include "CoreMinimal.h"

struct FRoomLightSpec
{
	FVector LocationCm = FVector::ZeroVector;
	/** Point-light intensity in the engine's default point-light units. */
	double Intensity = 0.0;
	FLinearColor Color = FLinearColor::White;
	double AttenuationRadiusCm = 1000.0;
	double SourceRadiusCm = 5.0;
	/** Static lights are baked entirely; stationary lights bake indirect light and keep dynamic shadows. */
	bool bStationary = false;
};

/** One box of a piece of furniture: its centre, from the piece's own place on the floor, and its size. cm. */
struct FRoomBoxSpec
{
	FVector CenterCm = FVector::ZeroVector;
	FVector SizeCm = FVector::ZeroVector;
};

/** A piece of furniture: static boxes that stand together (a table: its top and four legs). */
struct FRoomFurnitureSpec
{
	FString Name;
	/** Where it stands: the point on the floor its boxes are placed from, and its turn about the vertical. */
	FVector LocationCm = FVector::ZeroVector;
	double YawDeg = 0.0;
	TArray<FRoomBoxSpec> Boxes;
	/** "mass_kg": above 0 the piece is a thing with mass, which the scenario or the session puts into the world when it loads this map (FScenarioSpec), and not a static part of the map. */
	double MassKg = 0.0;
};

/**
 * A box room described in Specs/rooms/*.json (format in Specs/README.md).
 * The floor's top face is at z = 0 and the room is centred on the origin, matching the scenarios.
 */
struct SANDBOXEDITOR_API FRoomSpec
{
	FString Name;
	/** Long package name of the generated map, e.g. /Game/Generated/Maps/TestRoom. */
	FString OutputPath;
	/** Interior size: x, y, height. */
	FVector SizeCm = FVector(500.0, 400.0, 270.0);
	double WallThicknessCm = 20.0;
	int32 LightmapResolution = 128;
	TArray<FRoomLightSpec> Lights;
	TArray<FRoomFurnitureSpec> Furniture;

	static bool LoadFile(const FString& FilePath, FRoomSpec& OutSpec, FString& OutError);
	static TArray<FString> FindSpecFiles();
};

/** Builds the room's map (static shell, furniture, lights, reflection capture, player start) and saves it unlit. */
struct SANDBOXEDITOR_API FRoomGenerator
{
	static bool Generate(const FRoomSpec& Spec, FString& OutError);
};
