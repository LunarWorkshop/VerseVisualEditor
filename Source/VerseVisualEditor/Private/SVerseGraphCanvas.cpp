#include "SVerseGraphCanvas.h"

#include "Layout/Clipping.h"
#include "Rendering/DrawElements.h"
#include "Styling/AppStyle.h"
#include "VerseGraphBackground.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/Layout/SScrollBar.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSpacer.h"

namespace
{
	constexpr float GraphMinimumZoom = 0.5f;
	constexpr float GraphMaximumZoom = 2.0f;
	constexpr float GraphZoomStep = 0.1f;
	constexpr float GraphEdgePeek = 48.0f;
}

void SVerseGraphCanvas::Construct(
	const FArguments& InArgs,
	FVerseCanvasViewState InitialViewState,
	bool bCenterInitially)
{
	Zoom = FMath::Clamp(InitialViewState.Zoom, GraphMinimumZoom, GraphMaximumZoom);
	bPendingInitialCenter = bCenterInitially;
	InitialAnchor = InArgs._InitialAnchor;
	HorizontalScrollbar = SNew(SScrollBar).Orientation(Orient_Horizontal);
	VerticalScrollbar = SNew(SScrollBar).Orientation(Orient_Vertical);

	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				SAssignNew(VerticalScrollBox, SScrollBox)
				.Orientation(Orient_Vertical)
				.ExternalScrollbar(VerticalScrollbar)
				.ConsumeMouseWheel(EConsumeMouseWheel::Never)
				+ SScrollBox::Slot()
				[
					SAssignNew(HorizontalScrollBox, SScrollBox)
					.Orientation(Orient_Horizontal)
					.ExternalScrollbar(HorizontalScrollbar)
					.ConsumeMouseWheel(EConsumeMouseWheel::Never)
					+ SScrollBox::Slot()
					[
						SNew(SBorder)
						.BorderImage(nullptr)
						.Padding(this, &SVerseGraphCanvas::GetPanPadding)
						[
							SAssignNew(ScaleBox, SScaleBox)
							.Stretch(EStretch::UserSpecified)
							.StretchDirection(EStretchDirection::Both)
							.UserSpecifiedScale(Zoom)
							.HAlign(HAlign_Left)
							.VAlign(VAlign_Top)
							[
								InArgs._Content.Widget
							]
						]
					]
				]
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				VerticalScrollbar.ToSharedRef()
			]
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				HorizontalScrollbar.ToSharedRef()
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SSpacer).Size(FVector2D(12.0f, 12.0f))
			]
		]
	];

	if (!bCenterInitially)
	{
		HorizontalScrollBox->SetScrollOffset(FMath::Max(0.0, InitialViewState.ScrollOffset.X));
		VerticalScrollBox->SetScrollOffset(FMath::Max(0.0, InitialViewState.ScrollOffset.Y));
	}
}

FVerseCanvasViewState SVerseGraphCanvas::GetViewState() const
{
	FVerseCanvasViewState State;
	State.ScrollOffset = FVector2D(
		HorizontalScrollBox.IsValid() ? HorizontalScrollBox->GetScrollOffset() : 0.0f,
		VerticalScrollBox.IsValid() ? VerticalScrollBox->GetScrollOffset() : 0.0f);
	State.Zoom = Zoom;
	return State;
}

void SVerseGraphCanvas::Tick(
	const FGeometry& AllottedGeometry,
	const double InCurrentTime,
	const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	if (bPendingInitialCenter
		&& HorizontalScrollBox.IsValid()
		&& VerticalScrollBox.IsValid()
		&& AllottedGeometry.GetLocalSize().GetMin() > 0.0f)
	{
		const TSharedPtr<SWidget> Anchor = InitialAnchor.IsValid()
			? InitialAnchor.Pin()
			: ScaleBox;
		if (Anchor.IsValid()
			&& Anchor->GetCachedGeometry().GetLocalSize().GetMin() > 0.0f
			&& HorizontalScrollBox->GetScrollOffsetOfEnd() > 0.0f
			&& VerticalScrollBox->GetScrollOffsetOfEnd() > 0.0f)
		{
			HorizontalScrollBox->ScrollDescendantIntoView(
				Anchor,
				false,
				EDescendantScrollDestination::Center,
				0.0f);
			VerticalScrollBox->ScrollDescendantIntoView(
				Anchor,
				false,
				EDescendantScrollDestination::TopOrLeft,
				48.0f);
			bPendingInitialCenter = false;
		}
	}
}

