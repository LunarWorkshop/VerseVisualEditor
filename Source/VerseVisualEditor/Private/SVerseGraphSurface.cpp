#include "SVerseGraphSurface.h"

#include "Brushes/SlateColorBrush.h"
#include "GraphEditorSettings.h"
#include "Layout/Clipping.h"
#include "Rendering/DrawElements.h"
#include "Styling/AppStyle.h"
#include "VerseGraphBackground.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/Layout/SScrollBar.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSpacer.h"

TArray<FVector2D> BuildVerseSplineMarkerCenters(
	FVector2D Start,
	FVector2D StartTangent,
	FVector2D End,
	FVector2D EndTangent,
	float Spacing)
{
	const float Distance = FVector2D::Distance(Start, End);
	if (Distance < 24.0f)
	{
		return {};
	}
	const int32 MarkerCount = FMath::Max(
		1,
		FMath::FloorToInt(Distance / FMath::Max(1.0f, Spacing)));
	TArray<FVector2D> Result;
	Result.Reserve(MarkerCount);
	for (int32 Index = 0; Index < MarkerCount; ++Index)
	{
		const float Alpha = static_cast<float>(Index + 1)
			/ static_cast<float>(MarkerCount + 1);
		Result.Add(FMath::CubicInterp(
			Start, StartTangent, End, EndTangent, Alpha));
	}
	return Result;
}

namespace
{
	constexpr float MinimumZoom = 0.5f;
	constexpr float MaximumZoom = 2.0f;
	constexpr float ZoomStep = 0.1f;
	constexpr float EdgePeek = 48.0f;

	FVersePaintPoint AnchorPoint(
		const TWeakPtr<SWidget>& WeakAnchor,
		FVector2D NormalizedCoordinate = FVector2D(0.5f, 0.5f))
	{
		const TSharedPtr<SWidget> Anchor = WeakAnchor.Pin();
		if (!Anchor.IsValid())
		{
			return FVersePaintPoint();
		}
		return FVersePaintPoint(
			Anchor->GetPaintSpaceGeometry().GetAbsolutePositionAtCoordinates(NormalizedCoordinate));
	}

	FVector2D GetSplineTangent(
		FVector2D Start,
		FVector2D End,
		EVerseGraphConnectionAxis Axis)
	{
		return Axis == EVerseGraphConnectionAxis::Vertical
			? FVector2D(0.0f, FMath::Max(24.0f, FMath::Abs(End.Y - Start.Y) * 0.5f))
			: FVector2D(GetDefault<UGraphEditorSettings>()->ComputeSplineTangent(Start, End));
	}

	void DrawSpline(
		FSlateWindowElementList& Elements,
		int32 Layer,
		FVersePaintPoint StartPoint,
		FVersePaintPoint EndPoint,
		EVerseGraphConnectionAxis Axis,
		float Thickness,
		FLinearColor Color)
	{
		const FVector2D Start = StartPoint.Value;
		const FVector2D End = EndPoint.Value;
		const FVector2D Tangent = GetSplineTangent(Start, End, Axis);
		FSlateDrawElement::MakeDrawSpaceSpline(
			Elements,
			Layer,
			Start,
			Tangent,
			End,
			Tangent,
			Thickness,
			ESlateDrawEffect::None,
			Color);
	}

	void DrawFailureMarkers(
		FSlateWindowElementList& Elements,
		int32 Layer,
		FVersePaintPoint StartPoint,
		FVersePaintPoint EndPoint,
		EVerseGraphConnectionAxis Axis)
	{
		const FVector2D Start = StartPoint.Value;
		const FVector2D End = EndPoint.Value;
		const FVector2D Tangent = GetSplineTangent(Start, End, Axis);
		static const FSlateColorBrush WhiteBrush(FLinearColor::White);
		const FVector2D MarkerSize(7.0f, 7.0f);
		for (const FVector2D Center : BuildVerseSplineMarkerCenters(
			Start, Tangent, End, Tangent))
		{
			FSlateDrawElement::MakeRotatedBox(
				Elements,
				Layer,
				FPaintGeometry(Center - MarkerSize * 0.5f, MarkerSize, 1.0f),
				&WhiteBrush,
				ESlateDrawEffect::None,
				PI * 0.25f,
				MarkerSize * 0.5f,
				FSlateDrawElement::RelativeToElement,
				GetVerseFailureDecorationColor());
		}
	}

