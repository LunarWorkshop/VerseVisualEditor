#include "VerseParseSnapshot.h"

FVerseParseSnapshot FVerseParseSnapshot::CreateRaw(TSharedRef<const FVerseDocument> Document)
{
	TArray<FVerseSourceRegion> Regions;
	Regions.Add({Document->GetWholeOriginalRange(), EVerseSourceRegionKind::Raw, NAME_None});
	return FVerseParseSnapshot(MoveTemp(Document), MoveTemp(Regions));
}

FVerseParseSnapshot FVerseParseSnapshot::CreateRecognized(
	TSharedRef<const FVerseDocument> Document,
	TArray<FVerseSourceRegion> SourceRegions)
{
	return FVerseParseSnapshot(MoveTemp(Document), MoveTemp(SourceRegions));
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
