#pragma once

#include "CoreMinimal.h"
#include "VerseDocumentRevision.h"
#include "VerseParseSnapshot.h"
#include "VerseVisualTile.generated.h"

class FVerseSemanticSnapshot;
class FVerseDocument;
namespace uLang
{
	class CDataDefinition;
	class CFunction;
	class CScope;
	class CTypeBase;
}

UENUM()
enum class EVerseVisualTileKind : uint8
{
	Definition,
	Comment,
	Expression,
	FailableBlock,
	FunctionEntry,
	FunctionReturn,
	Unknown
};

/** Compiler-derived failure behavior, independent of the carried Verse value type. */
enum class EVerseExpressionOutcome : uint8
{
	/** No exact semantic expression is available for this revision. */
	Unresolved,
	/** The expression does not propagate failure. */
	Ordinary,
	/** The expression may fail and carries its declared result when it succeeds. */
	FailableValue,
	/** The expression may fail but has no carried value. */
	FailureOnly,
};

/** How a complete statement-level failure is handled by its enclosing context. */
enum class EVerseStatementFailureDisposition : uint8
{
	None,
	/** The failure is consumed by the nearest visible failable context. */
	ContextBoundary,
	/** The failure legally propagates through an invisible context such as <decides>. */
	Propagated,
	/** The compiler reported that this statement's failure is not handled. */
	CompilerError,
};

struct FVerseVisualTileId
{
	int32 Value = INDEX_NONE;
	bool IsValid() const { return Value != INDEX_NONE; }
	friend bool operator==(FVerseVisualTileId Left, FVerseVisualTileId Right)
	{
		return Left.Value == Right.Value;
	}
};

FORCEINLINE uint32 GetTypeHash(const FVerseVisualTileId& Id)
{
	return GetTypeHash(Id.Value);
}

/** Revision-local owner for one independently layered graph region. */
struct FVerseGraphRenderScopeId
{
	int32 Value = INDEX_NONE;
	bool IsValid() const { return Value != INDEX_NONE; }
	friend bool operator==(FVerseGraphRenderScopeId Left, FVerseGraphRenderScopeId Right)
	{
		return Left.Value == Right.Value;
	}

	static FVerseGraphRenderScopeId Root() { return {0}; }
	static FVerseGraphRenderScopeId ForTile(FVerseVisualTileId Tile)
	{
		return {Tile.IsValid() ? Tile.Value + 1 : INDEX_NONE};
	}
};

FORCEINLINE uint32 GetTypeHash(const FVerseGraphRenderScopeId& Id)
{
	return GetTypeHash(Id.Value);
}

enum class EVerseGraphRenderScopeBackground : uint8
{
	Root,
	Failable,
};

struct FVerseGraphRenderScope
{
	FVerseGraphRenderScopeId Id;
	FVerseGraphRenderScopeId Parent;
	FVerseVisualTileId OwnerTile;
	EVerseGraphRenderScopeBackground Background = EVerseGraphRenderScopeBackground::Root;
	bool bClipToBounds = false;
};

enum class EVerseVisualSocketDirection : uint8 { Input, Output };
enum class EVerseVisualSocketRole : uint8
{
	Value,
	Execution,
	FailureContext,
	ClauseInsertion,
	BoundaryBinding,
};

struct FVerseVisualSocketId
{
	EVerseVisualSocketDirection Direction = EVerseVisualSocketDirection::Input;
	EVerseVisualSocketRole Role = EVerseVisualSocketRole::Value;
	int32 Index = INDEX_NONE;
	bool IsValid() const { return Index != INDEX_NONE; }
	friend bool operator==(const FVerseVisualSocketId&, const FVerseVisualSocketId&) = default;
};

FORCEINLINE uint32 GetTypeHash(const FVerseVisualSocketId& Id)
{
	return HashCombine(
		HashCombine(GetTypeHash(static_cast<uint8>(Id.Direction)),
			GetTypeHash(static_cast<uint8>(Id.Role))),
		GetTypeHash(Id.Index));
}

struct FVerseVisualSocketEndpoint
{
	FVerseVisualTileId Tile;
	FVerseVisualSocketId Socket;
	bool IsValid() const { return Tile.IsValid() && Socket.IsValid(); }
	friend bool operator==(const FVerseVisualSocketEndpoint&, const FVerseVisualSocketEndpoint&) = default;
};

FORCEINLINE uint32 GetTypeHash(const FVerseVisualSocketEndpoint& Endpoint)
{
	return HashCombine(GetTypeHash(Endpoint.Tile.Value), GetTypeHash(Endpoint.Socket));
}

