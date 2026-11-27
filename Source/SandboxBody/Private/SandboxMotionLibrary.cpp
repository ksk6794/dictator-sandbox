#include "SandboxMotionLibrary.h"

#include "AnimationRuntime.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Misc/FileHelper.h"
#include "PhysicsEngine/BodySetup.h"
#include "SandboxClipPose.h"
#include "SandboxJson.h"

const TCHAR* const FSandboxMotionSpec::GroupNames[FSandboxMotionSpec::GroupCount] = {
	TEXT("pelvis"), TEXT("trunk"), TEXT("hands"), TEXT("elbows"), TEXT("feet"), TEXT("knees") };

const TCHAR* const FSandboxMotionLibrary::MirrorMark = TEXT("|mirror");

bool FSandboxMotionSpec::Parse(const FString& Text, FSandboxMotionSpec& Out, FString& OutError)
{
	Out = FSandboxMotionSpec();
	TSharedPtr<FJsonObject> Root;
	if (!SandboxJson::ParseObject(Text, Root, OutError))
	{
		return false;
	}
	for (const TPair<FString, TSharedPtr<FJsonValue>>& Field : Root->Values)
	{
		if (Field.Key != TEXT("comment") && Field.Key != TEXT("folder") && Field.Key != TEXT("features") && Field.Key != TEXT("clips"))
		{
			OutError = FString::Printf(TEXT("the motion library has a field '%s' it does not know"), *Field.Key);
			return false;
		}
	}
	if (!Root->TryGetStringField(TEXT("folder"), Out.Folder) || Out.Folder.IsEmpty())
	{
		OutError = TEXT("the motion library needs a 'folder'");
		return false;
	}
	const TSharedPtr<FJsonObject>* Features = nullptr;
	if (!Root->TryGetObjectField(TEXT("features"), Features))
	{
		OutError = TEXT("the motion library needs 'features', the bones it compares by group");
		return false;
	}
	for (const TPair<FString, TSharedPtr<FJsonValue>>& Group : (*Features)->Values)
	{
		int32 GroupIndex = INDEX_NONE;
		for (int32 Index = 0; Index < GroupCount; ++Index)
		{
			if (Group.Key == GroupNames[Index])
			{
				GroupIndex = Index;
			}
		}
		const TArray<TSharedPtr<FJsonValue>>* Bones = nullptr;
		if (GroupIndex == INDEX_NONE || !Group.Value.IsValid() || !Group.Value->TryGetArray(Bones))
		{
			OutError = FString::Printf(TEXT("the motion library's group '%s' is not one of pelvis, trunk, hands, elbows, feet, knees with a list of bones"), *Group.Key);
			return false;
		}
		for (const TSharedPtr<FJsonValue>& Bone : *Bones)
		{
			FString Name;
			if (!Bone.IsValid() || Bone->Type != EJson::String || (Name = Bone->AsString()).IsEmpty() || Out.FeatureBones.Contains(FName(*Name)))
			{
				OutError = FString::Printf(TEXT("the motion library's group '%s' has a bone that is not a name, or one named twice"), *Group.Key);
				return false;
			}
			Out.FeatureBones.Add(FName(*Name));
			Out.FeatureGroups.Add(GroupIndex);
		}
	}
	if (Out.FeatureBones.Num() == 0 || Out.FeatureBones.Num() > 32 || !Out.FeatureGroups.Contains(0))
	{
		OutError = TEXT("the motion library compares 1 to 32 bones, the pelvis's group among them");
		return false;
	}
	const TArray<TSharedPtr<FJsonValue>>* Clips = nullptr;
	if (!Root->TryGetArrayField(TEXT("clips"), Clips) || Clips->Num() == 0)
	{
		OutError = TEXT("the motion library needs 'clips'");
		return false;
	}
	for (const TSharedPtr<FJsonValue>& Value : *Clips)
	{
		const TSharedPtr<FJsonObject>* Object = nullptr;
		if (!Value.IsValid() || !Value->TryGetObject(Object))
		{
			OutError = TEXT("a clip of the motion library is not an object");
			return false;
		}
		FSandboxMotionSpecClip& Clip = Out.Clips.AddDefaulted_GetRef();
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Field : (*Object)->Values)
		{
			const bool bText = Field.Key == TEXT("name") || Field.Key == TEXT("kind") || Field.Key == TEXT("lies");
			const bool bFlag = Field.Key == TEXT("mirror") || Field.Key == TEXT("match");
			if (!bText && !bFlag)
			{
				OutError = FString::Printf(TEXT("a clip of the motion library has a field '%s' it does not know"), *Field.Key);
				return false;
			}
			// Checked by type: the JSON reader would take a number for a boolean.
			if (!Field.Value.IsValid() || Field.Value->Type != (bText ? EJson::String : EJson::Boolean))
			{
				OutError = FString::Printf(TEXT("a clip of the motion library has '%s' of the wrong kind (%s)"), *Field.Key, bText ? TEXT("text") : TEXT("true or false"));
				return false;
			}
		}
		(*Object)->TryGetStringField(TEXT("name"), Clip.Name);
		(*Object)->TryGetStringField(TEXT("kind"), Clip.Kind);
		(*Object)->TryGetStringField(TEXT("lies"), Clip.Lies);
		(*Object)->TryGetBoolField(TEXT("mirror"), Clip.bMirror);
		(*Object)->TryGetBoolField(TEXT("match"), Clip.bMatch);
		if (Clip.Name.IsEmpty() || (Clip.Kind != TEXT("getup") && Clip.Kind != TEXT("roll")))
		{
			OutError = FString::Printf(TEXT("the motion library's clip '%s' needs a name and a kind, getup or roll"), *Clip.Name);
			return false;
		}
		if (Clip.bMatch && Clip.Kind != TEXT("getup"))
		{
			OutError = FString::Printf(TEXT("the motion library's clip '%s' is a %s and cannot be chosen to get up by"), *Clip.Name, *Clip.Kind);
			return false;
		}
	}
	return true;
}

