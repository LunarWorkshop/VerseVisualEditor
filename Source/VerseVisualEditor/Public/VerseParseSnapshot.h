#pragma once

#include "Containers/Array.h"
#include "Containers/UnrealString.h"
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

enum class EVerseClauseDelimiter : uint8
{
	None,
	Parentheses,
	Braces,
	Colon,
	BareIndentation,
	Dot,
};

/** Parser-owned relationship between a comment and its surrounding VST node. */
enum class EVerseCommentAttachment : uint8
{
	None,
	Prefix,
	Postfix,
	Inline,
	Unattached,
};

/** Source-exact comment attached to an ordered expression by the official VST. */
struct VERSEVISUALEDITOR_API FVerseCommentDescriptor
{
	FVerseByteRange Range;
	EVerseCommentKind Kind = EVerseCommentKind::None;
	EVerseCommentAttachment Attachment = EVerseCommentAttachment::None;
};

enum class EVerseClauseKeyword : uint8
{
	None,
	Then,
	Do,
	Else,
};

enum class EVerseSyntaxLayout : uint8
{
	Inline,
	Multiline,
};

enum class EVerseBracePlacement : uint8
{
	NotApplicable,
	SameLine,
	NextLine,
};

enum class EVerseSeparatorToken : uint8
{
	None,
	Comma,
	Semicolon,
};

enum class EVerseSeparatorLayout : uint8
{
	None,
	HorizontalSpace,
	Newline,
	TokenAndSpace,
	TokenAndNewline,
	CustomPreserved,
};

enum class EVerseExpressionKind : uint8
{
	Unsupported,
	Identifier,
	Literal,
	Call,
	Definition,
	Control,
	BinaryOperator,
	UnaryOperator,
};

enum class EVerseControlKind : uint8
{
	None,
	If,
	For,
	Loop,
	Sync,
	/** Source-only sequential wrapper used by a sync arm. */
	Block,
};

enum class EVerseControlRegionKind : uint8
{
	Condition,
	Body,
	Else,
};

/** One explicit grouping-parentheses wrapper retained from source. */
struct VERSEVISUALEDITOR_API FVerseGroupingLayer
{
	FVerseByteRange Range;
	FVerseByteRange OpeningRange;
	FVerseByteRange ClosingRange;
};

/** Source-exact separator and following layout between ordered clause items. */
struct VERSEVISUALEDITOR_API FVerseSeparatorDescriptor
{
	FVerseByteRange TokenRange;
	FVerseByteRange WhitespaceRange;
	EVerseSeparatorToken Token = EVerseSeparatorToken::None;
	EVerseSeparatorLayout Layout = EVerseSeparatorLayout::None;
	int32 BlankLineCount = 0;
	bool bIsEndOfClause = false;
};

/** Source-exact syntax and layout of one clause/body. */
struct VERSEVISUALEDITOR_API FVerseClauseSyntaxDescriptor
{
	EVerseClauseDelimiter Delimiter = EVerseClauseDelimiter::None;
	EVerseClauseKeyword Keyword = EVerseClauseKeyword::None;
	EVerseSyntaxLayout Layout = EVerseSyntaxLayout::Inline;
	EVerseBracePlacement BracePlacement = EVerseBracePlacement::NotApplicable;
	EVerseLineEnding LineEnding = EVerseLineEnding::None;
	FString IndentationPrefix;
	FString IndentationUnit;
	FVerseByteRange KeywordRange;
	FVerseByteRange OpeningRange;
	FVerseByteRange ClosingRange;
	FVerseByteRange LeadingWhitespaceRange;
	FVerseByteRange TrailingWhitespaceRange;
	bool bHasCustomWhitespace = false;
};

/** Source-exact occurrence of one ordered expression inside a control clause. */
struct VERSEVISUALEDITOR_API FVerseExpressionControlItem
{
	FVerseByteRange ExpressionRange;
	FVerseByteRange LeadingTriviaRange;
	FVerseByteRange TrailingTriviaRange;
	TArray<FVerseCommentDescriptor> Comments;
	FVerseSeparatorDescriptor Separator;
};

struct VERSEVISUALEDITOR_API FVerseExpressionControlRegion
{
	FVerseByteRange Range;
	FVerseByteRange InteriorRange;
	FVerseByteRange OpeningPunctuationRange;
	FVerseByteRange ClosingPunctuationRange;
	EVerseControlRegionKind Kind = EVerseControlRegionKind::Body;
	FVerseClauseSyntaxDescriptor Syntax;
	int32 EmptyBodyInsertionByte = INDEX_NONE;
	int32 FirstOperandIndex = 0;
	int32 OperandCount = 0;
	TArray<FVerseExpressionControlItem> Items;
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
	/** Display spelling derived from the official VST operator identity, never decoded from source. */
	FString OperatorSpelling;
	/** Official Verse VST identity. These values are deliberately not a plugin syntax catalog. */
	uint8 VstNodeType = 0;
	uint8 VstTag = 0;
	EVerseExpressionKind Kind = EVerseExpressionKind::Unsupported;
	EVerseLiteralKind LiteralKind = EVerseLiteralKind::None;
	EVerseControlKind ControlKind = EVerseControlKind::None;
	/** True only for the official `Name : Iterable` VST form in a for iteration clause. */
	bool bForGenerator = false;
	FName DefinitionKind;
	FVerseByteRange NameRange;
	/** Additional names introduced by a multi-binding form such as `Key -> Value : Map`. */
	TArray<FVerseByteRange> AdditionalBindingNameRanges;
	FVerseByteRange DeclaredTypeRange;
	FVerseExpressionType Type;
	TArray<FVerseGroupingLayer> GroupingLayers;
	TArray<FVerseExpressionDescriptor> Operands;
	TArray<FVerseExpressionControlRegion> ControlRegions;
};

/** A root expression's occurrence within an executable clause. */
struct VERSEVISUALEDITOR_API FVerseClauseItemDescriptor
{
	FVerseExpressionDescriptor Expression;
	FVerseByteRange LeadingTriviaRange;
	FVerseByteRange TrailingTriviaRange;
	TArray<FVerseCommentDescriptor> Comments;
	FVerseSeparatorDescriptor Separator;
	int32 ExtraBlankLineCount = 0;
	bool bIsFinalValuePosition = false;
};

/** Parser-derived structure for a definition body. All ranges are source-exact and half-open. */
struct VERSEVISUALEDITOR_API FVerseClauseDescriptor
{
	FVerseByteRange InteriorRange;
	FVerseByteRange OpeningPunctuationRange;
	FVerseByteRange ClosingPunctuationRange;
	FVerseClauseSyntaxDescriptor Syntax;
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
	EVerseCommentAttachment CommentAttachment = EVerseCommentAttachment::None;
	/** VST node that owns this comment. Unset for unattached comments. */
	FVerseByteRange CommentOwnerRange;
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
