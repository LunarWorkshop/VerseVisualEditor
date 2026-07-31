#pragma once

#include "CoreMinimal.h"
#include "VerseDocumentRevision.h"
#include "VerseVisualTile.h"
#include "VerseFunctionNavigation.h"

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
	/** Parser-known construct with a complete source-safe template. */
	StructuralExpression,
};

/** One expression creation choice which is valid at a particular typed socket. */
struct FVerseExpressionAction
{
	EVerseExpressionSourceForm SourceForm =
		EVerseExpressionSourceForm::IdentifierReference;
	FText DisplayName;
	FText Category;
	FText ModuleCategory;
	/** Result/value type used to tint this action's Blueprint-style icon. */
	FString ResultTypeName;
	FVerseTextRange IdentifierNameRange;
	/** Direct source spelling for compiler-discovered identifiers and callables. */
	FString SourceSpelling;
	bool bUsesFailureCallSyntax = false;
	int32 BoundInputIndex = INDEX_NONE;
	TArray<FString> InputDefaultSources;
	TArray<FString> InputNames;
	TArray<bool> NamedInputs;
};

/** Materializes one action's source recipe without mutating a document. */
bool BuildVerseExpressionActionSource(
	const FVerseExpressionAction& Action,
	FStringView BoundExpressionSource,
	FString& OutSource,
	FText& OutError);

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
	FText& OutError);
