#include "PhysicsAssetSpec.h"

#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "SandboxJson.h"

namespace
{
	/** An axis is a range [min, max] in degrees, or the word "locked" or "free". */
	bool ParseAxis(const TSharedPtr<FJsonObject>& Json, const FString& Field, const FString& Context, FPhysicsAxisSpec& OutAxis, FString& OutError)
	{
		const TSharedPtr<FJsonValue> Value = Json->TryGetField(Field);
		if (!Value.IsValid())
		{
			OutError = FString::Printf(TEXT("%s.%s is missing"), *Context, *Field);
			return false;
		}

		const TArray<TSharedPtr<FJsonValue>>* Range = nullptr;
		FString Word;
		if (Value->Type == EJson::Array && Value->TryGetArray(Range))
		{
			if (Range->Num() != 2 || (*Range)[0]->Type != EJson::Number || (*Range)[1]->Type != EJson::Number)
			{
				OutError = FString::Printf(TEXT("%s.%s must be [min, max] in degrees"), *Context, *Field);
				return false;
			}
			OutAxis.Motion = ACM_Limited;
			OutAxis.MinDeg = (*Range)[0]->AsNumber();
			OutAxis.MaxDeg = (*Range)[1]->AsNumber();
			// The engine's limit is a half-angle; anything wider than a full turn has no meaning.
			if (OutAxis.MaxDeg <= OutAxis.MinDeg || OutAxis.MaxDeg - OutAxis.MinDeg > 360.0)
			{
				OutError = FString::Printf(TEXT("%s.%s: max must be above min and the range at most 360 degrees"), *Context, *Field);
				return false;
			}
			return true;
		}
		if (Value->Type == EJson::String && Value->TryGetString(Word))
		{
			if (Word == TEXT("locked") || Word == TEXT("free"))
			{
				OutAxis = FPhysicsAxisSpec();
				OutAxis.Motion = Word == TEXT("locked") ? ACM_Locked : ACM_Free;
				return true;
			}
		}
		OutError = FString::Printf(TEXT("%s.%s must be [min, max] in degrees, \"locked\" or \"free\""), *Context, *Field);
		return false;
	}

