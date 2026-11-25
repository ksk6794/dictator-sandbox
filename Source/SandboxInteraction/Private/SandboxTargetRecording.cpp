#include "SandboxTargetRecording.h"

#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "SandboxJson.h"

namespace
{
	constexpr int32 FormatVersion = 1;
	// Place, turn (a quaternion), velocity and grip of one effector in one frame.
	constexpr int32 NumbersPerTarget = 12;
	const TCHAR* const Columns = TEXT("x_cm,y_cm,z_cm,qx,qy,qz,qw,vx_cm_s,vy_cm_s,vz_cm_s,grip,strength");
}

bool FSandboxTargetRecording::Find(FName Effector, int32 Frame, FSandboxEffectorTarget& OutTarget) const
{
	const int32 Column = Effectors.IndexOfByKey(Effector);
	const int32 Frames = NumFrames();
	if (Column == INDEX_NONE || Frames == 0)
	{
		return false;
	}
	const int32 Clamped = FMath::Clamp(Frame, 0, Frames - 1);
	OutTarget = Targets[Clamped * Effectors.Num() + Column];
	if (Clamped != Frame)
	{
		OutTarget.Velocity = FVector::ZeroVector;
	}
	return true;
}

bool FSandboxTargetRecording::Save(const FString& FilePath, FString& OutError) const
{
	if (Fps <= 0 || Effectors.Num() == 0)
	{
		OutError = TEXT("a recording needs its frame rate and at least one effector");
		return false;
	}
	// Written by hand, one line a frame, so that a recording can be read, cut and compared as text. 17
	// significant digits give every number back bit for bit.
	FString Text;
	Text.Reserve(Targets.Num() * 220 + 256);
	Text += FString::Printf(TEXT("{\n  \"format\": %d,\n  \"fps\": %d,\n  \"effectors\": ["), FormatVersion, Fps);
	for (int32 Index = 0; Index < Effectors.Num(); ++Index)
	{
		Text += FString::Printf(TEXT("%s\"%s\""), Index > 0 ? TEXT(", ") : TEXT(""), *Effectors[Index].ToString());
	}
	Text += FString::Printf(TEXT("],\n  \"columns\": \"%s\",\n  \"frames\": [\n"), Columns);
	const int32 Frames = NumFrames();
	for (int32 Frame = 0; Frame < Frames; ++Frame)
	{
		Text += TEXT("[");
		for (int32 Column = 0; Column < Effectors.Num(); ++Column)
		{
			const FSandboxEffectorTarget& Target = Targets[Frame * Effectors.Num() + Column];
			Text += FString::Printf(TEXT("%s[%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.9g,%.9g]"), Column > 0 ? TEXT(",") : TEXT(""),
				Target.Location.X, Target.Location.Y, Target.Location.Z,
				Target.Rotation.X, Target.Rotation.Y, Target.Rotation.Z, Target.Rotation.W,
				Target.Velocity.X, Target.Velocity.Y, Target.Velocity.Z, static_cast<double>(Target.Grip), static_cast<double>(Target.Strength));
		}
		Text += Frame + 1 < Frames ? TEXT("],\n") : TEXT("]\n");
	}
	Text += TEXT("  ]\n}\n");

	IFileManager::Get().MakeDirectory(*FPaths::GetPath(FilePath), /*Tree*/ true);
	if (!FFileHelper::SaveStringToFile(Text, *FilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		OutError = FString::Printf(TEXT("could not write %s"), *FilePath);
		return false;
	}
	return true;
}

bool FSandboxTargetRecording::Load(const FString& FilePath, FSandboxTargetRecording& OutRecording, FString& OutError)
{
	TSharedPtr<FJsonObject> Json;
	if (!SandboxJson::LoadObject(FilePath, Json, OutError))
	{
		return false;
	}
	const auto Fail = [&FilePath, &OutError](const FString& What)
	{
		OutError = FString::Printf(TEXT("%s: %s"), *FilePath, *What);
		return false;
	};

	int32 Format = 0;
	if (!Json->TryGetNumberField(TEXT("format"), Format) || Format != FormatVersion)
	{
		return Fail(FString::Printf(TEXT("'format' must be %d"), FormatVersion));
	}
	FSandboxTargetRecording Read;
	if (!Json->TryGetNumberField(TEXT("fps"), Read.Fps) || Read.Fps < 1 || Read.Fps > 1000)
	{
		return Fail(TEXT("'fps' must be an integer between 1 and 1000"));
	}
	const TArray<TSharedPtr<FJsonValue>>* Names = nullptr;
	if (!Json->TryGetArrayField(TEXT("effectors"), Names) || Names->Num() == 0)
	{
		return Fail(TEXT("'effectors' must name at least one effector"));
	}
	for (const TSharedPtr<FJsonValue>& Value : *Names)
	{
		FString Name;
		if (!Value.IsValid() || !Value->TryGetString(Name) || !SandboxEffectors::IsKnown(FName(*Name)) || Read.Effectors.Contains(FName(*Name)))
		{
			return Fail(FString::Printf(TEXT("'%s' in 'effectors' is not an effector (hand_left, hand_right), or is named twice"), *Name));
		}
		Read.Effectors.Add(FName(*Name));
	}
	const TArray<TSharedPtr<FJsonValue>>* Frames = nullptr;
	if (!Json->TryGetArrayField(TEXT("frames"), Frames) || Frames->Num() == 0)
	{
		return Fail(TEXT("'frames' must hold at least one frame"));
	}
	Read.Targets.Reserve(Frames->Num() * Read.Effectors.Num());
	for (int32 Frame = 0; Frame < Frames->Num(); ++Frame)
	{
		const TArray<TSharedPtr<FJsonValue>>* Row = nullptr;
		if (!(*Frames)[Frame].IsValid() || !(*Frames)[Frame]->TryGetArray(Row) || Row->Num() != Read.Effectors.Num())
		{
			return Fail(FString::Printf(TEXT("frame %d must hold one target for each of the %d effector(s)"), Frame, Read.Effectors.Num()));
		}
		for (const TSharedPtr<FJsonValue>& Entry : *Row)
		{
			const TArray<TSharedPtr<FJsonValue>>* Numbers = nullptr;
			double Values[NumbersPerTarget];
			// A recording made before a target had a strength has eleven numbers, and its effectors their whole strength.
			bool bGood = Entry.IsValid() && Entry->TryGetArray(Numbers) && (Numbers->Num() == NumbersPerTarget || Numbers->Num() == NumbersPerTarget - 1);
			Values[NumbersPerTarget - 1] = 1.0;
			for (int32 Index = 0; bGood && Index < Numbers->Num(); ++Index)
			{
				bGood = (*Numbers)[Index].IsValid() && (*Numbers)[Index]->Type == EJson::Number && (*Numbers)[Index]->TryGetNumber(Values[Index]) && FMath::IsFinite(Values[Index]);
			}
			if (!bGood)
			{
				return Fail(FString::Printf(TEXT("frame %d: a target is %d numbers (%s)"), Frame, NumbersPerTarget, Columns));
			}
			FSandboxEffectorTarget Target;
			Target.Location = FVector(Values[0], Values[1], Values[2]);
			// As written: a quaternion put through a normalisation would not be the one that was recorded.
			Target.Rotation = FQuat(Values[3], Values[4], Values[5], Values[6]);
			if (!Target.Rotation.IsNormalized())
			{
				return Fail(FString::Printf(TEXT("frame %d: the turn is not a unit quaternion"), Frame));
			}
			Target.Velocity = FVector(Values[7], Values[8], Values[9]);
			Target.Grip = static_cast<float>(Values[10]);
			Target.Strength = static_cast<float>(FMath::Clamp(Values[11], 0.0, 1.0));
			Read.Targets.Add(Target);
		}
	}
	OutRecording = MoveTemp(Read);
	return true;
}
