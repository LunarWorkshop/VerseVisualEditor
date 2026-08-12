#include "Document/VerseDocumentSession.h"

#include "HAL/FileManager.h"
#include "Internationalization/Text.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "VerseParseSnapshotBuilder.h"
#include "Infrastructure/VerseVisualEditorLifetimeDiagnostics.h"

#define LOCTEXT_NAMESPACE "VerseDocumentSession"

FVerseDocumentSession::FVerseDocumentSession(TSharedRef<const FVerseDocument> InOriginalDocument)
	: OriginalDocument(MoveTemp(InOriginalDocument))
	, EditBuffer(OriginalDocument)
	, CurrentSourceDocument(OriginalDocument)
{
	VerseVisualEditorLifetimeDiagnostics::Track(
		this,
		TEXT("DocumentSession"));
	ParseSnapshot.Emplace(FVerseParseSnapshotBuilder::Build(CurrentSourceDocument.ToSharedRef()));
	Tiles = FVerseVisualTileBuilder::Build(ParseSnapshot.GetValue(), Revision);
}

FVerseDocumentSession::~FVerseDocumentSession()
{
	VerseVisualEditorLifetimeDiagnostics::Event(
		TEXT("DocumentSession.Destroy"),
		this,
		CurrentSourceDocument.Get());
	VerseVisualEditorLifetimeDiagnostics::Untrack(
		this,
		TEXT("DocumentSession"));
}

bool FVerseDocumentSession::Replace(
	FVerseTextRange Range,
	FUtf8StringView Replacement,
	FText& OutError)
{
	FVerseEditTransaction Transaction;
	Transaction.Description = LOCTEXT("ReplaceSourceTransaction", "Edit Verse source");
	Transaction.Edits.Add({Range, FUtf8String(Replacement)});
	return ApplyTransaction(Transaction, OutError);
}

bool FVerseDocumentSession::ReplaceMany(
	TConstArrayView<FVerseDocumentEdit> Edits,
	FText& OutError)
{
	FVerseEditTransaction Transaction;
	Transaction.Description = LOCTEXT("ReplaceManySourceTransaction", "Edit Verse source");
	Transaction.Edits.Append(Edits);
	return ApplyTransaction(Transaction, OutError);
}

bool FVerseDocumentSession::ApplyTransaction(
	const FVerseEditTransaction& Transaction,
	FText& OutError)
{
	if (Transaction.Edits.IsEmpty())
	{
		OutError = LOCTEXT("EmptyEditTransaction", "The edit transaction is empty.");
		return false;
	}
	if (Transaction.BeforeSelection.IsSet()
		&& Transaction.BeforeSelection->Revision != Revision)
	{
		OutError = LOCTEXT(
			"StaleTransactionSelection",
			"The transaction selection belongs to an obsolete document revision.");
		return false;
	}

	TArray<FVerseDocumentEdit> Sorted(Transaction.Edits);
	Sorted.Sort([](const FVerseDocumentEdit& Left, const FVerseDocumentEdit& Right)
	{
		return Left.Range.BeginByte > Right.Range.BeginByte;
	});
	for (int32 Index = 0; Index < Sorted.Num(); ++Index)
	{
		if (Sorted[Index].Range.Revision != Revision)
		{
			OutError = LOCTEXT("StaleEditTransaction", "An edit range belongs to an obsolete document revision.");
			return false;
		}
		if (Index + 1 < Sorted.Num()
			&& Sorted[Index + 1].Range.EndByte() > Sorted[Index].Range.BeginByte)
		{
			OutError = LOCTEXT("OverlappingEditTransaction", "The edit transaction contains overlapping ranges.");
			return false;
		}
	}

	const FVerseEditBuffer BeforeBuffer = EditBuffer;
	const FVerseContentStateId BeforeContentStateId = ContentStateId;
	const TOptional<FVerseTextRange> BeforeSelection = Transaction.BeforeSelection.IsSet()
		? Transaction.BeforeSelection
		: CurrentSelectionRange;
	FVerseEditBuffer Candidate = EditBuffer;
	for (const FVerseDocumentEdit& Edit : Sorted)
	{
		if (!Candidate.Replace(Edit.Range, Edit.Replacement, OutError))
		{
			return false;
		}
	}

	TArray<FVerseDocumentEdit> Ascending(Transaction.Edits);
	Ascending.Sort([](const FVerseDocumentEdit& Left, const FVerseDocumentEdit& Right)
	{
		return Left.Range.BeginByte < Right.Range.BeginByte;
	});
	FVerseDocumentSourceTransition Transition;
	Transition.PreviousRevision = Revision;
	int32 AccumulatedDelta = 0;
	for (const FVerseDocumentEdit& Edit : Ascending)
	{
		FVerseDocumentTransitionEdit& TransitionEdit =
			Transition.Edits.AddDefaulted_GetRef();
		TransitionEdit.PreviousRange = Edit.Range;
		TransitionEdit.CurrentRange = FVerseTextRange(
			FVerseDocumentRevision{Revision.Value + 1},
			FVerseByteRange{
				Edit.Range.BeginByte + AccumulatedDelta,
				Edit.Replacement.Len()});
		AccumulatedDelta += Edit.Replacement.Len() - Edit.Range.NumBytes;
	}

	++Revision.Value;
	Transition.CurrentRevision = Revision;
	LastSourceTransition = MoveTemp(Transition);
	const FVerseContentStateId NewContentStateId{++NextContentStateValue};
	const TOptional<FVerseTextRange> AfterSelection = Transaction.AfterSelection.IsSet()
		? TOptional<FVerseTextRange>(FVerseTextRange(Revision, Transaction.AfterSelection.GetValue()))
		: TransformSelectionForward(BeforeSelection, LastSourceTransition.GetValue());
	if (HistoryCursor < History.Num())
	{
		History.RemoveAt(HistoryCursor, History.Num() - HistoryCursor);
	}
	History.Add({
		Transaction.Description,
		BeforeBuffer,
		BeforeContentStateId,
		BeforeSelection,
		Candidate,
		NewContentStateId,
		AfterSelection});
	++HistoryCursor;
	EditBuffer = MoveTemp(Candidate);
	ContentStateId = NewContentStateId;
	CurrentSelectionRange = AfterSelection;
	MaterializedSource.Reset();
	RebuildDerivedRepresentations();
	OutError = FText::GetEmpty();
	return true;
}

