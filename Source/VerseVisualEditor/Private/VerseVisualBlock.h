#pragma once

#include "CoreMinimal.h"
#include "VerseParseSnapshot.h"

enum class EVerseVisualBlockKind : uint8
{
	Definition,
	Comment,
	Unknown
};

/** Read-only presentation data. All text remains referenced by snapshot byte ranges. */
struct FVerseVisualBlock
{
	FVerseByteRange Range;
	EVerseVisualBlockKind Kind = EVerseVisualBlockKind::Unknown;
	FName DefinitionKind;
	FVerseByteRange NameRange;
	FVerseByteRange TypeRange;
	FVerseByteRange BodyRange;
	EVerseCommentKind CommentKind = EVerseCommentKind::None;
};

class FVerseVisualBlockBuilder
{
public:
	static TArray<FVerseVisualBlock> Build(const FVerseParseSnapshot& Snapshot);
};
