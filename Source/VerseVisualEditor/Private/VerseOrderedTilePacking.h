#pragma once

#include "CoreMinimal.h"

struct FVerseOrderedTilePackingResult
{
	TArray<FVector2D> Positions;
	FVector2D Size = FVector2D::ZeroVector;
};

/** Packs an ordered sequence into top-to-bottom columns without reordering it. */
FVerseOrderedTilePackingResult PackVerseTilesApproximatelySquare(
	TConstArrayView<FVector2D> TileSizes,
	float HorizontalGap,
	float VerticalGap);
