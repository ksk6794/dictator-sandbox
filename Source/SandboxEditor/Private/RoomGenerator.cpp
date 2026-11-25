#include "RoomGenerator.h"

#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/PointLight.h"
#include "Engine/SphereReflectionCapture.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "FileHelpers.h"
#include "GameFramework/PlayerStart.h"
#include "HAL/FileManager.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "SandboxJson.h"

bool FRoomSpec::LoadFile(const FString& FilePath, FRoomSpec& OutSpec, FString& OutError)
{
	OutSpec = FRoomSpec();

	TSharedPtr<FJsonObject> Json;
	if (!SandboxJson::LoadObject(FilePath, Json, OutError))
	{
		return false;
	}
	const auto Fail = [&OutError, &FilePath](const FString& Message)
	{
		OutError = FString::Printf(TEXT("%s (in '%s')"), *Message, *FilePath);
		return false;
	};

	if (!Json->TryGetStringField(TEXT("name"), OutSpec.Name) || OutSpec.Name.IsEmpty()) { return Fail(TEXT("'name' is missing")); }
	if (!Json->TryGetStringField(TEXT("output"), OutSpec.OutputPath) || !OutSpec.OutputPath.StartsWith(TEXT("/Game/"))) { return Fail(TEXT("'output' must be a /Game/... package path")); }
	if (!SandboxJson::TryGetVector(Json, TEXT("size_cm"), OutSpec.SizeCm) || OutSpec.SizeCm.GetMin() <= 0.0) { return Fail(TEXT("'size_cm' must be [x, y, height], all > 0")); }
	if (Json->HasField(TEXT("wall_thickness_cm")) && (!Json->TryGetNumberField(TEXT("wall_thickness_cm"), OutSpec.WallThicknessCm) || OutSpec.WallThicknessCm <= 0.0))
	{
		return Fail(TEXT("'wall_thickness_cm' must be a number > 0"));
	}
	if (Json->HasField(TEXT("lightmap_resolution")) && (!Json->TryGetNumberField(TEXT("lightmap_resolution"), OutSpec.LightmapResolution) || OutSpec.LightmapResolution < 4))
	{
		return Fail(TEXT("'lightmap_resolution' must be an integer >= 4"));
	}

	const TArray<TSharedPtr<FJsonValue>>* Lights = nullptr;
	if (!Json->TryGetArrayField(TEXT("lights"), Lights) || Lights->Num() == 0) { return Fail(TEXT("'lights' must list at least one light")); }
	for (int32 Index = 0; Index < Lights->Num(); ++Index)
	{
		const TSharedPtr<FJsonObject> LightJson = (*Lights)[Index].IsValid() ? (*Lights)[Index]->AsObject() : nullptr;
		const FString Context = FString::Printf(TEXT("lights[%d]"), Index);
		if (!LightJson.IsValid()) { return Fail(Context + TEXT(" is not an object")); }

		FRoomLightSpec Light;
		if (!SandboxJson::TryGetVector(LightJson, TEXT("location_cm"), Light.LocationCm)) { return Fail(Context + TEXT(".location_cm must be [x, y, z]")); }
		if (!LightJson->TryGetNumberField(TEXT("intensity"), Light.Intensity) || Light.Intensity <= 0.0) { return Fail(Context + TEXT(".intensity must be a number > 0")); }
		FVector Color;
		if (LightJson->HasField(TEXT("color")))
		{
			if (!SandboxJson::TryGetVector(LightJson, TEXT("color"), Color)) { return Fail(Context + TEXT(".color must be [r, g, b] in 0..1")); }
			Light.Color = FLinearColor(Color.X, Color.Y, Color.Z);
		}
		LightJson->TryGetNumberField(TEXT("attenuation_radius_cm"), Light.AttenuationRadiusCm);
		LightJson->TryGetNumberField(TEXT("source_radius_cm"), Light.SourceRadiusCm);
		FString Mobility;
		if (LightJson->TryGetStringField(TEXT("mobility"), Mobility))
		{
			if (Mobility != TEXT("static") && Mobility != TEXT("stationary")) { return Fail(Context + TEXT(".mobility must be \"static\" or \"stationary\"")); }
			Light.bStationary = Mobility == TEXT("stationary");
		}
		OutSpec.Lights.Add(Light);
	}

	const TArray<TSharedPtr<FJsonValue>>* Furniture = nullptr;
	if (Json->TryGetArrayField(TEXT("furniture"), Furniture))
	{
		for (int32 Index = 0; Index < Furniture->Num(); ++Index)
		{
			const TSharedPtr<FJsonObject> PieceJson = (*Furniture)[Index].IsValid() ? (*Furniture)[Index]->AsObject() : nullptr;
			const FString Context = FString::Printf(TEXT("furniture[%d]"), Index);
			if (!PieceJson.IsValid()) { return Fail(Context + TEXT(" is not an object")); }

			FRoomFurnitureSpec Piece;
			if (!PieceJson->TryGetStringField(TEXT("name"), Piece.Name) || Piece.Name.IsEmpty()) { return Fail(Context + TEXT(".name is missing")); }
			if (!SandboxJson::TryGetVector(PieceJson, TEXT("location_cm"), Piece.LocationCm)) { return Fail(Context + TEXT(".location_cm must be [x, y, z]")); }
			if (PieceJson->HasField(TEXT("yaw_deg")) && !PieceJson->TryGetNumberField(TEXT("yaw_deg"), Piece.YawDeg)) { return Fail(Context + TEXT(".yaw_deg must be a number")); }
			const TArray<TSharedPtr<FJsonValue>>* Boxes = nullptr;
			if (!PieceJson->TryGetArrayField(TEXT("boxes"), Boxes) || Boxes->Num() == 0) { return Fail(Context + TEXT(".boxes must list at least one box")); }
			for (int32 BoxIndex = 0; BoxIndex < Boxes->Num(); ++BoxIndex)
			{
				const TSharedPtr<FJsonObject> BoxJson = (*Boxes)[BoxIndex].IsValid() ? (*Boxes)[BoxIndex]->AsObject() : nullptr;
				FRoomBoxSpec Box;
				if (!BoxJson.IsValid() || !SandboxJson::TryGetVector(BoxJson, TEXT("center_cm"), Box.CenterCm) ||
					!SandboxJson::TryGetVector(BoxJson, TEXT("size_cm"), Box.SizeCm) || Box.SizeCm.GetMin() <= 0.0)
				{
					return Fail(FString::Printf(TEXT("%s.boxes[%d] needs center_cm [x, y, z] and size_cm [x, y, z], all > 0"), *Context, BoxIndex));
				}
				Piece.Boxes.Add(Box);
			}
			if (PieceJson->HasField(TEXT("mass_kg")) && (!PieceJson->TryGetNumberField(TEXT("mass_kg"), Piece.MassKg) || Piece.MassKg <= 0.0))
			{
				return Fail(Context + TEXT(".mass_kg must be a number above 0"));
			}
			OutSpec.Furniture.Add(Piece);
		}
	}
	return true;
}

