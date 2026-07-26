#pragma once

#include "CoreMinimal.h"
#include "VerseParseSnapshot.h"

enum class EVerseVisualTileKind : uint8
{
	Definition,
	Comment,
	Unknown
};

/** Read-only presentation data. All text remains referenced by snapshot byte ranges. */
struct FVerseVisualTile
{
	FVerseByteRange Range;
	EVerseVisualTileKind Kind = EVerseVisualTileKind::Unknown;
	FName DefinitionKind;
	FVerseByteRange NameRange;
	FVerseByteRange TypeRange;
	FVerseByteRange BodyRange;
	EVerseCommentKind CommentKind = EVerseCommentKind::None;
};

class FVerseVisualTileBuilder
{
public:
	static TArray<FVerseVisualTile> Build(const FVerseParseSnapshot& Snapshot);
};
