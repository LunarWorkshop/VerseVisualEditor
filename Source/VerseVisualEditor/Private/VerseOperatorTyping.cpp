#include "VerseOperatorTyping.h"

#include "Containers/StringConv.h"
#include "VerseIntrinsicPresentation.h"

namespace
{
	FString NormalizeType(FString Type)
	{
		Type.TrimStartAndEndInline();
		Type.ReplaceInline(TEXT(" "), TEXT(""));
		Type.ReplaceInline(TEXT("\t"), TEXT(""));
		return Type.ToLower();
	}

	FString Decode(FUtf8StringView Source, FVerseByteRange Range)
	{
		if (!Range.IsSet() || Range.BeginByte < 0 || Range.EndByte() > Source.Len())
		{
			return FString();
		}
		const FUTF8ToTCHAR Converted(
			reinterpret_cast<const ANSICHAR*>(Source.GetData() + Range.BeginByte),
			Range.NumBytes);
		return FString(Converted.Length(), Converted.Get());
	}

	FString GetEvidenceName(const FVerseExpressionType& Type, FUtf8StringView Source)
	{
		return NormalizeType(Type.SourceRange.IsSet()
			? Decode(Source, Type.SourceRange)
			: Type.IntrinsicName.ToString());
	}

	bool IsStructuralOperator(
		const FVerseIntrinsicPresentationDescriptor& Descriptor,
		FStringView Spelling)
	{
		return Descriptor.bStructuralSignature
			&& Descriptor.Key.Form == EVerseIntrinsicCallableForm::InfixOperator
			&& Descriptor.Key.Spelling == FString(Spelling);
	}

	bool MatchPattern(
		FStringView Pattern,
		FStringView Evidence,
		TConstArrayView<FString> OperandEvidence)
	{
		if (Evidence.IsEmpty())
		{
			return true;
		}
		if (Pattern == TEXT("*"))
		{
			return true;
		}
		if (Pattern == TEXT("$0"))
		{
			return OperandEvidence.IsEmpty()
				|| OperandEvidence[0].IsEmpty()
				|| Evidence == OperandEvidence[0];
		}
		if (Pattern == TEXT("[]*"))
		{
			return Evidence.StartsWith(TEXT("[]"))
				&& (OperandEvidence.IsEmpty()
					|| OperandEvidence[0].IsEmpty()
					|| !OperandEvidence[0].StartsWith(TEXT("[]"))
					|| Evidence == OperandEvidence[0]);
		}
		return Evidence == NormalizeType(FString(Pattern));
	}

	FString ResolvePattern(FStringView Pattern, TConstArrayView<FString> OperandEvidence)
	{
		if (Pattern == TEXT("$0") || Pattern == TEXT("*") || Pattern == TEXT("[]*"))
		{
			return OperandEvidence.IsEmpty() ? FString() : OperandEvidence[0];
		}
		return NormalizeType(FString(Pattern));
	}
}

FVerseExpressionType FVerseOperatorTyping::Resolve(
	FStringView OperatorSpelling,
	TConstArrayView<FVerseExpressionType> OperandTypes,
	const FVerseExpressionType& ExpectedResult,
	FUtf8StringView Source)
{
	TArray<FString> OperandEvidence;
	OperandEvidence.Reserve(OperandTypes.Num());
	for (const FVerseExpressionType& Operand : OperandTypes)
	{
		OperandEvidence.Add(GetEvidenceName(Operand, Source));
	}
	const FString Expected = GetEvidenceName(ExpectedResult, Source);
	TSet<FString> MatchingResults;
	for (const FVerseIntrinsicPresentationDescriptor& Descriptor : GetVerseIntrinsicPresentationTable())
	{
		if (!IsStructuralOperator(Descriptor, OperatorSpelling)
			|| Descriptor.Key.ParameterTypes.Num() != OperandTypes.Num())
		{
			continue;
		}
		bool bMatches = true;
		for (int32 Index = 0; Index < OperandEvidence.Num(); ++Index)
		{
			if (!MatchPattern(
				Descriptor.Key.ParameterTypes[Index], OperandEvidence[Index], OperandEvidence))
			{
				bMatches = false;
				break;
			}
		}
		const FString ResultType = ResolvePattern(Descriptor.Key.ResultType, OperandEvidence);
		if (bMatches && !ResultType.IsEmpty()
			&& MatchPattern(Descriptor.Key.ResultType, Expected, OperandEvidence))
		{
			MatchingResults.Add(ResultType);
		}
	}
	if (MatchingResults.Num() != 1)
	{
		return {};
	}

	const FString ResolvedName = MatchingResults.Array()[0];
	FVerseExpressionType Resolved;
	Resolved.Provenance = EVerseTypeResolutionProvenance::LocallyInferred;
	for (const FVerseExpressionType& Operand : OperandTypes)
	{
		if (Operand.SourceRange.IsSet() && GetEvidenceName(Operand, Source) == ResolvedName)
		{
			Resolved.SourceRange = Operand.SourceRange;
			return Resolved;
		}
	}
	if (ExpectedResult.SourceRange.IsSet() && Expected == ResolvedName)
	{
		Resolved.SourceRange = ExpectedResult.SourceRange;
		return Resolved;
	}
	Resolved.IntrinsicName = FName(*ResolvedName);
	return Resolved;
}

bool FVerseOperatorTyping::SupportsOperandCount(FStringView OperatorSpelling, int32 OperandCount)
{
	return GetVerseIntrinsicPresentationTable().ContainsByPredicate(
		[OperatorSpelling, OperandCount](const FVerseIntrinsicPresentationDescriptor& Descriptor)
		{
			return IsStructuralOperator(Descriptor, OperatorSpelling)
				&& Descriptor.Key.ParameterTypes.Num() == OperandCount;
		});
}

bool FVerseOperatorTyping::CanAcceptOperand(
	FStringView OperatorSpelling,
	const FVerseExpressionType& OperandType,
	FUtf8StringView Source)
{
	const FString Evidence = GetEvidenceName(OperandType, Source);
	if (Evidence.IsEmpty())
	{
		return false;
	}
	for (const FVerseIntrinsicPresentationDescriptor& Descriptor : GetVerseIntrinsicPresentationTable())
	{
		if (!IsStructuralOperator(Descriptor, OperatorSpelling))
		{
			continue;
		}
		for (const FString& Pattern : Descriptor.Key.ParameterTypes)
		{
			if (Pattern == TEXT("*") || Pattern == TEXT("$0")
				|| (Pattern == TEXT("[]*") && Evidence.StartsWith(TEXT("[]")))
				|| NormalizeType(Pattern) == Evidence)
			{
				return true;
			}
		}
	}
	return false;
}

bool FVerseOperatorTyping::CanProduceResult(
	FStringView OperatorSpelling,
	const FVerseExpressionType& ResultType,
	FUtf8StringView Source)
{
	const FString Evidence = GetEvidenceName(ResultType, Source);
	if (Evidence.IsEmpty())
	{
		return false;
	}
	return GetVerseIntrinsicPresentationTable().ContainsByPredicate(
		[OperatorSpelling, &Evidence](const FVerseIntrinsicPresentationDescriptor& Descriptor)
		{
			return IsStructuralOperator(Descriptor, OperatorSpelling)
				&& (Descriptor.Key.ResultType == TEXT("*")
					|| Descriptor.Key.ResultType == TEXT("$0")
					|| (Descriptor.Key.ResultType == TEXT("[]*") && Evidence.StartsWith(TEXT("[]")))
					|| NormalizeType(Descriptor.Key.ResultType) == Evidence);
		});
}
