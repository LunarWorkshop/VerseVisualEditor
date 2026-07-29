#pragma once

#include "CoreMinimal.h"

namespace uLang
{
	class CFunctionType;
}

struct FVerseBlueprintCallablePresentation
{
	/** Present only when the matched UFunction explicitly declares DisplayName metadata. */
	FText ExplicitDisplayName;
	/** Blueprint's generated/prettified label, used only as an intrinsic fallback. */
	FText IntrinsicFallbackDisplayName;
	FText Category;
};

/** Finds the Blueprint action-menu presentation for an equivalent Verse callable. */
TOptional<FVerseBlueprintCallablePresentation> ResolveVerseBlueprintCallablePresentation(
	FStringView VerseName,
	const uLang::CFunctionType& VerseFunctionType);
