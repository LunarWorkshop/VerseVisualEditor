#include "Editing/VerseBlueprintCallablePresentation.h"

#include "K2Node_CallFunction.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet/KismetStringLibrary.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectIterator.h"
#include "Editing/VerseIntrinsicPresentation.h"
#include "uLang/Semantics/SemanticTypes.h"

namespace
{
	const TMultiMap<FString, const UFunction*>& GetBlueprintCallableIndex()
	{
		// Native Blueprint function-library classes and their functions live for the
		// editor process, so indexing them once avoids rescanning reflection for every
		// semantic candidate in an expression search.
		static const TMultiMap<FString, const UFunction*> Index = []
		{
			// The fallback must not depend on which editor feature happened to load
			// these native libraries before the first expression search.
			UKismetMathLibrary::StaticClass();
			UKismetStringLibrary::StaticClass();
			TMultiMap<FString, const UFunction*> Result;
			for (UClass* Class : TObjectRange<UClass>())
			{
				if (Class == nullptr
					|| !Class->IsChildOf(UBlueprintFunctionLibrary::StaticClass()))
				{
					continue;
				}
				for (TFieldIterator<UFunction> It(
					Class, EFieldIteratorFlags::ExcludeSuper); It; ++It)
				{
					const UFunction* Function = *It;
					if (!Function->HasAnyFunctionFlags(
						FUNC_BlueprintCallable | FUNC_BlueprintPure))
					{
						continue;
					}
					const FString FunctionName = Function->GetName();
					Result.Add(FunctionName.ToLower(), Function);
					int32 SuffixSeparator = INDEX_NONE;
					if (FunctionName.FindChar(TEXT('_'), SuffixSeparator))
					{
						Result.Add(FunctionName.Left(SuffixSeparator).ToLower(), Function);
					}
					if (Function->HasMetaData(TEXT("ScriptMethod")))
					{
						Result.Add(
							Function->GetMetaData(TEXT("ScriptMethod")).ToLower(),
							Function);
					}
				}
			}
			return Result;
		}();
		return Index;
	}

	int32 ScoreName(FStringView VerseName, const UFunction& Function)
	{
		const FString VerseNameString(VerseName);
		const FString FunctionName = Function.GetName();
		if (FunctionName.Equals(VerseNameString, ESearchCase::IgnoreCase))
		{
			return 100;
		}
		if (FunctionName.StartsWith(VerseNameString + TEXT("_"), ESearchCase::IgnoreCase))
		{
			return 80;
		}
		if (Function.HasMetaData(TEXT("ScriptMethod"))
			&& Function.GetMetaData(TEXT("ScriptMethod")).Equals(
				VerseNameString, ESearchCase::IgnoreCase))
		{
			return 90;
		}
		return INDEX_NONE;
	}

	int32 ScoreProperty(const uLang::CTypeBase& VerseType, const FProperty& Property)
	{
		// Check named aliases before normalizing: Verse string normalizes to its
		// array representation, which would otherwise be mistaken for TArray.
		if (FString(UTF8_TO_TCHAR(VerseType.AsCode().AsCString())) == TEXT("string"))
		{
			return CastField<FStrProperty>(&Property) != nullptr ? 10 : INDEX_NONE;
		}
		switch (VerseType.GetNormalType().GetKind())
		{
		case uLang::ETypeKind::Logic:
		case uLang::ETypeKind::True:
		case uLang::ETypeKind::False:
			return CastField<FBoolProperty>(&Property) != nullptr ? 10 : INDEX_NONE;
		case uLang::ETypeKind::Int:
			if (CastField<FIntProperty>(&Property) != nullptr)
			{
				return 10;
			}
			return CastField<FInt64Property>(&Property) != nullptr ? 8 : INDEX_NONE;
		case uLang::ETypeKind::Float:
			if (CastField<FDoubleProperty>(&Property) != nullptr)
			{
				return 10;
			}
			return CastField<FFloatProperty>(&Property) != nullptr ? 8 : INDEX_NONE;
		case uLang::ETypeKind::Char8:
		case uLang::ETypeKind::Char32:
			return CastField<FStrProperty>(&Property) != nullptr ? 8 : INDEX_NONE;
		case uLang::ETypeKind::Array:
			return CastField<FArrayProperty>(&Property) != nullptr ? 6 : INDEX_NONE;
		case uLang::ETypeKind::Void:
			return INDEX_NONE;
		default:
			return INDEX_NONE;
		}
	}