TArray<FString> FRoomSpec::FindSpecFiles()
{
	const FString Dir = FPaths::Combine(FPaths::ProjectDir(), TEXT("Specs"), TEXT("rooms"));
	TArray<FString> Names;
	IFileManager::Get().FindFiles(Names, *FPaths::Combine(Dir, TEXT("*.json")), true, false);
	Names.Sort();

	TArray<FString> Paths;
	for (const FString& Name : Names)
	{
		Paths.Add(FPaths::Combine(Dir, Name));
	}
	return Paths;
}

bool FRoomGenerator::Generate(const FRoomSpec& Spec, FString& OutError)
{
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (!Cube)
	{
		OutError = TEXT("Cannot load /Engine/BasicShapes/Cube");
		return false;
	}

	// Lightmaps from an earlier bake belong to the old geometry.
	const FString BuiltDataFile = FPackageName::LongPackageNameToFilename(Spec.OutputPath + TEXT("_BuiltData"), FPackageName::GetAssetPackageExtension());
	IFileManager::Get().Delete(*BuiltDataFile, /*RequireExists*/ false, /*EvenReadOnly*/ true);

	UWorld* World = UEditorLoadingAndSavingUtils::NewBlankMap(/*bSaveExistingMap*/ false);
	if (!World)
	{
		OutError = TEXT("Could not create a blank map");
		return false;
	}

	// The engine cube is 100 cm on a side, so the scale is the size in metres.
	const auto SpawnBox = [World, Cube, &Spec](const TCHAR* Label, const FVector& Center, const FVector& SizeCm)
	{
		AStaticMeshActor* Actor = World->SpawnActor<AStaticMeshActor>(Center, FRotator::ZeroRotator);
		Actor->SetActorLabel(Label);
		UStaticMeshComponent* Component = Actor->GetStaticMeshComponent();
		Component->SetStaticMesh(Cube);
		Component->SetMobility(EComponentMobility::Static);
		Component->bOverrideLightMapRes = true;
		Component->OverriddenLightMapRes = Spec.LightmapResolution;
		Actor->SetActorScale3D(SizeCm / 100.0);
	};

	const double X = Spec.SizeCm.X, Y = Spec.SizeCm.Y, Z = Spec.SizeCm.Z, T = Spec.WallThicknessCm;
	SpawnBox(TEXT("Floor"), FVector(0.0, 0.0, -T / 2), FVector(X + 2 * T, Y + 2 * T, T));
	SpawnBox(TEXT("Ceiling"), FVector(0.0, 0.0, Z + T / 2), FVector(X + 2 * T, Y + 2 * T, T));
	SpawnBox(TEXT("Wall_PosX"), FVector(X / 2 + T / 2, 0.0, Z / 2), FVector(T, Y + 2 * T, Z));
	SpawnBox(TEXT("Wall_NegX"), FVector(-X / 2 - T / 2, 0.0, Z / 2), FVector(T, Y + 2 * T, Z));
	SpawnBox(TEXT("Wall_PosY"), FVector(0.0, Y / 2 + T / 2, Z / 2), FVector(X, T, Z));
	SpawnBox(TEXT("Wall_NegY"), FVector(0.0, -Y / 2 - T / 2, Z / 2), FVector(X, T, Z));

	// Furniture: static boxes, each piece turned about the vertical where it stands.
	for (const FRoomFurnitureSpec& Piece : Spec.Furniture)
	{
		if (Piece.MassKg > 0.0)
		{
			continue; // A thing with mass is put into the world by the scenario, not into the map.
		}
		const FRotator Turn(0.0, Piece.YawDeg, 0.0);
		for (int32 BoxIndex = 0; BoxIndex < Piece.Boxes.Num(); ++BoxIndex)
		{
			const FRoomBoxSpec& Box = Piece.Boxes[BoxIndex];
			AStaticMeshActor* Actor = World->SpawnActor<AStaticMeshActor>(Piece.LocationCm + Turn.RotateVector(Box.CenterCm), Turn);
			Actor->SetActorLabel(FString::Printf(TEXT("%s_%d"), *Piece.Name, BoxIndex));
			UStaticMeshComponent* Component = Actor->GetStaticMeshComponent();
			Component->SetStaticMesh(Cube);
			Component->SetMobility(EComponentMobility::Static);
			Component->bOverrideLightMapRes = true;
			Component->OverriddenLightMapRes = FMath::Max(Spec.LightmapResolution / 4, 4);
			Actor->SetActorScale3D(Box.SizeCm / 100.0);
		}
	}

	for (int32 Index = 0; Index < Spec.Lights.Num(); ++Index)
	{
		const FRoomLightSpec& LightSpec = Spec.Lights[Index];
		APointLight* Light = World->SpawnActor<APointLight>(LightSpec.LocationCm, FRotator::ZeroRotator);
		Light->SetActorLabel(FString::Printf(TEXT("Light_%d"), Index));
		UPointLightComponent* Component = Light->PointLightComponent;
		Component->SetMobility(LightSpec.bStationary ? EComponentMobility::Stationary : EComponentMobility::Static);
		Component->SetIntensity(static_cast<float>(LightSpec.Intensity));
		Component->SetLightColor(LightSpec.Color);
		Component->SetAttenuationRadius(static_cast<float>(LightSpec.AttenuationRadiusCm));
		Component->SetSourceRadius(static_cast<float>(LightSpec.SourceRadiusCm));
	}

	ASphereReflectionCapture* Capture = World->SpawnActor<ASphereReflectionCapture>(FVector(0.0, 0.0, Z / 2), FRotator::ZeroRotator);
	Capture->SetActorLabel(TEXT("ReflectionCapture"));

	APlayerStart* Start = World->SpawnActor<APlayerStart>(FVector(0.0, -Y / 4, 100.0), FRotator(0.0, 90.0, 0.0));
	Start->SetActorLabel(TEXT("PlayerStart"));

	if (!UEditorLoadingAndSavingUtils::SaveMap(World, Spec.OutputPath))
	{
		OutError = FString::Printf(TEXT("Failed to save map '%s'"), *Spec.OutputPath);
		return false;
	}
	return true;
}