bool FSandboxMotionSpec::Read(const FString& File, FSandboxMotionSpec& Out, FString& OutError)
{
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *File))
	{
		OutError = FString::Printf(TEXT("the motion library '%s' cannot be read"), *File);
		return false;
	}
	if (!Parse(Text, Out, OutError))
	{
		OutError = FString::Printf(TEXT("%s: %s"), *File, *OutError);
		return false;
	}
	return true;
}

FString FSandboxMotionSpec::KeyOf(int32 Clip, bool bMirrored) const
{
	const FString& Name = Clips[Clip].Name;
	return FString::Printf(TEXT("%s/%s.%s%s"), *Folder, *Name, *Name, bMirrored ? FSandboxMotionLibrary::MirrorMark : TEXT(""));
}

void FSandboxMotionLibrary::MarkContacts(TConstArrayView<double> LowestCm, TConstArrayView<double> SpeedMps, double KeyS, double ContactCm, double ContactMps, TArray<bool>& OutTouch, TArray<FVector2D>& OutStretches)
{
	OutTouch.Reset();
	OutStretches.Reset();
	const int32 Keys = FMath::Min(LowestCm.Num(), SpeedMps.Num());
	int32 From = INDEX_NONE;
	for (int32 Key = 0; Key < Keys; ++Key)
	{
		const bool bTouch = LowestCm[Key] <= ContactCm && SpeedMps[Key] <= ContactMps;
		OutTouch.Add(bTouch);
		if (bTouch && From == INDEX_NONE)
		{
			From = Key;
		}
		if (!bTouch && From != INDEX_NONE)
		{
			OutStretches.Emplace(From * KeyS, (Key - 1) * KeyS);
			From = INDEX_NONE;
		}
	}
	if (From != INDEX_NONE)
	{
		OutStretches.Emplace(From * KeyS, (Keys - 1) * KeyS);
	}
}

