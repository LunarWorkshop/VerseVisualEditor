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
	if (Range.Revision != Revision)
	{
		OutError = LOCTEXT("StaleRange", "The edit range belongs to an obsolete document revision.");
		return false;
	}
	if (!EditBuffer.Replace(Range, Replacement, OutError))
	{
		return false;
	}

	++Revision.Value;
	++ContentStateId.Value;
	MaterializedSource.Reset();
	RebuildDerivedRepresentations();
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
