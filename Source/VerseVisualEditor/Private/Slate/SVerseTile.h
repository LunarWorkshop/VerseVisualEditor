#pragma once

#include "VisualModel/VerseVisualTile.h"
#include "VerseVisualEditorSettings.h"
#include "Slate/VerseGraphCoordinates.h"
#include "Widgets/SCompoundWidget.h"

class FVerseDocument;
class SVerseGraphRenderScope;
class SVerseGraphMotionWidget;

struct FVerseFailablePatternSegment
{
	FVector2D Start;
	FVector2D End;
};

/** Local-space diamond crosshatch clipped to a failable block's interior. */
TArray<FVerseFailablePatternSegment> BuildVerseFailablePatternSegments(FVector2D Size);

/** Local-space centers of the four failable-block corner diamonds. */
TStaticArray<FVector2D, 4> BuildVerseFailableCornerCenters(FVector2D Size);

/** Normalized location at which an execution pin actually paints its home plate. */
FVector2D GetVerseExecutionPinAnchorCoordinate(
	bool bInput,
	bool bCompact,
	EVerseFunctionGraphPresentation Presentation =
		EVerseFunctionGraphPresentation::VerticalExecution);

/** Desired size used by the execution-pin widget for the same presentation. */
FVector2D GetVerseExecutionPinDesiredSize(
	bool bInput,
	bool bCompact,
	EVerseFunctionGraphPresentation Presentation =
		EVerseFunctionGraphPresentation::VerticalExecution);

/** Spline orientation used while dragging from an execution home plate. */
EVerseVisualConnectionAxis GetVerseExecutionPreviewAxis(
	EVerseFunctionGraphPresentation Presentation);

struct FVerseSocketDragStart
{
	enum class EPurpose : uint8
	{
		ValueConnection,
		ClauseInsertion,
	};

	TSharedPtr<SWidget> Anchor;
	FVerseVisualSocketEndpoint Endpoint;
	FVector2D AnchorCoordinate = FVector2D(0.5f, 0.5f);
	FVerseTextRange TileRange;
	/** Nearest statement ancestor used solely for lexical semantic lookup. */
	FVerseTextRange SemanticScopeRange;
	TOptional<FVerseVisualClauseDescriptor> Clause;
	EVerseVisualSocketInsertionKind InsertionKind =
		EVerseVisualSocketInsertionKind::Clause;
	FVerseTextRange InsertionOwnerRange;
	/** Existing provisional clause item replaced by this insertion gesture, if any. */
	TOptional<FVerseTextRange> ProvisionalReplacementRange;
	/** Value expression represented by an output binding or parameter socket. */
	FVerseTextRange BoundSourceRange;
	/** Omitted named/default input which must be materialized in its call. */
	FString MaterializedInputName;
	FVerseDesktopPoint DesktopPosition;
	FLinearColor WireColor = FLinearColor::White;
	EVerseExpressionOutcome Outcome = EVerseExpressionOutcome::Unresolved;
	/** Presentation-resolved spline orientation for the live preview. */
	EVerseVisualConnectionAxis PreviewAxis = EVerseVisualConnectionAxis::Horizontal;
	bool bOutput = false;
	/** Starting this drag adopts the source tile and clears its provisional state. */
	bool bAdoptsProvisionalTile = false;
	int32 ClauseInsertionIndex = INDEX_NONE;
	EPurpose Purpose = EPurpose::ValueConnection;
	TWeakPtr<SVerseGraphRenderScope> RenderScope;
	bool bScopedToNestedRenderScope = false;
};

DECLARE_DELEGATE_RetVal_OneParam(FReply, FOnVerseSocketDragStarted, const FVerseSocketDragStart&);
DECLARE_DELEGATE_TwoParams(FOnVerseInlineLiteralCommitted, FVerseTextRange, FText);
DECLARE_DELEGATE_RetVal_ThreeParams(
	FReply,
	FOnVerseClauseReordered,
	const FVerseVisualClauseDescriptor&,
	int32,
	int32);

