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
	if (Heads.Num() > 0 && Heads.Num() != NumFrames())
	{
		OutError = FString::Printf(TEXT("the head was kept for %d of the recording's %d frames"), Heads.Num(), NumFrames());
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
	Text += TEXT("  ]");
	// R10, step 1: the head, one a frame, after the frames (a reader made before skips a field it does not know).
	if (Heads.Num() > 0)
	{
		Text += TEXT(",\n  \"heads\": [\n");
		for (int32 Frame = 0; Frame < Heads.Num(); ++Frame)
		{
			Text += FString::Printf(TEXT("[%.17g,%.17g,%.17g]%s"), Heads[Frame].X, Heads[Frame].Y, Heads[Frame].Z, Frame + 1 < Heads.Num() ? TEXT(",\n") : TEXT("\n"));
		}
		Text += TEXT("  ]");
	}
	// R10, step 8 in part: the commands, each with its frame.
	if (Commands.Num() > 0)
	{
		Text += TEXT(",\n  \"commands\": [");
		for (int32 Index = 0; Index < Commands.Num(); ++Index)
		{
			Text += FString::Printf(TEXT("%s[%d, \"%s\"]"), Index > 0 ? TEXT(", ") : TEXT(""), Commands[Index].Key, *Commands[Index].Value);
		}
		Text += TEXT("]");
	}
	Text += TEXT("\n}\n");

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
	// R10, step 1: the head, if the session kept it (a recording made before has none): one a frame, or none at all.
	const TArray<TSharedPtr<FJsonValue>>* Heads = nullptr;
	if (Json->HasField(TEXT("heads")))
	{
		if (!Json->TryGetArrayField(TEXT("heads"), Heads) || Heads->Num() != Frames->Num())
		{
			return Fail(FString::Printf(TEXT("'heads' must hold one point [x, y, z] for each of the %d frames"), Frames->Num()));
		}
		Read.Heads.Reserve(Heads->Num());
		for (int32 Frame = 0; Frame < Heads->Num(); ++Frame)
		{
			const TArray<TSharedPtr<FJsonValue>>* Point = nullptr;
			double Values[3] = {};
			bool bGood = (*Heads)[Frame].IsValid() && (*Heads)[Frame]->TryGetArray(Point) && Point->Num() == 3;
			for (int32 Index = 0; bGood && Index < 3; ++Index)
			{
				bGood = (*Point)[Index].IsValid() && (*Point)[Index]->Type == EJson::Number && (*Point)[Index]->TryGetNumber(Values[Index]) && FMath::IsFinite(Values[Index]);
			}
			if (!bGood)
			{
				return Fail(FString::Printf(TEXT("heads[%d] must be [x, y, z]"), Frame));
			}
			Read.Heads.Add(FVector(Values[0], Values[1], Values[2]));
		}
	}
	// R10, step 8 in part: the commands, if the session gave any.
	const TArray<TSharedPtr<FJsonValue>>* CommandList = nullptr;
	if (Json->HasField(TEXT("commands")))
	{
		if (!Json->TryGetArrayField(TEXT("commands"), CommandList))
		{
			return Fail(TEXT("'commands' must be a list of [frame, \"name\"]"));
		}
		for (int32 Index = 0; Index < CommandList->Num(); ++Index)
		{
			const TArray<TSharedPtr<FJsonValue>>* Pair = nullptr;
			double FrameNumber = 0.0;
			FString Name;
			if (!(*CommandList)[Index].IsValid() || !(*CommandList)[Index]->TryGetArray(Pair) || Pair->Num() != 2 || !(*Pair)[0].IsValid() ||
				(*Pair)[0]->Type != EJson::Number || !(*Pair)[0]->TryGetNumber(FrameNumber) || !(*Pair)[1].IsValid() || !(*Pair)[1]->TryGetString(Name) || Name.IsEmpty())
			{
				return Fail(FString::Printf(TEXT("commands[%d] must be [frame, \"name\"]"), Index));
			}
			if (FrameNumber < 0.0 || FrameNumber > static_cast<double>(TNumericLimits<int32>::Max()) || FrameNumber != FMath::FloorToDouble(FrameNumber))
			{
				return Fail(FString::Printf(TEXT("commands[%d]: the frame %g is no frame"), Index, FrameNumber));
			}
			// A name is a word of small letters and underscores (it goes into a console line when replayed).
			for (const TCHAR Letter : Name)
			{
				if (!((Letter >= TEXT('a') && Letter <= TEXT('z')) || Letter == TEXT('_')))
				{
					return Fail(FString::Printf(TEXT("commands[%d]: '%s' is no command's name"), Index, *Name));
				}
			}
			Read.Commands.Emplace(static_cast<int32>(FrameNumber), Name);
		}
	}
	OutRecording = MoveTemp(Read);
	return true;
}