double FSandboxMotionLibrary::LowestPointCm(const UBodySetup& Setup, const FTransform& Body)
{
	double Lowest = TNumericLimits<double>::Max();
	for (const FKBoxElem& Box : Setup.AggGeom.BoxElems)
	{
		const FTransform Element = Box.GetTransform();
		for (int32 Corner = 0; Corner < 8; ++Corner)
		{
			const FVector Local((Corner & 1 ? 0.5 : -0.5) * Box.X, (Corner & 2 ? 0.5 : -0.5) * Box.Y, (Corner & 4 ? 0.5 : -0.5) * Box.Z);
			Lowest = FMath::Min(Lowest, Body.TransformPositionNoScale(Element.TransformPositionNoScale(Local)).Z);
		}
	}
	for (const FKSphylElem& Capsule : Setup.AggGeom.SphylElems)
	{
		const FTransform Element = Capsule.GetTransform();
		for (const double End : { -0.5, 0.5 })
		{
			Lowest = FMath::Min(Lowest, Body.TransformPositionNoScale(Element.TransformPositionNoScale(FVector(0.0, 0.0, End * Capsule.Length))).Z - Capsule.Radius);
		}
	}
	for (const FKSphereElem& Sphere : Setup.AggGeom.SphereElems)
	{
		Lowest = FMath::Min(Lowest, Body.TransformPositionNoScale(Sphere.GetTransform().GetLocation()).Z - Sphere.Radius);
	}
	return Lowest < TNumericLimits<double>::Max() ? Lowest : Body.GetLocation().Z;
}

