#include "VerseDocumentSession.h"

#include "HAL/FileManager.h"
#include "Internationalization/Text.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "VerseParseSnapshotBuilder.h"
#include "VerseVisualEditorLifetimeDiagnostics.h"

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
	const FVerseDocumentEdit Edit{Range, FUtf8String(Replacement)};
	return ReplaceMany(MakeArrayView(&Edit, 1), OutError);
}

bool FVerseDocumentSession::ReplaceMany(
	TConstArrayView<FVerseDocumentEdit> Edits,
	FText& OutError)
{
	if (Edits.IsEmpty())
	{
		OutError = LOCTEXT("EmptyEditTransaction", "The edit transaction is empty.");
		return false;
	}

	TArray<FVerseDocumentEdit> Sorted(Edits);
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

	FVerseEditBuffer Candidate = EditBuffer;
	for (const FVerseDocumentEdit& Edit : Sorted)
	{
		if (!Candidate.Replace(Edit.Range, Edit.Replacement, OutError))
		{
			return false;
		}
	}

	TArray<FVerseDocumentEdit> Ascending(Edits);
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

	EditBuffer = MoveTemp(Candidate);
	++Revision.Value;
	Transition.CurrentRevision = Revision;
	LastSourceTransition = MoveTemp(Transition);
	++ContentStateId.Value;
	MaterializedSource.Reset();
	RebuildDerivedRepresentations();
	OutError = FText::GetEmpty();
	return true;
}

void FVerseDocumentSession::Reload(TSharedRef<const FVerseDocument> InDocument)
{
	OriginalDocument = MoveTemp(InDocument);
	EditBuffer = FVerseEditBuffer(OriginalDocument);
	++Revision.Value;
	++ContentStateId.Value;
	SavedContentStateId = ContentStateId;
	MaterializedSource.Reset();
	CurrentSourceDocument = OriginalDocument;
	ParseSnapshot.Emplace(FVerseParseSnapshotBuilder::Build(CurrentSourceDocument.ToSharedRef()));
	Tiles = FVerseVisualTileBuilder::Build(ParseSnapshot.GetValue(), Revision);
	LastSourceTransition.Reset();
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
