#pragma once

#include "Containers/ArrayView.h"
#include "VerseParseSnapshot.h"

enum class EVerseOperatorKind : uint8
{
	Addition,
};

/** Resolves conservative UI types from declarative operator signatures. */
class FVerseOperatorTyping
{
public:
	static FVerseExpressionType Resolve(
		EVerseOperatorKind Operator,
		TConstArrayView<FVerseExpressionType> OperandTypes,
		const FVerseExpressionType& ExpectedResult,
		FUtf8StringView Source);

	static bool SupportsOperandCount(EVerseOperatorKind Operator, int32 OperandCount);

	/** True when at least one declared operand position accepts the supplied type evidence. */
	static bool CanAcceptOperand(
		EVerseOperatorKind Operator,
		const FVerseExpressionType& OperandType,
		FUtf8StringView Source);

	/** True when at least one overload can produce the supplied result type. */
	static bool CanProduceResult(
		EVerseOperatorKind Operator,
		const FVerseExpressionType& ResultType,
		FUtf8StringView Source);
};
