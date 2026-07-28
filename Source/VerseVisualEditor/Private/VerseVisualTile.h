#pragma once

#include "CoreMinimal.h"
#include "VerseDocumentRevision.h"
#include "VerseParseSnapshot.h"

enum class EVerseVisualTileKind : uint8
{
	Definition,
	Comment,
	Expression,
	FunctionEntry,
	FunctionReturn,
	Unknown
};

struct FVerseVisualSocket
{
	FVerseTextRange NameRange;
	FVerseTextRange TypeRange;
	bool bConnected = false;
};

struct FVerseVisualClauseItemDescriptor
{
	FVerseTextRange ExpressionRange;
	FVerseTextRange LeadingTriviaRange;
	FVerseTextRange TrailingTriviaRange;
	FVerseTextRange TypeRange;
	EVerseExpressionKind ExpressionKind = EVerseExpressionKind::Unsupported;
	EVerseClauseItemSeparator Separator = EVerseClauseItemSeparator::None;
	int32 ExtraBlankLineCount = 0;
	bool bIsFinalValuePosition = false;
};

struct FVerseVisualClauseDescriptor
{
	FVerseTextRange InteriorRange;
	FVerseTextRange OpeningPunctuationRange;
	FVerseTextRange ClosingPunctuationRange;
	EVerseClausePunctuationStyle PunctuationStyle = EVerseClausePunctuationStyle::None;
	FVerseTextRange EmptyBodyInsertionAnchor;
	TArray<FVerseVisualClauseItemDescriptor> Items;
};

struct FVerseVisualFunctionParameter
{
	FVerseTextRange Range;
	FVerseTextRange NameRange;
	FVerseTextRange TypeRange;
	TArray<FVerseTextRange> ReferenceRanges;

	bool IsUsed() const { return !ReferenceRanges.IsEmpty(); }
};

/** Read-only presentation data. All text remains referenced by snapshot byte ranges. */
struct FVerseVisualTile
{
	FVerseTextRange Range;
	int32 FirstSourceLine = INDEX_NONE;
	int32 LastSourceLine = INDEX_NONE;
	EVerseVisualTileKind Kind = EVerseVisualTileKind::Unknown;
	EVerseExpressionKind ExpressionKind = EVerseExpressionKind::Unsupported;
	FName DefinitionKind;
	FVerseTextRange NameRange;
	FVerseTextRange TypeRange;
	TArray<FVerseTextRange> SpecifierRanges;
	TArray<FVerseTextRange> FunctionAccessSpecifierRanges;
	TArray<FVerseTextRange> FunctionEffectSpecifierRanges;
	TArray<FVerseVisualFunctionParameter> FunctionParameters;
	FVerseTextRange HeaderRange;
	FVerseTextRange BodyRange;
	FVerseVisualClauseDescriptor BodyClause;
	TArray<FVerseVisualTile> Children;
	EVerseCommentKind CommentKind = EVerseCommentKind::None;
	TArray<FVerseVisualSocket> ValueInputs;
	TArray<FVerseVisualSocket> ValueOutputs;
	int32 ExtraBlankLineCount = 0;
	bool bHasExecutionInput = false;
	bool bHasExecutionOutput = false;
	bool bExecutionInputConnected = false;
	bool bExecutionOutputConnected = false;
	bool bImplicitReturnValue = false;
};

class FVerseVisualTileBuilder
{
public:
	static TArray<FVerseVisualTile> Build(
		const FVerseParseSnapshot& Snapshot,
		FVerseDocumentRevision Revision = {});
	static TArray<FVerseVisualTile> BuildFunctionGraph(
		const FVerseVisualTile& FunctionTile,
		const FVerseParseSnapshot& Snapshot);
};
