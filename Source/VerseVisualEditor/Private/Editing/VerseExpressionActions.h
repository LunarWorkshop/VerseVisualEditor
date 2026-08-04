#pragma once

#include "CoreMinimal.h"
#include "VerseDocumentRevision.h"
#include "VisualModel/VerseVisualTile.h"
#include "VisualModel/VerseFunctionNavigation.h"

class FVerseDocument;
class FVerseDocumentSession;
class FVerseSemanticSnapshot;

/** Editor-owned syntax shape; semantic operation identity remains compiler-owned. */
enum class EVerseExpressionSourceForm : uint8
{
	IdentifierReference,
	OrdinaryCall,
	InfixOperator,
	PrefixOperator,
	PostfixOperator,
	/** A source-safe primitive literal with no inputs or casts. */
	Literal,
	/** Parser-known construct with a complete source-safe template. */
	StructuralExpression,
	/** Local mutable or immutable definition with a complete source-safe template. */
	Definition,
};

/** Transient editor treatment requested by a generated structural template. */
enum class EVerseProvisionalContentTarget : uint8
{
	None,
	/** First expression inside the generated control's failable condition. */
	FirstConditionExpression,
	/** Condition placeholder plus the no-op expression required by a colon body. */
	FirstConditionAndBodyExpressions,
};

/** Editor-owned structural recipe materialized against its destination clause. */
enum class EVerseStructuralExpressionKind : uint8
{
	None,
	If,
};

/** Atomic recipe for replacing one operand while retargeting an operator's literal defaults. */
struct FVerseOperatorRetargetRecipe
{
	FVerseTextRange OperatorRange;
	FString OperatorSpelling;
	int32 OperandCount = 0;
	int32 ReplacedOperandIndex = INDEX_NONE;
	FString SignatureDisplayText;
	TArray<FString> OperandTypeNames;
	TArray<FVerseTextRange> InlineLiteralRanges;
};

/** One expression creation choice which is valid at a particular typed socket. */
struct FVerseExpressionAction
{
	EVerseExpressionSourceForm SourceForm =
		EVerseExpressionSourceForm::IdentifierReference;
	FText DisplayName;
	FText Category;
	FText ModuleCategory;
	/** Additional filter terms which are never displayed in the action row. */
	FText SearchKeywords;
	/** Result/value type used to tint this action's Blueprint-style icon. */
	FString ResultTypeName;
	FVerseTextRange IdentifierNameRange;
	/** Direct source spelling for compiler-discovered identifiers and callables. */
	FString SourceSpelling;
	EVerseStructuralExpressionKind StructuralKind = EVerseStructuralExpressionKind::None;
	EVerseProvisionalContentTarget ProvisionalContentTarget =
		EVerseProvisionalContentTarget::None;
	bool bUsesFailureCallSyntax = false;
	int32 BoundInputIndex = INDEX_NONE;
	/** Concrete formal types for overload grouping and deterministic untyped defaults. */
	TArray<FString> InputTypeNames;
	TArray<FString> InputDefaultSources;
	TArray<FString> InputNames;
	TArray<bool> NamedInputs;
	TOptional<FVerseOperatorRetargetRecipe> OperatorRetarget;
};

/** Syntax identity of an existing expression being embedded into a new parent. */
struct FVerseBoundExpressionSyntax
{
	EVerseExpressionKind Kind = EVerseExpressionKind::Unsupported;
	FString OperatorSpelling;
	bool bExplicitlyGrouped = false;
};

/** Syntax identity of the parent whose existing operand is being replaced. */
struct FVerseExpressionParentSyntax
{
	EVerseExpressionKind Kind = EVerseExpressionKind::Unsupported;
	FString OperatorSpelling;
	int32 OperandIndex = INDEX_NONE;
};

/** Combines canonical source spelling with editor-authored hidden search aliases. */
FText BuildVerseExpressionActionSearchKeywords(const FVerseExpressionAction& Action);

/** Returns the canonical source-safe initializer for an editor-supported primitive type. */
TOptional<FString> GetDefaultVerseLiteralSourceForType(FStringView TypeName);

/** Materializes one action's source recipe without mutating a document. */
bool BuildVerseExpressionActionSource(
	const FVerseExpressionAction& Action,
	FStringView BoundExpressionSource,
	FString& OutSource,
	FText& OutError,
	const FVerseBoundExpressionSyntax* BoundSyntax = nullptr);

/** Discovers expression actions from the current lexical scope and the expression registry. */
class FVerseExpressionActionQuery
{
public:
	static TArray<TSharedPtr<FVerseExpressionAction>> Build(
		TConstArrayView<FVerseFunctionNavigationParameter> Parameters,
		const FVerseVisualSocket& DraggedSocket,
		bool bDraggingFromOutput,
		const FVerseDocument& Document);
	static TArray<TSharedPtr<FVerseExpressionAction>> Build(
		TConstArrayView<FVerseFunctionNavigationParameter> Parameters,
		const FVerseVisualSocket& DraggedSocket,
		bool bDraggingFromOutput,
		const FVerseDocument& Document,
		FVerseTextRange ExpressionRange,
		const FString& FilePath,
		TConstArrayView<TSharedPtr<const FVerseSemanticSnapshot>> SemanticSnapshots);
	/** Builds untyped actions for a new expression position in an ordered clause. */
	static TArray<TSharedPtr<FVerseExpressionAction>> BuildAll(
		TConstArrayView<FVerseFunctionNavigationParameter> Parameters,
		const FVerseDocument& Document,
		FVerseTextRange ScopeAnchorRange,
		const FString& FilePath,
		TConstArrayView<TSharedPtr<const FVerseSemanticSnapshot>> SemanticSnapshots);
};

/** Applies an action only after a scratch parse proves that it produces the requested structure. */
bool TryApplyVerseExpressionAction(
	FVerseDocumentSession& Session,
	FVerseTextRange ExpressionRange,
	const FVerseExpressionAction& Action,
	FText& OutError,
	const FVerseBoundExpressionSyntax* BoundSyntax = nullptr,
	const FVerseExpressionParentSyntax* ParentSyntax = nullptr);

/** Applies a provider and all literal changes required by its selected concrete signature. */
bool TryApplyVerseOperatorOperandAction(
	FVerseDocumentSession& Session,
	const FVerseExpressionAction& Action,
	FText& OutError);

/** Materializes an omitted named/default argument with a selected provider expression. */
bool TryMaterializeVerseNamedInput(
	FVerseDocumentSession& Session,
	FVerseTextRange CallRange,
	FStringView InputName,
	const FVerseExpressionAction& Action,
	FText& OutError);
