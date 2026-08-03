#pragma once

#include "Input/CursorReply.h"
#include "Input/Reply.h"
#include "Slate/SVerseTile.h"
#include "Slate/VerseCanvasViewState.h"
#include "Slate/VerseGraphCoordinates.h"
#include "Slate/VerseGraphMotion.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SLeafWidget.h"

class SScaleBox;
class SBox;
class SScrollBar;
class SScrollBox;

enum class EVerseGraphConnectionAxis : uint8
{
	Horizontal,
	Vertical,
};

/** Applies a prototype presentation to an otherwise presentation-neutral wire. */
EVerseGraphConnectionAxis GetVersePresentedConnectionAxis(
	EVerseVisualConnectionAxis ModelAxis,
	EVerseVisualSocketRole SourceRole,
	EVerseFunctionGraphPresentation Presentation);

struct FVerseGraphConnection
{
	FVerseVisualSocketEndpoint Source;
	FVerseVisualSocketEndpoint Target;
	TSharedPtr<const class FVerseGraphEndpointRegistry> EndpointRegistry;
	EVerseGraphConnectionAxis Axis = EVerseGraphConnectionAxis::Horizontal;
	FLinearColor Color = FLinearColor::White;
	float Thickness = 2.0f;
	int32 ExtraBlankLineMarkers = 0;
	EVerseExpressionOutcome Outcome = EVerseExpressionOutcome::Unresolved;
};

struct FVerseGraphEndpointBinding
{
	TWeakPtr<SWidget> Anchor;
	FVector2D AnchorCoordinate = FVector2D(0.5f, 0.5f);
	TWeakPtr<class SVerseGraphRenderScope> RenderScope;
	TWeakPtr<SVerseGraphMotionWidget> MotionOwner;
	bool bScopedToNestedRenderScope = false;
};

/** Resolves immutable model endpoints to the widgets arranged for the current graph revision. */
class FVerseGraphEndpointRegistry
{
public:
	void Register(FVerseVisualSocketEndpoint Endpoint, FVerseGraphEndpointBinding Binding);
	const FVerseGraphEndpointBinding* Find(FVerseVisualSocketEndpoint Endpoint) const;

private:
	TMap<FVerseVisualSocketEndpoint, FVerseGraphEndpointBinding> Bindings;
};

using FVerseGraphArrangedEndpointMap =
	TMap<TSharedRef<SWidget>, FArrangedWidget>;

/** Recursive graph region that owns its background, wires, clipping, and content. */
class SVerseGraphRenderScope final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SVerseGraphRenderScope)
		: _Background(EVerseGraphRenderScopeBackground::Root)
		, _ClipToBounds(false)
		, _VisibilityGuardOnly(false)
	{}
		SLATE_ARGUMENT(TArray<FVerseGraphConnection>, Connections)
		SLATE_ARGUMENT(EVerseGraphRenderScopeBackground, Background)
		SLATE_ARGUMENT(bool, ClipToBounds)
		SLATE_ARGUMENT(bool, VisibilityGuardOnly)
		SLATE_DEFAULT_SLOT(FArguments, Content)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	void SetConnections(TArray<FVerseGraphConnection> InConnections);
	void SetContent(TSharedRef<SWidget> InContent);
	bool WasPaintedThisFrame() const;
	bool WasPaintedRecently() const;
	bool CanSupplyVisibleEndpoints() const;
	FVerseGraphArrangedEndpointMap ArrangeEndpointsForPaint(
		const FGeometry& AllottedGeometry) const;
	virtual int32 OnPaint(
		const FPaintArgs& Args,
		const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements,
		int32 LayerId,
		const FWidgetStyle& InWidgetStyle,
		bool bParentEnabled) const override;

private:
	TArray<FVerseGraphConnection> Connections;
	EVerseGraphRenderScopeBackground Background = EVerseGraphRenderScopeBackground::Root;
	bool bVisibilityGuardOnly = false;
	mutable uint64 LastPaintFrame = MAX_uint64;
};

/** Samples decoration centers from the same Hermite spline used for graph wires. */
TArray<FVector2D> BuildVerseSplineMarkerCenters(
	FVector2D Start,
	FVector2D StartTangent,
	FVector2D End,
	FVector2D EndTangent,
	float Spacing = 72.0f);

/** Adjusts scrolling so a rebuilt graph anchor remains in the same viewport position. */
FVector2D ComputeVerseAnchorLockedScrollOffset(
	FVector2D CurrentScrollOffset,
	FVector2D PreviousAnchorDesktopPosition,
	FVector2D CurrentAnchorDesktopPosition);