enum class EVerseVisualConnectionAxis : uint8 { Horizontal, Vertical };

/** A connection may terminate without inventing a destination socket. */
enum class EVerseVisualConnectionTerminal : uint8
{
	Socket,
	RenderScopeRightBoundary,
	GoldDiamond,
	RedX,
};

struct FVerseVisualConnection
{
	FVerseVisualSocketEndpoint Source;
	FVerseVisualSocketEndpoint Target;
	EVerseVisualConnectionAxis Axis = EVerseVisualConnectionAxis::Horizontal;
	EVerseExpressionOutcome Outcome = EVerseExpressionOutcome::Unresolved;
	int32 ExtraBlankLineMarkers = 0;
	FVerseGraphRenderScopeId RenderScope = FVerseGraphRenderScopeId::Root();
	EVerseVisualConnectionTerminal Terminal = EVerseVisualConnectionTerminal::Socket;
};

inline FLinearColor GetVerseFailureDecorationColor()
{
	return FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("FFDC4A")));
}

struct FVerseVisualSocket
{
	FVerseVisualSocketId Id;
	FVerseTextRange NameRange;
	/** Compiler-authored parameter name when it is not represented by this expression's source. */
	FString SemanticName;
	FVerseTextRange TypeRange;
	FName IntrinsicTypeName;
	/** Compiler-authored type spelling when an exact semantic snapshot is available. */
	FString SemanticTypeName;
	/** Compiler-owned expected/result type, kept alive by SemanticSnapshot. */
	const uLang::CTypeBase* SemanticType = nullptr;
	/**
	 * Compiler-owned formal type accepted by this input. This intentionally
	 * differs from SemanticType, which describes the expression currently
	 * connected to the input and drives its concrete presentation.
	 */
	const uLang::CTypeBase* AcceptedSemanticType = nullptr;
	FString AcceptedSemanticTypeName;
	TSharedPtr<const FVerseSemanticSnapshot> AcceptedSemanticSnapshot;
	FVerseTextRange InlineLiteralRange;
	EVerseLiteralKind InlineLiteralKind = EVerseLiteralKind::None;
	/** An omitted fixed formal parameter currently uses its declared default. */
	bool bUsesDeclaredDefault = false;
	bool bNamedParameter = false;
	EVerseExpressionOutcome Outcome = EVerseExpressionOutcome::Unresolved;
	/** Exact compiler identity for a predicate binding exposed at a context boundary. */
	const uLang::CDataDefinition* SemanticDataDefinition = nullptr;
	/** Compiler scopes in which this boundary binding may legally be consumed. */
	TArray<const uLang::CScope*> LegalConsumerScopes;
	/** Keeps SemanticDataDefinition and LegalConsumerScopes alive. */
	TSharedPtr<const FVerseSemanticSnapshot> SemanticSnapshot;
};

class FVerseVisualSocketTopology
{
public:
	TConstArrayView<FVerseVisualSocket> GetValueInputs() const { return ValueInputs; }
	TConstArrayView<FVerseVisualSocket> GetValueOutputs() const { return ValueOutputs; }
	TConstArrayView<FVerseVisualSocket> GetOtherInputs() const { return OtherInputs; }
	TConstArrayView<FVerseVisualSocket> GetOtherOutputs() const { return OtherOutputs; }
	const FVerseVisualSocket* Find(FVerseVisualSocketId Id) const;
#if WITH_DEV_AUTOMATION_TESTS
	static FVerseVisualSocketTopology MakeInvalidForTesting(
		TArray<FVerseVisualSocket> InValueInputs,
		TArray<FVerseVisualSocket> InValueOutputs = {},
		TArray<FVerseVisualSocket> InOtherInputs = {},
		TArray<FVerseVisualSocket> InOtherOutputs = {})
	{
		FVerseVisualSocketTopology Result;
		Result.ValueInputs = MoveTemp(InValueInputs);
		Result.ValueOutputs = MoveTemp(InValueOutputs);
		Result.OtherInputs = MoveTemp(InOtherInputs);
		Result.OtherOutputs = MoveTemp(InOtherOutputs);
		return Result;
	}
#endif

private:
	TArray<FVerseVisualSocket> ValueInputs;
	TArray<FVerseVisualSocket> ValueOutputs;
	TArray<FVerseVisualSocket> OtherInputs;
	TArray<FVerseVisualSocket> OtherOutputs;
	friend class FVerseVisualTopologyBuilder;
};

