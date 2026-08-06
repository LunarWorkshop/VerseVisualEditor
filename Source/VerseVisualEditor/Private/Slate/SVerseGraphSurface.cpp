#include "Slate/SVerseGraphSurface.h"

#include "Brushes/SlateColorBrush.h"
#include "CoreGlobals.h"
#include "GraphEditorSettings.h"
#include "Layout/Clipping.h"
#include "Rendering/DrawElements.h"
#include "Styling/AppStyle.h"
#include "Slate/VerseGraphBackground.h"
#include "Slate/VerseVisualEditorStyle.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/Layout/SScrollBar.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSpacer.h"

EVerseGraphConnectionAxis GetVersePresentedConnectionAxis(
	EVerseVisualConnectionAxis ModelAxis,
	EVerseVisualSocketRole SourceRole,
	EVerseFunctionGraphPresentation Presentation)
{
	const bool bExecutionConnection = SourceRole == EVerseVisualSocketRole::Execution
		|| SourceRole == EVerseVisualSocketRole::ClauseInsertion;
	if (bExecutionConnection
		&& Presentation != EVerseFunctionGraphPresentation::VerticalExecution)
	{
		return EVerseGraphConnectionAxis::Horizontal;
	}
	return ModelAxis == EVerseVisualConnectionAxis::Horizontal
		? EVerseGraphConnectionAxis::Horizontal
		: EVerseGraphConnectionAxis::Vertical;
}

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

