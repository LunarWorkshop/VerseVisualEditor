#pragma once

#include "Containers/ArrayView.h"
#include "VerseParseSnapshot.h"

/** Resolves conservative UI types from declarative operator signatures. */
class FVerseOperatorTyping
{
public:
	static FVerseExpressionType Resolve(
		FStringView OperatorSpelling,
		TConstArrayView<FVerseExpressionType> OperandTypes,
		const FVerseExpressionType& ExpectedResult,
		FUtf8StringView Source);

	static bool SupportsOperandCount(FStringView OperatorSpelling, int32 OperandCount);

	/** True when at least one declared operand position accepts the supplied type evidence. */
	static bool CanAcceptOperand(
		FStringView OperatorSpelling,
		const FVerseExpressionType& OperandType,
		FUtf8StringView Source);

	/** True when at least one overload can produce the supplied result type. */
	static bool CanProduceResult(
		FStringView OperatorSpelling,
		const FVerseExpressionType& ResultType,
		FUtf8StringView Source);
};