/** Canvas-independent rendering of every Verse visual tile kind. */
class SVerseTile final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SVerseTile)
		: _TileColor(FLinearColor::White)
		, _UnselectedOutlineColor(FLinearColor::Transparent)
		, _HasMainContent(false)
		, _HasSourcePreview(false)
		, _Compact(false)
		, _CompactExecutionSpacing(false)
		, _FunctionGraphPresentation(EVerseFunctionGraphPresentation::VerticalExecution)
		, _IsSelected(false)
	{}
		SLATE_ARGUMENT(FVerseVisualTile, Tile)
		SLATE_ARGUMENT(TSharedPtr<const FVerseDocument>, Document)
		SLATE_ARGUMENT(FLinearColor, TileColor)
		SLATE_ARGUMENT(FLinearColor, UnselectedOutlineColor)
		SLATE_ARGUMENT(bool, HasMainContent)
		SLATE_ARGUMENT(bool, HasSourcePreview)
		SLATE_ARGUMENT(bool, Compact)
		SLATE_ARGUMENT(bool, CompactExecutionSpacing)
		SLATE_ARGUMENT(EVerseFunctionGraphPresentation, FunctionGraphPresentation)
		SLATE_ARGUMENT(FText, DiagnosticText)
		SLATE_ARGUMENT(TSet<FVerseVisualSocketId>, ConnectedSockets)
		SLATE_ATTRIBUTE(bool, IsSelected)
		SLATE_EVENT(FOnClicked, OnSelected)
		SLATE_EVENT(FOnClicked, OnOpened)
		SLATE_EVENT(FOnVerseSocketDragStarted, OnSocketDragStarted)
		SLATE_EVENT(FOnVerseInlineLiteralCommitted, OnInlineLiteralCommitted)
		SLATE_EVENT(FOnVerseClauseReordered, OnClauseReordered)
		SLATE_ARGUMENT(TSharedPtr<SVerseGraphRenderScope>, BodyRenderScope)
		SLATE_ARGUMENT(TSharedPtr<SVerseGraphRenderScope>, OwningRenderScope)
		/** Desired X center of a failable block's top insertion pin in body-local space. */
		SLATE_ARGUMENT(TOptional<float>, ClauseInsertionBodySpineX)
		SLATE_NAMED_SLOT(FArguments, MainContent)
		SLATE_NAMED_SLOT(FArguments, SourcePreview)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	TSharedPtr<SWidget> GetSocketAnchor(const FVerseVisualSocketId& SocketId) const
	{
		const TSharedPtr<SWidget>* Anchor = SocketAnchors.Find(SocketId);
		return Anchor != nullptr ? *Anchor : nullptr;
	}
	FVector2D GetSocketAnchorCoordinate(const FVerseVisualSocketId& SocketId) const;
	TWeakPtr<SVerseGraphRenderScope> GetSocketRenderScope(
		const FVerseVisualSocketId& SocketId) const
	{
		return Tile.Kind == EVerseVisualTileKind::FailableBlock
			&& SocketId.Role == EVerseVisualSocketRole::ClauseInsertion
			? BodyRenderScope
			: OwningRenderScope;
	}
	const FVerseVisualTile& GetTile() const { return Tile; }
#if WITH_DEV_AUTOMATION_TESTS
	bool HasIdentityBandForTesting() const { return bHasIdentityBand; }
	bool HasMainContentForTesting() const { return bHasMainContent; }
	bool HasSourcePreviewForTesting() const { return bHasSourcePreview; }
