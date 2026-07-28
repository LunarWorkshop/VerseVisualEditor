#pragma once

#include "CoreMinimal.h"
#include "VerseDocumentRevision.h"

class FVerseParseSnapshot;
struct FVerseVisualTile;

struct FVerseFunctionNavigationItem
{
	FString Name;
	TArray<FString> ScopePath;
	FVerseTextRange FunctionRange;
	FVerseTextRange BodyRange;
};

class FVerseFunctionNavigationBuilder
{
public:
	static TArray<FVerseFunctionNavigationItem> Build(
		TConstArrayView<FVerseVisualTile> Tiles,
		const FVerseParseSnapshot& Snapshot);
	static bool FindDefinitionPath(
		TConstArrayView<FVerseVisualTile> Tiles,
		const FVerseParseSnapshot& Snapshot,
		FVerseTextRange DefinitionRange,
		TArray<FString>& OutPath);
};
