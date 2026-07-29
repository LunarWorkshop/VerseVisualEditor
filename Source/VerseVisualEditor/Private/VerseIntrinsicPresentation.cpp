#include "VerseIntrinsicPresentation.h"

#include "Kismet/KismetMathLibrary.h"
#include "Kismet/KismetStringLibrary.h"

#define LOCTEXT_NAMESPACE "VerseIntrinsicPresentation"

namespace
{
	constexpr const TCHAR* AnyType = TEXT("*");
	constexpr const TCHAR* SameAsFirstType = TEXT("$0");

	FString NormalizePresentationType(FString Type)
	{
		Type.TrimStartAndEndInline();
		Type.ReplaceInline(TEXT(" "), TEXT(""));
		Type.ReplaceInline(TEXT("\t"), TEXT(""));
		return Type.ToLower();
	}

	FVerseIntrinsicPresentationDescriptor MakeDescriptor(
		EVerseIntrinsicCallableForm Form,
		const TCHAR* Spelling,
		std::initializer_list<const TCHAR*> ParameterTypes,
		const TCHAR* ResultType,
		EVerseIntrinsicBlueprintLibrary BlueprintLibrary,
		FName BlueprintFunctionName,
		FText FallbackDisplayName,
		FText FallbackCategory)
	{
		FVerseIntrinsicPresentationDescriptor Result;
		Result.Key.Form = Form;
		Result.Key.Spelling = Spelling;
		for (const TCHAR* ParameterType : ParameterTypes)
		{
			Result.Key.ParameterTypes.Add(ParameterType);
		}
		Result.Key.ResultType = ResultType;
		Result.BlueprintLibrary = BlueprintLibrary;
		Result.BlueprintFunctionName = BlueprintFunctionName;
		Result.FallbackDisplayName = MoveTemp(FallbackDisplayName);
		Result.FallbackCategory = MoveTemp(FallbackCategory);
		return Result;
	}

