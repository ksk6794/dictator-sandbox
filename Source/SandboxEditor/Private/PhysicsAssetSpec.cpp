#include "PhysicsAssetSpec.h"

#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "SandboxJson.h"

namespace
{
	/** An axis is a number (limited to +/- that many degrees) or the word "locked" or "free". */
	bool ParseAxis(const TSharedPtr<FJsonObject>& Json, const FString& Field, const FString& Context, FPhysicsAxisSpec& OutAxis, FString& OutError)
	{
		const TSharedPtr<FJsonValue> Value = Json->TryGetField(Field);
		if (!Value.IsValid())
		{
			OutError = FString::Printf(TEXT("%s.%s is missing"), *Context, *Field);
			return false;
		}

		double Degrees = 0.0;
		FString Word;
		if (Value->Type == EJson::Number && Value->TryGetNumber(Degrees))
		{
			if (Degrees <= 0.0 || Degrees > 180.0)
			{
				OutError = FString::Printf(TEXT("%s.%s must be between 0 and 180 degrees"), *Context, *Field);
				return false;
			}
			OutAxis.Motion = ACM_Limited;
			OutAxis.LimitDeg = Degrees;
			return true;
		}
		if (Value->TryGetString(Word))
		{
			if (Word == TEXT("locked"))
			{
				OutAxis.Motion = ACM_Locked;
				OutAxis.LimitDeg = 0.0;
				return true;
			}
			if (Word == TEXT("free"))
			{
				OutAxis.Motion = ACM_Free;
				OutAxis.LimitDeg = 0.0;
				return true;
			}
		}
		OutError = FString::Printf(TEXT("%s.%s must be a number of degrees, \"locked\" or \"free\""), *Context, *Field);
		return false;
	}

	bool ParsePairs(const TSharedPtr<FJsonObject>& Json, const FString& Field, TArray<TPair<FName, FName>>& OutPairs, FString& OutError)
	{
		const TArray<TSharedPtr<FJsonValue>>* Pairs = nullptr;
		if (!Json->TryGetArrayField(Field, Pairs))
		{
			return true;
		}
		for (const TSharedPtr<FJsonValue>& Value : *Pairs)
		{
			const TArray<TSharedPtr<FJsonValue>>* Pair = nullptr;
			FString A, B;
			if (!Value.IsValid() || !Value->TryGetArray(Pair) || Pair->Num() != 2 || !(*Pair)[0]->TryGetString(A) || !(*Pair)[1]->TryGetString(B))
			{
				OutError = FString::Printf(TEXT("collision.%s must be a list of [boneA, boneB] pairs"), *Field);
				return false;
			}
			OutPairs.Emplace(FName(*A), FName(*B));
		}
		return true;
	}
}

const FPhysicsBodySpec* FPhysicsAssetSpec::FindBody(FName Bone) const
{
	return Bodies.FindByPredicate([Bone](const FPhysicsBodySpec& Body) { return Body.Bone == Bone; });
}

const FPhysicsJointSpec* FPhysicsAssetSpec::FindJoint(FName Child) const
{
	return Joints.FindByPredicate([Child](const FPhysicsJointSpec& Joint) { return Joint.Child == Child; });
}

