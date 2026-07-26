#pragma once

#include "VerseParseSnapshot.h"

namespace VerseSyntaxKind
{
	extern VERSEVISUALEDITOR_API const FName Module;
	extern VERSEVISUALEDITOR_API const FName Class;
	extern VERSEVISUALEDITOR_API const FName Struct;
	extern VERSEVISUALEDITOR_API const FName Interface;
	extern VERSEVISUALEDITOR_API const FName Enum;
	extern VERSEVISUALEDITOR_API const FName Function;
	extern VERSEVISUALEDITOR_API const FName Variable;
	extern VERSEVISUALEDITOR_API const FName Constant;
	extern VERSEVISUALEDITOR_API const FName TypeAlias;
}

/**
 * Builds a lossless parse snapshot from the official Verse compiler VST.
 * Compiler errors and source not represented by supported VST definitions remain raw.
 */
class VERSEVISUALEDITOR_API FVerseParseSnapshotBuilder final
{
public:
	static FVerseParseSnapshot Build(TSharedRef<const FVerseDocument> Document);
};