	/** The swing1 axis is [x, y, z], {"from": bone, "to": bone} or the word "bend". */
	bool ParseSwingAxis(const TSharedPtr<FJsonObject>& Json, const FString& Context, FPhysicsJointSpec& OutJoint, FString& OutError)
	{
		const TSharedPtr<FJsonValue> Value = Json->TryGetField(TEXT("swing1_axis"));
		FString Word;
		const TSharedPtr<FJsonObject>* Bones = nullptr;
		if (Value.IsValid() && Value->Type == EJson::Array && SandboxJson::TryGetVector(Json, TEXT("swing1_axis"), OutJoint.Swing1Axis) && !OutJoint.Swing1Axis.IsNearlyZero())
		{
			OutJoint.Swing1AxisKind = EPhysicsSwingAxis::Vector;
			return true;
		}
		if (Value.IsValid() && Value->Type == EJson::String && Value->TryGetString(Word) && Word == TEXT("bend"))
		{
			OutJoint.Swing1AxisKind = EPhysicsSwingAxis::Bend;
			return true;
		}
		FString From, To;
		if (Value.IsValid() && Value->TryGetObject(Bones) && (*Bones)->TryGetStringField(TEXT("from"), From) && (*Bones)->TryGetStringField(TEXT("to"), To) &&
			!From.IsEmpty() && !To.IsEmpty() && From != To)
		{
			OutJoint.Swing1AxisKind = EPhysicsSwingAxis::Bones;
			OutJoint.Swing1AxisFrom = FName(*From);
			OutJoint.Swing1AxisTo = FName(*To);
			return true;
		}
		OutError = FString::Printf(TEXT("%s.swing1_axis must be a non-zero [x, y, z], {\"from\": bone, \"to\": bone} or \"bend\""), *Context);
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

const FPhysicsJointSpec* FPhysicsAssetSpec::FindJoint(FName Child, bool* bOutMirrored) const
{
	const FPhysicsJointSpec* Found = Joints.FindByPredicate([Child](const FPhysicsJointSpec& Joint) { return Joint.Child == Child || Joint.Mirror == Child; });
	if (bOutMirrored)
	{
		*bOutMirrored = Found && Found->Child != Child;
	}
	return Found;
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
				if (!ShapeJson.IsValid() || !ShapeJson->TryGetStringField(TEXT("type"), Type) || (Type != TEXT("box") && Type != TEXT("sphere") && Type != TEXT("capsule")))
				{
					return Fail(ShapeContext + TEXT(".type must be \"box\", \"sphere\" or \"capsule\""));
				}

				FPhysicsShapeSpec Shape;
				if (Type == TEXT("box"))
				{
					if (!SandboxJson::TryGetVector(ShapeJson, TEXT("center_cm"), Shape.CenterCm)) { return Fail(ShapeContext + TEXT(".center_cm must be [x, y, z]")); }
					if (!SandboxJson::TryGetVector(ShapeJson, TEXT("size_cm"), Shape.SizeCm) || Shape.SizeCm.GetMin() <= 0.0) { return Fail(ShapeContext + TEXT(".size_cm must be three lengths > 0")); }
					if (ShapeJson->HasField(TEXT("yaw_deg")) && !ShapeJson->TryGetNumberField(TEXT("yaw_deg"), Shape.YawDeg)) { return Fail(ShapeContext + TEXT(".yaw_deg must be a number")); }
				}
				else
				{
					if (!ShapeJson->TryGetNumberField(TEXT("radius_cm"), Shape.RadiusCm) || Shape.RadiusCm <= 0.0) { return Fail(ShapeContext + TEXT(".radius_cm must be a length > 0")); }
					if (Type == TEXT("sphere"))
					{
						Shape.Kind = EPhysicsShapeKind::Sphere;
						if (!SandboxJson::TryGetVector(ShapeJson, TEXT("center_cm"), Shape.CenterCm)) { return Fail(ShapeContext + TEXT(".center_cm must be [x, y, z]")); }
					}
					else
					{
						Shape.Kind = EPhysicsShapeKind::Capsule;
						if (!SandboxJson::TryGetVector(ShapeJson, TEXT("from_cm"), Shape.FromCm) || !SandboxJson::TryGetVector(ShapeJson, TEXT("to_cm"), Shape.ToCm) || Shape.FromCm.Equals(Shape.ToCm, 0.01))
						{
							return Fail(ShapeContext + TEXT(": a capsule needs from_cm and to_cm, two different points"));
						}
						Shape.CenterCm = (Shape.FromCm + Shape.ToCm) * 0.5;
					}
				}
				Body.Shapes.Add(Shape);
			}
		}
		if (Body.ShapesFrom.Num() == 0 && Body.Shapes.Num() == 0) { return Fail(Context + TEXT(" needs 'shapes_from' (template bodies) or 'shapes' (boxes written out), or both")); }
		if (BodyJson->HasField(TEXT("inertia_scale")) &&
			(!BodyJson->TryGetNumberField(TEXT("inertia_scale"), Body.InertiaScale) || BodyJson->Values[TEXT("inertia_scale")]->Type != EJson::Number ||
			 Body.InertiaScale < 1.0 || Body.InertiaScale > 20.0))
		{
			return Fail(Context + TEXT(".inertia_scale must be a number from 1 to 20"));
		}
		if (BodyJson->HasField(TEXT("ccd")) && (BodyJson->Values[TEXT("ccd")]->Type != EJson::Boolean || !BodyJson->TryGetBoolField(TEXT("ccd"), Body.bCcd)))
		{
			return Fail(Context + TEXT(".ccd must be true or false"));
		}
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

		FString Mirror;
		if (JointJson->TryGetStringField(TEXT("mirror"), Mirror))
		{
			Joint.Mirror = FName(*Mirror);
			if (Joint.Mirror == Joint.Child || !OutSpec.FindBody(Joint.Mirror)) { return Fail(FString::Printf(TEXT("%s: mirror '%s' is not another one of the bodies"), *Context, *Mirror)); }
			if (OutSpec.FindJoint(Joint.Mirror)) { return Fail(FString::Printf(TEXT("%s: mirror '%s' already has a joint"), *Context, *Mirror)); }
		}