bool FPhysicsAssetSpec::LoadFile(const FString& FilePath, FPhysicsAssetSpec& OutSpec, FString& OutError)
{
	OutSpec = FPhysicsAssetSpec();

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
	if (!Json->TryGetStringField(TEXT("skeletal_mesh"), OutSpec.SkeletalMeshPath) || OutSpec.SkeletalMeshPath.IsEmpty()) { return Fail(TEXT("'skeletal_mesh' is missing")); }
	if (!Json->TryGetStringField(TEXT("template"), OutSpec.TemplatePath) || OutSpec.TemplatePath.IsEmpty()) { return Fail(TEXT("'template' is missing")); }
	if (!Json->TryGetNumberField(TEXT("total_mass_kg"), OutSpec.TotalMassKg) || OutSpec.TotalMassKg <= 0.0) { return Fail(TEXT("'total_mass_kg' must be a number > 0")); }

	const TArray<TSharedPtr<FJsonValue>>* Bodies = nullptr;
	if (!Json->TryGetArrayField(TEXT("bodies"), Bodies) || Bodies->Num() == 0) { return Fail(TEXT("'bodies' must be a non-empty array")); }

	double FractionSum = 0.0;
	for (int32 Index = 0; Index < Bodies->Num(); ++Index)
	{
		const TSharedPtr<FJsonObject> BodyJson = (*Bodies)[Index].IsValid() ? (*Bodies)[Index]->AsObject() : nullptr;
		const FString Context = FString::Printf(TEXT("bodies[%d]"), Index);
		if (!BodyJson.IsValid()) { return Fail(Context + TEXT(" is not an object")); }

		FPhysicsBodySpec Body;
		FString Bone;
		if (!BodyJson->TryGetStringField(TEXT("bone"), Bone) || Bone.IsEmpty()) { return Fail(Context + TEXT(".bone is missing")); }
		Body.Bone = FName(*Bone);
		if (OutSpec.FindBody(Body.Bone)) { return Fail(FString::Printf(TEXT("%s: bone '%s' is listed twice"), *Context, *Bone)); }
		if (!BodyJson->TryGetNumberField(TEXT("mass_fraction"), Body.MassFraction) || Body.MassFraction <= 0.0) { return Fail(Context + TEXT(".mass_fraction must be a number > 0")); }
		FractionSum += Body.MassFraction;

		const TArray<TSharedPtr<FJsonValue>>* ShapesFrom = nullptr;
		if (BodyJson->TryGetArrayField(TEXT("shapes_from"), ShapesFrom))
		{
			for (const TSharedPtr<FJsonValue>& Shape : *ShapesFrom)
			{
				FString Source;
				if (!Shape.IsValid() || !Shape->TryGetString(Source) || Source.IsEmpty()) { return Fail(Context + TEXT(".shapes_from must contain bone names")); }
				Body.ShapesFrom.Add(FName(*Source));
			}
		}

		const TArray<TSharedPtr<FJsonValue>>* Shapes = nullptr;
		if (BodyJson->TryGetArrayField(TEXT("shapes"), Shapes))
		{
			for (int32 ShapeIndex = 0; ShapeIndex < Shapes->Num(); ++ShapeIndex)
			{
				const TSharedPtr<FJsonObject> ShapeJson = (*Shapes)[ShapeIndex].IsValid() ? (*Shapes)[ShapeIndex]->AsObject() : nullptr;
				const FString ShapeContext = FString::Printf(TEXT("%s.shapes[%d]"), *Context, ShapeIndex);
				FString Type;
				if (!ShapeJson.IsValid() || !ShapeJson->TryGetStringField(TEXT("type"), Type) || Type != TEXT("box")) { return Fail(ShapeContext + TEXT(".type must be \"box\"")); }

				FPhysicsShapeSpec Shape;
				if (!SandboxJson::TryGetVector(ShapeJson, TEXT("center_cm"), Shape.CenterCm)) { return Fail(ShapeContext + TEXT(".center_cm must be [x, y, z]")); }
				if (!SandboxJson::TryGetVector(ShapeJson, TEXT("size_cm"), Shape.SizeCm) || Shape.SizeCm.GetMin() <= 0.0) { return Fail(ShapeContext + TEXT(".size_cm must be three lengths > 0")); }
				if (ShapeJson->HasField(TEXT("yaw_deg")) && !ShapeJson->TryGetNumberField(TEXT("yaw_deg"), Shape.YawDeg)) { return Fail(ShapeContext + TEXT(".yaw_deg must be a number")); }
				Body.Shapes.Add(Shape);
			}
		}
		if (Body.ShapesFrom.Num() == 0 && Body.Shapes.Num() == 0) { return Fail(Context + TEXT(" needs 'shapes_from' (template bodies) or 'shapes' (boxes written out), or both")); }
		OutSpec.Bodies.Add(MoveTemp(Body));
	}
	if (FMath::Abs(FractionSum - 1.0) > 1.0e-3)
	{
		return Fail(FString::Printf(TEXT("mass fractions sum to %.4f, expected 1"), FractionSum));
	}

	const TArray<TSharedPtr<FJsonValue>>* Joints = nullptr;
	if (!Json->TryGetArrayField(TEXT("joints"), Joints)) { return Fail(TEXT("'joints' array is missing")); }
	for (int32 Index = 0; Index < Joints->Num(); ++Index)
	{
		const TSharedPtr<FJsonObject> JointJson = (*Joints)[Index].IsValid() ? (*Joints)[Index]->AsObject() : nullptr;
		const FString Context = FString::Printf(TEXT("joints[%d]"), Index);
		if (!JointJson.IsValid()) { return Fail(Context + TEXT(" is not an object")); }

		FPhysicsJointSpec Joint;
		FString Child;
		if (!JointJson->TryGetStringField(TEXT("child"), Child) || Child.IsEmpty()) { return Fail(Context + TEXT(".child is missing")); }
		Joint.Child = FName(*Child);
		if (!OutSpec.FindBody(Joint.Child)) { return Fail(FString::Printf(TEXT("%s: child '%s' is not one of the bodies"), *Context, *Child)); }
		if (OutSpec.FindJoint(Joint.Child)) { return Fail(FString::Printf(TEXT("%s: child '%s' already has a joint"), *Context, *Child)); }

		FString AxisError;
		if (!ParseAxis(JointJson, TEXT("swing1"), Context, Joint.Swing1, AxisError) ||
			!ParseAxis(JointJson, TEXT("swing2"), Context, Joint.Swing2, AxisError) ||
			!ParseAxis(JointJson, TEXT("twist"), Context, Joint.Twist, AxisError))
		{
			return Fail(AxisError);
		}
		OutSpec.Joints.Add(Joint);
	}

	const TSharedPtr<FJsonObject>* Collision = nullptr;
	if (Json->TryGetObjectField(TEXT("collision"), Collision))
	{
		FString PairError;
		if (!ParsePairs(*Collision, TEXT("disable"), OutSpec.DisableCollision, PairError) ||
			!ParsePairs(*Collision, TEXT("enable"), OutSpec.EnableCollision, PairError))
		{
			return Fail(PairError);
		}
	}
	return true;
}

TArray<FString> FPhysicsAssetSpec::FindSpecFiles()
{
	const FString Dir = FPaths::Combine(FPaths::ProjectDir(), TEXT("Specs"), TEXT("physics"));
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