struct FVerseVisualSeparatorDescriptor
{
	FVerseTextRange TokenRange;
	FVerseTextRange WhitespaceRange;
	EVerseSeparatorToken Token = EVerseSeparatorToken::None;
	EVerseSeparatorLayout Layout = EVerseSeparatorLayout::None;
	int32 BlankLineCount = 0;
	bool bIsEndOfClause = false;
};

struct FVerseVisualExpressionDescriptor
{
	FVerseTextRange Range;
	FVerseTextRange OperatorRange;
	FString OperatorSpelling;
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
	struct FGroupingLayer
	{
		FVerseTextRange Range;
		FVerseTextRange OpeningRange;
		FVerseTextRange ClosingRange;
	};
	TArray<FGroupingLayer> GroupingLayers;
	FString SemanticTypeName;
	TArray<FString> SemanticInputNames;
	TArray<FString> SemanticInputTypeNames;
	TArray<bool> SemanticInputNamed;
	TArray<bool> SemanticInputHasDefault;
	const uLang::CFunction* SemanticFunction = nullptr;
	TSharedPtr<const FVerseSemanticSnapshot> SemanticSnapshot;
	TArray<FVerseVisualExpressionDescriptor> Operands;
	struct FControlRegion
	{
		struct FItem
		{
			FVerseTextRange ExpressionRange;
			FVerseTextRange LeadingTriviaRange;
			FVerseTextRange TrailingTriviaRange;
			FVerseVisualSeparatorDescriptor Separator;
		};

		FVerseTextRange Range;
		FVerseTextRange InteriorRange;
		FVerseTextRange OpeningPunctuationRange;
		FVerseTextRange ClosingPunctuationRange;
		EVerseControlRegionKind Kind = EVerseControlRegionKind::Body;
		struct FSyntax
		{
			EVerseClauseDelimiter Delimiter = EVerseClauseDelimiter::None;
			EVerseClauseKeyword Keyword = EVerseClauseKeyword::None;
			EVerseSyntaxLayout Layout = EVerseSyntaxLayout::Inline;
			EVerseBracePlacement BracePlacement = EVerseBracePlacement::NotApplicable;
			EVerseLineEnding LineEnding = EVerseLineEnding::None;
			FString IndentationPrefix;
			FString IndentationUnit;
			FVerseTextRange KeywordRange;
			FVerseTextRange OpeningRange;
			FVerseTextRange ClosingRange;
			FVerseTextRange LeadingWhitespaceRange;
			FVerseTextRange TrailingWhitespaceRange;
			bool bHasCustomWhitespace = false;
		};
		FSyntax Syntax;
		FVerseTextRange EmptyBodyInsertionAnchor;
		int32 FirstOperandIndex = 0;
		int32 OperandCount = 0;
		TArray<FItem> Items;
	};
	TArray<FControlRegion> ControlRegions;
};

struct FVerseVisualClauseItemDescriptor
{
	FVerseVisualExpressionDescriptor Expression;
	FVerseTextRange LeadingTriviaRange;
	FVerseTextRange TrailingTriviaRange;
	FVerseVisualSeparatorDescriptor Separator;
	int32 ExtraBlankLineCount = 0;
	bool bIsFinalValuePosition = false;
};

struct FVerseVisualClauseDescriptor
{
	FVerseTextRange InteriorRange;
	FVerseTextRange OpeningPunctuationRange;
	FVerseTextRange ClosingPunctuationRange;
	FVerseVisualExpressionDescriptor::FControlRegion::FSyntax Syntax;
	FVerseTextRange EmptyBodyInsertionAnchor;
	/** This clause must retain one source-safe failable expression. */
	bool bRequiresFailablePlaceholder = false;
	TArray<FVerseVisualClauseItemDescriptor> Items;
};

enum class EVerseVisualSocketInsertionKind : uint8
{
	Clause,
	MissingElseClause,
};