#endif
	void SetMotionTarget(TSharedPtr<SVerseGraphMotionWidget> InMotionTarget)
	{
		MotionTarget = MoveTemp(InMotionTarget);
	}
	TWeakPtr<SVerseGraphMotionWidget> GetMotionTarget() const { return MotionTarget; }
	/** Desired-layout Y coordinate of an indexed value pin center relative to this tile. */
	float GetValueSocketCenterY(int32 SocketIndex, bool bOutput) const;
	/**
	 * Desired-layout Y coordinate of this tile's primary horizontal execution
	 * anchor. This is derived from the composed socket dock that paints the pin;
	 * graph layout must not infer the execution lane from tile height.
	 */
	float GetHorizontalExecutionSpineY() const;

	virtual FReply OnMouseButtonDoubleClick(
		const FGeometry& MyGeometry,
		const FPointerEvent& MouseEvent) override;
	virtual FReply OnDragDetected(
		const FGeometry& MyGeometry,
		const FPointerEvent& MouseEvent) override;
	virtual FReply OnDrop(
		const FGeometry& MyGeometry,
		const FDragDropEvent& DragDropEvent) override;
	virtual bool SupportsKeyboardFocus() const override { return true; }

private:
	TSharedRef<SWidget> BuildIdentityBand(bool bCompact) const;
	TSharedRef<SWidget> BuildMainIdentity(bool bCompact) const;
	FOptionalSize GetSourcePreviewMaxWidth() const;
	TSharedRef<SWidget> BuildSocketColumn(TConstArrayView<FVerseVisualSocket> Sockets, bool bOutput);
	FReply HandleSocketMouseButtonDown(
		const FGeometry& Geometry,
		const FPointerEvent& MouseEvent,
		TSharedPtr<SWidget> Anchor,
		FVerseVisualSocket Socket,
		bool bOutput,
		int32 SocketIndex);
	FReply HandleClauseInsertionMouseButtonDown(
		const FGeometry& Geometry,
		const FPointerEvent& MouseEvent,
		TSharedPtr<SWidget> Anchor,
		FVector2D AnchorCoordinate,
		FVerseVisualSocketId SocketId,
		TOptional<FVerseVisualClauseDescriptor> Clause,
		EVerseVisualSocketInsertionKind InsertionKind,
		FVerseTextRange InsertionOwnerRange,
		int32 InsertIndex);
	FText Decode(FVerseByteRange Range) const;
	FText GetKindText() const;
	FText GetNameText() const;
	FText GetTypeText() const;
	FText GetSpecifierText() const;
	FText GetLineText() const;
	const FSlateBrush* GetIcon() const;
	FReply HandleTileMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent);
	FReply HandleIdentityMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent);
	FSlateColor GetOutlineColor() const;
	FSlateColor GetShadowColor() const;

	FVerseVisualTile Tile;
	TSharedPtr<const FVerseDocument> Document;
	TAttribute<bool> IsSelected;
	FOnClicked OnSelected;
	FOnClicked OnOpened;
	FOnVerseSocketDragStarted OnSocketDragStarted;
	FOnVerseInlineLiteralCommitted OnInlineLiteralCommitted;
	FOnVerseClauseReordered OnClauseReordered;
	FLinearColor UnselectedOutlineColor = FLinearColor::Transparent;
	TMap<FVerseVisualSocketId, TSharedPtr<SWidget>> SocketAnchors;
	TSet<FVerseVisualSocketId> ConnectedSockets;
	TSharedPtr<SWidget> IdentityBandWidget;
	TSharedPtr<SWidget> MainSocketRowWidget;
	TSharedPtr<SWidget> HorizontalExecutionInputDockWidget;
	TSharedPtr<SWidget> HorizontalExecutionOutputDockWidget;
	TSharedPtr<SWidget> ValueInputColumn;
	TSharedPtr<SWidget> ValueOutputColumn;
	TSharedPtr<SWidget> ValueOutputDockWidget;
	TArray<TSharedPtr<SWidget>> ValueInputRows;
	TArray<TSharedPtr<SWidget>> ValueOutputRows;
	TWeakPtr<SVerseGraphRenderScope> OwningRenderScope;
	TWeakPtr<SVerseGraphRenderScope> BodyRenderScope;
	TWeakPtr<SVerseGraphMotionWidget> MotionTarget;
	bool bHasIdentityBand = true;
	bool bHasMainContent = false;
	bool bHasSourcePreview = false;
	bool bCompactExecutionSpacing = false;
	EVerseFunctionGraphPresentation FunctionGraphPresentation =
		EVerseFunctionGraphPresentation::VerticalExecution;
};