	void PaintConnectionRecord(
		const FVerseGraphConnection& Connection,
		FSlateWindowElementList& OutDrawElements,
		int32 LayerId)
	{
		const TSharedPtr<SWidget> Source = Connection.SourceAnchor.Pin();
		const TSharedPtr<SWidget> Target = Connection.TargetAnchor.Pin();
		if (!Source.IsValid() || !Target.IsValid())
		{
			return;
		}
		const FVersePaintPoint Start = AnchorPoint(
			Source, Connection.SourceAnchorCoordinate);
		const FVersePaintPoint End = AnchorPoint(
			Target, Connection.TargetAnchorCoordinate);
		DrawSpline(
			OutDrawElements, LayerId, Start, End,
			Connection.Axis, Connection.Thickness, Connection.Color);
		if (Connection.Outcome == EVerseExpressionOutcome::FailableValue
			|| Connection.Outcome == EVerseExpressionOutcome::FailureOnly)
		{
			DrawFailureMarkers(
				OutDrawElements, LayerId, Start, End, Connection.Axis);
		}
		for (int32 Index = 0;
			Index < Connection.ExtraBlankLineMarkers;
			++Index)
		{
			const float Alpha = static_cast<float>(Index + 1)
				/ static_cast<float>(Connection.ExtraBlankLineMarkers + 1);
			const FVector2D Center =
				FMath::Lerp(Start.Value, End.Value, Alpha);
			TArray<FVector2f> Points({
				FVector2f(Center - FVector2D(6.0f, 0.0f)),
				FVector2f(Center + FVector2D(6.0f, 0.0f))});
			FSlateDrawElement::MakeLines(
				OutDrawElements,
				LayerId,
				FPaintGeometry(),
				MoveTemp(Points),
				ESlateDrawEffect::None,
				Connection.Color,
				true,
				Connection.Thickness);
		}
	}
}

void SVerseGraphConnectionLayer::Construct(const FArguments& InArgs)
{
	Connections = InArgs._Connections;
	SetCanTick(false);
	SetVisibility(EVisibility::HitTestInvisible);
}

void SVerseGraphConnectionLayer::SetConnections(
	TArray<FVerseGraphConnection> InConnections)
{
	Connections = MoveTemp(InConnections);
	Invalidate(EInvalidateWidgetReason::Paint);
}

int32 SVerseGraphConnectionLayer::OnPaint(
	const FPaintArgs& Args,
	const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements,
	int32 LayerId,
	const FWidgetStyle& InWidgetStyle,
	bool bParentEnabled) const
{
	for (const FVerseGraphConnection& Connection : Connections)
	{
		PaintConnectionRecord(Connection, OutDrawElements, LayerId);
	}
	return LayerId;
}

void SVerseGraphSurface::Construct(
	const FArguments& InArgs,
	FVerseCanvasViewState InitialViewState,
	bool bCenterInitially)
{
	Zoom = FMath::Clamp(InitialViewState.Zoom, MinimumZoom, MaximumZoom);
	bUseEdgePanPadding = InArgs._UseEdgePanPadding;
	bPendingInitialCenter = bCenterInitially;
	InitialAnchor = InArgs._InitialAnchor;
	Connections = InArgs._Connections;
	OnConnectionDropped = InArgs._OnConnectionDropped;
	OnConnectionCancelled = InArgs._OnConnectionCancelled;
	OnBackgroundClicked = InArgs._OnBackgroundClicked;
	HorizontalScrollbar = SNew(SScrollBar).Orientation(Orient_Horizontal);
	VerticalScrollbar = SNew(SScrollBar).Orientation(Orient_Vertical);

	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().FillHeight(1.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f)
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
						.Padding(this, &SVerseGraphSurface::GetPanPadding)
						[
							SAssignNew(ScaleBox, SScaleBox)
							.Stretch(EStretch::UserSpecified)
							.StretchDirection(EStretchDirection::Both)
							.UserSpecifiedScale(Zoom)
							.HAlign(HAlign_Left)
							.VAlign(VAlign_Top)
							[
								SAssignNew(ContentHost, SBox)
								[
									InArgs._Content.Widget
								]
							]
						]
					]
				]
			]
			+ SHorizontalBox::Slot().AutoWidth()[VerticalScrollbar.ToSharedRef()]
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f)[HorizontalScrollbar.ToSharedRef()]
			+ SHorizontalBox::Slot().AutoWidth()[SNew(SSpacer).Size(FVector2D(12.0f))]
		]
	];

	if (!bCenterInitially)
	{
		HorizontalScrollBox->SetScrollOffset(FMath::Max(0.0, InitialViewState.ScrollOffset.X));
		VerticalScrollBox->SetScrollOffset(FMath::Max(0.0, InitialViewState.ScrollOffset.Y));
	}
}