FVector2D ComputeVerseAnchorLockedScrollOffset(
	FVector2D CurrentScrollOffset,
	FVector2D PreviousAnchorDesktopPosition,
	FVector2D CurrentAnchorDesktopPosition)
{
	return CurrentScrollOffset
		+ CurrentAnchorDesktopPosition
		- PreviousAnchorDesktopPosition;
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

	FVersePaintPoint AnchorPoint(
		const FGeometry& Geometry,
		FVector2D NormalizedCoordinate)
	{
		return FVersePaintPoint(
			Geometry.GetAbsolutePositionAtCoordinates(NormalizedCoordinate));
	}

	TSet<TSharedRef<SWidget>> CollectConnectionAnchors(
		TConstArrayView<FVerseGraphConnection> Connections,
		TSharedPtr<SWidget> AdditionalAnchor = nullptr)
	{
		TSet<TSharedRef<SWidget>> Anchors;
		for (const FVerseGraphConnection& Connection : Connections)
		{
			if (!Connection.EndpointRegistry.IsValid())
			{
				continue;
			}
			for (const FVerseVisualSocketEndpoint Endpoint : {
				Connection.Source, Connection.Target})
			{
				const FVerseGraphEndpointBinding* Binding =
					Connection.EndpointRegistry->Find(Endpoint);
				const TSharedPtr<SWidget> Anchor = Binding != nullptr
					? Binding->Anchor.Pin()
					: nullptr;
				if (Anchor.IsValid())
				{
					Anchors.Add(Anchor.ToSharedRef());
				}
			}
		}
		if (AdditionalAnchor.IsValid())
		{
			Anchors.Add(AdditionalAnchor.ToSharedRef());
		}
		return Anchors;
	}

	FVector2D GetSplineTangent(
		FVector2D Start,
		FVector2D End,
		EVerseGraphConnectionAxis Axis)
	{
		if (Axis == EVerseGraphConnectionAxis::Vertical)
		{
			const float Direction = End.Y >= Start.Y ? 1.0f : -1.0f;
			return FVector2D(
				0.0f,
				Direction * FMath::Max(24.0f, FMath::Abs(End.Y - Start.Y) * 0.5f));
		}
		return FVector2D(GetDefault<UGraphEditorSettings>()->ComputeSplineTangent(Start, End));
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
		EVerseGraphConnectionAxis Axis,
		float Opacity = 1.0f)
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
				GetVerseFailureDecorationColor().CopyWithNewOpacity(Opacity));
		}
	}

	void PaintConnectionRecord(
		const FVerseGraphConnection& Connection,
		FSlateWindowElementList& OutDrawElements,
		int32 LayerId,
		const FVerseGraphArrangedEndpointMap& ArrangedEndpoints,
		TOptional<float> RenderScopeRight = {},
		TOptional<float> RenderScopeTop = {})
	{
		if (!Connection.EndpointRegistry.IsValid())
		{
			return;
		}
		const FVerseGraphEndpointBinding* SourceBinding =
			Connection.EndpointRegistry->Find(Connection.Source);
		const bool bSocketTarget =
			Connection.Terminal == EVerseVisualConnectionTerminal::Socket;
		const FVerseGraphEndpointBinding* TargetBinding = bSocketTarget
			? Connection.EndpointRegistry->Find(Connection.Target)
			: nullptr;
		if (SourceBinding == nullptr || (bSocketTarget && TargetBinding == nullptr))
		{
			return;
		}
		auto IsBindingVisible = [](const FVerseGraphEndpointBinding& Binding)
		{
			if (!Binding.bScopedToNestedRenderScope)
			{
				return true;
			}
			const TSharedPtr<SVerseGraphRenderScope> Scope = Binding.RenderScope.Pin();
			return Scope.IsValid() && Scope->CanSupplyVisibleEndpoints();
		};
		if (!IsBindingVisible(*SourceBinding)
			|| (TargetBinding != nullptr && !IsBindingVisible(*TargetBinding)))
		{
			return;
		}
		const TSharedPtr<SWidget> Source = SourceBinding->Anchor.Pin();
		const TSharedPtr<SWidget> Target = TargetBinding != nullptr
			? TargetBinding->Anchor.Pin() : nullptr;
		if (!Source.IsValid() || (bSocketTarget && !Target.IsValid()))
		{
			return;
		}
		const FArrangedWidget* SourceArrangement =
			ArrangedEndpoints.Find(Source.ToSharedRef());
		const FArrangedWidget* TargetArrangement = Target.IsValid()
			? ArrangedEndpoints.Find(Target.ToSharedRef()) : nullptr;
		if (SourceArrangement == nullptr || (bSocketTarget && TargetArrangement == nullptr))
		{
			return;
		}
		const FGeometry& SourceGeometry = SourceArrangement->Geometry;
		if (!Source->GetVisibility().IsVisible()
			|| SourceGeometry.GetLocalSize().GetMin() <= 0.0f
			|| (Target.IsValid() && !Target->GetVisibility().IsVisible())
			|| (TargetArrangement != nullptr
				&& TargetArrangement->Geometry.GetLocalSize().GetMin() <= 0.0f))
		{
			return;
		}
		const FVersePaintPoint Start = AnchorPoint(
			SourceGeometry, SourceBinding->AnchorCoordinate);
		FVersePaintPoint End;
		if (bSocketTarget)
		{
			End = AnchorPoint(
				TargetArrangement->Geometry, TargetBinding->AnchorCoordinate);
		}
		else
		{
			if (Connection.TerminalEdge == EVerseGraphTerminalEdge::Top)
			{
				const float EndY = RenderScopeTop.IsSet()
					? RenderScopeTop.GetValue()
					: Start.Value.Y - 96.0f;
				End = FVersePaintPoint(FVector2D(
					Start.Value.X, FMath::Min(Start.Value.Y, EndY)));
			}
			else
			{
				const float EndX =
					Connection.Terminal == EVerseVisualConnectionTerminal::RenderScopeRightBoundary
						&& RenderScopeRight.IsSet()
						? RenderScopeRight.GetValue()
						: Start.Value.X + 96.0f;
				End = FVersePaintPoint(FVector2D(
					FMath::Max(Start.Value.X, EndX), Start.Value.Y));
			}
		}
		const TSharedPtr<SVerseGraphMotionWidget> SourceMotion =
			SourceBinding->MotionOwner.Pin();
		const TSharedPtr<SVerseGraphMotionWidget> TargetMotion = TargetBinding != nullptr
			? TargetBinding->MotionOwner.Pin() : nullptr;
		const float WireOpacity = FMath::Min(
			SourceMotion.IsValid() ? SourceMotion->GetCurrentOpacity() : 1.0f,
			TargetMotion.IsValid() ? TargetMotion->GetCurrentOpacity() : 1.0f);
		FLinearColor WireColor = Connection.Color;
		WireColor.A *= WireOpacity;
		DrawSpline(
			OutDrawElements, LayerId, Start, End,
			Connection.Axis, Connection.Thickness, WireColor);
		if (Connection.Outcome == EVerseExpressionOutcome::FailableValue
			|| Connection.Outcome == EVerseExpressionOutcome::FailureOnly)
		{
			DrawFailureMarkers(
				OutDrawElements, LayerId, Start, End, Connection.Axis, WireOpacity);
		}
		if (Connection.Terminal == EVerseVisualConnectionTerminal::GoldDiamond)
		{
			static const FSlateColorBrush WhiteBrush(FLinearColor::White);
			const FVector2D MarkerSize(8.0f, 8.0f);
			FSlateDrawElement::MakeRotatedBox(
				OutDrawElements,
				LayerId + 1,
				FPaintGeometry(End.Value - MarkerSize * 0.5f, MarkerSize, 1.0f),
				&WhiteBrush,
				ESlateDrawEffect::None,
				PI * 0.25f,
				MarkerSize * 0.5f,
				FSlateDrawElement::RelativeToElement,
				GetVerseFailureDecorationColor().CopyWithNewOpacity(WireOpacity));
		}
		else if (Connection.Terminal == EVerseVisualConnectionTerminal::RedX)
		{
			const FVector2D Half(4.0f, 4.0f);
			TArray<FVector2f> First({FVector2f(End.Value - Half), FVector2f(End.Value + Half)});
			TArray<FVector2f> Second({
				FVector2f(End.Value + FVector2D(-Half.X, Half.Y)),
				FVector2f(End.Value + FVector2D(Half.X, -Half.Y))});
			FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 1, FPaintGeometry(),
				MoveTemp(First), ESlateDrawEffect::None, FLinearColor(1.0f, 0.08f, 0.05f), true, 2.0f);
			FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 1, FPaintGeometry(),
				MoveTemp(Second), ESlateDrawEffect::None, FLinearColor(1.0f, 0.08f, 0.05f), true, 2.0f);
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
				WireColor,
				true,
				Connection.Thickness);
		}
	}
}