FText FVerseDocumentSession::GetUndoDescription() const
{
	return CanUndo() ? History[HistoryCursor - 1].Description : FText::GetEmpty();
}

FText FVerseDocumentSession::GetRedoDescription() const
{
	return CanRedo() ? History[HistoryCursor].Description : FText::GetEmpty();
}

bool FVerseDocumentSession::Undo(TOptional<FVerseTextRange>& OutRestoredSelection)
{
	if (!CanUndo())
	{
		return false;
	}
	FHistoryEntry& Entry = History[HistoryCursor - 1];
	Entry.AfterSelection = CurrentSelectionRange;
	--HistoryCursor;
	RestoreHistoryState(
		Entry.BeforeBuffer,
		Entry.BeforeContentStateId,
		Entry.BeforeSelection,
		OutRestoredSelection);
	return true;
}

bool FVerseDocumentSession::Redo(TOptional<FVerseTextRange>& OutRestoredSelection)
{
	if (!CanRedo())
	{
		return false;
	}
	FHistoryEntry& Entry = History[HistoryCursor];
	Entry.BeforeSelection = CurrentSelectionRange;
	++HistoryCursor;
	RestoreHistoryState(
		Entry.AfterBuffer,
		Entry.AfterContentStateId,
		Entry.AfterSelection,
		OutRestoredSelection);
	return true;
}

void FVerseDocumentSession::SetCurrentSelectionRange(TOptional<FVerseTextRange> SelectionRange)
{
	if (SelectionRange.IsSet() && SelectionRange->Revision != Revision)
	{
		SelectionRange.Reset();
	}
	CurrentSelectionRange = SelectionRange;
}

void FVerseDocumentSession::Reload(TSharedRef<const FVerseDocument> InDocument)
{
	OriginalDocument = MoveTemp(InDocument);
	EditBuffer = FVerseEditBuffer(OriginalDocument);
	++Revision.Value;
	ContentStateId.Value = ++NextContentStateValue;
	SavedContentStateId = ContentStateId;
	History.Reset();
	HistoryCursor = 0;
	CurrentSelectionRange.Reset();
	MaterializedSource.Reset();
	CurrentSourceDocument = OriginalDocument;
	ParseSnapshot.Emplace(FVerseParseSnapshotBuilder::Build(CurrentSourceDocument.ToSharedRef()));
	Tiles = FVerseVisualTileBuilder::Build(ParseSnapshot.GetValue(), Revision);
	LastSourceTransition.Reset();
}

TOptional<FVerseTextRange> FVerseDocumentSession::RebaseSelection(
	const TOptional<FVerseTextRange>& Selection,
	FVerseDocumentRevision NewRevision)
{
	return Selection.IsSet()
		? TOptional<FVerseTextRange>(FVerseTextRange(NewRevision, *Selection))
		: TOptional<FVerseTextRange>();
}