bool FSandboxMotionLibrary::Build(const FSandboxMotionSpec& InSpec, const USkeletalMeshComponent* Mesh, const FBuildSettings& Settings, FReadClip ReadClip, FString& OutError)
{
	Spec = InSpec;
	Variants.Reset();
	const USkeletalMesh* Asset = Mesh ? Mesh->GetSkeletalMeshAsset() : nullptr;
	if (!Asset)
	{
		OutError = TEXT("the motion library needs a mesh");
		return false;
	}
	const int32 Count = Spec.FeatureBones.Num();
	TArray<const UBodySetup*> Setups;
	PelvisFeature = INDEX_NONE;
	TArray<int32> FeetFeatures;
	TArray<int32> Counterparts;
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const FName Bone = Spec.FeatureBones[Index];
		const FBodyInstance* Body = Mesh->GetBodyInstance(Bone);
		const UBodySetup* Setup = Body && Body->IsValidBodyInstance() ? Body->GetBodySetup() : nullptr;
		if (!Setup)
		{
			OutError = FString::Printf(TEXT("the motion library compares '%s', which has no body in the physics asset"), *Bone.ToString());
			return false;
		}
		Setups.Add(Setup);
		if (Bone == Settings.Pelvis)
		{
			PelvisFeature = Index;
		}
		if (Spec.FeatureGroups[Index] == 4)
		{
			FeetFeatures.Add(Index);
		}
		Counterparts.Add(Spec.FeatureBones.IndexOfByKey(FName(*FSandboxClipPose::MirrorBoneName(Bone.ToString()))));
	}
	if (PelvisFeature == INDEX_NONE)
	{
		OutError = FString::Printf(TEXT("the motion library has to compare the pelvis ('%s')"), *Settings.Pelvis.ToString());
		return false;
	}

	for (int32 ClipIndex = 0; ClipIndex < Spec.Clips.Num(); ++ClipIndex)
	{
		const FSandboxMotionSpecClip& Entry = Spec.Clips[ClipIndex];
		for (int32 Mirrored = 0; Mirrored < (Entry.bMirror ? 2 : 1); ++Mirrored)
		{
			FString ClipKey;
			const FSandboxClipPose* Clip = ReadClip(Spec.KeyOf(ClipIndex, Mirrored != 0), ClipKey, OutError);
			if (!Clip)
			{
				Variants.Reset();
				return false;
			}
			FSandboxMotionVariant& Variant = Variants.AddDefaulted_GetRef();
			Variant.Key = ClipKey;
			Variant.Name = Entry.Name + (Mirrored ? TEXT(" mirrored") : TEXT(""));
			Variant.Kind = Entry.Kind;
			Variant.Lies = Entry.Lies;
			Variant.bMirrored = Mirrored != 0;
			Variant.bMatch = Entry.bMatch;
			Variant.LengthS = Clip->GetLengthS();
			Variant.KeyS = Clip->GetKeyS();
			Variant.Keys = Clip->GetKeyCount();
			Variant.SourceAsymmetryCm = Clip->GetMirrorAsymmetryCm();
			// Everything the matcher and the report take of the clip, now: the controller's map of clips may move it later.
			TArray<TArray<double>> Lowest;
			TArray<TArray<double>> Speed;
			Lowest.SetNum(Count);
			Speed.SetNum(Count);
			for (int32 Key = 0; Key < Variant.Keys; ++Key)
			{
				const double TimeS = Key * Variant.KeyS;
				TArray<FVector>& Places = Variant.Places.AddDefaulted_GetRef();
				for (int32 Index = 0; Index < Count; ++Index)
				{
					FTransform Reached;
					if (!Clip->ReachedBodyAt(Spec.FeatureBones[Index], TimeS, Reached))
					{
						OutError = FString::Printf(TEXT("the clip '%s' has no body '%s'"), *ClipKey, *Spec.FeatureBones[Index].ToString());
						Variants.Reset();
						return false;
					}
					Places.Add(Reached.GetLocation());
					Lowest[Index].Add(LowestPointCm(*Setups[Index], Reached));
				}
				FTransform Pelvis;
				FTransform Chest;
				Clip->ReachedBodyAt(Settings.Pelvis, TimeS, Pelvis);
				if (!Clip->ReachedBodyAt(Settings.Chest, TimeS, Chest))
				{
					Chest = Pelvis;
				}
				Variant.Pelvis.Add(Pelvis.GetRotation());
				Variant.Chest.Add(Chest.GetRotation());
			}
			for (int32 Index = 0; Index < Count; ++Index)
			{
				for (int32 Key = 0; Key < Variant.Keys; ++Key)
				{
					const int32 Before = FMath::Max(Key - 1, 0);
					const int32 After = FMath::Min(Key + 1, Variant.Keys - 1);
					const double SpanS = (After - Before) * Variant.KeyS;
					Speed[Index].Add(SpanS > 0.0 ? 0.01 * FVector::Dist(Variant.Places[After][Index], Variant.Places[Before][Index]) / SpanS : 0.0);
				}
				TArray<bool> Touch;
				TArray<FVector2D> Stretches;
				MarkContacts(Lowest[Index], Speed[Index], Variant.KeyS, Settings.ContactCm, Settings.ContactMps, Touch, Stretches);
				Variant.Touches.SetNumZeroed(Variant.Keys);
				for (int32 Key = 0; Key < Touch.Num(); ++Key)
				{
					Variant.Touches[Key] |= Touch[Key] ? (1u << Index) : 0u;
				}
				for (const FVector2D& Stretch : Stretches)
				{
					FSandboxMotionContact& Contact = Variant.Contacts.AddDefaulted_GetRef();
					Contact.Bone = Spec.FeatureBones[Index];
					Contact.FromS = Stretch.X;
					Contact.ToS = Stretch.Y;
					Contact.LowestMinCm = TNumericLimits<double>::Max();
					Contact.LowestMaxCm = -TNumericLimits<double>::Max();
					for (int32 Key = FMath::RoundToInt32(Stretch.X / Variant.KeyS); Key <= FMath::RoundToInt32(Stretch.Y / Variant.KeyS) && Key < Variant.Keys; ++Key)
					{
						Contact.LowestMinCm = FMath::Min(Contact.LowestMinCm, Lowest[Index][Key]);
						Contact.LowestMaxCm = FMath::Max(Contact.LowestMaxCm, Lowest[Index][Key]);
					}
				}
			}
			// The fit to the floor of each key (step 10): the touching bones' lowest points, on this body, brought to the floor on average.
			Variant.FitCm.SetNumZeroed(Variant.Keys);
			for (int32 Key = 0; Key < Variant.Keys; ++Key)
			{
				double Sum = 0.0;
				int32 Touching = 0;
				for (int32 Index = 0; Index < Count; ++Index)
				{
					if ((Variant.Touches[Key] & (1u << Index)) != 0)
					{
						Sum += Lowest[Index][Key];
						++Touching;
					}
				}
				Variant.FitCm[Key] = Touching > 0 ? -Sum / Touching : 0.0;
			}
			// When it stands, and how far it moves at its start.
			if (Variant.Keys > 0)
			{
				const double LastZ = Variant.Places.Last()[PelvisFeature].Z;
				uint32 FeetMask = 0;
				for (const int32 Foot : FeetFeatures)
				{
					FeetMask |= 1u << Foot;
				}
				for (int32 Key = 0; Key < Variant.Keys; ++Key)
				{
					if (FMath::Abs(Variant.Places[Key][PelvisFeature].Z - LastZ) <= StandsWithinCm && FeetMask != 0 && (Variant.Touches[Key] & FeetMask) == FeetMask)
					{
						Variant.StandsS = Key * Variant.KeyS;
						break;
					}
				}
				for (int32 Key = 0; Key < Variant.Keys && Key * Variant.KeyS <= Settings.WindowS + KINDA_SMALL_NUMBER; ++Key)
				{
					Variant.WindowTravelCm = FMath::Max(Variant.WindowTravelCm, FVector::Dist(Variant.Places[Key][PelvisFeature], Variant.Places[0][PelvisFeature]));
				}
			}
		}
	}

	// A mirror image against its clip seen in this mesh's mirror: the same motion on the other side, to the error the two sides' bodies
	// and the joints' ranges leave.
	const FReferenceSkeleton& Skeleton = Asset->GetRefSkeleton();
	const int32 LeftThigh = Skeleton.FindBoneIndex(TEXT("thigh_l"));
	const int32 RightThigh = Skeleton.FindBoneIndex(TEXT("thigh_r"));
	if (LeftThigh != INDEX_NONE && RightThigh != INDEX_NONE)
	{
		const int32 Axis = FSandboxClipPose::MirrorAxis(FAnimationRuntime::GetComponentSpaceTransformRefPose(Skeleton, LeftThigh).GetLocation() -
			FAnimationRuntime::GetComponentSpaceTransformRefPose(Skeleton, RightThigh).GetLocation());
		for (int32 Index = 1; Index < Variants.Num(); ++Index)
		{
			FSandboxMotionVariant& Mirror = Variants[Index];
			const FSandboxMotionVariant& Original = Variants[Index - 1];
			if (!Mirror.bMirrored || Original.bMirrored || Original.Keys != Mirror.Keys)
			{
				continue;
			}
			Mirror.MirrorErrorCm = 0.0;
			for (int32 Key = 0; Key < Mirror.Keys; ++Key)
			{
				for (int32 Feature = 0; Feature < Count; ++Feature)
				{
					if (Counterparts[Feature] != INDEX_NONE)
					{
						Mirror.MirrorErrorCm = FMath::Max(Mirror.MirrorErrorCm,
							FVector::Dist(FSandboxClipPose::MirrorPoint(Original.Places[Key][Counterparts[Feature]], Axis), Mirror.Places[Key][Feature]));
					}
				}
			}
		}
	}

	// Which other get-up starts nearest each one: two recordings that are one show here.
	for (int32 Index = 0; Index < Variants.Num(); ++Index)
	{
		FSandboxMotionVariant& Variant = Variants[Index];
		if (Variant.Kind != TEXT("getup") || Variant.Keys == 0)
		{
			continue;
		}
		FSandboxMatchBody Body;
		Body.Places = Variant.Places[0];
		Body.PelvisUp = Variant.Pelvis[0].UnrotateVector(FVector::UpVector);
		Body.ChestUp = Variant.Chest[0].UnrotateVector(FVector::UpVector);
		for (int32 Other = 0; Other < Variants.Num(); ++Other)
		{
			const FSandboxMotionVariant& Them = Variants[Other];
			if (Other == Index || Them.Kind != TEXT("getup") || Them.Keys == 0)
			{
				continue;
			}
			FSandboxMatchSample Sample;
			Sample.Variant = Other;
			Sample.Places = Them.Places[0];
			Sample.PelvisUp = Them.Pelvis[0].UnrotateVector(FVector::UpVector);
			Sample.ChestUp = Them.Chest[0].UnrotateVector(FVector::UpVector);
			const FSandboxMatch Match = FSandboxMotionMatcher::Score(Body, Sample, Settings.Match);
			if (Match.bValid && (Variant.NearestCostCm < 0.0 || Match.CostCm < Variant.NearestCostCm))
			{
				Variant.NearestCostCm = Match.CostCm;
				Variant.Nearest = Them.Name;
			}
		}
	}
	return true;
}