DECLARE_DELEGATE_TwoParams(
	FOnVerseGraphConnectionDropped,
	const FVerseSocketDragStart&,
	FVerseDesktopPoint);

/** Shared transform, interaction, background, and connection owner for every Verse graph. */
class SVerseGraphSurface final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SVerseGraphSurface)
		: _UseEdgePanPadding(false)
	{}
		SLATE_ARGUMENT(bool, UseEdgePanPadding)
		SLATE_ARGUMENT(TSharedPtr<SWidget>, InitialAnchor)
		SLATE_ARGUMENT(TArray<FVerseGraphConnection>, Connections)
		SLATE_ARGUMENT(TSharedPtr<FVerseGraphMotionController>, MotionController)
		SLATE_EVENT(FSimpleDelegate, OnBackgroundClicked)
		SLATE_EVENT(FOnVerseGraphConnectionDropped, OnConnectionDropped)
		SLATE_EVENT(FSimpleDelegate, OnConnectionCancelled)
		SLATE_DEFAULT_SLOT(FArguments, Content)
	SLATE_END_ARGS()

	void Construct(
		const FArguments& InArgs,
		FVerseCanvasViewState InitialViewState,
		bool bCenterInitially);

	FVerseCanvasViewState GetViewState() const;
	bool FocusWidget(const TSharedPtr<SWidget>& Widget, float Padding = 20.0f);
	FReply BeginConnectionDrag(const FVerseSocketDragStart& DragStart);
	void EndConnectionPreview();
	void SetContent(TSharedRef<SWidget> InContent);
	void SetContentAndAnchor(
		TSharedRef<SWidget> InContent,
		TSharedPtr<SWidget> InAnchor);
	void SetInitialAnchor(TSharedPtr<SWidget> InAnchor);
	void SetConnections(TArray<FVerseGraphConnection> InConnections);
	TSharedRef<FVerseGraphMotionController> GetMotionController() const
	{
		return MotionController.ToSharedRef();
	}

	virtual void Tick(
		const FGeometry& AllottedGeometry,
		double InCurrentTime,
		float InDeltaTime) override;
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
	virtual FReply OnMouseButtonDown(
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
	FVector2D GetCanvasSize() const;
	FVector2D GetGraphOrigin() const;
	FVerseGraphArrangedEndpointMap ArrangeEndpointsForPaint(
		const FGeometry& AllottedGeometry,
		TSharedPtr<SWidget> AdditionalAnchor = nullptr) const;
	int32 PaintConnections(
		FSlateWindowElementList& OutDrawElements,
		int32 LayerId,
		const FVerseGraphArrangedEndpointMap& ArrangedEndpoints) const;
	void PaintConnection(
		const FVerseGraphConnection& Connection,
		FSlateWindowElementList& OutDrawElements,
		int32 LayerId,
		const FVerseGraphArrangedEndpointMap& ArrangedEndpoints) const;
	void PaintPreviewConnection(
		FSlateWindowElementList& OutDrawElements,
		int32 LayerId,
		const FVerseGraphArrangedEndpointMap& ArrangedEndpoints) const;

	TSharedPtr<SScrollBar> HorizontalScrollbar;
	TSharedPtr<SScrollBar> VerticalScrollbar;
	TSharedPtr<SScrollBox> HorizontalScrollBox;
	TSharedPtr<SScrollBox> VerticalScrollBox;
	TSharedPtr<SScaleBox> ScaleBox;
	TSharedPtr<SBox> ContentHost;
	TWeakPtr<SWidget> InitialAnchor;
	TOptional<FVector2D> PendingAnchorDesktopPosition;
	TArray<FVerseGraphConnection> Connections;
	TOptional<FVerseSocketDragStart> ConnectionDrag;
	TSharedPtr<FVerseGraphMotionController> MotionController;
	FVerseCanvasPoint PreviewEndpoint;
	FOnVerseGraphConnectionDropped OnConnectionDropped;
	FSimpleDelegate OnConnectionCancelled;
	FSimpleDelegate OnBackgroundClicked;
	float Zoom = 1.0f;
	bool bUseEdgePanPadding = false;
	bool bPendingInitialCenter = false;
	bool bIsPanning = false;
	bool bPreviewFrozen = false;
	FVerseCanvasPoint SoftwareCursorPosition;
};
