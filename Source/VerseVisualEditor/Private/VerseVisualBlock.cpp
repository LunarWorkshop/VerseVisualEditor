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

		FVerseVisualBlock& Block = Blocks.AddDefaulted_GetRef();
		Block.Range = Region.Range;
		if (Region.Kind == EVerseSourceRegionKind::Syntax)
		{
			Block.Kind = EVerseVisualBlockKind::Definition;
			Block.DefinitionKind = Region.SyntaxKind;
			Block.NameRange = Region.NameRange;
			Block.TypeRange = Region.TypeRange;
		}
		else if (Region.Kind == EVerseSourceRegionKind::Comment)
		{
			Block.Kind = EVerseVisualBlockKind::Comment;
		}
	}
	return Blocks;
}
