#include "GenerateAssetsCommandlet.h"

#include "HAL/FileManager.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "PhysicsAssetGenerator.h"
#include "PhysicsAssetSpec.h"
#include "RoomGenerator.h"
#include "Misc/Parse.h"
#include "SandboxCore.h"
#include "SandboxJson.h"

int32 UGenerateAssetsCommandlet::Main(const FString& Params)
{
	FString ResultPath;
	if (!FParse::Value(*Params, TEXT("Result="), ResultPath))
	{
		ResultPath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Tools"), TEXT("gen-assets.json"));
	}
	ResultPath = FPaths::ConvertRelativePathToFull(ResultPath);

	// -Force regenerates everything, including maps that are newer than their spec.
	const bool bForce = FParse::Param(*Params, TEXT("Force"));

	TArray<TSharedPtr<FJsonValue>> Generated;
	TArray<TSharedPtr<FJsonValue>> UpToDate;
	TArray<TSharedPtr<FJsonValue>> Errors;

	// Physics assets: one per Specs/physics/*.json.
	for (const FString& SpecFile : FPhysicsAssetSpec::FindSpecFiles())
	{
		FPhysicsAssetSpec Spec;
		FString Error;
		if (!FPhysicsAssetSpec::LoadFile(SpecFile, Spec, Error) || !FPhysicsAssetGenerator::Generate(Spec, Error))
		{
			UE_LOG(LogSandbox, Error, TEXT("GenerateAssets: %s"), *Error);
			Errors.Add(MakeShared<FJsonValueString>(Error));
			continue;
		}
		UE_LOG(LogSandbox, Display, TEXT("GenerateAssets: %s (%d bodies) from %s"), *Spec.OutputPath, Spec.Bodies.Num(), *SpecFile);
		Generated.Add(MakeShared<FJsonValueString>(Spec.OutputPath));
	}

	// Room maps: one per Specs/rooms/*.json. Saved unlit; Tools/bake-lighting.ps1 builds the lightmaps.
	for (const FString& SpecFile : FRoomSpec::FindSpecFiles())
	{
		FRoomSpec Spec;
		FString Error;
		if (!FRoomSpec::LoadFile(SpecFile, Spec, Error))
		{
			UE_LOG(LogSandbox, Error, TEXT("GenerateAssets: %s"), *Error);
			Errors.Add(MakeShared<FJsonValueString>(Error));
			continue;
		}

		// Regenerating a map discards its baked lighting, so a map newer than its spec is left alone.
		const FString MapFile = FPackageName::LongPackageNameToFilename(Spec.OutputPath, FPackageName::GetMapPackageExtension());
		const FDateTime MapTime = IFileManager::Get().GetTimeStamp(*MapFile);
		if (!bForce && MapTime != FDateTime::MinValue() && MapTime >= IFileManager::Get().GetTimeStamp(*SpecFile))
		{
			UE_LOG(LogSandbox, Display, TEXT("GenerateAssets: %s is up to date"), *Spec.OutputPath);
			UpToDate.Add(MakeShared<FJsonValueString>(Spec.OutputPath));
			continue;
		}

		if (!FRoomGenerator::Generate(Spec, Error))
		{
			UE_LOG(LogSandbox, Error, TEXT("GenerateAssets: %s"), *Error);
			Errors.Add(MakeShared<FJsonValueString>(Error));
			continue;
		}
		UE_LOG(LogSandbox, Display, TEXT("GenerateAssets: %s from %s"), *Spec.OutputPath, *SpecFile);
		Generated.Add(MakeShared<FJsonValueString>(Spec.OutputPath));
	}

	const TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("ok"), Errors.Num() == 0);
	Result->SetArrayField(TEXT("generated"), Generated);
	Result->SetArrayField(TEXT("up_to_date"), UpToDate);
	Result->SetArrayField(TEXT("errors"), Errors);

	FString Error;
	if (!SandboxJson::SaveObject(ResultPath, Result, Error))
	{
		UE_LOG(LogSandbox, Error, TEXT("GenerateAssets: %s"), *Error);
		return 2;
	}

	UE_LOG(LogSandbox, Display, TEXT("GenerateAssets: %d asset(s) generated, %d error(s). Result: %s"), Generated.Num(), Errors.Num(), *ResultPath);
	return Errors.Num() == 0 ? 0 : 1;
}
