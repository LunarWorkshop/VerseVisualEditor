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

	FVerseVisualExpressionDescriptor MakeVisualExpressionDescriptor(
		const FVerseExpressionDescriptor& Expression,
		FVerseDocumentRevision Revision)
	{
		FVerseVisualExpressionDescriptor Result;
		Result.Range = MakeTextRange(Revision, Expression.Range);
		Result.OperatorRange = MakeTextRange(Revision, Expression.OperatorRange);
		Result.Kind = Expression.Kind;
		Result.LiteralKind = Expression.LiteralKind;
		Result.TypeRange = MakeTextRange(Revision, Expression.Type.SourceRange);
		Result.IntrinsicTypeName = Expression.Type.IntrinsicName;
		Result.TypeProvenance = Expression.Type.Provenance;
		for (const FVerseExpressionDescriptor& Operand : Expression.Operands)
		{
			Result.Operands.Add(MakeVisualExpressionDescriptor(Operand, Revision));
		}
		return Result;
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
			VisualItem.Expression = MakeVisualExpressionDescriptor(Item.Expression, Revision);
			VisualItem.LeadingTriviaRange = MakeTextRange(Revision, Item.LeadingTriviaRange);
			VisualItem.TrailingTriviaRange = MakeTextRange(Revision, Item.TrailingTriviaRange);
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

	FVerseVisualSocket MakeSocket(
		FVerseTextRange TypeRange,
		FName IntrinsicTypeName,
		bool bConnected,
		FVerseTextRange NameRange = {})
	{
		FVerseVisualSocket Socket;
		Socket.NameRange = NameRange;
		Socket.TypeRange = TypeRange;
		Socket.IntrinsicTypeName = IntrinsicTypeName;
		Socket.bConnected = bConnected;
		return Socket;
	}

	FVerseVisualTile MakeExpressionTile(
		const FVerseVisualExpressionDescriptor& Descriptor,
		const FVerseParseSnapshot& Snapshot,
		bool bStatementLevel,
		bool bImplicitReturnValue)
	{
		FVerseVisualTile Tile;
		Tile.Kind = EVerseVisualTileKind::Expression;
		Tile.ExpressionKind = Descriptor.Kind;
		Tile.LiteralKind = Descriptor.LiteralKind;
		Tile.Range = Descriptor.Range;
		Tile.OperatorRange = Descriptor.OperatorRange;
		Tile.NameRange = Descriptor.Kind == EVerseExpressionKind::Identifier
			? Descriptor.Range
			: FVerseTextRange();
		Tile.TypeRange = Descriptor.TypeRange;
		Tile.IntrinsicTypeName = Descriptor.IntrinsicTypeName;
		Tile.TypeProvenance = Descriptor.TypeProvenance;
		if (bStatementLevel)
		{
			Tile.FirstSourceLine = Snapshot.GetDocument()->GetOriginalLineNumber(
				Descriptor.Range.BeginByte);
			Tile.LastSourceLine = Snapshot.GetDocument()->GetOriginalLineNumber(
				FMath::Max(Descriptor.Range.BeginByte, Descriptor.Range.EndByte() - 1));
			Tile.bHasExecutionInput = true;
			Tile.bHasExecutionOutput = true;
			Tile.bExecutionInputConnected = true;
			Tile.bExecutionOutputConnected = true;
			Tile.bImplicitReturnValue = bImplicitReturnValue;
		}

		for (const FVerseVisualExpressionDescriptor& Operand : Descriptor.Operands)
		{
			Tile.Children.Add(MakeExpressionTile(Operand, Snapshot, false, false));
		}
		if (Descriptor.Kind == EVerseExpressionKind::Addition)
		{
			for (int32 Index = 0; Index < Descriptor.Operands.Num(); ++Index)
			{
				Tile.ValueInputs.Add(MakeSocket(
					Descriptor.TypeRange,
					Descriptor.IntrinsicTypeName,
					true));
			}
			Tile.ValueOutputs.Add(MakeSocket(
				Descriptor.TypeRange,
				Descriptor.IntrinsicTypeName,
				bImplicitReturnValue));
			for (FVerseVisualTile& Child : Tile.Children)
			{
				if (Child.LiteralKind == EVerseLiteralKind::None)
				{
					Child.ValueOutputs.Add(MakeSocket(
						Child.TypeRange,
						Child.IntrinsicTypeName,
						true));
				}
			}
		}
		else if (Descriptor.Kind == EVerseExpressionKind::Identifier && bStatementLevel)
		{
			Tile.ValueInputs.Add(MakeSocket(Tile.TypeRange, Tile.IntrinsicTypeName, false));
			Tile.ValueOutputs.Add(MakeSocket(
				Tile.TypeRange,
				Tile.IntrinsicTypeName,
				bImplicitReturnValue));
		}
		else if (bStatementLevel && bImplicitReturnValue)
		{
			Tile.ValueOutputs.Add(MakeSocket(
				Tile.TypeRange,
				Tile.IntrinsicTypeName,
				true));
		}

		// Inline literal editing is a property of every input socket, independent
		// of which expression kind created that input. Future calls and operators
		// get the same behavior by exposing operands and corresponding inputs.
		const int32 InputCount = FMath::Min(Tile.ValueInputs.Num(), Descriptor.Operands.Num());
		for (int32 InputIndex = 0; InputIndex < InputCount; ++InputIndex)
		{
			const FVerseVisualExpressionDescriptor& Operand = Descriptor.Operands[InputIndex];
			if (Operand.LiteralKind != EVerseLiteralKind::None)
			{
				FVerseVisualSocket& Input = Tile.ValueInputs[InputIndex];
				Input.bConnected = false;
				Input.InlineLiteralRange = Operand.Range;
				Input.InlineLiteralKind = Operand.LiteralKind;
			}
		}
		return Tile;
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
		FVerseVisualTile Expression = MakeExpressionTile(
			Item.Expression,
			Snapshot,
			true,
			Item.bIsFinalValuePosition && bHasReturnValue);
		Expression.ExtraBlankLineCount = Item.ExtraBlankLineCount;
		if (!Expression.TypeRange.IsSet()
			&& Expression.IntrinsicTypeName.IsNone()
			&& Expression.bImplicitReturnValue)
		{
			Expression.TypeRange = FunctionTile.TypeRange;
			if (!Expression.ValueOutputs.IsEmpty())
			{
				Expression.ValueOutputs[0].TypeRange = FunctionTile.TypeRange;
			}
		}
		GraphTiles.Add(MoveTemp(Expression));
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
