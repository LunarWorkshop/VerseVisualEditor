#pragma once

#include "CoreMinimal.h"
#include "VerseVisualTile.h"

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
		FText& OutError);

	static bool DeleteExpression(
		FVerseDocumentSession& Session,
		const FVerseVisualClauseDescriptor& Clause,
		int32 ItemIndex,
		FText& OutError);

	static bool ReorderExpression(
		FVerseDocumentSession& Session,
		const FVerseVisualClauseDescriptor& Clause,
		int32 FromIndex,
		int32 ToIndex,
		FText& OutError);
};
