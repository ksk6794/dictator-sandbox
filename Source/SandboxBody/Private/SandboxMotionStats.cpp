#include "SandboxMotionStats.h"

double FSandboxMotionStats::Mean(TConstArrayView<double> Values)
{
	double Sum = 0.0;
	for (const double Value : Values)
	{
		Sum += Value;
	}
	return Values.Num() > 0 ? Sum / Values.Num() : 0.0;
}

double FSandboxMotionStats::RmsDeviation(TConstArrayView<double> Values)
{
	const double Middle = Mean(Values);
	double Sum = 0.0;
	for (const double Value : Values)
	{
		Sum += FMath::Square(Value - Middle);
	}
	return Values.Num() > 0 ? FMath::Sqrt(Sum / Values.Num()) : 0.0;
}

double FSandboxMotionStats::Percentile(TArray<double> Values, double Share)
{
	if (Values.Num() == 0)
	{
		return 0.0;
	}
	Values.Sort();
	return Values[FMath::Clamp(FMath::CeilToInt32(Share * Values.Num()) - 1, 0, Values.Num() - 1)];
}

TArray<FVector2D> FSandboxMotionStats::MovingMean(TConstArrayView<FVector2D> Points, int32 Half)
{
	TArray<FVector2D> Result;
	Result.Reserve(Points.Num());
	for (int32 Index = 0; Index < Points.Num(); ++Index)
	{
		const int32 From = FMath::Max(Index - Half, 0);
		const int32 To = FMath::Min(Index + Half, Points.Num() - 1);
		FVector2D Sum = FVector2D::ZeroVector;
		for (int32 Each = From; Each <= To; ++Each)
		{
			Sum += Points[Each];
		}
		Result.Add(Sum / (To - From + 1));
	}
	return Result;
}

double FSandboxMotionStats::PathSpeed(TConstArrayView<FVector2D> Points, double StepS)
{
	double Length = 0.0;
	for (int32 Index = 1; Index < Points.Num(); ++Index)
	{
		Length += FVector2D::Distance(Points[Index - 1], Points[Index]);
	}
	return Points.Num() > 1 && StepS > 0.0 ? Length / ((Points.Num() - 1) * StepS) : 0.0;
}

double FSandboxMotionStats::Period(TConstArrayView<FVector2D> Points, double StepS, double MinPeriodS, double MinCorrelation, double* OutCorrelation)
{
	if (OutCorrelation)
	{
		*OutCorrelation = 0.0;
	}
	const int32 Count = Points.Num();
	const int32 MinLag = FMath::Max(FMath::CeilToInt32(MinPeriodS / FMath::Max(StepS, UE_DOUBLE_SMALL_NUMBER)), 1);
	const int32 MaxLag = Count / 2;
	if (StepS <= 0.0 || MaxLag <= MinLag + 1)
	{
		return 0.0;
	}
	FVector2D Middle = FVector2D::ZeroVector;
	for (const FVector2D& Point : Points)
	{
		Middle += Point;
	}
	Middle /= Count;

	// The correlation of the two stretches a lag apart, each against its own size.
	const auto Correlation = [&](int32 Lag)
	{
		double Product = 0.0;
		double First = 0.0;
		double Second = 0.0;
		for (int32 Index = 0; Index + Lag < Count; ++Index)
		{
			const FVector2D A = Points[Index] - Middle;
			const FVector2D B = Points[Index + Lag] - Middle;
			Product += A | B;
			First += A | A;
			Second += B | B;
		}
		return First > 0.0 && Second > 0.0 ? Product / FMath::Sqrt(First * Second) : 0.0;
	};
	double Best = MinCorrelation;
	int32 BestLag = 0;
	double Before = Correlation(MinLag - 1);
	double Here = Correlation(MinLag);
	for (int32 Lag = MinLag; Lag < MaxLag; ++Lag)
	{
		const double After = Correlation(Lag + 1);
		// Of the maxima that come round again and again the first: a later one counts only if it is clearly higher.
		if (Here > Before && Here >= After && Here > Best + (BestLag > 0 ? 0.02 : 0.0))
		{
			Best = Here;
			BestLag = Lag;
		}
		Before = Here;
		Here = After;
	}
	if (OutCorrelation && BestLag > 0)
	{
		*OutCorrelation = Best;
	}
	return BestLag * StepS;
}