int32 FSandboxMotionLibrary::FindVariant(const FString& Key) const
{
	return Variants.IndexOfByPredicate([&Key](const FSandboxMotionVariant& Variant) { return Variant.Key == Key; });
}

int32 FSandboxMotionLibrary::CounterpartOf(int32 Variant) const
{
	if (!Variants.IsValidIndex(Variant))
	{
		return INDEX_NONE;
	}
	const FString& Key = Variants[Variant].Key;
	const FString Other = Variants[Variant].bMirrored ? Key.LeftChop(FCString::Strlen(MirrorMark)) : Key + MirrorMark;
	return FindVariant(Other);
}

void FSandboxMotionLibrary::Samples(const FQuat& StartRotation, double WindowS, TArray<FSandboxMatchSample>& OutSamples) const
{
	OutSamples.Reset();
	for (int32 Index = 0; Index < Variants.Num(); ++Index)
	{
		const FSandboxMotionVariant& Variant = Variants[Index];
		for (int32 Key = 0; Key < Variant.Keys && Key * Variant.KeyS <= WindowS + KINDA_SMALL_NUMBER; ++Key)
		{
			FSandboxMatchSample& Sample = OutSamples.AddDefaulted_GetRef();
			Sample.Variant = Index;
			Sample.TimeS = Key * Variant.KeyS;
			for (const FVector& Place : Variant.Places[Key])
			{
				Sample.Places.Add(StartRotation.RotateVector(Place));
			}
			Sample.PelvisUp = (StartRotation * Variant.Pelvis[Key]).UnrotateVector(FVector::UpVector);
			Sample.ChestUp = (StartRotation * Variant.Chest[Key]).UnrotateVector(FVector::UpVector);
		}
	}
}

double FSandboxMotionLibrary::MirrorErrorMaxCm() const
{
	double Worst = -1.0;
	for (const FSandboxMotionVariant& Variant : Variants)
	{
		Worst = FMath::Max(Worst, Variant.MirrorErrorCm);
	}
	return Worst;
}

int32 FSandboxMotionLibrary::GetUpsLyingAtStart() const
{
	uint32 TrunkMask = 0;
	for (int32 Index = 0; Index < Spec.FeatureGroups.Num(); ++Index)
	{
		TrunkMask |= Spec.FeatureGroups[Index] <= 1 ? (1u << Index) : 0u;
	}
	int32 Lying = 0;
	for (const FSandboxMotionVariant& Variant : Variants)
	{
		Lying += Variant.Kind == TEXT("getup") && Variant.Touches.Num() > 0 && (Variant.Touches[0] & TrunkMask) != 0 ? 1 : 0;
	}
	return Lying;
}
