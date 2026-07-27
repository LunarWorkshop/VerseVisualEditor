#include "VerseTileProperties.h"

#include "VerseParseSnapshot.h"
#include "VerseParseSnapshotBuilder.h"
#include "VerseVisualTile.h"

namespace
{
	FString GetTileKind(const FVerseVisualTile& Tile)
	{
		switch (Tile.Kind)
		{
		case EVerseVisualTileKind::Definition:
			return TEXT("Definition");
		case EVerseVisualTileKind::Comment:
			return TEXT("Comment");
		default:
			return TEXT("Unknown");
		}
	}

	FString GetCommentKind(EVerseCommentKind Kind)
	{
		switch (Kind)
		{
		case EVerseCommentKind::Line:
			return TEXT("Line");
		case EVerseCommentKind::Block:
			return TEXT("Block");
		case EVerseCommentKind::Indented:
			return TEXT("Indented");
		case EVerseCommentKind::Fragment:
			return TEXT("Fragment");
		default:
			return TEXT("None");
		}
	}

	FString GetSourceLines(const FVerseVisualTile& Tile)
	{
		return Tile.FirstSourceLine == Tile.LastSourceLine
			? FString::Printf(TEXT("L%d"), Tile.FirstSourceLine)
			: FString::Printf(TEXT("L%d-%d"), Tile.FirstSourceLine, Tile.LastSourceLine);
	}
}

TArray<FVerseTileProperty> FVerseTileProperties::Build(
	const FVerseVisualTile& Tile,
	const FVerseParseSnapshot& Snapshot)
{
	TArray<FVerseTileProperty> Properties;
	Properties.Add({TEXT("Tile"), GetTileKind(Tile)});

	if (Tile.Kind == EVerseVisualTileKind::Definition)
	{
		Properties.Add({TEXT("Kind"), Tile.DefinitionKind.ToString()});
		Properties.Add({TEXT("Name"), Snapshot.GetDocument()->DecodeOriginalRange(Tile.NameRange), true});
		if (Tile.TypeRange.IsSet())
		{
			Properties.Add({TEXT("Type"), Snapshot.GetDocument()->DecodeOriginalRange(Tile.TypeRange)});
		}
		if (Tile.DefinitionKind == VerseSyntaxKind::Module && !Tile.SpecifierRanges.IsEmpty())
		{
			FString Specifiers;
			for (const FVerseTextRange& Range : Tile.SpecifierRanges)
			{
				Specifiers += TEXT("<");
				Specifiers += Snapshot.GetDocument()->DecodeOriginalRange(Range);
				Specifiers += TEXT(">");
			}
			Properties.Add({TEXT("Effects / Specifiers"), MoveTemp(Specifiers)});
		}
	}
	else if (Tile.Kind == EVerseVisualTileKind::Comment)
	{
		Properties.Add({TEXT("Comment Style"), GetCommentKind(Tile.CommentKind)});
	}

	Properties.Add({TEXT("Lines"), GetSourceLines(Tile)});
	return Properties;
}

bool FVerseTileProperties::MatchesFilter(const FVerseTileProperty& Property, const FString& Filter)
{
	return Filter.IsEmpty()
		|| Property.Name.Contains(Filter, ESearchCase::IgnoreCase)
		|| Property.Value.Contains(Filter, ESearchCase::IgnoreCase);
}
