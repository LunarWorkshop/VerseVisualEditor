#pragma once

#include "Containers/Array.h"
#include "CoreTypes.h"
#include "Templates/SharedPointer.h"
#include "VerseDocument.h"

enum class EVerseSourceRegionKind : uint8
{
	/** Source not yet recognized by the visual editor. Its text remains authoritative. */
	Raw,

	/** Source recognized as a Verse construct. SyntaxKind identifies the construct. */
	Syntax,
};

/** A visual-model-facing description of a range in a particular source document. */
struct VERSEVISUALEDITOR_API FVerseSourceRegion
{
	FVerseByteRange Range;
	EVerseSourceRegionKind Kind = EVerseSourceRegionKind::Raw;
	FName SyntaxKind;
};

/**
 * Immutable recognition result whose ranges are valid only for its retained source document.
 */
class VERSEVISUALEDITOR_API FVerseParseSnapshot
{
public:
	static FVerseParseSnapshot CreateRaw(TSharedRef<const FVerseDocument> Document);

	const TSharedRef<const FVerseDocument>& GetDocument() const { return Document; }
	const TArray<FVerseSourceRegion>& GetSourceRegions() const { return SourceRegions; }
	FUtf8StringView GetSourceView(FVerseByteRange Range) const;
	FUtf8StringView GetSourceView(const FVerseSourceRegion& Region) const;

private:
	FVerseParseSnapshot(
		TSharedRef<const FVerseDocument> InDocument,
		TArray<FVerseSourceRegion> InSourceRegions);

	TSharedRef<const FVerseDocument> Document;
	TArray<FVerseSourceRegion> SourceRegions;
};

/** Error-tolerant recognition boundary used to build a parse snapshot from authoritative source. */
class VERSEVISUALEDITOR_API IVerseSourceRecognizer
{
public:
	virtual ~IVerseSourceRecognizer() = default;
	virtual FVerseParseSnapshot Recognize(TSharedRef<const FVerseDocument> Document) const = 0;
};

/** Fallback recognizer that preserves the complete source as one exact raw region. */
class VERSEVISUALEDITOR_API FVerseRawSourceRecognizer final : public IVerseSourceRecognizer
{
public:
	virtual FVerseParseSnapshot Recognize(TSharedRef<const FVerseDocument> Document) const override;
};
