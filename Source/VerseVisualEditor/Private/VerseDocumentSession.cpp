#include "VerseDocumentSession.h"

#include "Internationalization/Text.h"
#include "VerseParseSnapshotBuilder.h"

#define LOCTEXT_NAMESPACE "VerseDocumentSession"

FVerseDocumentSession::FVerseDocumentSession(TSharedRef<const FVerseDocument> InOriginalDocument)
	: OriginalDocument(MoveTemp(InOriginalDocument))
	, EditBuffer(OriginalDocument)
	, CurrentSourceDocument(OriginalDocument)
{
	ParseSnapshot.Emplace(FVerseParseSnapshotBuilder::Build(CurrentSourceDocument.ToSharedRef()));
	Tiles = FVerseVisualTileBuilder::Build(ParseSnapshot.GetValue(), Revision);
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
	MaterializedSource.Reset();
	RebuildDerivedRepresentations();
	return true;
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