FVerseCanvasViewState SVerseGraphSurface::GetViewState() const
{
	FVerseCanvasViewState State;
	State.ScrollOffset = FVector2D(
		HorizontalScrollBox.IsValid() ? HorizontalScrollBox->GetScrollOffset() : 0.0f,
		VerticalScrollBox.IsValid() ? VerticalScrollBox->GetScrollOffset() : 0.0f);
	State.Zoom = Zoom;
	return State;
}

bool SVerseGraphSurface::FocusWidget(const TSharedPtr<SWidget>& Widget, float Padding)
{
	if (!Widget.IsValid() || !HorizontalScrollBox.IsValid() || !VerticalScrollBox.IsValid())
	{
		return false;
	}
	HorizontalScrollBox->ScrollDescendantIntoView(
		Widget, true, EDescendantScrollDestination::Center, Padding);
	VerticalScrollBox->ScrollDescendantIntoView(
		Widget, true, EDescendantScrollDestination::Center, Padding);
	Invalidate(EInvalidateWidgetReason::Paint);
	return true;
}

FReply SVerseGraphSurface::BeginConnectionDrag(const FVerseSocketDragStart& DragStart)
{
	if (!DragStart.Anchor.IsValid())
	{
		return FReply::Unhandled();
	}
	ConnectionDrag = DragStart;
	bPreviewFrozen = false;
	PreviewEndpoint = VerseDesktopToCanvas(
		GetTickSpaceGeometry(),
		DragStart.DesktopPosition);
	Invalidate(EInvalidateWidgetReason::Paint);
	return FReply::Handled().CaptureMouse(SharedThis(this));
}

void SVerseGraphSurface::EndConnectionPreview()
{
	ConnectionDrag.Reset();
	bPreviewFrozen = false;
	Invalidate(EInvalidateWidgetReason::Paint);
}

void SVerseGraphSurface::SetContent(TSharedRef<SWidget> InContent)
{
	if (ContentHost.IsValid())
	{
		ContentHost->SetContent(InContent);
		Invalidate(EInvalidateWidgetReason::LayoutAndVolatility);
	}
}

void SVerseGraphSurface::SetInitialAnchor(TSharedPtr<SWidget> InAnchor)
{
	InitialAnchor = MoveTemp(InAnchor);
}

void SVerseGraphSurface::SetConnections(TArray<FVerseGraphConnection> InConnections)
{
	Connections = MoveTemp(InConnections);
	Invalidate(EInvalidateWidgetReason::Paint);
}

void SVerseGraphSurface::Tick(
	const FGeometry& AllottedGeometry,
	double InCurrentTime,
	float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	if (!bPendingInitialCenter || !HorizontalScrollBox.IsValid() || !VerticalScrollBox.IsValid())
	{
		return;
	}
	const TSharedPtr<SWidget> Anchor = InitialAnchor.IsValid() ? InitialAnchor.Pin() : ScaleBox;
	if (Anchor.IsValid()
		&& Anchor->GetTickSpaceGeometry().GetLocalSize().GetMin() > 0.0f
		&& HorizontalScrollBox->GetScrollOffsetOfEnd() > 0.0f
		&& VerticalScrollBox->GetScrollOffsetOfEnd() > 0.0f)
	{
		HorizontalScrollBox->ScrollDescendantIntoView(
			Anchor, false, EDescendantScrollDestination::Center, 0.0f);
		VerticalScrollBox->ScrollDescendantIntoView(
			Anchor, false, EDescendantScrollDestination::TopOrLeft, 48.0f);
		bPendingInitialCenter = false;
	}
}

