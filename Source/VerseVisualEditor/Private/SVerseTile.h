#pragma once

#include "VerseVisualTile.h"
#include "VerseGraphCoordinates.h"
#include "Widgets/SCompoundWidget.h"

class FVerseDocument;
struct FSlateRoundedBoxBrush;

struct FVerseSocketDragStart
{
	TSharedPtr<SWidget> Anchor;
	FVerseVisualTile Tile;
	FVerseVisualSocket Socket;
	FVerseDesktopPoint DesktopPosition;
	FLinearColor WireColor = FLinearColor::White;
	bool bOutput = false;
	int32 SocketIndex = INDEX_NONE;
};

DECLARE_DELEGATE_RetVal_OneParam(FReply, FOnVerseSocketDragStarted, const FVerseSocketDragStart&);

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
		SLATE_ARGUMENT(FText, DiagnosticText)
		SLATE_ATTRIBUTE(bool, IsSelected)
		SLATE_EVENT(FOnClicked, OnSelected)
		SLATE_EVENT(FOnClicked, OnOpened)
		SLATE_EVENT(FOnVerseSocketDragStarted, OnSocketDragStarted)
		SLATE_NAMED_SLOT(FArguments, BodyContent)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	TSharedPtr<SWidget> GetValueInputAnchor(int32 Index) const
	{
		return ValueInputAnchors.IsValidIndex(Index) ? ValueInputAnchors[Index] : nullptr;
	}
	TSharedPtr<SWidget> GetValueOutputAnchor(int32 Index) const
	{
		return ValueOutputAnchors.IsValidIndex(Index) ? ValueOutputAnchors[Index] : nullptr;
	}
	TSharedPtr<SWidget> GetFirstValueInputAnchor() const { return GetValueInputAnchor(0); }
	TSharedPtr<SWidget> GetFirstValueOutputAnchor() const { return GetValueOutputAnchor(0); }
	TSharedPtr<SWidget> GetExecutionInputAnchor() const { return ExecutionInputAnchor; }
	TSharedPtr<SWidget> GetExecutionOutputAnchor() const { return ExecutionOutputAnchor; }

	virtual FReply OnMouseButtonDoubleClick(
		const FGeometry& MyGeometry,
		const FPointerEvent& MouseEvent) override;

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
	FLinearColor UnselectedOutlineColor = FLinearColor::Transparent;
	TArray<TSharedPtr<SWidget>> ValueInputAnchors;
	TArray<TSharedPtr<SWidget>> ValueOutputAnchors;
	TSharedPtr<SWidget> ExecutionInputAnchor;
	TSharedPtr<SWidget> ExecutionOutputAnchor;
	TUniquePtr<FSlateRoundedBoxBrush> OuterBrush;
	TUniquePtr<FSlateRoundedBoxBrush> ExpandedHeaderBrush;
	TUniquePtr<FSlateRoundedBoxBrush> CollapsedHeaderBrush;
	TUniquePtr<FSlateRoundedBoxBrush> BodyBrush;
	bool bExpanded = true;
	bool bShowBody = true;
};
