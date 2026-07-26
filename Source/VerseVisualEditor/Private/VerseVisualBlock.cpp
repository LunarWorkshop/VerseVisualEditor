#include "VerseVisualBlock.h"

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
		const FVerseVisualBlock& Previous,
		const FVerseSourceRegion& Current)
	{
		if (Previous.Kind != EVerseVisualBlockKind::Comment
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
}

TArray<FVerseVisualBlock> FVerseVisualBlockBuilder::Build(const FVerseParseSnapshot& Snapshot)
{
	TArray<FVerseVisualBlock> Blocks;
	for (const FVerseSourceRegion& Region : Snapshot.GetSourceRegions())
	{
		if (Region.Kind == EVerseSourceRegionKind::Raw && IsWhitespace(Snapshot.GetSourceView(Region)))
		{
			continue;
		}
		if (Region.Kind == EVerseSourceRegionKind::Comment
			&& !Blocks.IsEmpty()
			&& CanMergeLineComment(Snapshot, Blocks.Last(), Region))
		{
			Blocks.Last().Range = FVerseByteRange::FromBounds(
				Blocks.Last().Range.BeginByte,
				Region.Range.EndByte());
			Blocks.Last().BodyRange = Blocks.Last().Range;
			continue;
		}

		FVerseVisualBlock& Block = Blocks.AddDefaulted_GetRef();
		Block.Range = Region.Range;
		if (Region.Kind == EVerseSourceRegionKind::Syntax)
		{
			Block.Kind = EVerseVisualBlockKind::Definition;
			Block.DefinitionKind = Region.SyntaxKind;
			Block.NameRange = Region.NameRange;
			Block.TypeRange = Region.TypeRange;
			Block.BodyRange = Region.BodyRange;
		}
		else if (Region.Kind == EVerseSourceRegionKind::Comment)
		{
			Block.Kind = EVerseVisualBlockKind::Comment;
			Block.BodyRange = Region.BodyRange;
			Block.CommentKind = Region.CommentKind;
		}
	}
	return Blocks;
}