FMargin SVerseGraphSurface::GetPanPadding() const
{
	if (!bUseEdgePanPadding)
	{
		return FMargin(0.0f);
	}
	const FVector2D ViewSize = GetCanvasSize();
	return FMargin(
		FMath::Max(EdgePeek, ViewSize.X - EdgePeek),
		FMath::Max(EdgePeek, ViewSize.Y - EdgePeek));
}

FVector2D SVerseGraphSurface::GetCanvasSize() const
{
	return VerticalScrollBox.IsValid()
		? VerticalScrollBox->GetTickSpaceGeometry().GetLocalSize()
		: FVector2D::ZeroVector;
}

FVector2D SVerseGraphSurface::GetGraphOrigin() const
{
	const FMargin Padding = GetPanPadding();
	return FVector2D(
		Padding.Left - (HorizontalScrollBox.IsValid() ? HorizontalScrollBox->GetScrollOffset() : 0.0f),
		Padding.Top - (VerticalScrollBox.IsValid() ? VerticalScrollBox->GetScrollOffset() : 0.0f));
}

int32 SVerseGraphSurface::OnPaint(
	const FPaintArgs& Args,
	const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements,
	int32 LayerId,
	const FWidgetStyle& InWidgetStyle,
	bool bParentEnabled) const
{
	const FVector2D CanvasSize = GetCanvasSize();
	const FPaintGeometry CanvasGeometry = AllottedGeometry.ToPaintGeometry(
		CanvasSize, FSlateLayoutTransform(FVector2D::ZeroVector));
	OutDrawElements.PushClip(FSlateClippingZone(CanvasGeometry));
	PaintVerseGraphBackground(
		CanvasGeometry, CanvasSize, GetGraphOrigin(), Zoom, OutDrawElements, LayerId);
	OutDrawElements.PopClip();

	const int32 ContentLayer = SCompoundWidget::OnPaint(
		Args, AllottedGeometry, MyCullingRect, OutDrawElements,
		LayerId + 3, InWidgetStyle, bParentEnabled);
	OutDrawElements.PushClip(FSlateClippingZone(CanvasGeometry));
	const int32 ConnectionLayer =
		PaintConnections(OutDrawElements, LayerId + 2);
	OutDrawElements.PopClip();

	int32 ResultLayer = FMath::Max(ContentLayer, ConnectionLayer);
	if (bIsPanning)
	{
		const FSlateBrush* CursorBrush = FAppStyle::GetBrush(TEXT("SoftwareCursor_Grab"));
		OutDrawElements.PushClip(FSlateClippingZone(CanvasGeometry));
		FSlateDrawElement::MakeBox(
			OutDrawElements,
			ResultLayer + 1,
			AllottedGeometry.ToPaintGeometry(
				CursorBrush->ImageSize,
				FSlateLayoutTransform(
					SoftwareCursorPosition.Value - CursorBrush->ImageSize * 0.5f)),
			CursorBrush);
		OutDrawElements.PopClip();
		++ResultLayer;
	}
	return ResultLayer;
}

int32 SVerseGraphSurface::PaintConnections(
	FSlateWindowElementList& OutDrawElements,
	int32 LayerId) const
{
	for (const FVerseGraphConnection& Connection : Connections)
	{
		PaintConnection(Connection, OutDrawElements, LayerId);
	}
	PaintPreviewConnection(OutDrawElements, LayerId);
	return LayerId;
}

void SVerseGraphSurface::PaintConnection(
	const FVerseGraphConnection& Connection,
	FSlateWindowElementList& OutDrawElements,
	int32 LayerId) const
{
	PaintConnectionRecord(Connection, OutDrawElements, LayerId);
}

