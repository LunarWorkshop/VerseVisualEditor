#pragma once

#include "CoreMinimal.h"
#include "VerseDocumentRevision.h"
#include "VerseVisualTile.h"
#include "VerseFunctionNavigation.h"

class FVerseDocument;
class FVerseDocumentSession;

enum class EVerseExpressionActionKind : uint8
{
	Identifier,
	Addition,
};

enum class EVerseExpressionActionValidation : uint8
{
	/** Current-revision ranges plus prospective syntax/VST validation are sufficient. */
	Structural,
	/** The action is bound to semantic claims that require the exact current snapshot. */
	ExactSemanticSnapshot,
};

/** One expression creation choice which is valid at a particular typed socket. */
struct FVerseExpressionAction
{
	EVerseExpressionActionKind Kind = EVerseExpressionActionKind::Identifier;
	EVerseExpressionActionValidation Validation =
		EVerseExpressionActionValidation::Structural;
	FText DisplayName;
	FText Category;
	FVerseTextRange IdentifierNameRange;
	/** Source-safe defaults for required inputs not supplied by the initiating wire. */
	TArray<FString> RemainingInputDefaultSources;
};

/** Discovers expression actions from the current lexical scope and the expression registry. */
class FVerseExpressionActionQuery
{
public:
	static TArray<TSharedPtr<FVerseExpressionAction>> Build(
		TConstArrayView<FVerseFunctionNavigationParameter> Parameters,
		const FVerseVisualSocket& DraggedSocket,
		bool bDraggingFromOutput,
		const FVerseDocument& Document);
};

/** Applies an action only after a scratch parse proves that it produces the requested structure. */
bool TryApplyVerseExpressionAction(
	FVerseDocumentSession& Session,
	FVerseTextRange ExpressionRange,
	const FVerseExpressionAction& Action,
	FText& OutError);
