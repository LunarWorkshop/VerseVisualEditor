#pragma once

#include "Widgets/SLeafWidget.h"

/** A reusable Blueprint-style spline between two widgets in a Verse graph. */
class SVerseGraphWire final : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SVerseGraphWire)
		: _WireColor(FLinearColor::White)
		, _WireThickness(2.0f)
	{}
		SLATE_ARGUMENT(TSharedPtr<SWidget>, SourceAnchor)
		SLATE_ARGUMENT(TSharedPtr<SWidget>, TargetAnchor)
		SLATE_ARGUMENT(FLinearColor, WireColor)
		SLATE_ARGUMENT(float, WireThickness)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual FVector2D ComputeDesiredSize(float) const override;
	virtual int32 OnPaint(
		const FPaintArgs& Args,
		const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements,
		int32 LayerId,
		const FWidgetStyle& InWidgetStyle,
		bool bParentEnabled) const override;

private:
	TWeakPtr<SWidget> SourceAnchor;
	TWeakPtr<SWidget> TargetAnchor;
	FLinearColor WireColor = FLinearColor::White;
	float WireThickness = 2.0f;
};