/** Source insertion destination owned by one already-declared socket. */
struct FVerseVisualSocketInsertionTarget
{
	FVerseVisualSocketId Socket;
	EVerseVisualSocketInsertionKind Kind = EVerseVisualSocketInsertionKind::Clause;
	FVerseVisualClauseDescriptor Clause;
	FVerseTextRange OwnerExpressionRange;
	int32 InsertIndex = INDEX_NONE;
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
	FVerseVisualTileId Id;
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
	FString OperatorSpelling;
	TArray<FVerseVisualExpressionDescriptor::FGroupingLayer> GroupingLayers;
	FName DefinitionKind;
	FVerseTextRange NameRange;
	FVerseTextRange TypeRange;
	FName IntrinsicTypeName;
	EVerseTypeResolutionProvenance TypeProvenance = EVerseTypeResolutionProvenance::Unresolved;
	FString SemanticTypeName;
	/** Compiler-authored definition name copied while the semantic snapshot is active. */
	FString SemanticDefinitionName;
	const uLang::CTypeBase* SemanticType = nullptr;
	TArray<FString> SemanticInputNames;
	TArray<FString> SemanticInputTypeNames;
	TArray<const uLang::CTypeBase*> SemanticInputTypes;
	TArray<bool> SemanticInputNamed;
	TArray<bool> SemanticInputHasDefault;
	EVerseExpressionOutcome Outcome = EVerseExpressionOutcome::Unresolved;
	EVerseStatementFailureDisposition StatementFailure =
		EVerseStatementFailureDisposition::None;
	const uLang::CDataDefinition* SemanticDataDefinition = nullptr;
	const uLang::CFunction* SemanticFunction = nullptr;
	TArray<const uLang::CScope*> LegalConsumerScopes;
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
	FVerseVisualSocketTopology SocketTopology;
	TArray<FVerseVisualSocketInsertionTarget> SocketInsertionTargets;
	TConstArrayView<FVerseVisualSocket> GetValueInputs() const { return SocketTopology.GetValueInputs(); }
	TConstArrayView<FVerseVisualSocket> GetValueOutputs() const { return SocketTopology.GetValueOutputs(); }
	TConstArrayView<FVerseVisualSocket> GetOtherInputs() const { return SocketTopology.GetOtherInputs(); }
	TConstArrayView<FVerseVisualSocket> GetOtherOutputs() const { return SocketTopology.GetOtherOutputs(); }
	const FVerseVisualSocket* FindSocket(FVerseVisualSocketId SocketId) const
	{
		return SocketTopology.Find(SocketId);
	}
	const FVerseVisualSocketInsertionTarget* FindSocketInsertionTarget(
		FVerseVisualSocketId SocketId) const
	{
		return SocketInsertionTargets.FindByPredicate(
			[SocketId](const FVerseVisualSocketInsertionTarget& Target)
			{
				return Target.Socket == SocketId;
			});
	}
	/** Ordered clause containing this statement-level tile, when directly editable. */
	TOptional<FVerseVisualClauseDescriptor> EditableClause;
	int32 ClauseItemIndex = INDEX_NONE;
	int32 ExtraBlankLineCount = 0;
	bool bStatementLevel = false;
	bool bValueConsumed = false;
	bool bProducesValue = false;
	bool bImplicitReturnValue = false;
	/** Transient editor state: replace this generated tile until the user adopts it. */
	bool bIsProvisional = false;
};

/** Value-wire constraints surrounding one operator in a function graph. */
struct FVerseOperatorConnectionConstraints
{
	TArray<TOptional<FVerseVisualSocket>> ConnectedOperands;
	TArray<FVerseVisualSocket> OutputConsumers;

	TArray<const FVerseVisualSocket*> GetConnectedOperandPointers() const;
	TArray<const FVerseVisualSocket*> GetOutputConsumerPointers() const;
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
	static void FinalizeSocketTopology(TArray<FVerseVisualTile>& GraphTiles);
	static TArray<FVerseVisualConnection> BuildConnections(
		TConstArrayView<FVerseVisualTile> GraphTiles);
	static FVerseOperatorConnectionConstraints BuildOperatorConnectionConstraints(
		TConstArrayView<FVerseVisualTile> GraphTiles,
		const FVerseVisualTile& Operator,
		const FVerseDocument& Document);
	static TArray<FVerseGraphRenderScope> BuildRenderScopes(
		TConstArrayView<FVerseVisualTile> GraphTiles);
	static bool IsSocketConnected(
		TConstArrayView<FVerseVisualConnection> Connections,
		FVerseVisualSocketEndpoint Endpoint);
	static bool ValidateConnections(
		TConstArrayView<FVerseVisualTile> GraphTiles,
		TConstArrayView<FVerseVisualConnection> Connections,
		FString* OutDiagnostic = nullptr);
	static bool ValidateRenderScopes(
		TConstArrayView<FVerseVisualTile> GraphTiles,
		TConstArrayView<FVerseGraphRenderScope> Scopes,
		TConstArrayView<FVerseVisualConnection> Connections,
		FString* OutDiagnostic = nullptr);
};
