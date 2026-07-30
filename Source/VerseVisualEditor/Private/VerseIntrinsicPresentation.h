#pragma once

#include "CoreMinimal.h"

class UFunction;

enum class EVerseIntrinsicCallableForm : uint8
{
	Ordinary,
	InfixOperator,
	PrefixOperator,
	PostfixOperator,
};

enum class EVerseIntrinsicBlueprintLibrary : uint8
{
	None,
	KismetArray,
	KismetMath,
	KismetString,
};

/** Declarative key used to find presentation metadata for an engine-provided Verse expression. */
struct FVerseIntrinsicPresentationKey
{
	EVerseIntrinsicCallableForm Form = EVerseIntrinsicCallableForm::Ordinary;
	FString Spelling;
	TArray<FString> ParameterTypes;
	FString ResultType;
};

/** One data row; built-in spellings and Blueprint aliases belong only in this table. */
struct FVerseIntrinsicPresentationDescriptor
{
	FVerseIntrinsicPresentationKey Key;
	EVerseIntrinsicBlueprintLibrary BlueprintLibrary =
		EVerseIntrinsicBlueprintLibrary::None;
	FName BlueprintFunctionName;
	FText FallbackDisplayName;
	FText FallbackCategory;
};

struct FVerseResolvedExpressionPresentation
{
	FText DisplayName;
	FText Category;
};

const FVerseIntrinsicPresentationDescriptor* FindVerseIntrinsicPresentation(
	const FVerseIntrinsicPresentationKey& Key);

const UFunction* ResolveVerseIntrinsicBlueprintFunction(
	const FVerseIntrinsicPresentationDescriptor& Descriptor);

/** Applies Verse, UFunction, descriptor, and final fallback precedence in one place. */
FVerseResolvedExpressionPresentation ResolveVerseExpressionPresentation(
	const FText& VerseDisplayName,
	const FText& VerseCategory,
	const FText& UFunctionDisplayName,
	const FText& UFunctionCategory,
	const FVerseIntrinsicPresentationDescriptor* Intrinsic,
	FStringView ActualName);

/** Test-visible read-only access to validate that the private table is unambiguous. */
TConstArrayView<FVerseIntrinsicPresentationDescriptor> GetVerseIntrinsicPresentationTable();
