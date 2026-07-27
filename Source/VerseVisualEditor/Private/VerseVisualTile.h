#pragma once

#include "CoreMinimal.h"
#include "VerseDocumentRevision.h"
#include "VerseParseSnapshot.h"

enum class EVerseVisualTileKind : uint8
{
	Definition,
	Comment,
	Unknown
};

struct FVerseVisualClauseDescriptor
{
	FVerseTextRange InteriorRange;
	FVerseTextRange OpeningPunctuationRange;
	FVerseTextRange ClosingPunctuationRange;
	EVerseClausePunctuationStyle PunctuationStyle = EVerseClausePunctuationStyle::None;
	FVerseTextRange EmptyBodyInsertionAnchor;
};

/** Read-only presentation data. All text remains referenced by snapshot byte ranges. */
struct FVerseVisualTile
{
	FVerseTextRange Range;
	int32 FirstSourceLine = INDEX_NONE;
	int32 LastSourceLine = INDEX_NONE;
	EVerseVisualTileKind Kind = EVerseVisualTileKind::Unknown;
	FName DefinitionKind;
	FVerseTextRange NameRange;
	FVerseTextRange TypeRange;
	FVerseTextRange BodyRange;
	FVerseVisualClauseDescriptor BodyClause;
	TArray<FVerseVisualTile> Children;
	EVerseCommentKind CommentKind = EVerseCommentKind::None;
};

class FVerseVisualTileBuilder
{
public:
	static TArray<FVerseVisualTile> Build(
		const FVerseParseSnapshot& Snapshot,
		FVerseDocumentRevision Revision = {});
};
