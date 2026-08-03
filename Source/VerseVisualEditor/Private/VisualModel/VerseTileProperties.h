#pragma once

#include "Containers/Array.h"
#include "Containers/UnrealString.h"
#include "Editing/VerseFormattingEdit.h"
#include "VisualModel/VerseVisualTile.h"

class FVerseParseSnapshot;
struct FVerseVisualTile;

enum class EVerseTilePropertyEditKind : uint8
{
	None,
	Name,
	Type,
	OperatorSignature,
	AccessSpecifiers,
	EffectSpecifiers,
	Literal,
	Syntax,
};

struct FVerseTileProperty
{
	FString Name;
	FString Value;
	bool bEditable = false;
	EVerseTilePropertyEditKind EditKind = EVerseTilePropertyEditKind::None;
	EVerseLiteralKind LiteralKind = EVerseLiteralKind::None;
	FVerseTextRange EditRange;
	EVerseSyntaxControlKind SyntaxControl = EVerseSyntaxControlKind::None;
	int32 SyntaxRegionIndex = INDEX_NONE;
	TArray<FString> Options;
	FString Example;
};

class FVerseTileProperties
{
public:
	static TArray<FVerseTileProperty> Build(
		const FVerseVisualTile& Tile,
		const FVerseParseSnapshot& Snapshot);

	static bool MatchesFilter(const FVerseTileProperty& Property, const FString& Filter);
};
