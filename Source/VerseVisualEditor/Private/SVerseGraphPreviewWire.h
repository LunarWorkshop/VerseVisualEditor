#pragma once

#include "Widgets/SLeafWidget.h"

/** Blueprint-style wire between a socket and a live or frozen screen-space point. */
class SVerseGraphPreviewWire final : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SVerseGraphPreviewWire)
		: _FreeEndIsSource(false)
		, _WireColor(FLinearColor::White)
		, _WireThickness(2.0f)
	{}
		SLATE_ARGUMENT(TSharedPtr<SWidget>, FixedAnchor)
		SLATE_ATTRIBUTE(FVector2D, FreeEnd)
		SLATE_ARGUMENT(bool, FreeEndIsSource)
		SLATE_ARGUMENT(FLinearColor, WireColor)
		SLATE_ARGUMENT(float, WireThickness)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D::ZeroVector; }
	virtual int32 OnPaint(
		const FPaintArgs& Args,
		const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements,
		int32 LayerId,
		const FWidgetStyle& InWidgetStyle,
		bool bParentEnabled) const override;

private:
	TWeakPtr<SWidget> FixedAnchor;
	TAttribute<FVector2D> FreeEnd;
	bool bFreeEndIsSource = false;
	FLinearColor WireColor;
	float WireThickness = 2.0f;
};
