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
	/**
	 * Optional parameter-to-parameter type sources used only when the compiler's
	 * instantiated formal remains abstract and cannot produce a source-safe
	 * default on its own. INDEX_NONE means to use the parameter's own type.
	 */
	TArray<int32> DefaultSourceTypeParameterIndices;
	/**
	 * Source-safe placeholder used for abstract parameters when an untyped
	 * clause-insertion search provides no dragged socket type.
	 */
	FString UntypedDefaultSource;
	/** Equivalent operands produce one canonical drag action rather than two. */
	bool bSymmetricOperands = false;
	/** The signature picker may omit a result that is not useful for choosing an overload. */
	bool bOmitResultInSignaturePicker = false;
	/** Present all compiler overloads as one polymorphic expression-search action. */
	bool bGroupOverloadsInActionMenu = false;
	/** Operand type preferred when a grouped action is created without a type constraint. */
	FString PreferredUntypedOperandType;
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

/** Finds presentation policy using callable identity without requiring concrete types. */
const FVerseIntrinsicPresentationDescriptor* FindVerseIntrinsicOperatorPresentation(
	EVerseIntrinsicCallableForm Form,
	FStringView Spelling);
