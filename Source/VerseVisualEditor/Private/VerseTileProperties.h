#pragma once

#include "Containers/Array.h"
#include "Containers/UnrealString.h"

class FVerseParseSnapshot;
struct FVerseVisualTile;

struct FVerseTileProperty
{
	FString Name;
	FString Value;
};

class FVerseTileProperties
{
public:
	static TArray<FVerseTileProperty> Build(
		const FVerseVisualTile& Tile,
		const FVerseParseSnapshot& Snapshot);

	static bool MatchesFilter(const FVerseTileProperty& Property, const FString& Filter);
};
