#pragma once

#include "VerseVisualTile.h"
#include "VerseGraphCoordinates.h"
#include "Widgets/SCompoundWidget.h"

class FVerseDocument;
struct FSlateRoundedBoxBrush;

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
FVector2D GetVerseExecutionPinAnchorCoordinate(bool bInput, bool bCompact);

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
	bool bOutput = false;
	/** Starting this drag adopts the source tile and clears its provisional state. */
	bool bAdoptsProvisionalTile = false;
	int32 ClauseInsertionIndex = INDEX_NONE;
	EPurpose Purpose = EPurpose::ValueConnection;
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
		, _HeaderPadding(FMargin(0.0f, 6.0f, 8.0f, 6.0f))
		, _ArrowPadding(FMargin(8.0f, 14.0f, 3.0f, 0.0f))
		, _ShowBody(true)
		, _Compact(false)
		, _CompactExecutionSpacing(false)
		, _IsSelected(false)
	{}
		SLATE_ARGUMENT(FVerseVisualTile, Tile)
		SLATE_ARGUMENT(TSharedPtr<const FVerseDocument>, Document)
		SLATE_ARGUMENT(FLinearColor, TileColor)
		SLATE_ARGUMENT(FLinearColor, UnselectedOutlineColor)
		SLATE_ARGUMENT(FMargin, HeaderPadding)
		SLATE_ARGUMENT(FMargin, ArrowPadding)
		SLATE_ARGUMENT(bool, ShowBody)
		SLATE_ARGUMENT(bool, Compact)
		SLATE_ARGUMENT(bool, CompactExecutionSpacing)
		SLATE_ARGUMENT(FText, DiagnosticText)
		SLATE_ARGUMENT(TArray<FText>, ExecutionOutputLabels)
		SLATE_ARGUMENT(TSet<FVerseVisualSocketId>, ConnectedSockets)
		SLATE_ATTRIBUTE(bool, IsSelected)
		SLATE_EVENT(FOnClicked, OnSelected)
		SLATE_EVENT(FOnClicked, OnOpened)
		SLATE_EVENT(FOnVerseSocketDragStarted, OnSocketDragStarted)
		SLATE_EVENT(FOnVerseInlineLiteralCommitted, OnInlineLiteralCommitted)
		SLATE_EVENT(FOnVerseClauseReordered, OnClauseReordered)
		SLATE_NAMED_SLOT(FArguments, BodyUnderlay)
		SLATE_NAMED_SLOT(FArguments, BodyContent)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	TSharedPtr<SWidget> GetSocketAnchor(const FVerseVisualSocketId& SocketId) const
	{
		const TSharedPtr<SWidget>* Anchor = SocketAnchors.Find(SocketId);
		return Anchor != nullptr ? *Anchor : nullptr;
	}
	FVector2D GetSocketAnchorCoordinate(const FVerseVisualSocketId& SocketId) const;
	const FVerseVisualTile& GetTile() const { return Tile; }
	/** Desired-layout Y coordinate of an indexed value pin center relative to this tile. */
	float GetValueSocketCenterY(int32 SocketIndex, bool bOutput) const;

	virtual FReply OnMouseButtonDoubleClick(
		const FGeometry& MyGeometry,
		const FPointerEvent& MouseEvent) override;
	virtual FReply OnDragDetected(
		const FGeometry& MyGeometry,
		const FPointerEvent& MouseEvent) override;
	virtual FReply OnDrop(
		const FGeometry& MyGeometry,
		const FDragDropEvent& DragDropEvent) override;

private:
	TSharedRef<SWidget> BuildHeader(bool bCompact, const FText& DiagnosticText) const;
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
	FReply HandleHeaderMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent);
	FReply ToggleExpanded();
	const FSlateBrush* GetExpansionImage() const;
	const FSlateBrush* GetHeaderBrush() const;
	EVisibility GetBodyVisibility() const;
	FSlateColor GetOutlineColor() const;

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
	TSharedPtr<SWidget> OperatorLineWidget;
	TSharedPtr<SWidget> HeaderSocketRow;
	TSharedPtr<SWidget> ValueInputColumn;
	TSharedPtr<SWidget> ValueOutputColumn;
	TArray<TSharedPtr<SWidget>> ValueInputRows;
	TArray<TSharedPtr<SWidget>> ValueOutputRows;
	TUniquePtr<FSlateRoundedBoxBrush> OuterBrush;
	TUniquePtr<FSlateRoundedBoxBrush> ExpandedHeaderBrush;
	TUniquePtr<FSlateRoundedBoxBrush> CollapsedHeaderBrush;
	TUniquePtr<FSlateRoundedBoxBrush> BodyBrush;
	bool bExpanded = true;
	bool bShowBody = true;
	bool bCollapsible = true;
	bool bCompactExecutionSpacing = false;
};
