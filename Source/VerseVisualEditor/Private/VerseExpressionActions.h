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

/** One expression creation choice which is valid at a particular typed socket. */
struct FVerseExpressionAction
{
	EVerseExpressionActionKind Kind = EVerseExpressionActionKind::Identifier;
	FText DisplayName;
	FText Category;
	FVerseTextRange IdentifierNameRange;
};

/** Discovers expression actions from the current lexical scope and the expression registry. */
class FVerseExpressionActionQuery
{
public:
	static TArray<TSharedPtr<FVerseExpressionAction>> Build(
		TConstArrayView<FVerseFunctionNavigationParameter> Parameters,
		const FVerseVisualTile& DraggedExpression,
		const FVerseDocument& Document);
};

/** Applies an action only after a scratch parse proves that it produces the requested structure. */
bool TryApplyVerseExpressionAction(
	FVerseDocumentSession& Session,
	FVerseTextRange ExpressionRange,
	const FVerseExpressionAction& Action,
	FText& OutError);