void SVerseGraphSurface::PaintPreviewConnection(
	FSlateWindowElementList& OutDrawElements,
	int32 LayerId) const
{
	if (!ConnectionDrag.IsSet() || !ConnectionDrag->Anchor.IsValid())
	{
		return;
	}
	const FVersePaintPoint Free = VerseCanvasToPaint(GetPaintSpaceGeometry(), PreviewEndpoint);
	const FVersePaintPoint Fixed = AnchorPoint(ConnectionDrag->Anchor);
	const FVersePaintPoint Start = ConnectionDrag->bOutput ? Fixed : Free;
	const FVersePaintPoint End = ConnectionDrag->bOutput ? Free : Fixed;
	DrawSpline(
		OutDrawElements, LayerId, Start, End,
		EVerseGraphConnectionAxis::Horizontal, 2.0f,
		ConnectionDrag->WireColor);
	if (ConnectionDrag->Outcome == EVerseExpressionOutcome::FailableValue
		|| ConnectionDrag->Outcome == EVerseExpressionOutcome::FailureOnly)
	{
		DrawFailureMarkers(
			OutDrawElements,
			LayerId,
			Start,
			End,
			EVerseGraphConnectionAxis::Horizontal);
	}
}

FReply SVerseGraphSurface::OnPreviewMouseButtonDown(
	const FGeometry& MyGeometry,
	const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() != EKeys::RightMouseButton || !VerticalScrollBox.IsValid())
	{
		return FReply::Unhandled();
	}
	const FVerseCanvasPoint Local = VerseDesktopToCanvas(
		MyGeometry, FVerseDesktopPoint(MouseEvent.GetScreenSpacePosition()));
	const FVector2D CanvasSize = GetCanvasSize();
	if (Local.Value.X < 0.0f || Local.Value.Y < 0.0f
		|| Local.Value.X > CanvasSize.X || Local.Value.Y > CanvasSize.Y)
	{
		return FReply::Unhandled();
	}
	bIsPanning = true;
	SoftwareCursorPosition = Local;
	Invalidate(EInvalidateWidgetReason::Paint);
	return FReply::Handled()
		.CaptureMouse(SharedThis(this))
		.UseHighPrecisionMouseMovement(SharedThis(this));
}

