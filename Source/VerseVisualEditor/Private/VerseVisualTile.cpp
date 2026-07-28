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

	FVerseTextRange MakeTextRange(FVerseDocumentRevision Revision, FVerseByteRange Range)
	{
		return Range.IsSet() ? FVerseTextRange(Revision, Range) : FVerseTextRange();
	}

	FVerseVisualClauseDescriptor MakeVisualClauseDescriptor(
		const FVerseClauseDescriptor& Descriptor,
		FVerseDocumentRevision Revision)
	{
		FVerseVisualClauseDescriptor Result;
		Result.InteriorRange = MakeTextRange(Revision, Descriptor.InteriorRange);
		Result.OpeningPunctuationRange = MakeTextRange(Revision, Descriptor.OpeningPunctuationRange);
		Result.ClosingPunctuationRange = MakeTextRange(Revision, Descriptor.ClosingPunctuationRange);
		Result.PunctuationStyle = Descriptor.PunctuationStyle;
		if (Descriptor.EmptyBodyInsertionByte != INDEX_NONE)
		{
			Result.EmptyBodyInsertionAnchor = FVerseTextRange(
				Revision,
				FVerseByteRange::FromBounds(
					Descriptor.EmptyBodyInsertionByte,
					Descriptor.EmptyBodyInsertionByte));
		}
		for (const FVerseClauseItemDescriptor& Item : Descriptor.Items)
		{
			FVerseVisualClauseItemDescriptor& VisualItem = Result.Items.AddDefaulted_GetRef();
			VisualItem.ExpressionRange = MakeTextRange(Revision, Item.ExpressionRange);
			VisualItem.LeadingTriviaRange = MakeTextRange(Revision, Item.LeadingTriviaRange);
			VisualItem.TrailingTriviaRange = MakeTextRange(Revision, Item.TrailingTriviaRange);
			VisualItem.TypeRange = MakeTextRange(Revision, Item.TypeRange);
			VisualItem.ExpressionKind = Item.ExpressionKind;
			VisualItem.Separator = Item.Separator;
			VisualItem.ExtraBlankLineCount = Item.ExtraBlankLineCount;
			VisualItem.bIsFinalValuePosition = Item.bIsFinalValuePosition;
		}
		return Result;
	}

	FVerseVisualFunctionParameter MakeVisualFunctionParameter(
		const FVerseFunctionParameter& Parameter,
		FVerseDocumentRevision Revision)
	{
		FVerseVisualFunctionParameter Result;
		Result.Range = MakeTextRange(Revision, Parameter.Range);
		Result.NameRange = MakeTextRange(Revision, Parameter.NameRange);
		Result.TypeRange = MakeTextRange(Revision, Parameter.TypeRange);
		for (const FVerseByteRange ReferenceRange : Parameter.ReferenceRanges)
		{
			Result.ReferenceRanges.Add(MakeTextRange(Revision, ReferenceRange));
		}
		return Result;
	}

	TArray<FVerseVisualTile> BuildTiles(
		const FVerseParseSnapshot& Snapshot,
		TConstArrayView<FVerseSourceRegion> Regions,
		FVerseDocumentRevision Revision)
	{
		TArray<FVerseVisualTile> Tiles;
		for (const FVerseSourceRegion& Region : Regions)
		{
			if (Region.Kind == EVerseSourceRegionKind::Raw && IsWhitespace(Snapshot.GetSourceView(Region)))
			{
				continue;
			}
			if (Region.Kind == EVerseSourceRegionKind::Comment
				&& !Tiles.IsEmpty()
				&& CanMergeLineComment(Snapshot, Tiles.Last(), Region))
			{
				Tiles.Last().Range = FVerseTextRange(
					Revision,
					FVerseByteRange::FromBounds(
						Tiles.Last().Range.BeginByte,
						Region.Range.EndByte()));
				Tiles.Last().BodyRange = Tiles.Last().Range;
				UpdateSourceLines(Tiles.Last(), *Snapshot.GetDocument());
				continue;
			}

			FVerseVisualTile& Tile = Tiles.AddDefaulted_GetRef();
			Tile.Range = FVerseTextRange(Revision, Region.Range);
			UpdateSourceLines(Tile, *Snapshot.GetDocument());
			if (Region.Kind == EVerseSourceRegionKind::Syntax)
			{
				Tile.Kind = EVerseVisualTileKind::Definition;
				Tile.DefinitionKind = Region.SyntaxKind;
				Tile.NameRange = MakeTextRange(Revision, Region.NameRange);
				Tile.TypeRange = MakeTextRange(Revision, Region.TypeRange);
				for (const FVerseByteRange SpecifierRange : Region.SpecifierRanges)
				{
					Tile.SpecifierRanges.Add(MakeTextRange(Revision, SpecifierRange));
				}
				for (const FVerseByteRange SpecifierRange : Region.FunctionAccessSpecifierRanges)
				{
					Tile.FunctionAccessSpecifierRanges.Add(MakeTextRange(Revision, SpecifierRange));
				}
				for (const FVerseByteRange SpecifierRange : Region.FunctionEffectSpecifierRanges)
				{
					Tile.FunctionEffectSpecifierRanges.Add(MakeTextRange(Revision, SpecifierRange));
				}
				for (const FVerseFunctionParameter& Parameter : Region.FunctionParameters)
				{
					Tile.FunctionParameters.Add(MakeVisualFunctionParameter(Parameter, Revision));
				}
				Tile.HeaderRange = MakeTextRange(Revision, Region.HeaderRange);
				Tile.BodyRange = MakeTextRange(Revision, Region.BodyRange);
				Tile.BodyClause = MakeVisualClauseDescriptor(Region.BodyClause, Revision);
				Tile.Children = BuildTiles(Snapshot, Region.Children, Revision);
			}
			else if (Region.Kind == EVerseSourceRegionKind::Comment)
			{
				Tile.Kind = EVerseVisualTileKind::Comment;
				Tile.BodyRange = FVerseTextRange(Revision, Region.BodyRange);
				Tile.CommentKind = Region.CommentKind;
			}
		}
		return Tiles;
	}
}

