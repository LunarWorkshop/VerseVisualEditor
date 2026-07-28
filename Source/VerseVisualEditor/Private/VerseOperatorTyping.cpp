#include "VerseOperatorTyping.h"

#include "Containers/StringConv.h"

namespace
{
	enum class ETypePatternKind : uint8
	{
		Concrete,
		ArrayVariable,
	};

	struct FTypePattern
	{
		ETypePatternKind Kind = ETypePatternKind::Concrete;
		FName ConcreteName;
		int32 VariableIndex = INDEX_NONE;
	};

	struct FOperatorSignature
	{
		EVerseOperatorKind Operator = EVerseOperatorKind::Addition;
		int32 MinimumOperands = 2;
		TArray<FTypePattern> FixedOperands;
		TOptional<FTypePattern> VariadicOperand;
		FTypePattern Result;
	};

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
		if (Type.SourceRange.IsSet())
		{
			return NormalizeType(Decode(Source, Type.SourceRange));
		}
		return NormalizeType(Type.IntrinsicName.ToString());
	}

	TArray<FOperatorSignature> GetSignatures(EVerseOperatorKind Operator)
	{
		TArray<FOperatorSignature> Result;
		if (Operator != EVerseOperatorKind::Addition)
		{
			return Result;
		}

		auto AddConcrete = [&Result, Operator](const FName TypeName)
		{
			FOperatorSignature& Signature = Result.AddDefaulted_GetRef();
			Signature.Operator = Operator;
			Signature.VariadicOperand = FTypePattern{ETypePatternKind::Concrete, TypeName};
			Signature.Result = FTypePattern{ETypePatternKind::Concrete, TypeName};
		};
		AddConcrete(TEXT("int"));
		AddConcrete(TEXT("float"));

		FOperatorSignature& Array = Result.AddDefaulted_GetRef();
		Array.Operator = Operator;
		Array.VariadicOperand = FTypePattern{ETypePatternKind::ArrayVariable, NAME_None, 0};
		Array.Result = FTypePattern{ETypePatternKind::ArrayVariable, NAME_None, 0};
		return Result;
	}

	bool MatchPattern(
		const FTypePattern& Pattern,
		const FString& Evidence,
		TMap<int32, FString>& Variables)
	{
		if (Evidence.IsEmpty())
		{
			return true;
		}
		if (Pattern.Kind == ETypePatternKind::Concrete)
		{
			return Evidence == Pattern.ConcreteName.ToString();
		}
		if (!Evidence.StartsWith(TEXT("[]")) || Evidence.Len() <= 2)
		{
			return false;
		}
		const FString ElementType = Evidence.Mid(2);
		if (const FString* Existing = Variables.Find(Pattern.VariableIndex))
		{
			return *Existing == ElementType;
		}
		Variables.Add(Pattern.VariableIndex, ElementType);
		return true;
	}

	FString ResolvePattern(const FTypePattern& Pattern, const TMap<int32, FString>& Variables)
	{
		if (Pattern.Kind == ETypePatternKind::Concrete)
		{
			return Pattern.ConcreteName.ToString();
		}
		const FString* Element = Variables.Find(Pattern.VariableIndex);
		return Element != nullptr ? TEXT("[]") + *Element : FString();
	}
}

FVerseExpressionType FVerseOperatorTyping::Resolve(
	EVerseOperatorKind Operator,
	TConstArrayView<FVerseExpressionType> OperandTypes,
	const FVerseExpressionType& ExpectedResult,
	FUtf8StringView Source)
{
	FVerseExpressionType Unresolved;
	TArray<FString> OperandNames;
	OperandNames.Reserve(OperandTypes.Num());
	for (const FVerseExpressionType& Operand : OperandTypes)
	{
		OperandNames.Add(GetEvidenceName(Operand, Source));
	}
	const FString ExpectedName = GetEvidenceName(ExpectedResult, Source);

	struct FMatch
	{
		FString ResultType;
	};
	TArray<FMatch> Matches;
	for (const FOperatorSignature& Signature : GetSignatures(Operator))
	{
		if (OperandTypes.Num() < Signature.MinimumOperands
			|| (!Signature.VariadicOperand.IsSet()
				&& OperandTypes.Num() != Signature.FixedOperands.Num()))
		{
			continue;
		}

		TMap<int32, FString> Variables;
		bool bMatches = true;
		for (int32 Index = 0; Index < OperandNames.Num(); ++Index)
		{
			const FTypePattern* Pattern = Signature.FixedOperands.IsValidIndex(Index)
				? &Signature.FixedOperands[Index]
				: Signature.VariadicOperand.IsSet() ? &Signature.VariadicOperand.GetValue() : nullptr;
			if (Pattern == nullptr || !MatchPattern(*Pattern, OperandNames[Index], Variables))
			{
				bMatches = false;
				break;
			}
		}
		if (bMatches && !MatchPattern(Signature.Result, ExpectedName, Variables))
		{
			bMatches = false;
		}
		const FString ResultType = bMatches ? ResolvePattern(Signature.Result, Variables) : FString();
		if (bMatches && !ResultType.IsEmpty())
		{
			Matches.Add({ResultType});
		}
	}

	if (Matches.Num() != 1)
	{
		return Unresolved;
	}

	FVerseExpressionType Resolved;
	Resolved.Provenance = EVerseTypeResolutionProvenance::LocallyInferred;
	for (const FVerseExpressionType& Operand : OperandTypes)
	{
		if (Operand.SourceRange.IsSet()
			&& GetEvidenceName(Operand, Source) == Matches[0].ResultType)
		{
			Resolved.SourceRange = Operand.SourceRange;
			return Resolved;
		}
	}
	if (ExpectedResult.SourceRange.IsSet()
		&& ExpectedName == Matches[0].ResultType)
	{
		Resolved.SourceRange = ExpectedResult.SourceRange;
		return Resolved;
	}
	Resolved.IntrinsicName = FName(*Matches[0].ResultType);
	return Resolved;
}

bool FVerseOperatorTyping::SupportsOperandCount(
	EVerseOperatorKind Operator,
	int32 OperandCount)
{
	for (const FOperatorSignature& Signature : GetSignatures(Operator))
	{
		if (OperandCount >= Signature.MinimumOperands
			&& (Signature.VariadicOperand.IsSet() || OperandCount == Signature.FixedOperands.Num()))
		{
			return true;
		}
	}
	return false;
}
