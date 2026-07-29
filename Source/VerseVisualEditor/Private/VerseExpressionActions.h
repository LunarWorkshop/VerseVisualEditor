#pragma once

#include "CoreMinimal.h"
#include "VerseDocumentRevision.h"
#include "VerseVisualTile.h"
#include "VerseFunctionNavigation.h"

class FVerseDocument;
class FVerseDocumentSession;
class FVerseSemanticSnapshot;

namespace uLang
{
	class CDataDefinition;
	class CFunction;
}

/** Editor-owned syntax shape; semantic operation identity remains compiler-owned. */
enum class EVerseExpressionSourceForm : uint8
{
	IdentifierReference,
	OrdinaryCall,
	InfixOperator,
	PrefixOperator,
	PostfixOperator,
};

enum class EVerseExpressionActionValidation : uint8
{
	/** Current-revision ranges plus prospective syntax/VST validation are sufficient. */
	Structural,
	/** Compiler-owned callable signature is stable; local syntax validation still applies. */
	StableSemanticSignature,
	/** The action is bound to semantic claims that require the exact current snapshot. */
	ExactSemanticSnapshot,
};

/** One expression creation choice which is valid at a particular typed socket. */
struct FVerseExpressionAction
{
	EVerseExpressionSourceForm SourceForm =
		EVerseExpressionSourceForm::IdentifierReference;
	EVerseExpressionActionValidation Validation =
		EVerseExpressionActionValidation::Structural;
	FText DisplayName;
	FText Category;
	FVerseTextRange IdentifierNameRange;
	/** Direct source spelling for compiler-discovered identifiers and callables. */
	FString SourceSpelling;
	bool bUsesFailureCallSyntax = false;
	int32 BoundInputIndex = INDEX_NONE;
	TArray<FString> InputDefaultSources;
	const uLang::CDataDefinition* SemanticDataDefinition = nullptr;
	const uLang::CFunction* SemanticFunction = nullptr;
	TSharedPtr<const FVerseSemanticSnapshot> SemanticSnapshot;
};

using FVerseExpressionSemanticValidator =
	TFunction<bool(const FUtf8String& ProspectiveSource, FText& OutError)>;

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
};

/** Applies an action only after a scratch parse proves that it produces the requested structure. */
bool TryApplyVerseExpressionAction(
	FVerseDocumentSession& Session,
	FVerseTextRange ExpressionRange,
	const FVerseExpressionAction& Action,
	const FVerseExpressionSemanticValidator& SemanticValidator,
	FText& OutError);
