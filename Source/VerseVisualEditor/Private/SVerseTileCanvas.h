#pragma once

#include "Input/CursorReply.h"
#include "Input/Reply.h"
#include "VerseCompilation.h"
#include "VerseParseSnapshot.h"
#include "VerseTileSelection.h"
#include "VerseVisualTile.h"
#include "Widgets/SCompoundWidget.h"

class SScaleBox;
class SScrollBar;
class SScrollBox;
class FVerseDocumentSession;

DECLARE_DELEGATE_OneParam(FOnVerseTileSelected, const FVerseVisualTile&);
DECLARE_DELEGATE_OneParam(FOnVerseFunctionOpened, const FVerseVisualTile&);

struct FVerseCanvasViewState
{
	FVector2D ScrollOffset = FVector2D::ZeroVector;
	float Zoom = 1.0f;
};

/** Zoomable, pannable, read-only presentation of a Verse parse snapshot. */
class SVerseTileCanvas final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SVerseTileCanvas) {}
		SLATE_ARGUMENT(TArray<FVerseCompilationDiagnostic>, Diagnostics)
		SLATE_EVENT(FOnVerseFunctionOpened, OnFunctionOpened)
	SLATE_END_ARGS()

	void Construct(
		const FArguments& InArgs,
		TSharedRef<const FVerseDocumentSession> InSession,
		FVerseCanvasViewState InitialViewState,
		TOptional<FVerseTextRange> InitialSelectedRange,
		FOnVerseTileSelected InOnTileSelected,
		FSimpleDelegate InOnSelectionCleared);

	FVerseCanvasViewState GetViewState() const;
	void SelectTile(const FVerseVisualTile& Tile);
	void ClearTileSelection();
	/** Selects a tile and centers its widget, or its nearest rendered containing tile. */
	bool FocusTile(const FVerseVisualTile& Tile);

	virtual int32 OnPaint(
		const FPaintArgs& Args,
		const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements,
		int32 LayerId,
		const FWidgetStyle& InWidgetStyle,
		bool bParentEnabled) const override;
	virtual FReply OnPreviewMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FCursorReply OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const override;
	virtual void OnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent) override;

private:
	TSharedRef<SWidget> BuildTileRow();
	TSharedRef<SWidget> BuildTileSequence(
		TConstArrayView<struct FVerseVisualTile> TilesToBuild,
		int32 SharedDiagnosticTileIndex,
		bool bShowEmptyDocumentMessage);
	TSharedRef<SWidget> BuildTile(const struct FVerseVisualTile& Tile, int32 TileIndex);
	TSharedRef<SWidget> BuildStructuralTile(const struct FVerseVisualTile& Tile, int32 TileIndex);
	TSharedRef<SWidget> BuildCompactTile(const struct FVerseVisualTile& Tile, int32 TileIndex);
	TSharedRef<SWidget> BuildFunctionSignature(const struct FVerseVisualTile& Tile) const;
	FText Decode(FVerseByteRange Range) const;
	FText FormatSpecifiers(const struct FVerseVisualTile& Tile) const;
	FText FormatSourceLines(const struct FVerseVisualTile& Tile) const;
	FText FormatDiagnosticMessages(int32 TileIndex) const;
	bool HasDiagnosticForTile(int32 TileIndex) const;
	FReply SelectTileFromClick(FVerseVisualTile Tile);
	FReply OpenFunctionTile(FVerseVisualTile Tile);
	bool IsTileSelected(FVerseTextRange TileRange) const;

	struct FTileWidgetEntry
	{
		FVerseTextRange Range;
		TWeakPtr<SWidget> Widget;
	};

	TOptional<FVerseParseSnapshot> Snapshot;
	TArray<FVerseVisualTile> Tiles;
	TArray<FTileWidgetEntry> TileWidgets;
	TArray<FVerseCompilationDiagnostic> Diagnostics;
	TSharedPtr<SScrollBar> HorizontalScrollbar;
	TSharedPtr<SScrollBar> VerticalScrollbar;
	TSharedPtr<SScrollBox> HorizontalScrollBox;
	TSharedPtr<SScrollBox> VerticalScrollBox;
	TSharedPtr<SScaleBox> ScaleBox;
	FVerseTileSelection Selection;
	FOnVerseTileSelected OnTileSelected;
	FOnVerseFunctionOpened OnFunctionOpened;
	FSimpleDelegate OnSelectionCleared;
	float Zoom = 1.0f;
	bool bIsPanning = false;
	FVector2D SoftwareCursorPosition = FVector2D::ZeroVector;
};
