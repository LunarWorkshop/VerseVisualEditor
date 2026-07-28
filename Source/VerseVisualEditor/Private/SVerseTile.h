#pragma once

#include "VerseVisualTile.h"
#include "Widgets/SCompoundWidget.h"

class FVerseDocument;
struct FSlateRoundedBoxBrush;

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
		SLATE_NAMED_SLOT(FArguments, BodyContent)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	TSharedPtr<SWidget> GetFirstValueInputAnchor() const { return FirstValueInputAnchor; }
	TSharedPtr<SWidget> GetFirstValueOutputAnchor() const { return FirstValueOutputAnchor; }

	virtual FReply OnMouseButtonDoubleClick(
		const FGeometry& MyGeometry,
		const FPointerEvent& MouseEvent) override;

private:
	TSharedRef<SWidget> BuildHeader(bool bCompact, const FText& DiagnosticText) const;
	TSharedRef<SWidget> BuildSocketColumn(TConstArrayView<FVerseVisualSocket> Sockets, bool bOutput);
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
	FLinearColor UnselectedOutlineColor = FLinearColor::Transparent;
	TSharedPtr<SWidget> FirstValueInputAnchor;
	TSharedPtr<SWidget> FirstValueOutputAnchor;
	TUniquePtr<FSlateRoundedBoxBrush> OuterBrush;
	TUniquePtr<FSlateRoundedBoxBrush> ExpandedHeaderBrush;
	TUniquePtr<FSlateRoundedBoxBrush> CollapsedHeaderBrush;
	TUniquePtr<FSlateRoundedBoxBrush> BodyBrush;
	bool bExpanded = true;
	bool bShowBody = true;
};
