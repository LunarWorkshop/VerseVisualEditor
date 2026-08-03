#pragma once

#include "CoreMinimal.h"
#include "VisualModel/VerseVisualTile.h"

class FVerseDocumentSession;
struct FVerseExpressionAction;

/** Lossless editing operations shared by executable and failure-context clauses. */
class FVerseClauseEditing
{
public:
	static bool InsertExpression(
		FVerseDocumentSession& Session,
		const FVerseVisualClauseDescriptor& Clause,
		int32 InsertIndex,
		const FVerseExpressionAction& Action,
		FText& OutError,
		FVerseTextRange* OutInsertedRange = nullptr,
		FStringView BoundExpressionSource = {});

	/** Adds an else clause containing one expression to an if which has none. */
	static bool AddElseExpression(
		FVerseDocumentSession& Session,
		FVerseTextRange IfExpressionRange,
		EVerseClausePunctuationStyle BodyStyle,
		const FVerseExpressionAction& Action,
		FText& OutError,
		FVerseTextRange* OutInsertedRange = nullptr,
		FStringView BoundExpressionSource = {});

	static bool ReplaceExpression(
		FVerseDocumentSession& Session,
		const FVerseVisualClauseDescriptor& Clause,
		int32 ItemIndex,
		const FVerseExpressionAction& Action,
		FText& OutError,
		FVerseTextRange* OutReplacementRange = nullptr);

	static bool DeleteExpression(
		FVerseDocumentSession& Session,
		const FVerseVisualClauseDescriptor& Clause,
		int32 ItemIndex,
		FText& OutError,
		FVerseTextRange* OutProvisionalReplacementRange = nullptr);

	static bool ReorderExpression(
		FVerseDocumentSession& Session,
		const FVerseVisualClauseDescriptor& Clause,
		int32 FromIndex,
		int32 ToIndex,
		FText& OutError);
};
