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

	FString FormatSpecifiers(
		TConstArrayView<FVerseTextRange> Ranges,
		const FVerseParseSnapshot& Snapshot)
	{
		FString Result;
		for (const FVerseTextRange& Range : Ranges)
		{
			Result += TEXT("<");
			Result += Snapshot.GetDocument()->DecodeOriginalRange(Range);
			Result += TEXT(">");
		}
		return Result;
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
		Properties.Add({
			TEXT("Name"),
			Snapshot.GetDocument()->DecodeOriginalRange(Tile.NameRange),
			true,
			EVerseTilePropertyEditKind::Name});
		if (Tile.TypeRange.IsSet())
		{
			Properties.Add({TEXT("Type"), Snapshot.GetDocument()->DecodeOriginalRange(Tile.TypeRange)});
		}
		if (Tile.DefinitionKind == VerseSyntaxKind::Module && !Tile.SpecifierRanges.IsEmpty())
		{
			Properties.Add({
				TEXT("Effects / Specifiers"),
				FormatSpecifiers(Tile.SpecifierRanges, Snapshot)});
		}
		else if (Tile.DefinitionKind == VerseSyntaxKind::Function)
		{
			Properties.Add({
				TEXT("Access Specifiers"),
				FormatSpecifiers(Tile.FunctionAccessSpecifierRanges, Snapshot),
				true,
				EVerseTilePropertyEditKind::AccessSpecifiers});
			if (!Tile.FunctionEffectSpecifierRanges.IsEmpty())
			{
				Properties.Add({
					TEXT("Effects"),
					FormatSpecifiers(Tile.FunctionEffectSpecifierRanges, Snapshot),
					true,
					EVerseTilePropertyEditKind::EffectSpecifiers});
			}
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
