#include "VerseOrderedTilePacking.h"

namespace
{
	FVerseOrderedTilePackingResult PackToTargetHeight(
		TConstArrayView<FVector2D> TileSizes,
		float TargetHeight,
		float HorizontalGap,
		float VerticalGap)
	{
		FVerseOrderedTilePackingResult Result;
		Result.Positions.SetNum(TileSizes.Num());
		float ColumnX = 0.0f;
		float ColumnY = 0.0f;
		float ColumnWidth = 0.0f;
		float MaximumHeight = 0.0f;
		for (int32 Index = 0; Index < TileSizes.Num(); ++Index)
		{
			const float SizeX = FMath::Max(0.0f, static_cast<float>(TileSizes[Index].X));
			const float SizeY = FMath::Max(0.0f, static_cast<float>(TileSizes[Index].Y));
			const float ProposedHeight = ColumnY > 0.0f
				? ColumnY + VerticalGap + SizeY
				: SizeY;
			if (ColumnY > 0.0f && ProposedHeight > TargetHeight)
			{
				ColumnX += ColumnWidth + HorizontalGap;
				ColumnY = 0.0f;
				ColumnWidth = 0.0f;
			}

			Result.Positions[Index] = FVector2D(ColumnX, ColumnY);
			ColumnWidth = FMath::Max(ColumnWidth, SizeX);
			ColumnY += (ColumnY > 0.0f ? VerticalGap : 0.0f) + SizeY;
			MaximumHeight = FMath::Max(MaximumHeight, ColumnY);
		}
		Result.Size = TileSizes.IsEmpty()
			? FVector2D::ZeroVector
			: FVector2D(ColumnX + ColumnWidth, MaximumHeight);
		return Result;
	}

	double ScoreLayout(
		const FVerseOrderedTilePackingResult& Layout,
		double TileArea)
	{
		if (Layout.Size.X <= 0.0 || Layout.Size.Y <= 0.0)
		{
			return 0.0;
		}
		const double AspectScore = FMath::Abs(FMath::Loge(Layout.Size.X / Layout.Size.Y));
		const double BoundsArea = Layout.Size.X * Layout.Size.Y;
		const double WasteScore = TileArea > 0.0
			? FMath::Max(0.0, BoundsArea / TileArea - 1.0)
			: 0.0;
		return AspectScore + WasteScore * 0.01;
	}
}

FVerseOrderedTilePackingResult PackVerseTilesApproximatelySquare(
	TConstArrayView<FVector2D> TileSizes,
	float HorizontalGap,
	float VerticalGap)
{
	if (TileSizes.IsEmpty())
	{
		return {};
	}

	double TileArea = 0.0;
	float TallestTile = 0.0f;
	float TotalHeight = 0.0f;
	for (int32 Index = 0; Index < TileSizes.Num(); ++Index)
	{
		const float Width = FMath::Max(0.0, static_cast<float>(TileSizes[Index].X));
		const float Height = FMath::Max(0.0, static_cast<float>(TileSizes[Index].Y));
		TileArea += Width * Height;
		TallestTile = FMath::Max(TallestTile, Height);
		TotalHeight += Height + (Index > 0 ? VerticalGap : 0.0f);
	}

	const float SquareSide = FMath::Max(TallestTile, FMath::Sqrt(static_cast<float>(TileArea)));
	FVerseOrderedTilePackingResult Best;
	double BestScore = TNumericLimits<double>::Max();
	auto ConsiderTarget = [&](float TargetHeight)
	{
		FVerseOrderedTilePackingResult Candidate = PackToTargetHeight(
			TileSizes, FMath::Max(TallestTile, TargetHeight), HorizontalGap, VerticalGap);
		const double Score = ScoreLayout(Candidate, TileArea);
		if (Score < BestScore)
		{
			BestScore = Score;
			Best = MoveTemp(Candidate);
		}
	};

	// Nearby height targets provide stable, inexpensive alternatives while every
	// candidate retains the original top-to-bottom, then left-to-right order.
	for (int32 Sample = 0; Sample <= 40; ++Sample)
	{
		ConsiderTarget(SquareSide * (0.5f + Sample * 0.05f));
	}
	ConsiderTarget(TotalHeight);
	return Best;
}