TOptional<FVerseTextRange> FVerseDocumentSession::TransformSelectionForward(
	const TOptional<FVerseTextRange>& Selection,
	const FVerseDocumentSourceTransition& Transition) const
{
	if (!Selection.IsSet() || Selection->Revision != Transition.PreviousRevision)
	{
		return {};
	}
	int32 Begin = Selection->BeginByte;
	int32 End = Selection->EndByte();
	for (const FVerseDocumentTransitionEdit& Edit : Transition.Edits)
	{
		const int32 Delta = Edit.CurrentRange.NumBytes - Edit.PreviousRange.NumBytes;
		if (Edit.PreviousRange.EndByte() <= Begin)
		{
			Begin += Delta;
			End += Delta;
		}
		else if (Edit.PreviousRange.BeginByte >= End)
		{
			continue;
		}
		else if (Edit.PreviousRange.BeginByte >= Begin
				&& Edit.PreviousRange.EndByte() <= End)
		{
			End += Delta;
		}
		else
		{
			return {};
		}
	}
	return FVerseTextRange(Transition.CurrentRevision, {Begin, End - Begin});
}

void FVerseDocumentSession::RestoreHistoryState(
	const FVerseEditBuffer& Buffer,
	FVerseContentStateId StateId,
	const TOptional<FVerseTextRange>& Selection,
	TOptional<FVerseTextRange>& OutRestoredSelection)
{
	EditBuffer = Buffer;
	ContentStateId = StateId;
	++Revision.Value;
	CurrentSelectionRange = RebaseSelection(Selection, Revision);
	OutRestoredSelection = CurrentSelectionRange;
	MaterializedSource.Reset();
	LastSourceTransition.Reset();
	RebuildDerivedRepresentations();
}

bool FVerseDocumentSession::SaveToFile(const FString& FilePath, FText& OutError)
{
	const TArray<uint8> FileBytes = BuildCurrentFileBytes();
	const FString Directory = FPaths::GetPath(FilePath);
	const FString TemporaryPath = FPaths::CreateTempFilename(
		*Directory,
		TEXT(".VerseVisualEditor-"),
		TEXT(".tmp"));
	if (!FFileHelper::SaveArrayToFile(FileBytes, *TemporaryPath))
	{
		OutError = FText::Format(
			LOCTEXT("TemporaryWriteFailed", "Could not write temporary save file: {0}"),
			FText::FromString(TemporaryPath));
		return false;
	}

	if (!IFileManager::Get().Move(*FilePath, *TemporaryPath, true, false, false, true))
	{
		IFileManager::Get().Delete(*TemporaryPath, false, true);
		OutError = FText::Format(
			LOCTEXT("ReplaceFailed", "Could not replace Verse file: {0}"),
			FText::FromString(FilePath));
		return false;
	}

	SavedContentStateId = ContentStateId;
	OutError = FText::GetEmpty();
	return true;
}

TArray<uint8> FVerseDocumentSession::BuildCurrentFileBytes() const
{
	TArray<uint8> FileBytes;
	if (OriginalDocument->HasUtf8Bom())
	{
		FileBytes.Append({0xEF, 0xBB, 0xBF});
	}
	const FUtf8String& CurrentSource = GetCurrentUtf8();
	FileBytes.Append(
		reinterpret_cast<const uint8*>(*CurrentSource),
		CurrentSource.Len());
	return FileBytes;
}

FVerseTextRange FVerseDocumentSession::GetWholeTextRange() const
{
	return FVerseTextRange(Revision, {0, EditBuffer.Len()});
}

const FUtf8String& FVerseDocumentSession::GetCurrentUtf8() const
{
	if (!MaterializedSource.IsSet())
	{
		MaterializedSource.Emplace(EditBuffer.Materialize());
		++MaterializationCount;
	}
	return MaterializedSource.GetValue();
}

void FVerseDocumentSession::RebuildDerivedRepresentations()
{
	const FUtf8String& CurrentSource = GetCurrentUtf8();
	const TConstArrayView<uint8> CurrentBytes(
		reinterpret_cast<const uint8*>(*CurrentSource),
		CurrentSource.Len());
	FText Error;
	CurrentSourceDocument = FVerseDocument::CreateFromBytes(CurrentBytes, Error);
	checkf(CurrentSourceDocument.IsValid(), TEXT("Validated edit buffer produced invalid UTF-8: %s"), *Error.ToString());
	ParseSnapshot.Emplace(FVerseParseSnapshotBuilder::Build(CurrentSourceDocument.ToSharedRef()));
	Tiles = FVerseVisualTileBuilder::Build(ParseSnapshot.GetValue(), Revision);
}

#undef LOCTEXT_NAMESPACE
