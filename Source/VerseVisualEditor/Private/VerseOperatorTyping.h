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
};
