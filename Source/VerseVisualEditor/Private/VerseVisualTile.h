#pragma once

#include "CoreMinimal.h"
#include "VerseDocumentRevision.h"
#include "VerseParseSnapshot.h"

class FVerseSemanticSnapshot;
namespace uLang { class CFunction; }

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
	/** Compiler-authored parameter name when it is not represented by this expression's source. */
	FString SemanticName;
	FVerseTextRange TypeRange;
	bool bConnected = false;
	FName IntrinsicTypeName;
	/** Compiler-authored type spelling when an exact semantic snapshot is available. */
	FString SemanticTypeName;
	FVerseTextRange InlineLiteralRange;
	EVerseLiteralKind InlineLiteralKind = EVerseLiteralKind::None;
};

struct FVerseVisualExpressionDescriptor
{
	FVerseTextRange Range;
	FVerseTextRange OperatorRange;
	uint8 VstNodeType = 0;
	uint8 VstTag = 0;
	EVerseExpressionKind Kind = EVerseExpressionKind::Unsupported;
	EVerseLiteralKind LiteralKind = EVerseLiteralKind::None;
	EVerseControlKind ControlKind = EVerseControlKind::None;
	FName DefinitionKind;
	FVerseTextRange NameRange;
	FVerseTextRange DeclaredTypeRange;
	FVerseTextRange TypeRange;
	FName IntrinsicTypeName;
	EVerseTypeResolutionProvenance TypeProvenance = EVerseTypeResolutionProvenance::Unresolved;
	FString SemanticTypeName;
	const uLang::CFunction* SemanticFunction = nullptr;
	TSharedPtr<const FVerseSemanticSnapshot> SemanticSnapshot;
	TArray<FVerseVisualExpressionDescriptor> Operands;
	struct FControlRegion
	{
		FVerseTextRange Range;
		EVerseControlRegionKind Kind = EVerseControlRegionKind::Body;
		EVerseClausePunctuationStyle PunctuationStyle = EVerseClausePunctuationStyle::None;
		int32 FirstOperandIndex = 0;
		int32 OperandCount = 0;
	};
	TArray<FControlRegion> ControlRegions;
};

struct FVerseVisualClauseItemDescriptor
{
	FVerseVisualExpressionDescriptor Expression;
	FVerseTextRange LeadingTriviaRange;
	FVerseTextRange TrailingTriviaRange;
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
	EVerseLiteralKind LiteralKind = EVerseLiteralKind::None;
	EVerseControlKind ControlKind = EVerseControlKind::None;
	uint8 VstNodeType = 0;
	uint8 VstTag = 0;
	FVerseTextRange OperatorRange;
	FName DefinitionKind;
	FVerseTextRange NameRange;
	FVerseTextRange TypeRange;
	FName IntrinsicTypeName;
	EVerseTypeResolutionProvenance TypeProvenance = EVerseTypeResolutionProvenance::Unresolved;
	FString SemanticTypeName;
	const uLang::CFunction* SemanticFunction = nullptr;
	TSharedPtr<const FVerseSemanticSnapshot> SemanticSnapshot;
	TArray<FVerseTextRange> SpecifierRanges;
	TArray<FVerseTextRange> FunctionAccessSpecifierRanges;
	TArray<FVerseTextRange> FunctionEffectSpecifierRanges;
	TArray<FVerseVisualFunctionParameter> FunctionParameters;
	FVerseTextRange HeaderRange;
	FVerseTextRange BodyRange;
	FVerseVisualClauseDescriptor BodyClause;
	TArray<FVerseVisualTile> Children;
	TArray<FVerseVisualExpressionDescriptor::FControlRegion> ControlRegions;
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