	const TArray<FVerseIntrinsicPresentationDescriptor>& GetTable()
	{
		static const TArray<FVerseIntrinsicPresentationDescriptor> Table = {
			MakeDescriptor(EVerseIntrinsicCallableForm::Ordinary, TEXT("BitAnd"), {TEXT("int"), TEXT("int")}, TEXT("int"), EVerseIntrinsicBlueprintLibrary::KismetMath, GET_FUNCTION_NAME_CHECKED(UKismetMathLibrary, And_IntInt), LOCTEXT("BitAndName", "Bitwise AND"), LOCTEXT("IntegerCategory", "Math|Integer")),
			MakeDescriptor(EVerseIntrinsicCallableForm::Ordinary, TEXT("BitOr"), {TEXT("int"), TEXT("int")}, TEXT("int"), EVerseIntrinsicBlueprintLibrary::KismetMath, GET_FUNCTION_NAME_CHECKED(UKismetMathLibrary, Or_IntInt), LOCTEXT("BitOrName", "Bitwise OR"), LOCTEXT("IntegerCategory", "Math|Integer")),
			MakeDescriptor(EVerseIntrinsicCallableForm::Ordinary, TEXT("BitXor"), {TEXT("int"), TEXT("int")}, TEXT("int"), EVerseIntrinsicBlueprintLibrary::KismetMath, GET_FUNCTION_NAME_CHECKED(UKismetMathLibrary, Xor_IntInt), LOCTEXT("BitXorName", "Bitwise XOR"), LOCTEXT("IntegerCategory", "Math|Integer")),
			MakeDescriptor(EVerseIntrinsicCallableForm::Ordinary, TEXT("BitNot"), {TEXT("int")}, TEXT("int"), EVerseIntrinsicBlueprintLibrary::KismetMath, GET_FUNCTION_NAME_CHECKED(UKismetMathLibrary, Not_Int), LOCTEXT("BitNotName", "Bitwise NOT"), LOCTEXT("IntegerCategory", "Math|Integer")),
			MakeDescriptor(EVerseIntrinsicCallableForm::Ordinary, TEXT("Ceil"), {TEXT("float")}, TEXT("int"), EVerseIntrinsicBlueprintLibrary::KismetMath, GET_FUNCTION_NAME_CHECKED(UKismetMathLibrary, FCeil), LOCTEXT("CeilName", "Ceil"), LOCTEXT("FloatCategory", "Math|Float")),
			MakeDescriptor(EVerseIntrinsicCallableForm::Ordinary, TEXT("Floor"), {TEXT("float")}, TEXT("int"), EVerseIntrinsicBlueprintLibrary::KismetMath, GET_FUNCTION_NAME_CHECKED(UKismetMathLibrary, FFloor), LOCTEXT("FloorName", "Floor"), LOCTEXT("FloatCategory", "Math|Float")),
			MakeDescriptor(EVerseIntrinsicCallableForm::Ordinary, TEXT("Mod"), {TEXT("int"), TEXT("int")}, TEXT("int"), EVerseIntrinsicBlueprintLibrary::KismetMath, GET_FUNCTION_NAME_CHECKED(UKismetMathLibrary, Percent_IntInt), LOCTEXT("ModName", "% (Integer)"), LOCTEXT("IntegerCategory", "Math|Integer")),
			MakeDescriptor(EVerseIntrinsicCallableForm::Ordinary, TEXT("Quotient"), {TEXT("int"), TEXT("int")}, TEXT("int"), EVerseIntrinsicBlueprintLibrary::KismetMath, GET_FUNCTION_NAME_CHECKED(UKismetMathLibrary, Divide_IntInt), LOCTEXT("QuotientName", "int / int"), LOCTEXT("IntegerCategory", "Math|Integer")),
			MakeDescriptor(EVerseIntrinsicCallableForm::Ordinary, TEXT("Sin"), {TEXT("float")}, TEXT("float"), EVerseIntrinsicBlueprintLibrary::KismetMath, GET_FUNCTION_NAME_CHECKED(UKismetMathLibrary, Sin), LOCTEXT("SinName", "Sin (Radians)"), LOCTEXT("TrigCategory", "Math|Trig")),
			MakeDescriptor(EVerseIntrinsicCallableForm::Ordinary, TEXT("ToString"), {TEXT("int")}, TEXT("string"), EVerseIntrinsicBlueprintLibrary::KismetString, GET_FUNCTION_NAME_CHECKED(UKismetStringLibrary, Conv_IntToString), LOCTEXT("IntToStringName", "To String (Integer)"), LOCTEXT("StringCategory", "Utilities|String")),
			MakeDescriptor(EVerseIntrinsicCallableForm::Ordinary, TEXT("ToString"), {TEXT("float")}, TEXT("string"), EVerseIntrinsicBlueprintLibrary::KismetString, GET_FUNCTION_NAME_CHECKED(UKismetStringLibrary, Conv_DoubleToString), LOCTEXT("FloatToStringName", "To String (Float)"), LOCTEXT("StringCategory", "Utilities|String")),
			MakeDescriptor(EVerseIntrinsicCallableForm::Ordinary, TEXT("ToDiagnostic"), {}, TEXT("*"), EVerseIntrinsicBlueprintLibrary::None, NAME_None, LOCTEXT("ToDiagnosticName", "To Diagnostic"), LOCTEXT("StringCategory", "Utilities|String")),

			MakeDescriptor(EVerseIntrinsicCallableForm::InfixOperator, TEXT("+"), {AnyType, SameAsFirstType}, SameAsFirstType, EVerseIntrinsicBlueprintLibrary::None, NAME_None, LOCTEXT("AddName", "Add"), LOCTEXT("OperatorsCategory", "Utilities|Operators")),
			MakeDescriptor(EVerseIntrinsicCallableForm::InfixOperator, TEXT("-"), {AnyType, SameAsFirstType}, SameAsFirstType, EVerseIntrinsicBlueprintLibrary::None, NAME_None, LOCTEXT("SubtractName", "Subtract"), LOCTEXT("OperatorsCategory", "Utilities|Operators")),
			MakeDescriptor(EVerseIntrinsicCallableForm::InfixOperator, TEXT("*"), {AnyType, SameAsFirstType}, SameAsFirstType, EVerseIntrinsicBlueprintLibrary::None, NAME_None, LOCTEXT("MultiplyName", "Multiply"), LOCTEXT("OperatorsCategory", "Utilities|Operators")),
			MakeDescriptor(EVerseIntrinsicCallableForm::InfixOperator, TEXT("/"), {AnyType, SameAsFirstType}, SameAsFirstType, EVerseIntrinsicBlueprintLibrary::None, NAME_None, LOCTEXT("DivideName", "Divide"), LOCTEXT("OperatorsCategory", "Utilities|Operators")),
			MakeDescriptor(EVerseIntrinsicCallableForm::InfixOperator, TEXT("="), {AnyType, SameAsFirstType}, TEXT("logic"), EVerseIntrinsicBlueprintLibrary::None, NAME_None, LOCTEXT("EqualName", "Equal"), LOCTEXT("OperatorsCategory", "Utilities|Operators")),
			MakeDescriptor(EVerseIntrinsicCallableForm::InfixOperator, TEXT("<>"), {AnyType, SameAsFirstType}, TEXT("logic"), EVerseIntrinsicBlueprintLibrary::None, NAME_None, LOCTEXT("NotEqualName", "Not Equal"), LOCTEXT("OperatorsCategory", "Utilities|Operators")),
			MakeDescriptor(EVerseIntrinsicCallableForm::InfixOperator, TEXT("<"), {AnyType, SameAsFirstType}, TEXT("logic"), EVerseIntrinsicBlueprintLibrary::None, NAME_None, LOCTEXT("LessName", "Less"), LOCTEXT("OperatorsCategory", "Utilities|Operators")),
			MakeDescriptor(EVerseIntrinsicCallableForm::InfixOperator, TEXT("<="), {AnyType, SameAsFirstType}, TEXT("logic"), EVerseIntrinsicBlueprintLibrary::None, NAME_None, LOCTEXT("LessEqualName", "Less or Equal"), LOCTEXT("OperatorsCategory", "Utilities|Operators")),
			MakeDescriptor(EVerseIntrinsicCallableForm::InfixOperator, TEXT(">"), {AnyType, SameAsFirstType}, TEXT("logic"), EVerseIntrinsicBlueprintLibrary::None, NAME_None, LOCTEXT("GreaterName", "Greater"), LOCTEXT("OperatorsCategory", "Utilities|Operators")),
			MakeDescriptor(EVerseIntrinsicCallableForm::InfixOperator, TEXT(">="), {AnyType, SameAsFirstType}, TEXT("logic"), EVerseIntrinsicBlueprintLibrary::None, NAME_None, LOCTEXT("GreaterEqualName", "Greater or Equal"), LOCTEXT("OperatorsCategory", "Utilities|Operators")),
			MakeDescriptor(EVerseIntrinsicCallableForm::PrefixOperator, TEXT("-"), {TEXT("int")}, TEXT("int"), EVerseIntrinsicBlueprintLibrary::None, NAME_None, LOCTEXT("NegateIntName", "Negate Int"), LOCTEXT("OperatorsCategory", "Utilities|Operators")),
			MakeDescriptor(EVerseIntrinsicCallableForm::PrefixOperator, TEXT("-"), {TEXT("float")}, TEXT("float"), EVerseIntrinsicBlueprintLibrary::None, NAME_None, LOCTEXT("NegateFloatName", "Negate Float"), LOCTEXT("OperatorsCategory", "Utilities|Operators")),
		};
		return Table;
	}

