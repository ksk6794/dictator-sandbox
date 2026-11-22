#include "SandboxJson.h"

#include "HAL/PlatformFileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonWriter.h"

namespace SandboxJson
{
	bool ParseObject(const FString& Text, TSharedPtr<FJsonObject>& OutObject, FString& OutError)
	{
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
		if (!FJsonSerializer::Deserialize(Reader, OutObject) || !OutObject.IsValid())
		{
			OutError = FString::Printf(TEXT("JSON parse error: %s"), *Reader->GetErrorMessage());
			OutObject.Reset();
			return false;
		}
		return true;
	}

	bool LoadObject(const FString& FilePath, TSharedPtr<FJsonObject>& OutObject, FString& OutError)
	{
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *FilePath))
		{
			OutError = FString::Printf(TEXT("Cannot read file '%s'"), *FilePath);
			return false;
		}
		if (!ParseObject(Text, OutObject, OutError))
		{
			OutError = FString::Printf(TEXT("%s (in '%s')"), *OutError, *FilePath);
			return false;
		}
		return true;
	}

	bool SaveObject(const FString& FilePath, const TSharedRef<FJsonObject>& Object, FString& OutError, bool bPretty)
	{
		FString Text;
		bool bSerialized = false;
		if (bPretty)
		{
			const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Text);
			bSerialized = FJsonSerializer::Serialize(Object, Writer);
		}
		else
		{
			using FCondensedWriter = TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>;
			const TSharedRef<FCondensedWriter> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text);
			bSerialized = FJsonSerializer::Serialize(Object, Writer);
		}
		if (!bSerialized)
		{
			OutError = TEXT("JSON serialization failed");
			return false;
		}

		IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
		PlatformFile.CreateDirectoryTree(*FPaths::GetPath(FilePath));
		if (!FFileHelper::SaveStringToFile(Text, *FilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
		{
			OutError = FString::Printf(TEXT("Cannot write file '%s'"), *FilePath);
			return false;
		}
		return true;
	}

	TArray<TSharedPtr<FJsonValue>> VectorToArray(const FVector& V)
	{
		return {
			MakeShared<FJsonValueNumber>(V.X),
			MakeShared<FJsonValueNumber>(V.Y),
			MakeShared<FJsonValueNumber>(V.Z) };
	}

	TArray<TSharedPtr<FJsonValue>> QuatToArray(const FQuat& Q)
	{
		return {
			MakeShared<FJsonValueNumber>(Q.X),
			MakeShared<FJsonValueNumber>(Q.Y),
			MakeShared<FJsonValueNumber>(Q.Z),
			MakeShared<FJsonValueNumber>(Q.W) };
	}

	bool TryGetVector(const TSharedPtr<FJsonObject>& Object, const FString& Field, FVector& Out)
	{
		const TArray<TSharedPtr<FJsonValue>>* Array = nullptr;
		if (!Object.IsValid() || !Object->TryGetArrayField(Field, Array) || Array->Num() != 3)
		{
			return false;
		}
		double Values[3];
		for (int32 Index = 0; Index < 3; ++Index)
		{
			if (!(*Array)[Index].IsValid() || !(*Array)[Index]->TryGetNumber(Values[Index]))
			{
				return false;
			}
		}
		Out = FVector(Values[0], Values[1], Values[2]);
		return true;
	}
}
