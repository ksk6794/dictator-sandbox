#include "GenerateAssetsCommandlet.h"

#include "Misc/Paths.h"
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

	// Generators are added here as milestones need them (M1: physics asset, room map).
	TArray<TSharedPtr<FJsonValue>> Generated;
	TArray<TSharedPtr<FJsonValue>> Errors;

	const TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("ok"), Errors.Num() == 0);
	Result->SetArrayField(TEXT("generated"), Generated);
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
