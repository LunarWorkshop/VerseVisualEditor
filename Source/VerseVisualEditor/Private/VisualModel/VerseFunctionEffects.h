#pragma once

#include "VerseDocument.h"
#include "VisualModel/VerseVisualTile.h"
#include "uLang/Semantics/SemanticFunction.h"

inline bool IsVerseSuspendingFunctionTile(
	const FVerseVisualTile& Tile,
	const FVerseDocument& Document)
{
	if (Tile.SemanticFunction != nullptr
		&& Tile.SemanticFunction->_Signature.GetEffects()[uLang::EEffect::suspends])
	{
		return true;
	}

	for (const FVerseTextRange& Range : Tile.FunctionEffectSpecifierRanges)
	{
		if (Document.DecodeOriginalRange(Range).TrimStartAndEnd().Equals(
			TEXT("suspends"), ESearchCase::IgnoreCase))
		{
			return true;
		}
	}
	return false;
}