FMargin SVerseGraphCanvas::GetPanPadding() const
{
	const FVector2D ViewSize = VerticalScrollBox.IsValid()
		? VerticalScrollBox->GetCachedGeometry().GetLocalSize()
		: FVector2D::ZeroVector;
	return FMargin(
		FMath::Max(GraphEdgePeek, ViewSize.X - GraphEdgePeek),
		FMath::Max(GraphEdgePeek, ViewSize.Y - GraphEdgePeek));
}

int32 SVerseGraphCanvas::OnPaint(
	const FPaintArgs& Args,
	const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements,
	int32 LayerId,
	const FWidgetStyle& InWidgetStyle,
	bool bParentEnabled) const
{
	const FGeometry& ScrollGeometry = VerticalScrollBox->GetCachedGeometry();
	const FVector2D CanvasSize = ScrollGeometry.GetLocalSize();
	const FPaintGeometry CanvasPaintGeometry = AllottedGeometry.ToPaintGeometry(
		CanvasSize,
		FSlateLayoutTransform(FVector2D::ZeroVector));
	const FMargin PanPadding = GetPanPadding();
	const FVector2D GraphOrigin(
		PanPadding.Left - HorizontalScrollBox->GetScrollOffset(),
		PanPadding.Top - VerticalScrollBox->GetScrollOffset());
	OutDrawElements.PushClip(FSlateClippingZone(CanvasPaintGeometry));
	PaintVerseGraphBackground(
		CanvasPaintGeometry,
		CanvasSize,
		GraphOrigin,
		Zoom,
		OutDrawElements,
		LayerId);
	OutDrawElements.PopClip();
	const int32 ContentLayer = SCompoundWidget::OnPaint(
		Args,
		AllottedGeometry,
		MyCullingRect,
		OutDrawElements,
		LayerId + 2,
		InWidgetStyle,
		bParentEnabled);
	if (!bIsPanning)
	{
		return ContentLayer;
	}

	const FSlateBrush* CursorBrush = FAppStyle::GetBrush(TEXT("SoftwareCursor_Grab"));
	OutDrawElements.PushClip(FSlateClippingZone(CanvasPaintGeometry));
	FSlateDrawElement::MakeBox(
		OutDrawElements,
		ContentLayer + 1,
		AllottedGeometry.ToPaintGeometry(
			CursorBrush->ImageSize,
			FSlateLayoutTransform(SoftwareCursorPosition - CursorBrush->ImageSize / 2.0f)),
		CursorBrush);
	OutDrawElements.PopClip();
	return ContentLayer + 1;
}

FReply SVerseGraphCanvas::OnPreviewMouseButtonDown(
	const FGeometry& MyGeometry,
	const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() != EKeys::RightMouseButton || !VerticalScrollBox.IsValid())
	{
		return FReply::Unhandled();
	}

	const FVector2D LocalCursorPosition = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
	const FVector2D CanvasSize = VerticalScrollBox->GetCachedGeometry().GetLocalSize();
	if (LocalCursorPosition.X < 0.0f || LocalCursorPosition.Y < 0.0f
		|| LocalCursorPosition.X > CanvasSize.X || LocalCursorPosition.Y > CanvasSize.Y)
	{
		return FReply::Unhandled();
	}

	bIsPanning = true;
	SoftwareCursorPosition = LocalCursorPosition;
	Invalidate(EInvalidateWidgetReason::Paint);
	return FReply::Handled()
		.CaptureMouse(SharedThis(this))
		.UseHighPrecisionMouseMovement(SharedThis(this));
}