	int32 ScoreSignature(const uLang::CFunctionType& VerseType, const UFunction& Function)
	{
		TArray<const FProperty*> Inputs;
		const FProperty* Return = nullptr;
		for (TFieldIterator<FProperty> It(&Function); It; ++It)
		{
			const FProperty* Property = *It;
			if (!Property->HasAnyPropertyFlags(CPF_Parm))
			{
				continue;
			}
			if (Property->HasAnyPropertyFlags(CPF_ReturnParm))
			{
				Return = Property;
			}
			else
			{
				Inputs.Add(Property);
			}
		}

		const uLang::CFunctionType::ParamTypes Params = VerseType.GetParamTypes();
		if (Inputs.Num() != Params.Num())
		{
			return INDEX_NONE;
		}

		int32 Score = 0;
		for (int32 Index = 0; Index < Params.Num(); ++Index)
		{
			const int32 ParamScore = ScoreProperty(*Params[Index], *Inputs[Index]);
			if (ParamScore == INDEX_NONE)
			{
				return INDEX_NONE;
			}
			Score += ParamScore;
		}

		if (VerseType.GetReturnType().GetNormalType().GetKind() == uLang::ETypeKind::Void)
		{
			return Return == nullptr ? Score + 10 : INDEX_NONE;
		}
		if (Return == nullptr)
		{
			return INDEX_NONE;
		}
		const int32 ReturnScore = ScoreProperty(VerseType.GetReturnType(), *Return);
		return ReturnScore == INDEX_NONE ? INDEX_NONE : Score + ReturnScore;
	}

	TOptional<FVerseBlueprintCallablePresentation> BuildPresentation(
		const UFunction* Function)
	{
		if (Function == nullptr)
		{
			return {};
		}
		const FText ExplicitDisplayName = Function->HasMetaData(TEXT("DisplayName"))
			? Function->GetDisplayNameText()
			: FText::GetEmpty();
		return FVerseBlueprintCallablePresentation{
			ExplicitDisplayName,
			UK2Node_CallFunction::GetDefaultCategoryForFunction(
				Function, FText::GetEmpty())};
	}
}

TOptional<FVerseBlueprintCallablePresentation> ResolveVerseBlueprintCallablePresentation(
	FStringView VerseName,
	const uLang::CFunctionType& VerseFunctionType,
	const FVerseIntrinsicPresentationDescriptor* Intrinsic)
{
	if (Intrinsic != nullptr
		&& Intrinsic->BlueprintLibrary != EVerseIntrinsicBlueprintLibrary::None)
	{
		return BuildPresentation(ResolveVerseIntrinsicBlueprintFunction(*Intrinsic));
	}

	const UFunction* BestFunction = nullptr;
	int32 BestScore = INDEX_NONE;
	TArray<const UFunction*> Functions;
	GetBlueprintCallableIndex().MultiFind(FString(VerseName).ToLower(), Functions);
	for (const UFunction* Function : Functions)
	{
		if (Function == nullptr)
		{
			continue;
		}
		const int32 NameScore = ScoreName(VerseName, *Function);
		if (NameScore == INDEX_NONE)
		{
			continue;
		}
		const int32 SignatureScore = ScoreSignature(VerseFunctionType, *Function);
		if (SignatureScore == INDEX_NONE)
		{
			continue;
		}
		const int32 Score = NameScore + SignatureScore;
		if (Score > BestScore)
		{
			BestFunction = Function;
			BestScore = Score;
		}
	}

	return BuildPresentation(BestFunction);
}