		FString Along;
		const TSharedPtr<FJsonValue> AlongValue = JointJson->TryGetField(TEXT("along"));
		if (AlongValue.IsValid() && AlongValue->Type == EJson::Array)
		{
			if (!SandboxJson::TryGetVector(JointJson, TEXT("along"), Joint.AlongDirection) || !Joint.AlongDirection.Normalize())
			{
				return Fail(Context + TEXT(".along must be a bone or a non-zero [x, y, z]"));
			}
		}
		else if (!JointJson->TryGetStringField(TEXT("along"), Along) || Along.IsEmpty() || Along == Child) { return Fail(Context + TEXT(".along must name the bone the limb runs toward, or be a direction [x, y, z]")); }
		else
		{
			Joint.Along = FName(*Along);
		}

		// Swings limited in the child's frame went with the wrist's asset (R2's step 0, 26 November 2026): a spec that still asks
		// for them would be built otherwise than it says.
		if (JointJson->HasField(TEXT("limits_in"))) { return Fail(Context + TEXT(".limits_in is no longer read: every joint's swings are limited in the parent's frame")); }

		FString AxisError;
		if (!ParseSwingAxis(JointJson, Context, Joint, AxisError) ||
			!ParseAxis(JointJson, TEXT("swing1"), Context, Joint.Swing1, AxisError) ||
			!ParseAxis(JointJson, TEXT("swing2"), Context, Joint.Swing2, AxisError) ||
			!ParseAxis(JointJson, TEXT("twist"), Context, Joint.Twist, AxisError))
		{
			return Fail(AxisError);
		}

		// "limit": "hard", or {"stiffness": n, "damping": n} for a soft one. Written out for every joint: what a
		// joint does at the end of its range is not something to inherit from a template without knowing.
		const TSharedPtr<FJsonValue> Limit = JointJson->TryGetField(TEXT("limit"));
		FString LimitText;
		const TSharedPtr<FJsonObject>* LimitObject = nullptr;
		if (Limit.IsValid() && Limit->Type == EJson::String && Limit->TryGetString(LimitText) && LimitText == TEXT("hard"))
		{
			Joint.bSoftLimit = false;
		}
		else if (Limit.IsValid() && Limit->TryGetObject(LimitObject) &&
			(*LimitObject)->HasTypedField<EJson::Number>(TEXT("stiffness")) && (*LimitObject)->HasTypedField<EJson::Number>(TEXT("damping")) &&
			(*LimitObject)->Values.Num() == 2)
		{
			Joint.bSoftLimit = true;
			Joint.LimitStiffness = (*LimitObject)->GetNumberField(TEXT("stiffness"));
			Joint.LimitDamping = (*LimitObject)->GetNumberField(TEXT("damping"));
			if (Joint.LimitStiffness <= 0.0 || Joint.LimitStiffness > 100000.0 || Joint.LimitDamping < 0.0 || Joint.LimitDamping > 100000.0)
			{
				return Fail(Context + TEXT(".limit: stiffness must be above 0 and damping not below it, both at most 100000"));
			}
		}
		else
		{
			return Fail(Context + TEXT(".limit must be \"hard\" or {\"stiffness\": number, \"damping\": number}"));
		}
		OutSpec.Joints.Add(Joint);
	}

	const TSharedPtr<FJsonObject>* Collision = nullptr;
	if (Json->TryGetObjectField(TEXT("collision"), Collision))
	{
		FString PairError;
		if (!ParsePairs(*Collision, TEXT("disable"), OutSpec.DisableCollision, PairError))
		{
			return Fail(PairError);
		}
		// Every pair that is not jointed collides unless it is listed, so there is nothing to switch on.
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Entry : (*Collision)->Values)
		{
			if (Entry.Key != TEXT("disable"))
			{
				return Fail(FString::Printf(TEXT("'collision.%s' is not part of the format: bodies collide unless 'collision.disable' names the pair"), *Entry.Key));
			}
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
