#include "AuthorityTally.h"

#include "Dom/JsonValue.h"

namespace
{
	/** A force's size, its upward part, its part over the floor and its downward part, N. */
	void Ways(const FVector& ForceN, double (&OutN)[4])
	{
		OutN[FSandboxAuthorityTally::All] = ForceN.Size();
		OutN[FSandboxAuthorityTally::Up] = FMath::Max(ForceN.Z, 0.0);
		OutN[FSandboxAuthorityTally::Horizontal] = ForceN.Size2D();
		OutN[FSandboxAuthorityTally::Down] = FMath::Max(-ForceN.Z, 0.0);
	}

	const TCHAR* SourceName(FSandboxAuthorityEntry::ESource Source)
	{
		return Source == FSandboxAuthorityEntry::ESource::Carry ? TEXT("carry") : TEXT("assist");
	}
}

void FSandboxAuthorityTally::AddFrame(const FString& State, TConstArrayView<FSandboxAuthorityEntry> Entries, double WeightN, double FrameS, bool bByBody)
{
	if (WeightN <= 0.0)
	{
		return;
	}
	LastWeightN = WeightN;
	FSums& Sums = States.FindOrAdd(State);
	SecondsOf.FindOrAdd(State) += FrameS;
	++Sums.Frames;
	double FrameN[4] = { 0.0, 0.0, 0.0, 0.0 };
	double FrameNm = 0.0;
	TArray<FBodySums>* StateBodies = bByBody ? &Bodies.FindOrAdd(State) : nullptr;
	for (const FSandboxAuthorityEntry& Entry : Entries)
	{
		double EntryN[4];
		Ways(Entry.ForceN, EntryN);
		for (int32 Way = 0; Way < 4; ++Way)
		{
			FrameN[Way] += EntryN[Way];
		}
		FrameNm += Entry.TorqueNm.Size();
		if (StateBodies)
		{
			FBodySums* Body = StateBodies->FindByPredicate([&Entry](const FBodySums& Other) { return Other.Bone == Entry.Bone && Other.Source == Entry.Source; });
			if (!Body)
			{
				Body = &StateBodies->AddDefaulted_GetRef();
				Body->Bone = Entry.Bone;
				Body->Source = Entry.Source;
			}
			for (int32 Way = 0; Way < 4; ++Way)
			{
				Body->Sum[Way] += EntryN[Way] / WeightN;
			}
			Body->Peak = FMath::Max(Body->Peak, EntryN[All] / WeightN);
		}
	}
	for (int32 Way = 0; Way < 4; ++Way)
	{
		Sums.Sum[Way] += FrameN[Way] / WeightN;
		Sums.Peak[Way] = FMath::Max(Sums.Peak[Way], FrameN[Way] / WeightN);
	}
	Sums.TorqueSumNm += FrameNm;
	Sums.TorquePeakNm = FMath::Max(Sums.TorquePeakNm, FrameNm);
}

TArray<FSandboxAuthorityTally::FBodySums> FSandboxAuthorityTally::BodiesOf(const FString& State) const
{
	TArray<FBodySums> Result;
	if (const TArray<FBodySums>* Found = Bodies.Find(State))
	{
		for (const FBodySums& Body : *Found)
		{
			if (Body.Sum[All] > 0.0)
			{
				Result.Add(Body);
			}
		}
	}
	Result.StableSort([](const FBodySums& A, const FBodySums& B) { return A.Sum[All] > B.Sum[All]; });
	return Result;
}

TSharedRef<FJsonObject> FSandboxAuthorityTally::ToJson() const
{
	const TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
	Json->SetNumberField(TEXT("weight_n"), LastWeightN);
	const TSharedRef<FJsonObject> StatesJson = MakeShared<FJsonObject>();
	for (const TPair<FString, FSums>& Pair : States)
	{
		const FSums& Sums = Pair.Value;
		const TSharedRef<FJsonObject> One = MakeShared<FJsonObject>();
		One->SetNumberField(TEXT("seconds"), SecondsOf.FindRef(Pair.Key));
		One->SetNumberField(TEXT("mean"), Sums.Mean(All));
		One->SetNumberField(TEXT("peak"), Sums.Peak[All]);
		One->SetNumberField(TEXT("up_mean"), Sums.Mean(Up));
		One->SetNumberField(TEXT("up_peak"), Sums.Peak[Up]);
		One->SetNumberField(TEXT("horizontal_mean"), Sums.Mean(Horizontal));
		One->SetNumberField(TEXT("horizontal_peak"), Sums.Peak[Horizontal]);
		One->SetNumberField(TEXT("down_mean"), Sums.Mean(Down));
		One->SetNumberField(TEXT("down_peak"), Sums.Peak[Down]);
		One->SetNumberField(TEXT("torque_mean_nm"), Sums.Frames > 0 ? Sums.TorqueSumNm / Sums.Frames : 0.0);
		One->SetNumberField(TEXT("torque_peak_nm"), Sums.TorquePeakNm);
		StatesJson->SetObjectField(Pair.Key, One);
	}
	Json->SetObjectField(TEXT("states"), StatesJson);
	for (const TPair<FString, TArray<FBodySums>>& Pair : Bodies)
	{
		const FSums* Sums = States.Find(Pair.Key);
		const int32 Frames = Sums ? Sums->Frames : 0;
		TArray<TSharedPtr<FJsonValue>> List;
		for (const FBodySums& Body : BodiesOf(Pair.Key))
		{
			const TSharedRef<FJsonObject> One = MakeShared<FJsonObject>();
			One->SetStringField(TEXT("body"), Body.Bone.ToString());
			One->SetStringField(TEXT("source"), SourceName(Body.Source));
			One->SetNumberField(TEXT("mean"), Frames > 0 ? Body.Sum[All] / Frames : 0.0);
			One->SetNumberField(TEXT("peak"), Body.Peak);
			One->SetNumberField(TEXT("up_mean"), Frames > 0 ? Body.Sum[Up] / Frames : 0.0);
			One->SetNumberField(TEXT("horizontal_mean"), Frames > 0 ? Body.Sum[Horizontal] / Frames : 0.0);
			One->SetNumberField(TEXT("down_mean"), Frames > 0 ? Body.Sum[Down] / Frames : 0.0);
			List.Add(MakeShared<FJsonValueObject>(One));
		}
		Json->SetArrayField(Pair.Key + TEXT("_bodies"), List);
	}
	return Json;
}