void FVerseGraphEndpointRegistry::Register(
	FVerseVisualSocketEndpoint Endpoint,
	FVerseGraphEndpointBinding Binding)
{
	Bindings.Add(Endpoint, MoveTemp(Binding));
}

const FVerseGraphEndpointBinding* FVerseGraphEndpointRegistry::Find(
	FVerseVisualSocketEndpoint Endpoint) const
{
	return Bindings.Find(Endpoint);
}

void FVerseGraphEndpointRegistry::SetDragStates(
	TMap<FVerseVisualSocketEndpoint, EVerseSocketDragVisualState> InStates)
{
	DragStates = MoveTemp(InStates);
	HoveredEndpoint.Reset();
}

void FVerseGraphEndpointRegistry::SetHoveredEndpoint(
	TOptional<FVerseVisualSocketEndpoint> Endpoint)
{
	HoveredEndpoint = Endpoint;
}

void FVerseGraphEndpointRegistry::ClearDragStates()
{
	DragStates.Reset();
	HoveredEndpoint.Reset();
}

EVerseSocketDragVisualState FVerseGraphEndpointRegistry::GetDragState(
	FVerseVisualSocketEndpoint Endpoint) const
{
	if (HoveredEndpoint.IsSet() && HoveredEndpoint.GetValue() == Endpoint
		&& IsCompatibleTarget(Endpoint))
	{
		return EVerseSocketDragVisualState::HoveredCompatible;
	}
	return DragStates.FindRef(Endpoint);
}

bool FVerseGraphEndpointRegistry::IsCompatibleTarget(
	FVerseVisualSocketEndpoint Endpoint) const
{
	const EVerseSocketDragVisualState State = DragStates.FindRef(Endpoint);
	return State == EVerseSocketDragVisualState::Compatible
		|| State == EVerseSocketDragVisualState::HoveredCompatible;
}

