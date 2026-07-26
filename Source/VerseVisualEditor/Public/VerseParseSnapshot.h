#pragma once

#include "Containers/Array.h"
#include "CoreTypes.h"
#include "Templates/SharedPointer.h"
#include "VerseDocument.h"

enum class EVerseSourceRegionKind : uint8
{
	/** Source not yet recognized by the visual editor. Its text remains authoritative. */
	Raw,

	/** A comment recognized by the official Verse parser. */
	Comment,

	/** Source recognized as a Verse construct. SyntaxKind identifies the construct. */
	Syntax,
};

enum class EVerseCommentKind : uint8
{
	None,
	Line,
	Block,
	Indented,
	Fragment,
};

/** A visual-model-facing description of a range in a particular source document. */
struct VERSEVISUALEDITOR_API FVerseSourceRegion
{
	FVerseByteRange Range;
	EVerseSourceRegionKind Kind = EVerseSourceRegionKind::Raw;
	FName SyntaxKind;
	FVerseByteRange NameRange;
	FVerseByteRange TypeRange;
	FVerseByteRange BodyRange;
	EVerseCommentKind CommentKind = EVerseCommentKind::None;
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
	friend class FVerseParseSnapshotBuilder;

	static FVerseParseSnapshot CreateRecognized(
		TSharedRef<const FVerseDocument> Document,
		TArray<FVerseSourceRegion> SourceRegions);

	FVerseParseSnapshot(
		TSharedRef<const FVerseDocument> InDocument,
		TArray<FVerseSourceRegion> InSourceRegions);

	TSharedRef<const FVerseDocument> Document;
	TArray<FVerseSourceRegion> SourceRegions;
};
