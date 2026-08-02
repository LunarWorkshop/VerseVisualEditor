#pragma once

#include "SVerseGraphSurface.h"
#include "VerseCanvasViewState.h"
#include "VerseCompilation.h"
#include "VerseParseSnapshot.h"
#include "VerseTileSelection.h"
#include "VerseVisualTile.h"
#include "Widgets/SCompoundWidget.h"

class FVerseDocumentSession;

DECLARE_DELEGATE_OneParam(FOnVerseTileSelected, const FVerseVisualTile&);
DECLARE_DELEGATE_OneParam(FOnVerseFunctionOpened, const FVerseVisualTile&);

/** Zoomable, pannable, read-only presentation of a Verse parse snapshot. */
class SVerseFileCanvas final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SVerseFileCanvas) {}
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
	void RefreshContent(
		TSharedRef<const FVerseDocumentSession> InSession,
		TOptional<FVerseTextRange> SelectedRange,
		TArray<FVerseCompilationDiagnostic> InDiagnostics);
	void SelectTile(const FVerseVisualTile& Tile);
	void ClearTileSelection();
	/** Selects a tile and centers its widget, or its nearest rendered containing tile. */
	bool FocusTile(const FVerseVisualTile& Tile);

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
	TSharedPtr<SVerseGraphSurface> GraphSurface;
	TSharedPtr<FVerseGraphMotionController> MotionController;
	TSharedPtr<SVerseTile> LastBuiltRootTile;
	TArray<FString> MotionParentKeys;
	FVerseTileSelection Selection;
	FOnVerseTileSelected OnTileSelected;
	FOnVerseFunctionOpened OnFunctionOpened;
	FSimpleDelegate OnSelectionCleared;
};
