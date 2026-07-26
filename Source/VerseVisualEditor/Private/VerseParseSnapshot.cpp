#include "VerseParseSnapshot.h"

FVerseParseSnapshot FVerseParseSnapshot::CreateRaw(TSharedRef<const FVerseDocument> Document)
{
	TArray<FVerseSourceRegion> Regions;
	Regions.Add({Document->GetWholeOriginalRange(), EVerseSourceRegionKind::Raw, NAME_None});
	return FVerseParseSnapshot(MoveTemp(Document), MoveTemp(Regions));
}

FVerseParseSnapshot::FVerseParseSnapshot(
	TSharedRef<const FVerseDocument> InDocument,
	TArray<FVerseSourceRegion> InSourceRegions)
	: Document(MoveTemp(InDocument))
	, SourceRegions(MoveTemp(InSourceRegions))
{
}

FUtf8StringView FVerseParseSnapshot::GetSourceView(FVerseByteRange Range) const
{
	return Document->GetOriginalUtf8View(Range);
}

FUtf8StringView FVerseParseSnapshot::GetSourceView(const FVerseSourceRegion& Region) const
{
	return GetSourceView(Region.Range);
}

FVerseParseSnapshot FVerseRawSourceRecognizer::Recognize(
	TSharedRef<const FVerseDocument> Document) const
{
	return FVerseParseSnapshot::CreateRaw(MoveTemp(Document));
}
