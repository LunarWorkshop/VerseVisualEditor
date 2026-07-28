#pragma once

#include "Input/CursorReply.h"
#include "Input/Reply.h"
#include "VerseCanvasViewState.h"
#include "Widgets/SCompoundWidget.h"

class SBorder;
class SScaleBox;
class SScrollBar;
class SScrollBox;

/** Shared pan/zoom viewport for non-global Verse graphs. */
class SVerseFunctionCanvas final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SVerseFunctionCanvas) {}
		SLATE_ARGUMENT(TSharedPtr<SWidget>, InitialAnchor)
		SLATE_DEFAULT_SLOT(FArguments, Content)
	SLATE_END_ARGS()

	void Construct(
		const FArguments& InArgs,
		FVerseCanvasViewState InitialViewState,
		bool bCenterInitially);

	FVerseCanvasViewState GetViewState() const;

	virtual void Tick(
		const FGeometry& AllottedGeometry,
		const double InCurrentTime,
		const float InDeltaTime) override;
	virtual int32 OnPaint(
		const FPaintArgs& Args,
		const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements,
		int32 LayerId,
		const FWidgetStyle& InWidgetStyle,
		bool bParentEnabled) const override;
	virtual FReply OnPreviewMouseButtonDown(
		const FGeometry& MyGeometry,
		const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(
		const FGeometry& MyGeometry,
		const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseMove(
		const FGeometry& MyGeometry,
		const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseWheel(
		const FGeometry& MyGeometry,
		const FPointerEvent& MouseEvent) override;
	virtual FCursorReply OnCursorQuery(
		const FGeometry& MyGeometry,
		const FPointerEvent& CursorEvent) const override;
	virtual void OnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent) override;

private:
	FMargin GetPanPadding() const;

	TSharedPtr<SScrollBar> HorizontalScrollbar;
	TSharedPtr<SScrollBar> VerticalScrollbar;
	TSharedPtr<SScrollBox> HorizontalScrollBox;
	TSharedPtr<SScrollBox> VerticalScrollBox;
	TSharedPtr<SScaleBox> ScaleBox;
	TWeakPtr<SWidget> InitialAnchor;
	float Zoom = 1.0f;
	bool bPendingInitialCenter = false;
	bool bIsPanning = false;
	FVector2D SoftwareCursorPosition = FVector2D::ZeroVector;
};