	bool MatchesTypePattern(
		FStringView Pattern,
		FStringView Actual,
		TConstArrayView<FString> ActualParameters)
	{
		if (Pattern == AnyType)
		{
			return true;
		}
		if (Pattern == SameAsFirstType)
		{
			return !ActualParameters.IsEmpty()
				&& NormalizePresentationType(FString(Actual))
					== NormalizePresentationType(ActualParameters[0]);
		}
		return NormalizePresentationType(FString(Pattern))
			== NormalizePresentationType(FString(Actual));
	}

	bool Matches(
		const FVerseIntrinsicPresentationDescriptor& Descriptor,
		const FVerseIntrinsicPresentationKey& Key)
	{
		if (Descriptor.Key.Form != Key.Form
			|| !Descriptor.Key.Spelling.Equals(Key.Spelling, ESearchCase::IgnoreCase))
		{
			return false;
		}
		if (!Descriptor.Key.ParameterTypes.IsEmpty() && !Key.ParameterTypes.IsEmpty())
		{
			if (Descriptor.Key.ParameterTypes.Num() != Key.ParameterTypes.Num())
			{
				return false;
			}
			for (int32 Index = 0; Index < Key.ParameterTypes.Num(); ++Index)
			{
				if (!MatchesTypePattern(
					Descriptor.Key.ParameterTypes[Index],
					Key.ParameterTypes[Index],
					Key.ParameterTypes))
				{
					return false;
				}
			}
		}
		return Key.ResultType.IsEmpty() || Descriptor.Key.ResultType.IsEmpty()
			|| MatchesTypePattern(
				Descriptor.Key.ResultType, Key.ResultType, Key.ParameterTypes);
	}
}

