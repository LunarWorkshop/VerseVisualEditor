#pragma once

#include "CoreMinimal.h"
#include "VerseDocumentRevision.h"

class FVerseParseSnapshot;
struct FVerseVisualTile;

struct FVerseOutlinerItem
{
	FString Name;
	FString Label;
	FName DefinitionKind;
	FVerseTextRange TileRange;
	TArray<TSharedPtr<FVerseOutlinerItem>> Children;
};

class FVerseOutlinerBuilder
{
public:
	static TArray<TSharedPtr<FVerseOutlinerItem>> Build(
		TConstArrayView<FVerseVisualTile> Tiles,
		const FVerseParseSnapshot& Snapshot);
};