FVector FSandboxMotionStats::DriveTorqueNm(const FQuat& Parent, const FQuat& Child, const FQuat& Target, const FVector& ParentSpeed, const FVector& ChildSpeed,
	const FVector& TargetSpeed, double StiffnessNmPerRad, double DampingNmsPerRad, double CapNm, bool& bOutAtCap)
{
	bOutAtCap = false;
	const FQuat Relative = Parent.Inverse() * Child;
	FQuat Error = Target.Inverse() * Relative;
	if (Error.W < 0.0)
	{
		Error = FQuat(-Error.X, -Error.Y, -Error.Z, -Error.W);
	}
	const FVector Angles(
		2.0 * FMath::Asin(FMath::Clamp(Error.X, -1.0, 1.0)), 2.0 * FMath::Asin(FMath::Clamp(Error.Y, -1.0, 1.0)), 2.0 * FMath::Asin(FMath::Clamp(Error.Z, -1.0, 1.0)));
	const FVector Speed = Child.UnrotateVector(ChildSpeed - ParentSpeed - TargetSpeed);
	FVector Torque = Angles * -StiffnessNmPerRad - Speed * DampingNmsPerRad;
	if (CapNm > 0.0)
	{
		bOutAtCap = Torque.GetAbsMax() >= CapShare * CapNm;
		Torque = Torque.BoundToCube(CapNm);
	}
	return Child.RotateVector(Torque);
}

double FSandboxMotionStats::CommandJumpNm(const FSandboxDriveCommand (&Commands)[4], const FQuat& Parent, const FQuat& Child, const FVector& ParentSpeed,
	const FVector& ChildSpeed, double& OutTargetDeg)
{
	// Where a motion from one command to the next goes on to. The cap is a limit and has no way to go on.
	const auto GoOn = [](const FSandboxDriveCommand& From, const FSandboxDriveCommand& To)
	{
		FSandboxDriveCommand Next = To;
		Next.Target = (To.Target * From.Target.Inverse() * To.Target).GetNormalized();
		Next.StiffnessNmPerRad = FMath::Max(2.0 * To.StiffnessNmPerRad - From.StiffnessNmPerRad, 0.0);
		Next.DampingNmsPerRad = FMath::Max(2.0 * To.DampingNmsPerRad - From.DampingNmsPerRad, 0.0);
		return Next;
	};
	const auto Torque = [&](const FSandboxDriveCommand& Asked)
	{
		bool bUnused = false;
		return DriveTorqueNm(Parent, Child, Asked.Target, ParentSpeed, ChildSpeed, FVector::ZeroVector, Asked.StiffnessNmPerRad, Asked.DampingNmsPerRad, Asked.CapNm, bUnused);
	};
	const FSandboxDriveCommand Ahead = GoOn(Commands[0], Commands[1]);
	const FSandboxDriveCommand Behind = GoOn(Commands[3], Commands[2]);
	const FVector Was = Torque(Commands[1]);
	const FVector Is = Torque(Commands[2]);
	OutTargetDeg = FMath::RadiansToDegrees(FMath::Min3(Commands[1].Target.AngularDistance(Commands[2].Target), Ahead.Target.AngularDistance(Commands[2].Target),
		Commands[1].Target.AngularDistance(Behind.Target)));
	return FMath::Min3((Is - Was).Size(), (Is - Torque(Ahead)).Size(), (Was - Torque(Behind)).Size());
}

FVector FSandboxMotionStats::TurnSpeed(const FQuat& From, const FQuat& To, double StepS)
{
	const FQuat Turn = (To * From.Inverse()).GetShortestArcWith(FQuat::Identity);
	return StepS > 0.0 ? Turn.ToRotationVector() / StepS : FVector::ZeroVector;
}
