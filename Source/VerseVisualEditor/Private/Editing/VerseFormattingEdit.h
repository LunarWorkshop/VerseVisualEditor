#pragma once

#include "CoreMinimal.h"
#include "VisualModel/VerseVisualTile.h"

class FVerseDocumentSession;

enum class EVerseSyntaxControlKind : uint8
{
	None,
	GroupingLayers,
	StatementSeparator,
	BlankLinesAfter,
	BodyDelimiter,
	BodyLayout,
	BracePlacement,
	Indentation,
	LineEnding,
	OperatorSpacing,
	TypeColonSpacing,
	InitializerSpacing,
	CallSpacing,
	CommentOpenerSpacing,
	CommentStyle,
};

/** Meaning-preserving, localized formatting transactions for source-backed tiles. */
class FVerseFormattingEditService
{
public:
	static bool Apply(
		FVerseDocumentSession& Session,
		const FVerseVisualTile& Tile,
		EVerseSyntaxControlKind Control,
		FStringView Value,
		FText& OutError,
		int32 ControlRegionIndex = INDEX_NONE);
};