void SVerseGraphRenderScope::Construct(const FArguments& InArgs)
{
	Connections = InArgs._Connections;
	Background = InArgs._Background;
	bVisibilityGuardOnly = InArgs._VisibilityGuardOnly;
	SetCanTick(false);
	SetClipping(InArgs._ClipToBounds
		? EWidgetClipping::ClipToBounds
		: EWidgetClipping::Inherit);
	ChildSlot
	[
		InArgs._Content.Widget
	];
}

void SVerseGraphRenderScope::SetConnections(
	TArray<FVerseGraphConnection> InConnections)
{
	Connections = MoveTemp(InConnections);
	Invalidate(EInvalidateWidgetReason::Paint);
}

void SVerseGraphRenderScope::SetContent(TSharedRef<SWidget> InContent)
{
	ChildSlot[InContent];
	Invalidate(EInvalidateWidgetReason::LayoutAndVolatility);
}

bool SVerseGraphRenderScope::WasPaintedThisFrame() const
{
	return LastPaintFrame == GFrameCounter;
}

bool SVerseGraphRenderScope::WasPaintedRecently() const
{
	return LastPaintFrame != MAX_uint64 && GFrameCounter <= LastPaintFrame + 1;
}

bool SVerseGraphRenderScope::CanSupplyVisibleEndpoints() const
{
	return GetVisibility().IsVisible();
}

FVerseGraphArrangedEndpointMap SVerseGraphRenderScope::ArrangeEndpointsForPaint(
	const FGeometry& AllottedGeometry) const
{
	const TSet<TSharedRef<SWidget>> Anchors =
		CollectConnectionAnchors(Connections);
	FVerseGraphArrangedEndpointMap Result;
	if (!Anchors.IsEmpty())
	{
		FindChildGeometries(AllottedGeometry, Anchors, Result);
	}
	return Result;
}

int32 SVerseGraphRenderScope::OnPaint(
	const FPaintArgs& Args,
	const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements,
	int32 LayerId,
	const FWidgetStyle& InWidgetStyle,
	bool bParentEnabled) const
{
	LastPaintFrame = GFrameCounter;
	int32 BackgroundLayer = LayerId;
	if (Background == EVerseGraphRenderScopeBackground::Failable)
	{
		const FLinearColor WidgetTint = InWidgetStyle.GetColorAndOpacityTint();
		const ISlateStyle& VisualStyle = VerseVisualEditorStyle::Get();
		const FLinearColor FailureGlass =
			VisualStyle.GetColor(TEXT("Color.FailureGlass")) * WidgetTint;
		FSlateDrawElement::MakeBox(
			OutDrawElements,
			BackgroundLayer,
			AllottedGeometry.ToPaintGeometry(),
			VisualStyle.GetBrush(TEXT("Tile.BodyOverlay")),
			ESlateDrawEffect::None,
			FailureGlass);
		const FLinearColor PatternColor =
			VisualStyle.GetColor(TEXT("Color.FailurePattern")) * WidgetTint;
		for (const FVerseFailablePatternSegment& Segment :
			BuildVerseFailablePatternSegments(AllottedGeometry.GetLocalSize()))
		{
			TArray<FVector2f> Points({FVector2f(Segment.Start), FVector2f(Segment.End)});
			FSlateDrawElement::MakeLines(
				OutDrawElements,
				BackgroundLayer + 1,
				AllottedGeometry.ToPaintGeometry(),
				MoveTemp(Points),
				ESlateDrawEffect::None,
				PatternColor,
				true,
				1.0f);
		}
		++BackgroundLayer;
	}
	else if (Background == EVerseGraphRenderScopeBackground::Synchronization)
	{
		const FLinearColor WidgetTint = InWidgetStyle.GetColorAndOpacityTint();
		const ISlateStyle& VisualStyle = VerseVisualEditorStyle::Get();
		FSlateDrawElement::MakeBox(
			OutDrawElements,
			BackgroundLayer,
			AllottedGeometry.ToPaintGeometry(),
			VisualStyle.GetBrush(TEXT("Tile.BodyOverlay")),
			ESlateDrawEffect::None,
			VisualStyle.GetColor(TEXT("Color.SynchronizationGlass")) * WidgetTint);
		const FLinearColor ThreadColor =
			VisualStyle.GetColor(TEXT("Color.SynchronizationThread")) * WidgetTint;
		const FVector2D Size = AllottedGeometry.GetLocalSize();
		for (float Y = 18.0f; Y < Size.Y; Y += 24.0f)
		{
			TArray<FVector2f> Points({FVector2f(0.0f, Y), FVector2f(Size.X, Y)});
			FSlateDrawElement::MakeLines(
				OutDrawElements,
				BackgroundLayer + 1,
				AllottedGeometry.ToPaintGeometry(),
				MoveTemp(Points),
				ESlateDrawEffect::None,
				ThreadColor,
				true,
				1.0f);
		}
		++BackgroundLayer;
	}

	const int32 ConnectionLayer = BackgroundLayer + 1;
	const FVerseGraphArrangedEndpointMap ArrangedEndpoints =
		ArrangeEndpointsForPaint(AllottedGeometry);
	for (const FVerseGraphConnection& Connection : Connections)
	{
		PaintConnectionRecord(
			Connection,
			OutDrawElements,
			ConnectionLayer,
			ArrangedEndpoints,
			AllottedGeometry.GetRenderBoundingRect().Right - 2.0f,
			AllottedGeometry.GetRenderBoundingRect().Top + 2.0f);
	}
	const int32 ContentLayer = SCompoundWidget::OnPaint(
		Args,
		AllottedGeometry,
		MyCullingRect,
		OutDrawElements,
		ConnectionLayer + 1,
		InWidgetStyle,
		bParentEnabled);
	return FMath::Max(ContentLayer, ConnectionLayer);
}

