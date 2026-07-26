#include "VerseVisualTile.h"

namespace
{
	bool IsWhitespace(FUtf8StringView Text)
	{
		for (const UTF8CHAR Character : Text)
		{
			if (Character != static_cast<UTF8CHAR>(' ')
				&& Character != static_cast<UTF8CHAR>('\t')
				&& Character != static_cast<UTF8CHAR>('\r')
				&& Character != static_cast<UTF8CHAR>('\n'))
			{
				return false;
			}
		}
		return true;
	}

	bool CanMergeLineComment(
		const FVerseParseSnapshot& Snapshot,
		const FVerseVisualTile& Previous,
		const FVerseSourceRegion& Current)
	{
		if (Previous.Kind != EVerseVisualTileKind::Comment
			|| Previous.CommentKind != EVerseCommentKind::Line
			|| Current.CommentKind != EVerseCommentKind::Line
			|| Current.Range.BeginByte < Previous.Range.EndByte())
		{
			return false;
		}

		const FVerseByteRange Gap = FVerseByteRange::FromBounds(
			Previous.Range.EndByte(),
			Current.Range.BeginByte);
		const FUtf8StringView GapText = Snapshot.GetSourceView(Gap);
		if (!IsWhitespace(GapText))
		{
			return false;
		}

		int32 LineBreakCount = 0;
		for (int32 Index = 0; Index < GapText.Len(); ++Index)
		{
			if (GapText[Index] == static_cast<UTF8CHAR>('\n')
				|| (GapText[Index] == static_cast<UTF8CHAR>('\r')
					&& (Index + 1 >= GapText.Len() || GapText[Index + 1] != static_cast<UTF8CHAR>('\n'))))
			{
				++LineBreakCount;
			}
		}
		return LineBreakCount <= 1;
	}

	void UpdateSourceLines(FVerseVisualTile& Tile, const FVerseDocument& Document)
	{
		Tile.FirstSourceLine = Document.GetOriginalLineNumber(Tile.Range.BeginByte);
		const int32 LastOccupiedByte = Tile.Range.NumBytes > 0
			? Tile.Range.EndByte() - 1
			: Tile.Range.BeginByte;
		Tile.LastSourceLine = Document.GetOriginalLineNumber(LastOccupiedByte);
	}
}

TArray<FVerseVisualTile> FVerseVisualTileBuilder::Build(const FVerseParseSnapshot& Snapshot)
{
	TArray<FVerseVisualTile> Tiles;
	for (const FVerseSourceRegion& Region : Snapshot.GetSourceRegions())
	{
		if (Region.Kind == EVerseSourceRegionKind::Raw && IsWhitespace(Snapshot.GetSourceView(Region)))
		{
			continue;
		}
		if (Region.Kind == EVerseSourceRegionKind::Comment
			&& !Tiles.IsEmpty()
			&& CanMergeLineComment(Snapshot, Tiles.Last(), Region))
		{
			Tiles.Last().Range = FVerseByteRange::FromBounds(
				Tiles.Last().Range.BeginByte,
				Region.Range.EndByte());
			Tiles.Last().BodyRange = Tiles.Last().Range;
			UpdateSourceLines(Tiles.Last(), *Snapshot.GetDocument());
			continue;
		}

		FVerseVisualTile& Tile = Tiles.AddDefaulted_GetRef();
		Tile.Range = Region.Range;
		UpdateSourceLines(Tile, *Snapshot.GetDocument());
		if (Region.Kind == EVerseSourceRegionKind::Syntax)
		{
			Tile.Kind = EVerseVisualTileKind::Definition;
			Tile.DefinitionKind = Region.SyntaxKind;
			Tile.NameRange = Region.NameRange;
			Tile.TypeRange = Region.TypeRange;
			Tile.BodyRange = Region.BodyRange;
		}
		else if (Region.Kind == EVerseSourceRegionKind::Comment)
		{
			Tile.Kind = EVerseVisualTileKind::Comment;
			Tile.BodyRange = Region.BodyRange;
			Tile.CommentKind = Region.CommentKind;
		}
	}
	return Tiles;
}