FReply SVerseGraphSurface::OnMouseButtonDown(
	const FGeometry& MyGeometry,
	const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton && OnBackgroundClicked.IsBound())
	{
		OnBackgroundClicked.Execute();
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

FReply SVerseGraphSurface::OnMouseButtonUp(
	const FGeometry& MyGeometry,
	const FPointerEvent& MouseEvent)
{
	if (ConnectionDrag.IsSet() && MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		PreviewEndpoint = VerseDesktopToCanvas(
			MyGeometry, FVerseDesktopPoint(MouseEvent.GetScreenSpacePosition()));
		bPreviewFrozen = true;
		OnConnectionDropped.ExecuteIfBound(
			ConnectionDrag.GetValue(),
			FVerseDesktopPoint(MouseEvent.GetScreenSpacePosition()));
		Invalidate(EInvalidateWidgetReason::Paint);
		return FReply::Handled().ReleaseMouseCapture();
	}
	if (!bIsPanning || MouseEvent.GetEffectingButton() != EKeys::RightMouseButton)
	{
		return FReply::Unhandled();
	}
	const FVector2D CanvasSize = GetCanvasSize();
	const FVector2D TopLeft = MyGeometry.LocalToAbsolute(FVector2D::ZeroVector);
	const FVector2D BottomRight = MyGeometry.LocalToAbsolute(CanvasSize);
	const FVector2D Unclamped = MyGeometry.LocalToAbsolute(SoftwareCursorPosition.Value);
	bIsPanning = false;
	Invalidate(EInvalidateWidgetReason::Paint);
	return FReply::Handled()
		.ReleaseMouseCapture()
		.SetMousePos(FIntPoint(
			FMath::RoundToInt(FMath::Clamp(Unclamped.X, TopLeft.X, BottomRight.X)),
			FMath::RoundToInt(FMath::Clamp(Unclamped.Y, TopLeft.Y, BottomRight.Y))));
}

FReply SVerseGraphSurface::OnMouseMove(
	const FGeometry& MyGeometry,
	const FPointerEvent& MouseEvent)
{
	if (ConnectionDrag.IsSet() && HasMouseCapture())
	{
		PreviewEndpoint = VerseDesktopToCanvas(
			MyGeometry, FVerseDesktopPoint(MouseEvent.GetScreenSpacePosition()));
		Invalidate(EInvalidateWidgetReason::Paint);
		return FReply::Handled();
	}
	if (!bIsPanning || !HasMouseCapture())
	{
		return FReply::Unhandled();
	}
	const FVector2D Delta = MouseEvent.GetCursorDelta();
	const float OldX = FMath::Clamp(
		HorizontalScrollBox->GetScrollOffset(), 0.0f, HorizontalScrollBox->GetScrollOffsetOfEnd());
	const float OldY = FMath::Clamp(
		VerticalScrollBox->GetScrollOffset(), 0.0f, VerticalScrollBox->GetScrollOffsetOfEnd());
	const float NewX = FMath::Clamp(
		OldX - Delta.X, 0.0f, HorizontalScrollBox->GetScrollOffsetOfEnd());
	const float NewY = FMath::Clamp(
		OldY - Delta.Y, 0.0f, VerticalScrollBox->GetScrollOffsetOfEnd());
	HorizontalScrollBox->SetScrollOffset(NewX);
	VerticalScrollBox->SetScrollOffset(NewY);
	SoftwareCursorPosition.Value.X -= NewX - OldX;
	SoftwareCursorPosition.Value.Y -= NewY - OldY;
	Invalidate(EInvalidateWidgetReason::Paint);
	return FReply::Handled();
}

FReply SVerseGraphSurface::OnMouseWheel(
	const FGeometry& MyGeometry,
	const FPointerEvent& MouseEvent)
{
	const float NewZoom = FMath::Clamp(
		Zoom + FMath::Sign(MouseEvent.GetWheelDelta()) * ZoomStep,
		MinimumZoom, MaximumZoom);
	if (!FMath::IsNearlyEqual(NewZoom, Zoom))
	{
		const FVerseCanvasPoint Cursor = VerseDesktopToCanvas(
			MyGeometry,
			FVerseDesktopPoint(MouseEvent.GetScreenSpacePosition()));
		const FMargin PanPadding = GetPanPadding();
		const FVector2D AnchoredScrollOffset = VerseScrollOffsetForZoomAnchor(
			Cursor,
			FVector2D(
				HorizontalScrollBox->GetScrollOffset(),
				VerticalScrollBox->GetScrollOffset()),
			FVector2D(PanPadding.Left, PanPadding.Top),
			Zoom,
			NewZoom);
		Zoom = NewZoom;
		ScaleBox->SetUserSpecifiedScale(Zoom);
		HorizontalScrollBox->SetScrollOffset(FMath::Max(0.0, AnchoredScrollOffset.X));
		VerticalScrollBox->SetScrollOffset(FMath::Max(0.0, AnchoredScrollOffset.Y));
		Invalidate(EInvalidateWidgetReason::LayoutAndVolatility);
	}
	return FReply::Handled();
}

FCursorReply SVerseGraphSurface::OnCursorQuery(
	const FGeometry& MyGeometry,
	const FPointerEvent& CursorEvent) const
{
	return FCursorReply::Cursor(bIsPanning ? EMouseCursor::None : EMouseCursor::Default);
}

void SVerseGraphSurface::OnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent)
{
	bIsPanning = false;
	if (ConnectionDrag.IsSet() && !bPreviewFrozen)
	{
		ConnectionDrag.Reset();
		OnConnectionCancelled.ExecuteIfBound();
	}
	Invalidate(EInvalidateWidgetReason::Paint);
	SCompoundWidget::OnMouseCaptureLost(CaptureLostEvent);
}
