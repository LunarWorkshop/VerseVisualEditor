#pragma once

#include "Containers/Utf8String.h"
#include "Internationalization/Text.h"
#include "Templates/SharedPointer.h"
#include "VerseDocumentRevision.h"
#include "Document/VerseEditBuffer.h"
#include "VerseParseSnapshot.h"
#include "VisualModel/VerseVisualTile.h"

struct FVerseDocumentEdit
{
	FVerseTextRange Range;
	FUtf8String Replacement;
};

/** One user-visible source operation, committed atomically as one history entry. */
struct FVerseEditTransaction
{
	/** Human-readable operation name used by undo and redo presentation. */
	FText Description;
	/** Localized replacements. Every range must belong to the current revision. */
	TArray<FVerseDocumentEdit> Edits;
	/** Optional selection snapshot to restore when this transaction is undone. */
	TOptional<FVerseTextRange> BeforeSelection;
	/** Optional byte range in the resulting document to restore when redone. */
	TOptional<FVerseByteRange> AfterSelection;
};

struct FVerseDocumentTransitionEdit
{
	FVerseTextRange PreviousRange;
	FVerseTextRange CurrentRange;
};

/** Last successful localized source transaction, used to reconcile revision-local views. */
struct FVerseDocumentSourceTransition
{
	FVerseDocumentRevision PreviousRevision;
	FVerseDocumentRevision CurrentRevision;
	TArray<FVerseDocumentTransitionEdit> Edits;
};

/** Coordinates authoritative editable source and all revision-specific derived representations. */
class FVerseDocumentSession
{
public:
	explicit FVerseDocumentSession(TSharedRef<const FVerseDocument> InOriginalDocument);
	~FVerseDocumentSession();

	bool Replace(FVerseTextRange Range, FUtf8StringView Replacement, FText& OutError);
	/** Applies non-overlapping current-revision edits atomically as one revision. */
	bool ReplaceMany(TConstArrayView<FVerseDocumentEdit> Edits, FText& OutError);
	/** Validates and commits a complete source transaction as one revision and undo step. */
	bool ApplyTransaction(const FVerseEditTransaction& Transaction, FText& OutError);
	bool CanUndo() const { return HistoryCursor > 0; }
	bool CanRedo() const { return HistoryCursor < History.Num(); }
	FText GetUndoDescription() const;
	FText GetRedoDescription() const;
	bool Undo(TOptional<FVerseTextRange>& OutRestoredSelection);
	bool Redo(TOptional<FVerseTextRange>& OutRestoredSelection);
	/** Keeps the optional UI selection attached to the current history state. */
	void SetCurrentSelectionRange(TOptional<FVerseTextRange> SelectionRange);
	void Reload(TSharedRef<const FVerseDocument> InDocument);
	bool SaveToFile(const FString& FilePath, FText& OutError);
	TArray<uint8> BuildCurrentFileBytes() const;

	const TSharedRef<const FVerseDocument>& GetOriginalDocument() const { return OriginalDocument; }
	FVerseDocumentRevision GetRevision() const { return Revision; }
	FVerseContentStateId GetContentStateId() const { return ContentStateId; }
	FVerseContentStateId GetSavedContentStateId() const { return SavedContentStateId; }
	bool IsDirty() const { return ContentStateId != SavedContentStateId; }
	FVerseTextRange GetWholeTextRange() const;
	const FVerseEditBuffer& GetEditBuffer() const { return EditBuffer; }
	const FUtf8String& GetCurrentUtf8() const;
	const FVerseParseSnapshot& GetParseSnapshot() const { return ParseSnapshot.GetValue(); }
	const TArray<FVerseVisualTile>& GetTiles() const { return Tiles; }
	uint32 GetMaterializationCount() const { return MaterializationCount; }
	const TOptional<FVerseDocumentSourceTransition>& GetLastSourceTransition() const
	{
		return LastSourceTransition;
	}

private:
	struct FHistoryEntry
	{
		FText Description;
		FVerseEditBuffer BeforeBuffer;
		FVerseContentStateId BeforeContentStateId;
		TOptional<FVerseTextRange> BeforeSelection;
		FVerseEditBuffer AfterBuffer;
		FVerseContentStateId AfterContentStateId;
		TOptional<FVerseTextRange> AfterSelection;
	};

	static TOptional<FVerseTextRange> RebaseSelection(
		const TOptional<FVerseTextRange>& Selection,
		FVerseDocumentRevision Revision);
	TOptional<FVerseTextRange> TransformSelectionForward(
		const TOptional<FVerseTextRange>& Selection,
		const FVerseDocumentSourceTransition& Transition) const;
	void RestoreHistoryState(
		const FVerseEditBuffer& Buffer,
		FVerseContentStateId StateId,
		const TOptional<FVerseTextRange>& Selection,
		TOptional<FVerseTextRange>& OutRestoredSelection);
	void RebuildDerivedRepresentations();

	TSharedRef<const FVerseDocument> OriginalDocument;
	FVerseEditBuffer EditBuffer;
	FVerseDocumentRevision Revision;
	FVerseContentStateId ContentStateId;
	FVerseContentStateId SavedContentStateId;
	uint64 NextContentStateValue = 0;
	TArray<FHistoryEntry> History;
	int32 HistoryCursor = 0;
	TOptional<FVerseTextRange> CurrentSelectionRange;
	mutable TOptional<FUtf8String> MaterializedSource;
	mutable uint32 MaterializationCount = 0;
	TSharedPtr<const FVerseDocument> CurrentSourceDocument;
	TOptional<FVerseParseSnapshot> ParseSnapshot;
	TArray<FVerseVisualTile> Tiles;
	TOptional<FVerseDocumentSourceTransition> LastSourceTransition;
};
