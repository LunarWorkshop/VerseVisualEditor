#pragma once

#include "CoreMinimal.h"
#include "VerseDocumentRevision.h"
#include "VisualModel/VerseVisualTile.h"

class FVerseParseSnapshot;

struct FVerseFunctionNavigationParameter
{
	FVerseTextRange NameRange;
	FVerseTextRange TypeRange;
};

struct FVerseFunctionNavigationItem
{
	FString Name;
	TArray<FString> ScopePath;
	FVerseTextRange FunctionRange;
	FVerseTextRange DeclarationRange;
	FVerseTextRange BodyRange;
	FVerseTextRange ReturnTypeRange;
	TArray<FVerseFunctionNavigationParameter> Parameters;
	TArray<FVerseVisualTile> GraphTiles;
	int32 FirstDeclarationLine = INDEX_NONE;
	int32 LastDeclarationLine = INDEX_NONE;
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
