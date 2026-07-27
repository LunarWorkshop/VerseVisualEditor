#pragma once

#include "Containers/Utf8String.h"
#include "Templates/SharedPointer.h"
#include "VerseDocumentRevision.h"
#include "VerseEditBuffer.h"
#include "VerseParseSnapshot.h"
#include "VerseVisualTile.h"

class FText;

/** Coordinates authoritative editable source and all revision-specific derived representations. */
class FVerseDocumentSession
{
public:
	explicit FVerseDocumentSession(TSharedRef<const FVerseDocument> InOriginalDocument);

	bool Replace(FVerseTextRange Range, FUtf8StringView Replacement, FText& OutError);

	const TSharedRef<const FVerseDocument>& GetOriginalDocument() const { return OriginalDocument; }
	FVerseDocumentRevision GetRevision() const { return Revision; }
	FVerseTextRange GetWholeTextRange() const;
	const FVerseEditBuffer& GetEditBuffer() const { return EditBuffer; }
	const FUtf8String& GetCurrentUtf8() const;
	const FVerseParseSnapshot& GetParseSnapshot() const { return ParseSnapshot.GetValue(); }
	const TArray<FVerseVisualTile>& GetTiles() const { return Tiles; }
	uint32 GetMaterializationCount() const { return MaterializationCount; }

private:
	void RebuildDerivedRepresentations();

	TSharedRef<const FVerseDocument> OriginalDocument;
	FVerseEditBuffer EditBuffer;
	FVerseDocumentRevision Revision;
	mutable TOptional<FUtf8String> MaterializedSource;
	mutable uint32 MaterializationCount = 0;
	TSharedPtr<const FVerseDocument> CurrentSourceDocument;
	TOptional<FVerseParseSnapshot> ParseSnapshot;
	TArray<FVerseVisualTile> Tiles;
};
