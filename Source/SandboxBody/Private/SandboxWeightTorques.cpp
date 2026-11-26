#include "SandboxWeightTorques.h"

bool FSandboxWeightTorques::Compute(TConstArrayView<FBody> Bodies, const FVector& GravityMps2, TArray<FVector>& OutTorquesNm)
{
	OutTorquesNm.Init(FVector::ZeroVector, Bodies.Num());
	// The force on everything from a body outward, N, and its moment about the world's origin, N*m.
	TArray<FVector, TInlineAllocator<32>> ForceN;
	TArray<FVector, TInlineAllocator<32>> MomentNm;
	ForceN.Init(FVector::ZeroVector, Bodies.Num());
	MomentNm.Init(FVector::ZeroVector, Bodies.Num());
	for (int32 Index = Bodies.Num() - 1; Index >= 0; --Index)
	{
		const FBody& Body = Bodies[Index];
		if (Body.Parent >= Index)
		{
			OutTorquesNm.Init(FVector::ZeroVector, Bodies.Num());
			return false;
		}
		const FVector WeightN = GravityMps2 * Body.MassKg;
		ForceN[Index] += WeightN + Body.PushN;
		MomentNm[Index] += FVector::CrossProduct(Body.CenterOfMassCm * 0.01, WeightN) + FVector::CrossProduct(Body.PushAtCm * 0.01, Body.PushN);
		if (Body.Parent != INDEX_NONE)
		{
			OutTorquesNm[Index] = -(MomentNm[Index] - FVector::CrossProduct(Body.JointCm * 0.01, ForceN[Index]));
			ForceN[Body.Parent] += ForceN[Index];
			MomentNm[Body.Parent] += MomentNm[Index];
		}
	}
	return true;
}

FVector2D FSandboxWeightTorques::NearestInside(const TArray<FVector2D>& Polygon, const FVector2D& Point)
{
	if (Polygon.Num() == 0)
	{
		return Point;
	}
	if (Polygon.Num() < 3)
	{
		return Polygon.Num() == 1 ? Polygon[0] : FMath::ClosestPointOnSegment2D(Point, Polygon[0], Polygon[1]);
	}
	// Inside: on the same side of every edge.
	int32 Left = 0;
	int32 Right = 0;
	FVector2D Nearest = Polygon[0];
	double NearestSquared = TNumericLimits<double>::Max();
	for (int32 Index = 0; Index < Polygon.Num(); ++Index)
	{
		const FVector2D& A = Polygon[Index];
		const FVector2D& B = Polygon[(Index + 1) % Polygon.Num()];
		const double Side = FVector2D::CrossProduct(B - A, Point - A);
		Left += Side > 0.0 ? 1 : 0;
		Right += Side < 0.0 ? 1 : 0;
		const FVector2D OnEdge = FMath::ClosestPointOnSegment2D(Point, A, B);
		const double Squared = FVector2D::DistSquared(Point, OnEdge);
		if (Squared < NearestSquared)
		{
			NearestSquared = Squared;
			Nearest = OnEdge;
		}
	}
	return Left == 0 || Right == 0 ? Point : Nearest;
}

void FSandboxWeightTorques::ShareBetweenFeet(const TArray<FVector2D>& FirstSole, const TArray<FVector2D>& SecondSole, const FVector2D& Place, double& OutFirstShare, FVector2D OutAt[2])
{
	const auto Middle = [](const TArray<FVector2D>& Sole)
	{
		FBox2D Box(ForceInit);
		for (const FVector2D& Corner : Sole)
		{
			Box += Corner;
		}
		return Box.bIsValid ? Box.GetCenter() : FVector2D::ZeroVector;
	};
	const TArray<FVector2D>* Soles[2] = { &FirstSole, &SecondSole };
	OutAt[0] = Place;
	OutAt[1] = Place;
	if (FirstSole.Num() == 0 || SecondSole.Num() == 0)
	{
		OutFirstShare = FirstSole.Num() > 0 ? 1.0 : 0.0;
		for (int32 Foot = 0; Foot < 2; ++Foot)
		{
			OutAt[Foot] = NearestInside(*Soles[Foot], Place);
		}
		return;
	}
	const FVector2D First = Middle(FirstSole);
	const FVector2D Second = Middle(SecondSole);
	const FVector2D Between = Second - First;
	const double LengthSquared = Between.SizeSquared();
	const double Along = LengthSquared > KINDA_SMALL_NUMBER ? FMath::Clamp(((Place - First) | Between) / LengthSquared, 0.0, 1.0) : 0.5;
	OutFirstShare = 1.0 - Along;
	// What the two middles do not reach between them, each sole makes up for as far as it goes.
	const FVector2D Left = Place - (First * OutFirstShare + Second * Along);
	OutAt[0] = NearestInside(FirstSole, First + Left);
	OutAt[1] = NearestInside(SecondSole, Second + Left);
}
