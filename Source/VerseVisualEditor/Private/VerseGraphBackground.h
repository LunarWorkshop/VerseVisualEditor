#pragma once

#include "CoreMinimal.h"

class FSlateWindowElementList;

void PaintVerseGraphBackground(
	const FPaintGeometry& CanvasPaintGeometry,
	FVector2D CanvasSize,
	FVector2D GraphOrigin,
	float Zoom,
	FSlateWindowElementList& OutDrawElements,
	int32 LayerId);
