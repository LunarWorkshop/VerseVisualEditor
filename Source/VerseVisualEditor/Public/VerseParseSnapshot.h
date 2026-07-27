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

enum class EVerseClausePunctuationStyle : uint8
{
	None,
	Braces,
	ColonOrIndentation,
};

/** Parser-derived structure for a definition body. All ranges are source-exact and half-open. */
struct VERSEVISUALEDITOR_API FVerseClauseDescriptor
{
	FVerseByteRange InteriorRange;
	FVerseByteRange OpeningPunctuationRange;
	FVerseByteRange ClosingPunctuationRange;
	EVerseClausePunctuationStyle PunctuationStyle = EVerseClausePunctuationStyle::None;
	int32 EmptyBodyInsertionByte = INDEX_NONE;

	bool IsSet() const { return InteriorRange.IsSet(); }
};

/** Parser-derived function parameter and the identifier occurrences in its body. */
struct VERSEVISUALEDITOR_API FVerseFunctionParameter
{
	FVerseByteRange Range;
	FVerseByteRange NameRange;
	FVerseByteRange TypeRange;
	TArray<FVerseByteRange> ReferenceRanges;

	bool IsUsed() const { return !ReferenceRanges.IsEmpty(); }
};

/** A visual-model-facing description of a range in a particular source document. */
struct VERSEVISUALEDITOR_API FVerseSourceRegion
{
	FVerseByteRange Range;
	EVerseSourceRegionKind Kind = EVerseSourceRegionKind::Raw;
	FName SyntaxKind;
	FVerseByteRange NameRange;
	FVerseByteRange TypeRange;
	/** VST append-specifier/effect identifiers attached to this definition's name. */
	TArray<FVerseByteRange> SpecifierRanges;
	/** Function specifiers before the parameter clause (for example, access). */
	TArray<FVerseByteRange> FunctionAccessSpecifierRanges;
	/** Function specifiers after the parameter clause (for example, effects). */
	TArray<FVerseByteRange> FunctionEffectSpecifierRanges;
	/** Populated only for function definitions. */
	TArray<FVerseFunctionParameter> FunctionParameters;
	/** Definition text preceding the body's opening punctuation or expression. */
	FVerseByteRange HeaderRange;
	/** Exact body interior, retained separately from the complete definition Range. */
	FVerseByteRange BodyRange;
	FVerseClauseDescriptor BodyClause;
	/** Complete ordered coverage of BodyRange, recursively derived from VST clause children. */
	TArray<FVerseSourceRegion> Children;
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