void SVerseGraphSurface::Construct(
	const FArguments& InArgs,
	FVerseCanvasViewState InitialViewState,
	bool bCenterInitially)
{
	Zoom = FMath::Clamp(InitialViewState.Zoom, MinimumZoom, MaximumZoom);
	bUseEdgePanPadding = InArgs._UseEdgePanPadding;
	BackgroundTint = InArgs._BackgroundTint;
	bPendingInitialCenter = bCenterInitially;
	InitialAnchor = InArgs._InitialAnchor;
	Connections = InArgs._Connections;
	EndpointRegistry = InArgs._EndpointRegistry;
	MotionController = InArgs._MotionController.IsValid()
		? InArgs._MotionController
		: MakeShared<FVerseGraphMotionController>();
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

FReply SVerseGraphSurface::BeginConnectionDrag(
	const FVerseSocketDragStart& DragStart,
	TMap<FVerseVisualSocketEndpoint, EVerseSocketDragVisualState> DragStates)
{
	if (!DragStart.Anchor.IsValid())
	{
		return FReply::Unhandled();
	}
	ConnectionDrag = DragStart;
	if (EndpointRegistry.IsValid())
	{
		EndpointRegistry->SetDragStates(MoveTemp(DragStates));
	}
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
	if (EndpointRegistry.IsValid())
	{
		EndpointRegistry->ClearDragStates();
	}
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

void SVerseGraphSurface::SetContentAndAnchor(
	TSharedRef<SWidget> InContent,
	TSharedPtr<SWidget> InAnchor)
{
	const TSharedPtr<SWidget> PreviousAnchor = InitialAnchor.Pin();
	if (PreviousAnchor.IsValid()
		&& PreviousAnchor->GetTickSpaceGeometry().GetLocalSize().GetMin() > 0.0f)
	{
		PendingAnchorDesktopPosition =
			PreviousAnchor->GetTickSpaceGeometry().GetAbsolutePosition();
	}
	else
	{
		PendingAnchorDesktopPosition.Reset();
	}
	InitialAnchor = MoveTemp(InAnchor);
	if (MotionController.IsValid())
	{
		// The replacement anchor has not been arranged yet. Do not let new motion
		// widgets record poses in the old anchor's coordinate space.
		MotionController->InvalidateSurfaceGeometry();
	}
	SetContent(InContent);
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

void SVerseGraphSurface::SetEndpointRegistry(
	TSharedPtr<FVerseGraphEndpointRegistry> InRegistry)
{
	if (EndpointRegistry.IsValid())
	{
		EndpointRegistry->ClearDragStates();
	}
	EndpointRegistry = MoveTemp(InRegistry);
	Invalidate(EInvalidateWidgetReason::Paint);
}

void SVerseGraphSurface::SetBackgroundTint(FLinearColor InBackgroundTint)
{
	BackgroundTint = InBackgroundTint;
	Invalidate(EInvalidateWidgetReason::Paint);
}

void SVerseGraphSurface::Tick(
	const FGeometry& AllottedGeometry,
	double InCurrentTime,
	float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	if (ConnectionDrag.IsSet() && EndpointRegistry.IsValid())
	{
		// Socket halos pulse while the pointer is stationary; paint invalidation is
		// sufficient because drag feedback never participates in desired layout.
		Invalidate(EInvalidateWidgetReason::Paint);
	}
	const TSharedPtr<SWidget> Anchor = InitialAnchor.Pin();
	if (PendingAnchorDesktopPosition.IsSet()
		&& Anchor.IsValid()
		&& Anchor->GetTickSpaceGeometry().GetLocalSize().GetMin() > 0.0f
		&& HorizontalScrollBox.IsValid()
		&& VerticalScrollBox.IsValid())
	{
		const FVector2D LockedScrollOffset = ComputeVerseAnchorLockedScrollOffset(
			FVector2D(
				HorizontalScrollBox->GetScrollOffset(),
				VerticalScrollBox->GetScrollOffset()),
			PendingAnchorDesktopPosition.GetValue(),
			Anchor->GetTickSpaceGeometry().GetAbsolutePosition());
		HorizontalScrollBox->SetScrollOffset(FMath::Clamp(
			LockedScrollOffset.X, 0.0f, HorizontalScrollBox->GetScrollOffsetOfEnd()));
		VerticalScrollBox->SetScrollOffset(FMath::Clamp(
			LockedScrollOffset.Y, 0.0f, VerticalScrollBox->GetScrollOffsetOfEnd()));
		PendingAnchorDesktopPosition.Reset();
	}
	if (MotionController.IsValid() && ContentHost.IsValid())
	{
		// Descendant cached geometry is from the preceding layout epoch here. An
		// anchored graph becomes resolvable when its anchor ticks with current
		// AllottedGeometry; unanchored file graphs are immediately resolvable.
		MotionController->SetSurfaceGeometry(
			ContentHost->GetTickSpaceGeometry(), Zoom, InitialAnchor.IsValid());
	}
	if (ConnectionDrag.IsSet() && ConnectionDrag->bScopedToNestedRenderScope)
	{
		const TSharedPtr<SVerseGraphRenderScope> Scope = ConnectionDrag->RenderScope.Pin();
		if (!Scope.IsValid() || !Scope->CanSupplyVisibleEndpoints())
		{
			ConnectionDrag.Reset();
			if (EndpointRegistry.IsValid())
			{
				EndpointRegistry->ClearDragStates();
			}
			bPreviewFrozen = false;
			OnConnectionCancelled.ExecuteIfBound();
		}
	}
	if (!bPendingInitialCenter || !HorizontalScrollBox.IsValid() || !VerticalScrollBox.IsValid())
	{
		return;
	}
	const TSharedPtr<SWidget> CenterAnchor = InitialAnchor.IsValid() ? InitialAnchor.Pin() : ScaleBox;
	if (CenterAnchor.IsValid()
		&& CenterAnchor->GetTickSpaceGeometry().GetLocalSize().GetMin() > 0.0f
		&& HorizontalScrollBox->GetScrollOffsetOfEnd() > 0.0f
		&& VerticalScrollBox->GetScrollOffsetOfEnd() > 0.0f)
	{
		HorizontalScrollBox->ScrollDescendantIntoView(
			CenterAnchor, false, EDescendantScrollDestination::Center, 0.0f);
		VerticalScrollBox->ScrollDescendantIntoView(
			CenterAnchor, false, EDescendantScrollDestination::TopOrLeft, 48.0f);
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

FVerseGraphArrangedEndpointMap SVerseGraphSurface::ArrangeEndpointsForPaint(
	const FGeometry& AllottedGeometry,
	TSharedPtr<SWidget> AdditionalAnchor) const
{
	TSet<TSharedRef<SWidget>> Anchors =
		CollectConnectionAnchors(Connections, MoveTemp(AdditionalAnchor));
	if (EndpointRegistry.IsValid())
	{
		for (const TPair<FVerseVisualSocketEndpoint, FVerseGraphEndpointBinding>& Pair :
			EndpointRegistry->GetBindings())
		{
			if (const TSharedPtr<SWidget> Anchor = Pair.Value.Anchor.Pin())
			{
				Anchors.Add(Anchor.ToSharedRef());
			}
		}
	}
	FVerseGraphArrangedEndpointMap Result;
	if (!Anchors.IsEmpty())
	{
		FindChildGeometries(AllottedGeometry, Anchors, Result);
	}
	return Result;
}

TOptional<FVerseVisualSocketEndpoint> SVerseGraphSurface::FindCompatibleEndpointAt(
	const FGeometry& AllottedGeometry,
	FVerseDesktopPoint Position) const
{
	if (!EndpointRegistry.IsValid())
	{
		return {};
	}
	const FVerseGraphArrangedEndpointMap Arranged =
		ArrangeEndpointsForPaint(AllottedGeometry);
	TOptional<FVerseVisualSocketEndpoint> Best;
	float BestDistanceSquared = TNumericLimits<float>::Max();
	for (const TPair<FVerseVisualSocketEndpoint, FVerseGraphEndpointBinding>& Pair :
		EndpointRegistry->GetBindings())
	{
		if (!EndpointRegistry->IsCompatibleTarget(Pair.Key))
		{
			continue;
		}
		if (Pair.Value.bScopedToNestedRenderScope)
		{
			const TSharedPtr<SVerseGraphRenderScope> Scope =
				Pair.Value.RenderScope.Pin();
			if (!Scope.IsValid() || !Scope->CanSupplyVisibleEndpoints())
			{
				continue;
			}
		}
		const TSharedPtr<SWidget> Anchor = Pair.Value.Anchor.Pin();
		const FArrangedWidget* Widget = Anchor.IsValid()
			? Arranged.Find(Anchor.ToSharedRef()) : nullptr;
		if (Widget == nullptr)
		{
			continue;
		}
		const FVector2D Center = Widget->Geometry.GetAbsolutePositionAtCoordinates(
			Pair.Value.AnchorCoordinate);
		const FVector2D Extent = Widget->Geometry.GetAbsoluteSize() * 0.5f
			+ FVector2D(7.0f, 7.0f);
		const FVector2D Delta = Position.Value - Center;
		if (FMath::Abs(Delta.X) <= Extent.X && FMath::Abs(Delta.Y) <= Extent.Y)
		{
			const float DistanceSquared = Delta.SizeSquared();
			if (DistanceSquared < BestDistanceSquared)
			{
				Best = Pair.Key;
				BestDistanceSquared = DistanceSquared;
			}
		}
	}
	return Best;
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
		CanvasGeometry, CanvasSize, GetGraphOrigin(), Zoom, OutDrawElements, LayerId,
		BackgroundTint);
	OutDrawElements.PopClip();

	const int32 ContentLayer = SCompoundWidget::OnPaint(
		Args, AllottedGeometry, MyCullingRect, OutDrawElements,
		LayerId + 3, InWidgetStyle, bParentEnabled);
	OutDrawElements.PushClip(FSlateClippingZone(CanvasGeometry));
	const TSharedPtr<SWidget> PreviewAnchor = ConnectionDrag.IsSet()
		? ConnectionDrag->Anchor
		: nullptr;
	const FVerseGraphArrangedEndpointMap ArrangedEndpoints =
		ArrangeEndpointsForPaint(AllottedGeometry, PreviewAnchor);
	const int32 ConnectionLayer = PaintConnections(
		OutDrawElements, LayerId + 2, ArrangedEndpoints);
	OutDrawElements.PopClip();

	int32 ResultLayer = FMath::Max(ContentLayer, ConnectionLayer);
	if (ConnectionDrag.IsSet())
	{
		// Fixed graph connections belong beneath tiles, but the connection being
		// dragged is direct manipulation feedback and must remain visible over
		// nested panels such as an if's failable condition context.
		OutDrawElements.PushClip(FSlateClippingZone(CanvasGeometry));
		PaintPreviewConnection(
			OutDrawElements, ResultLayer + 1, ArrangedEndpoints);
		OutDrawElements.PopClip();
		++ResultLayer;
	}
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
	int32 LayerId,
	const FVerseGraphArrangedEndpointMap& ArrangedEndpoints) const
{
	for (const FVerseGraphConnection& Connection : Connections)
	{
		PaintConnection(
			Connection, OutDrawElements, LayerId, ArrangedEndpoints);
	}
	return LayerId;
}

void SVerseGraphSurface::PaintConnection(
	const FVerseGraphConnection& Connection,
	FSlateWindowElementList& OutDrawElements,
	int32 LayerId,
	const FVerseGraphArrangedEndpointMap& ArrangedEndpoints) const
{
	PaintConnectionRecord(
		Connection, OutDrawElements, LayerId, ArrangedEndpoints);
}

void SVerseGraphSurface::PaintPreviewConnection(
	FSlateWindowElementList& OutDrawElements,
	int32 LayerId,
	const FVerseGraphArrangedEndpointMap& ArrangedEndpoints) const
{
	if (!ConnectionDrag.IsSet() || !ConnectionDrag->Anchor.IsValid())
	{
		return;
	}
	if (ConnectionDrag->bScopedToNestedRenderScope)
	{
		const TSharedPtr<SVerseGraphRenderScope> Scope = ConnectionDrag->RenderScope.Pin();
		if (!Scope.IsValid() || !Scope->CanSupplyVisibleEndpoints())
		{
			return;
		}
	}
	const TSharedPtr<SWidget> FixedAnchor = ConnectionDrag->Anchor;
	const FArrangedWidget* FixedArrangement = FixedAnchor.IsValid()
		? ArrangedEndpoints.Find(FixedAnchor.ToSharedRef())
		: nullptr;
	if (FixedArrangement == nullptr)
	{
		return;
	}
	const FVersePaintPoint Free = VerseCanvasToPaint(GetPaintSpaceGeometry(), PreviewEndpoint);
	const FVersePaintPoint Fixed = AnchorPoint(
		FixedArrangement->Geometry,
		ConnectionDrag->AnchorCoordinate);
	const FVersePaintPoint Start = ConnectionDrag->bOutput ? Fixed : Free;
	const FVersePaintPoint End = ConnectionDrag->bOutput ? Free : Fixed;
	const EVerseGraphConnectionAxis PreviewAxis =
		ConnectionDrag->PreviewAxis == EVerseVisualConnectionAxis::Vertical
			? EVerseGraphConnectionAxis::Vertical
			: EVerseGraphConnectionAxis::Horizontal;
	DrawSpline(
		OutDrawElements, LayerId, Start, End,
		PreviewAxis,
		2.0f,
		ConnectionDrag->WireColor);
	if (ConnectionDrag->Outcome == EVerseExpressionOutcome::FailableValue
		|| ConnectionDrag->Outcome == EVerseExpressionOutcome::FailureOnly)
	{
		DrawFailureMarkers(
			OutDrawElements,
			LayerId,
			Start,
			End,
			PreviewAxis);
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
		const TOptional<FVerseVisualSocketEndpoint> Target =
			FindCompatibleEndpointAt(
				MyGeometry,
				FVerseDesktopPoint(MouseEvent.GetScreenSpacePosition()));
		if (EndpointRegistry.IsValid())
		{
			EndpointRegistry->ClearDragStates();
		}
		OnConnectionDropped.ExecuteIfBound(
			ConnectionDrag.GetValue(),
			FVerseDesktopPoint(MouseEvent.GetScreenSpacePosition()),
			Target);
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
		if (EndpointRegistry.IsValid())
		{
			EndpointRegistry->SetHoveredEndpoint(FindCompatibleEndpointAt(
				MyGeometry,
				FVerseDesktopPoint(MouseEvent.GetScreenSpacePosition())));
		}
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
		if (EndpointRegistry.IsValid())
		{
			EndpointRegistry->ClearDragStates();
		}
		OnConnectionCancelled.ExecuteIfBound();
	}
	Invalidate(EInvalidateWidgetReason::Paint);
	SCompoundWidget::OnMouseCaptureLost(CaptureLostEvent);
}