FReply SVerseGraphCanvas::OnMouseButtonUp(
	const FGeometry& MyGeometry,
	const FPointerEvent& MouseEvent)
{
	if (!bIsPanning || MouseEvent.GetEffectingButton() != EKeys::RightMouseButton)
	{
		return FReply::Unhandled();
	}

	const FVector2D CanvasSize = VerticalScrollBox->GetCachedGeometry().GetLocalSize();
	const FVector2D TopLeft = MyGeometry.LocalToAbsolute(FVector2D::ZeroVector);
	const FVector2D BottomRight = MyGeometry.LocalToAbsolute(CanvasSize);
	const FVector2D Unclamped = MyGeometry.LocalToAbsolute(SoftwareCursorPosition);
	const FVector2D CursorPosition(
		FMath::Clamp(Unclamped.X, TopLeft.X, BottomRight.X),
		FMath::Clamp(Unclamped.Y, TopLeft.Y, BottomRight.Y));
	bIsPanning = false;
	Invalidate(EInvalidateWidgetReason::Paint);
	return FReply::Handled()
		.ReleaseMouseCapture()
		.SetMousePos(FIntPoint(
			FMath::RoundToInt(CursorPosition.X),
			FMath::RoundToInt(CursorPosition.Y)));
}

FReply SVerseGraphCanvas::OnMouseMove(
	const FGeometry& MyGeometry,
	const FPointerEvent& MouseEvent)
{
	if (!bIsPanning || !HasMouseCapture())
	{
		return FReply::Unhandled();
	}

	const FVector2D CursorDelta = MouseEvent.GetCursorDelta();
	const float PreviousHorizontal = FMath::Clamp(
		HorizontalScrollBox->GetScrollOffset(), 0.0f, HorizontalScrollBox->GetScrollOffsetOfEnd());
	const float PreviousVertical = FMath::Clamp(
		VerticalScrollBox->GetScrollOffset(), 0.0f, VerticalScrollBox->GetScrollOffsetOfEnd());
	const float NewHorizontal = FMath::Clamp(
		PreviousHorizontal - CursorDelta.X, 0.0f, HorizontalScrollBox->GetScrollOffsetOfEnd());
	const float NewVertical = FMath::Clamp(
		PreviousVertical - CursorDelta.Y, 0.0f, VerticalScrollBox->GetScrollOffsetOfEnd());
	HorizontalScrollBox->SetScrollOffset(NewHorizontal);
	VerticalScrollBox->SetScrollOffset(NewVertical);
	SoftwareCursorPosition.X -= NewHorizontal - PreviousHorizontal;
	SoftwareCursorPosition.Y -= NewVertical - PreviousVertical;
	Invalidate(EInvalidateWidgetReason::Paint);
	return FReply::Handled();
}

FReply SVerseGraphCanvas::OnMouseWheel(
	const FGeometry& MyGeometry,
	const FPointerEvent& MouseEvent)
{
	const float NewZoom = FMath::Clamp(
		Zoom + FMath::Sign(MouseEvent.GetWheelDelta()) * GraphZoomStep,
		GraphMinimumZoom,
		GraphMaximumZoom);
	if (!FMath::IsNearlyEqual(NewZoom, Zoom))
	{
		Zoom = NewZoom;
		ScaleBox->SetUserSpecifiedScale(Zoom);
	}
	return FReply::Handled();
}

FCursorReply SVerseGraphCanvas::OnCursorQuery(
	const FGeometry& MyGeometry,
	const FPointerEvent& CursorEvent) const
{
	return FCursorReply::Cursor(bIsPanning ? EMouseCursor::None : EMouseCursor::Default);
}

void SVerseGraphCanvas::OnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent)
{
	bIsPanning = false;
	Invalidate(EInvalidateWidgetReason::Paint);
	SCompoundWidget::OnMouseCaptureLost(CaptureLostEvent);
}