const UFunction* ResolveVerseIntrinsicBlueprintFunction(
	const FVerseIntrinsicPresentationDescriptor& Descriptor)
{
	UClass* LibraryClass = nullptr;
	switch (Descriptor.BlueprintLibrary)
	{
	case EVerseIntrinsicBlueprintLibrary::KismetMath:
		LibraryClass = UKismetMathLibrary::StaticClass();
		break;
	case EVerseIntrinsicBlueprintLibrary::KismetString:
		LibraryClass = UKismetStringLibrary::StaticClass();
		break;
	case EVerseIntrinsicBlueprintLibrary::None:
		break;
	}
	return LibraryClass != nullptr && !Descriptor.BlueprintFunctionName.IsNone()
		? LibraryClass->FindFunctionByName(Descriptor.BlueprintFunctionName)
		: nullptr;
}

const FVerseIntrinsicPresentationDescriptor* FindVerseIntrinsicPresentation(
	const FVerseIntrinsicPresentationKey& Key)
{
	for (const FVerseIntrinsicPresentationDescriptor& Descriptor : GetTable())
	{
		if (Matches(Descriptor, Key))
		{
			return &Descriptor;
		}
	}
	return nullptr;
}

FVerseResolvedExpressionPresentation ResolveVerseExpressionPresentation(
	const FText& VerseDisplayName,
	const FText& VerseCategory,
	const FText& UFunctionDisplayName,
	const FText& UFunctionCategory,
	const FVerseIntrinsicPresentationDescriptor* Intrinsic,
	FStringView ActualName)
{
	FVerseResolvedExpressionPresentation Result;
	Result.DisplayName = !VerseDisplayName.IsEmpty()
		? VerseDisplayName
		: (!UFunctionDisplayName.IsEmpty()
			? UFunctionDisplayName
			: (Intrinsic != nullptr && !Intrinsic->FallbackDisplayName.IsEmpty()
				? Intrinsic->FallbackDisplayName
				: FText::FromString(FString(ActualName))));
	Result.Category = !VerseCategory.IsEmpty()
		? VerseCategory
		: (!UFunctionCategory.IsEmpty()
			? UFunctionCategory
			: (Intrinsic != nullptr && !Intrinsic->FallbackCategory.IsEmpty()
				? Intrinsic->FallbackCategory
				: LOCTEXT("Uncategorized", "Uncategorized")));
	return Result;
}

TConstArrayView<FVerseIntrinsicPresentationDescriptor> GetVerseIntrinsicPresentationTable()
{
	return GetTable();
}

#undef LOCTEXT_NAMESPACE
