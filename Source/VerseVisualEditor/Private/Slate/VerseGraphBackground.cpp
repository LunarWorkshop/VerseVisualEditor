#include "Slate/VerseGraphBackground.h"

#include "Rendering/DrawElements.h"
#include "Settings/EditorStyleSettings.h"
#include "Styling/AppStyle.h"

void PaintVerseGraphBackground(
	const FPaintGeometry& CanvasPaintGeometry,
	FVector2D CanvasSize,
	FVector2D GraphOrigin,
	float Zoom,
	FSlateWindowElementList& OutDrawElements,
	int32 LayerId)
{
	const UEditorStyleSettings* Settings = GetDefault<UEditorStyleSettings>();
	const FSlateBrush* DefaultBackground = FAppStyle::GetBrush(TEXT("Graph.Panel.SolidBackground"));
	const FSlateBrush* CustomBackground = &Settings->GraphBackgroundBrush;
	const FSlateBrush* Background = CustomBackground->HasUObject()
		? CustomBackground
		: DefaultBackground;
	FSlateDrawElement::MakeBox(
		OutDrawElements,
		LayerId,
		CanvasPaintGeometry,
		Background,
		ESlateDrawEffect::None,
		Background->TintColor.GetSpecifiedColor());

	if (!Settings->bUseGrid)
	{
		return;
	}

	const int32 RulePeriod = FMath::Max(
		1,
		FMath::RoundToInt(FAppStyle::GetFloat(TEXT("Graph.Panel.GridRulePeriod"))));
	const float NominalGridSize = static_cast<float>(Settings->GridSnapSize);
	float Inflation = 1.0f;
	while (Zoom * Inflation * NominalGridSize <= 8.0f)
	{
		Inflation *= 2.0f;
	}
	const float CellSize = NominalGridSize * Zoom * Inflation;
	const bool bAntialias = Settings->bAntiAliasGrid;
	TArray<FVector2f> LinePoints;
	LinePoints.SetNumUninitialized(2);

	auto PaintAxis = [&](bool bVertical)
	{
		const float Origin = bVertical ? GraphOrigin.X : GraphOrigin.Y;
		const float Extent = bVertical ? CanvasSize.X : CanvasSize.Y;
		const int32 FirstGridIndex = FMath::FloorToInt(-Origin / CellSize);
		for (int32 GridIndex = FirstGridIndex;; ++GridIndex)
		{
			const float Position = Origin + static_cast<float>(GridIndex) * CellSize;
			if (Position > Extent)
			{
				break;
			}
			if (Position < 0.0f)
			{
				continue;
			}

			const bool bCenterLine = GridIndex == 0;
			const bool bRuleLine = (GridIndex % RulePeriod) == 0;
			FLinearColor Color = bCenterLine
				? Settings->CenterColor
				: bRuleLine
					? Settings->RuleColor
					: Settings->RegularColor;
			if (bRuleLine || bCenterLine)
			{
				const float Emphasis = bCenterLine ? 1.30f : 1.18f;
				Color.R = FMath::Min(1.0f, Color.R * Emphasis);
				Color.G = FMath::Min(1.0f, Color.G * Emphasis);
				Color.B = FMath::Min(1.0f, Color.B * Emphasis);
			}
			else
			{
				Color.A *= 0.48f;
			}
			if (bVertical)
			{
				LinePoints[0] = FVector2f(Position, 0.0f);
				LinePoints[1] = FVector2f(Position, CanvasSize.Y);
			}
			else
			{
				LinePoints[0] = FVector2f(0.0f, Position);
				LinePoints[1] = FVector2f(CanvasSize.X, Position);
			}
			FSlateDrawElement::MakeLines(
				OutDrawElements,
				bRuleLine || bCenterLine ? LayerId + 1 : LayerId,
				CanvasPaintGeometry,
				LinePoints,
				ESlateDrawEffect::None,
				Color,
				bAntialias);
		}
	};

	PaintAxis(false);
	PaintAxis(true);
}