TArray<FVerseVisualTile> FVerseVisualTileBuilder::Build(
	const FVerseParseSnapshot& Snapshot,
	FVerseDocumentRevision Revision)
{
	return BuildTiles(Snapshot, Snapshot.GetSourceRegions(), Revision);
}

TArray<FVerseVisualTile> FVerseVisualTileBuilder::BuildFunctionGraph(
	const FVerseVisualTile& FunctionTile,
	const FVerseParseSnapshot& Snapshot)
{
	TArray<FVerseVisualTile> GraphTiles;
	if (FunctionTile.Kind != EVerseVisualTileKind::Definition
		|| FunctionTile.DefinitionKind != VerseSyntaxKind::Function)
	{
		return GraphTiles;
	}

	FVerseVisualTile& Entry = GraphTiles.AddDefaulted_GetRef();
	Entry.Kind = EVerseVisualTileKind::FunctionEntry;
	Entry.DefinitionKind = VerseSyntaxKind::Function;
	Entry.Range = FunctionTile.HeaderRange;
	Entry.HeaderRange = FunctionTile.HeaderRange;
	Entry.NameRange = FunctionTile.NameRange;
	Entry.FirstSourceLine = FunctionTile.FirstSourceLine;
	Entry.LastSourceLine = FunctionTile.HeaderRange.IsSet()
		? Snapshot.GetDocument()->GetOriginalLineNumber(
			FMath::Max(FunctionTile.HeaderRange.BeginByte, FunctionTile.HeaderRange.EndByte() - 1))
		: FunctionTile.FirstSourceLine;
	Entry.bHasExecutionOutput = true;
	Entry.bExecutionOutputConnected = true;
	for (const FVerseVisualFunctionParameter& Parameter : FunctionTile.FunctionParameters)
	{
		FVerseVisualSocket& Socket = Entry.ValueOutputs.AddDefaulted_GetRef();
		Socket.NameRange = Parameter.NameRange;
		Socket.TypeRange = Parameter.TypeRange;
	}

	const FString ReturnType = FunctionTile.TypeRange.IsSet()
		? Snapshot.GetDocument()->DecodeOriginalRange(FunctionTile.TypeRange).TrimStartAndEnd()
		: FString();
	const bool bHasReturnValue = !ReturnType.IsEmpty()
		&& !ReturnType.Equals(TEXT("void"), ESearchCase::IgnoreCase);
	for (const FVerseVisualClauseItemDescriptor& Item : FunctionTile.BodyClause.Items)
	{
		FVerseVisualTile& Expression = GraphTiles.AddDefaulted_GetRef();
		Expression.Kind = EVerseVisualTileKind::Expression;
		Expression.ExpressionKind = Item.ExpressionKind;
		Expression.Range = Item.ExpressionRange;
		Expression.NameRange = Item.ExpressionRange;
		Expression.TypeRange = Item.TypeRange;
		Expression.FirstSourceLine = Snapshot.GetDocument()->GetOriginalLineNumber(
			Item.ExpressionRange.BeginByte);
		Expression.LastSourceLine = Snapshot.GetDocument()->GetOriginalLineNumber(
			FMath::Max(Item.ExpressionRange.BeginByte, Item.ExpressionRange.EndByte() - 1));
		Expression.ExtraBlankLineCount = Item.ExtraBlankLineCount;
		Expression.bHasExecutionInput = true;
		Expression.bHasExecutionOutput = true;
		Expression.bExecutionInputConnected = true;
		Expression.bExecutionOutputConnected = true;
		Expression.bImplicitReturnValue = Item.bIsFinalValuePosition && bHasReturnValue;
		if (Item.ExpressionKind == EVerseExpressionKind::Identifier)
		{
			Expression.ValueInputs.Add({{}, Item.TypeRange, false});
			Expression.ValueOutputs.Add(
				{{}, Item.TypeRange.IsSet() ? Item.TypeRange : FunctionTile.TypeRange,
					Expression.bImplicitReturnValue});
		}
		else if (Expression.bImplicitReturnValue)
		{
			Expression.ValueOutputs.Add({{},
				Item.TypeRange.IsSet() ? Item.TypeRange : FunctionTile.TypeRange,
				true});
		}
	}

	FVerseVisualTile& Return = GraphTiles.AddDefaulted_GetRef();
	Return.Kind = EVerseVisualTileKind::FunctionReturn;
	Return.TypeRange = FunctionTile.TypeRange;
	Return.Range = FunctionTile.BodyRange.IsSet()
		? FVerseTextRange(
			FunctionTile.BodyRange.Revision,
			FVerseByteRange::FromBounds(
				FunctionTile.BodyRange.EndByte(),
				FunctionTile.BodyRange.EndByte()))
		: FVerseTextRange();
	Return.bHasExecutionInput = true;
	Return.bExecutionInputConnected = true;
	if (bHasReturnValue)
	{
		Return.ValueInputs.Add({{}, FunctionTile.TypeRange,
			FunctionTile.BodyClause.Items.ContainsByPredicate(
				[](const FVerseVisualClauseItemDescriptor& Item)
				{
					return Item.bIsFinalValuePosition;
				})});
	}
	return GraphTiles;
}
