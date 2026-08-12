#include "Slate/SVerseVisualEditor.h"

#include "Slate/VerseCanvasViewState.h"
#include "Slate/SVerseVisualEditorInternal.h"

#define LOCTEXT_NAMESPACE "SVerseVisualEditor"

using namespace VerseVisualEditorPrivate;

namespace
{
	const FVerseVisualTile* FindHistoryTile(
		const FOpenVerseDocument& Document,
		FVerseTextRange Range)
	{
		if (const FVerseVisualTile* FileTile = VerseVisualEditorPrivate::FindTileByRange(
			Document.Session->GetTiles(),
			Range))
		{
			return FileTile;
		}
		for (const FOpenVerseFunctionTab& Tab : Document.FunctionTabs)
		{
			if (const FVerseVisualTile* GraphTile = VerseVisualEditorPrivate::FindTileByRange(
				Tab.GraphTiles,
				Range))
			{
				return GraphTile;
			}
		}
		return nullptr;
	}
}

bool SVerseVisualEditor::CanUndoActiveDocument() const
{
	return ActiveDocument.IsValid()
		&& ActiveDocument->Session.IsValid()
		&& ActiveDocument->Session->CanUndo();
}

bool SVerseVisualEditor::CanRedoActiveDocument() const
{
	return ActiveDocument.IsValid()
		&& ActiveDocument->Session.IsValid()
		&& ActiveDocument->Session->CanRedo();
}

void SVerseVisualEditor::UndoActiveDocument()
{
	if (!CanUndoActiveDocument())
	{
		return;
	}
	ActiveDocument->Session->SetCurrentSelectionRange(
		ActiveDocument->SelectedTile.IsSet()
			? TOptional<FVerseTextRange>(ActiveDocument->SelectedTile->Range)
			: TOptional<FVerseTextRange>());
	TOptional<FVerseTextRange> RestoredSelection;
	if (!ActiveDocument->Session->Undo(RestoredSelection))
	{
		return;
	}
	ApplyHistorySelection(RestoredSelection);
}

void SVerseVisualEditor::RedoActiveDocument()
{
	if (!CanRedoActiveDocument())
	{
		return;
	}
	ActiveDocument->Session->SetCurrentSelectionRange(
		ActiveDocument->SelectedTile.IsSet()
			? TOptional<FVerseTextRange>(ActiveDocument->SelectedTile->Range)
			: TOptional<FVerseTextRange>());
	TOptional<FVerseTextRange> RestoredSelection;
	if (!ActiveDocument->Session->Redo(RestoredSelection))
	{
		return;
	}
	ApplyHistorySelection(RestoredSelection);
}

void SVerseVisualEditor::ApplyHistorySelection(TOptional<FVerseTextRange> SelectionRange)
{
	ActiveDocument->SelectedTile.Reset();
	ActiveDocument->ProvisionalTiles.Reset();
	ActiveDocument->PropertyValidationMessage = FText::GetEmpty();
	ActiveDocument->PendingRenameText.Reset();
	ActiveDocument->PendingSpecifierText.Reset();
	ActiveDocument->PendingOperatorSignatureText.Reset();
	ActiveDocument->LoadError = FText::GetEmpty();
	ActiveDocument->bIsTemporary = false;

	QueueSemanticAnalysis(true);
	InvalidateCompilationResult(ActiveDocument);
	if (CompilationMode == EVerseCompilationMode::Continuous)
	{
		QueueCompilation(ActiveDocument, true);
	}
	ReconcileFunctionTabs(
		*ActiveDocument,
		FindExactSemanticSnapshot(SemanticWorkspace.Get(), *ActiveDocument));
	if (SelectionRange.IsSet())
	{
		if (const FVerseVisualTile* Tile = FindHistoryTile(*ActiveDocument, SelectionRange.GetValue()))
		{
			ActiveDocument->SelectedTile = *Tile;
			ActiveDocument->Session->SetCurrentSelectionRange(Tile->Range);
		}
	}
	RebuildDocumentTabs();
	RefreshActiveDocument();
}

#undef LOCTEXT_NAMESPACE
