#pragma once

#include "Containers/Array.h"
#include "Containers/UnrealString.h"
#include "VerseVisualTile.h"

class FVerseParseSnapshot;
struct FVerseVisualTile;

enum class EVerseTilePropertyEditKind : uint8
{
	None,
	Name,
	Type,
	AccessSpecifiers,
	EffectSpecifiers,
	Literal,
};

struct FVerseTileProperty
{
	FString Name;
	FString Value;
	bool bEditable = false;
	EVerseTilePropertyEditKind EditKind = EVerseTilePropertyEditKind::None;
	EVerseLiteralKind LiteralKind = EVerseLiteralKind::None;
	FVerseTextRange EditRange;
};

class FVerseTileProperties
{
public:
	static TArray<FVerseTileProperty> Build(
		const FVerseVisualTile& Tile,
		const FVerseParseSnapshot& Snapshot);

	static bool MatchesFilter(const FVerseTileProperty& Property, const FString& Filter);
};
