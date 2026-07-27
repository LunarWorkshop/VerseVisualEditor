#pragma once

#include "Containers/Array.h"
#include "Containers/UnrealString.h"

class FVerseParseSnapshot;
struct FVerseVisualTile;

enum class EVerseTilePropertyEditKind : uint8
{
	None,
	Name,
	AccessSpecifiers,
	EffectSpecifiers,
};

struct FVerseTileProperty
{
	FString Name;
	FString Value;
	bool bEditable = false;
	EVerseTilePropertyEditKind EditKind = EVerseTilePropertyEditKind::None;
};

class FVerseTileProperties
{
public:
	static TArray<FVerseTileProperty> Build(
		const FVerseVisualTile& Tile,
		const FVerseParseSnapshot& Snapshot);

	static bool MatchesFilter(const FVerseTileProperty& Property, const FString& Filter);
};
