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

enum class EVerseExpressionKind : uint8
{
	Unsupported,
	Identifier,
	Literal,
	Call,
	BinaryOperator,
	UnaryOperator,
};

inline bool IsVerseBinaryOperatorExpression(EVerseExpressionKind Kind)
{
	return Kind == EVerseExpressionKind::BinaryOperator;
}

inline bool IsVerseOperatorExpression(EVerseExpressionKind Kind)
{
	return Kind == EVerseExpressionKind::BinaryOperator
		|| Kind == EVerseExpressionKind::UnaryOperator;
}

enum class EVerseLiteralKind : uint8
{
	None,
	Integer,
	Float,
	String,
	Character,
	Logic,
};

enum class EVerseTypeResolutionProvenance : uint8
{
	Unresolved,
	LocallyInferred,
	CompilerResolved,
};

/** Conservative type evidence. SourceRange is preferred over an intrinsic name. */
struct VERSEVISUALEDITOR_API FVerseExpressionType
{
	FVerseByteRange SourceRange;
	FName IntrinsicName;
	EVerseTypeResolutionProvenance Provenance = EVerseTypeResolutionProvenance::Unresolved;

	bool IsResolved() const
	{
		return Provenance != EVerseTypeResolutionProvenance::Unresolved
			&& (SourceRange.IsSet() || !IntrinsicName.IsNone());
	}
};

/** Recursive, source-exact expression recognized from the official Verse VST. */
struct VERSEVISUALEDITOR_API FVerseExpressionDescriptor
{
	FVerseByteRange Range;
	FVerseByteRange OperatorRange;
	/** Official Verse VST identity. These values are deliberately not a plugin syntax catalog. */
	uint8 VstNodeType = 0;
	uint8 VstTag = 0;
	EVerseExpressionKind Kind = EVerseExpressionKind::Unsupported;
	EVerseLiteralKind LiteralKind = EVerseLiteralKind::None;
	FVerseExpressionType Type;
	TArray<FVerseExpressionDescriptor> Operands;
};

enum class EVerseClauseItemSeparator : uint8
{
	None,
	Newline,
	Semicolon,
	Mixed,
	EndOfClause,
};

/** A root expression's occurrence within an executable clause. */
struct VERSEVISUALEDITOR_API FVerseClauseItemDescriptor
{
	FVerseExpressionDescriptor Expression;
	FVerseByteRange LeadingTriviaRange;
	FVerseByteRange TrailingTriviaRange;
	EVerseClauseItemSeparator Separator = EVerseClauseItemSeparator::None;
	int32 ExtraBlankLineCount = 0;
	bool bIsFinalValuePosition = false;
};

/** Parser-derived structure for a definition body. All ranges are source-exact and half-open. */
struct VERSEVISUALEDITOR_API FVerseClauseDescriptor
{
	FVerseByteRange InteriorRange;
	FVerseByteRange OpeningPunctuationRange;
	FVerseByteRange ClosingPunctuationRange;
	EVerseClausePunctuationStyle PunctuationStyle = EVerseClausePunctuationStyle::None;
	int32 EmptyBodyInsertionByte = INDEX_NONE;
	TArray<FVerseClauseItemDescriptor> Items;

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
