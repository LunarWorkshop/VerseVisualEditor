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

	FName GetLiteralTypeName(EVerseLiteralKind Kind)
	{
		switch (Kind)
		{
		case EVerseLiteralKind::Integer: return TEXT("int");
		case EVerseLiteralKind::Float: return TEXT("float");
		case EVerseLiteralKind::String: return TEXT("string");
		case EVerseLiteralKind::Character: return TEXT("char");
		case EVerseLiteralKind::Logic: return TEXT("logic");
		default: return NAME_None;
		}
	}

	FVerseVisualExpressionDescriptor MakeVisualExpressionDescriptor(
		const FVerseExpressionDescriptor& Expression,
		FVerseDocumentRevision Revision)
	{
		FVerseVisualExpressionDescriptor Result;
		Result.Range = MakeTextRange(Revision, Expression.Range);
		Result.OperatorRange = MakeTextRange(Revision, Expression.OperatorRange);
		Result.OperatorSpelling = Expression.OperatorSpelling;
		Result.VstNodeType = Expression.VstNodeType;
		Result.VstTag = Expression.VstTag;
		Result.Kind = Expression.Kind;
		Result.LiteralKind = Expression.LiteralKind;
		Result.ControlKind = Expression.ControlKind;
		Result.DefinitionKind = Expression.DefinitionKind;
		Result.NameRange = MakeTextRange(Revision, Expression.NameRange);
		Result.DeclaredTypeRange = MakeTextRange(Revision, Expression.DeclaredTypeRange);
		Result.TypeRange = MakeTextRange(Revision, Expression.Type.SourceRange);
		Result.IntrinsicTypeName = Expression.Type.IntrinsicName;
		Result.TypeProvenance = Expression.Type.Provenance;
		for (const FVerseExpressionDescriptor& Operand : Expression.Operands)
		{
			Result.Operands.Add(MakeVisualExpressionDescriptor(Operand, Revision));
		}
		for (const FVerseExpressionControlRegion& Region : Expression.ControlRegions)
		{
			FVerseVisualExpressionDescriptor::FControlRegion& VisualRegion =
				Result.ControlRegions.AddDefaulted_GetRef();
			VisualRegion.Range = MakeTextRange(Revision, Region.Range);
			VisualRegion.InteriorRange = MakeTextRange(Revision, Region.InteriorRange);
			VisualRegion.OpeningPunctuationRange =
				MakeTextRange(Revision, Region.OpeningPunctuationRange);
			VisualRegion.ClosingPunctuationRange =
				MakeTextRange(Revision, Region.ClosingPunctuationRange);
			VisualRegion.Kind = Region.Kind;
			VisualRegion.PunctuationStyle = Region.PunctuationStyle;
			if (Region.EmptyBodyInsertionByte != INDEX_NONE)
			{
				VisualRegion.EmptyBodyInsertionAnchor = FVerseTextRange(
					Revision,
					FVerseByteRange::FromBounds(
						Region.EmptyBodyInsertionByte,
						Region.EmptyBodyInsertionByte));
			}
			VisualRegion.FirstOperandIndex = Region.FirstOperandIndex;
			VisualRegion.OperandCount = Region.OperandCount;
			for (const FVerseExpressionControlItem& Item : Region.Items)
			{
				FVerseVisualExpressionDescriptor::FControlRegion::FItem& VisualItem =
					VisualRegion.Items.AddDefaulted_GetRef();
				VisualItem.ExpressionRange = MakeTextRange(Revision, Item.ExpressionRange);
				VisualItem.LeadingTriviaRange =
					MakeTextRange(Revision, Item.LeadingTriviaRange);
				VisualItem.TrailingTriviaRange =
					MakeTextRange(Revision, Item.TrailingTriviaRange);
				VisualItem.Separator = Item.Separator;
			}
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
		Tile.Kind = Descriptor.Kind == EVerseExpressionKind::Definition
			? EVerseVisualTileKind::Definition
			: EVerseVisualTileKind::Expression;
		Tile.ExpressionKind = Descriptor.Kind;
		Tile.LiteralKind = Descriptor.LiteralKind;
		Tile.ControlKind = Descriptor.ControlKind;
		Tile.DefinitionKind = Descriptor.DefinitionKind;
		Tile.VstNodeType = Descriptor.VstNodeType;
		Tile.VstTag = Descriptor.VstTag;
		Tile.Range = Descriptor.Range;
		Tile.OperatorRange = Descriptor.OperatorRange;
		Tile.OperatorSpelling = Descriptor.OperatorSpelling;
		Tile.NameRange = Descriptor.Kind == EVerseExpressionKind::Identifier
			|| Descriptor.Kind == EVerseExpressionKind::Call
			? Descriptor.Range
			: FVerseTextRange();
		if (Descriptor.Kind == EVerseExpressionKind::Call)
		{
			Tile.NameRange = Descriptor.OperatorRange;
		}
		else if (Descriptor.Kind == EVerseExpressionKind::Definition)
		{
			Tile.NameRange = Descriptor.NameRange;
		}
		Tile.TypeRange = Descriptor.Kind == EVerseExpressionKind::Definition
			? Descriptor.DeclaredTypeRange
			: Descriptor.TypeRange;
		Tile.IntrinsicTypeName = Descriptor.IntrinsicTypeName;
		if (Tile.ExpressionKind == EVerseExpressionKind::Literal
			&& Tile.IntrinsicTypeName.IsNone())
		{
			// Literal syntax determines its primitive type even when the VST did
			// not attach a separate type locus to the expression.
			Tile.IntrinsicTypeName = GetLiteralTypeName(Tile.LiteralKind);
		}
		Tile.TypeProvenance = Descriptor.TypeProvenance;
		if (bStatementLevel)
		{
			Tile.FirstSourceLine = Snapshot.GetDocument()->GetOriginalLineNumber(
				Descriptor.Range.BeginByte);
			Tile.LastSourceLine = Snapshot.GetDocument()->GetOriginalLineNumber(
				FMath::Max(Descriptor.Range.BeginByte, Descriptor.Range.EndByte() - 1));
			Tile.bHasExecutionInput = true;
			Tile.bHasExecutionOutput = true;
			Tile.bImplicitReturnValue = bImplicitReturnValue;
		}

		for (const FVerseVisualExpressionDescriptor::FControlRegion& Region :
			Descriptor.ControlRegions)
		{
			Tile.ControlRegions.Add(Region);
		}
		for (int32 OperandIndex = 0; OperandIndex < Descriptor.Operands.Num(); ++OperandIndex)
		{
			const FVerseVisualExpressionDescriptor& Operand = Descriptor.Operands[OperandIndex];
			const bool bConditionOperand = Descriptor.Kind == EVerseExpressionKind::Control
				&& Descriptor.ControlRegions.ContainsByPredicate(
					[OperandIndex](const FVerseVisualExpressionDescriptor::FControlRegion& Region)
					{
						return Region.Kind == EVerseControlRegionKind::Condition
							&& OperandIndex >= Region.FirstOperandIndex
							&& OperandIndex < Region.FirstOperandIndex + Region.OperandCount;
					});
			Tile.Children.Add(MakeExpressionTile(
				Operand,
				Snapshot,
				Descriptor.Kind == EVerseExpressionKind::Control
					&& (!bConditionOperand
						|| Descriptor.ControlKind == EVerseControlKind::If),
				false));
		}
		if (Descriptor.Kind == EVerseExpressionKind::Control
			&& Descriptor.ControlKind == EVerseControlKind::If)
		{
			const FVerseVisualExpressionDescriptor::FControlRegion* ConditionRegion =
				Descriptor.ControlRegions.FindByPredicate(
					[](const FVerseVisualExpressionDescriptor::FControlRegion& Region)
					{
						return Region.Kind == EVerseControlRegionKind::Condition;
					});
			if (ConditionRegion != nullptr
				&& ConditionRegion->FirstOperandIndex >= 0
				&& ConditionRegion->FirstOperandIndex <= Tile.Children.Num()
				&& ConditionRegion->OperandCount >= 0
				&& ConditionRegion->FirstOperandIndex + ConditionRegion->OperandCount
					<= Tile.Children.Num())
			{
				const int32 FirstConditionIndex = ConditionRegion->FirstOperandIndex;
				FVerseVisualTile FailablePredicate;
				FailablePredicate.Kind = EVerseVisualTileKind::FailableBlock;
				FailablePredicate.Range = ConditionRegion->Range;
				FailablePredicate.FirstSourceLine =
					Snapshot.GetDocument()->GetOriginalLineNumber(
						ConditionRegion->Range.BeginByte);
				FailablePredicate.LastSourceLine =
					Snapshot.GetDocument()->GetOriginalLineNumber(FMath::Max(
						ConditionRegion->Range.BeginByte,
						ConditionRegion->Range.EndByte() - 1));
				FailablePredicate.VstNodeType = Descriptor.VstNodeType;
				FailablePredicate.VstTag = Descriptor.VstTag;
				FailablePredicate.bHasInternalExecutionEntry = true;
				FailablePredicate.ControlRegions.Add(*ConditionRegion);
				FailablePredicate.ControlRegions[0].FirstOperandIndex = 0;
				FailablePredicate.BodyClause.InteriorRange = ConditionRegion->InteriorRange;
				FailablePredicate.BodyClause.OpeningPunctuationRange =
					ConditionRegion->OpeningPunctuationRange;
				FailablePredicate.BodyClause.ClosingPunctuationRange =
					ConditionRegion->ClosingPunctuationRange;
				FailablePredicate.BodyClause.PunctuationStyle =
					ConditionRegion->PunctuationStyle;
				FailablePredicate.BodyClause.EmptyBodyInsertionAnchor =
					ConditionRegion->EmptyBodyInsertionAnchor;
				for (int32 Offset = 0; Offset < ConditionRegion->OperandCount; ++Offset)
				{
					FVerseVisualTile Child =
						MoveTemp(Tile.Children[FirstConditionIndex + Offset]);
					FVerseVisualClauseItemDescriptor& ClauseItem =
						FailablePredicate.BodyClause.Items.AddDefaulted_GetRef();
					ClauseItem.Expression = Descriptor.Operands[
						ConditionRegion->FirstOperandIndex + Offset];
					if (ConditionRegion->Items.IsValidIndex(Offset))
					{
						const auto& RegionItem = ConditionRegion->Items[Offset];
						ClauseItem.LeadingTriviaRange = RegionItem.LeadingTriviaRange;
						ClauseItem.TrailingTriviaRange = RegionItem.TrailingTriviaRange;
						ClauseItem.Separator = RegionItem.Separator;
					}
					Child.EditableClause = FailablePredicate.BodyClause;
					Child.ClauseItemIndex = Offset;
					FailablePredicate.Children.Add(MoveTemp(Child));
				}
				// Every child must see the final descriptor rather than a partial copy.
				for (int32 Offset = 0; Offset < FailablePredicate.Children.Num(); ++Offset)
				{
					FailablePredicate.Children[Offset].EditableClause =
						FailablePredicate.BodyClause;
					FailablePredicate.Children[Offset].ClauseItemIndex = Offset;
				}
				FailablePredicate.EditableClause = FailablePredicate.BodyClause;
				Tile.Children.RemoveAt(
					FirstConditionIndex,
					ConditionRegion->OperandCount,
					EAllowShrinking::No);
				Tile.Children.Insert(MoveTemp(FailablePredicate), FirstConditionIndex);

				const int32 IndexDelta = 1 - ConditionRegion->OperandCount;
				for (FVerseVisualExpressionDescriptor::FControlRegion& Region :
					Tile.ControlRegions)
				{
					if (Region.Kind == EVerseControlRegionKind::Condition
						&& Region.FirstOperandIndex == FirstConditionIndex)
					{
						Region.OperandCount = 1;
					}
					else if (Region.FirstOperandIndex >= FirstConditionIndex)
					{
						Region.FirstOperandIndex += IndexDelta;
					}
				}
			}
		}
		else if (IsVerseOperatorExpression(Descriptor.Kind) || Descriptor.Kind == EVerseExpressionKind::Call)
		{
			for (const FVerseVisualExpressionDescriptor& Operand : Descriptor.Operands)
			{
				Tile.ValueInputs.Add(MakeSocket(
					Operand.TypeRange.IsSet() ? Operand.TypeRange : Descriptor.TypeRange,
					!Operand.IntrinsicTypeName.IsNone()
						? Operand.IntrinsicTypeName
						: Descriptor.IntrinsicTypeName,
					true));
			}
			const bool bVoidResult = Descriptor.IntrinsicTypeName == TEXT("void")
				|| (Descriptor.TypeRange.IsSet()
					&& Snapshot.GetDocument()->DecodeOriginalRange(Descriptor.TypeRange)
						.TrimStartAndEnd() == TEXT("void"));
			if (!bVoidResult)
			{
				Tile.ValueOutputs.Add(MakeSocket(
					Descriptor.TypeRange,
					Descriptor.IntrinsicTypeName,
					bImplicitReturnValue));
			}
			for (FVerseVisualTile& Child : Tile.Children)
			{
				if (Child.LiteralKind == EVerseLiteralKind::None)
				{
					if (Child.ValueOutputs.IsEmpty())
					{
						Child.ValueOutputs.Add(MakeSocket(
							Child.TypeRange,
							Child.IntrinsicTypeName,
							true));
					}
					else
					{
						Child.ValueOutputs[0].bConnected = true;
					}
				}
			}
		}
		else if (Descriptor.Kind == EVerseExpressionKind::Definition
			&& Descriptor.Operands.Num() == 1)
		{
			const FVerseVisualExpressionDescriptor& Value = Descriptor.Operands[0];
			Tile.ValueInputs.Add(MakeSocket(
				Descriptor.DeclaredTypeRange,
				Value.IntrinsicTypeName,
				Value.LiteralKind == EVerseLiteralKind::None));
			if (!Tile.Children.IsEmpty()
				&& Value.LiteralKind == EVerseLiteralKind::None)
			{
				Tile.Children[0].ValueOutputs.Add(MakeSocket(
					Value.TypeRange.IsSet() ? Value.TypeRange : Descriptor.DeclaredTypeRange,
					Value.IntrinsicTypeName,
					true));
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
		else if (Descriptor.Kind == EVerseExpressionKind::Literal && bStatementLevel)
		{
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

	void SetSequentialExecutionConnections(
		TConstArrayView<FVerseVisualTile*> Sequence,
		bool bHasIncomingConnection)
	{
		for (int32 Index = 0; Index < Sequence.Num(); ++Index)
		{
			FVerseVisualTile& Tile = *Sequence[Index];
			Tile.bExecutionInputConnected = Tile.bHasExecutionInput
				&& (Index > 0 || bHasIncomingConnection);
			Tile.bExecutionOutputConnected = Tile.bHasExecutionOutput
				&& Index + 1 < Sequence.Num();
		}
	}

	void SetNestedExecutionConnections(FVerseVisualTile& Tile)
	{
		for (FVerseVisualTile& Child : Tile.Children)
		{
			SetNestedExecutionConnections(Child);
		}

		if (Tile.Kind == EVerseVisualTileKind::FailableBlock)
		{
			TArray<FVerseVisualTile*> Sequence;
			Sequence.Reserve(Tile.Children.Num());
			for (FVerseVisualTile& Child : Tile.Children)
			{
				if (Child.bHasExecutionInput || Child.bHasExecutionOutput)
				{
					Sequence.Add(&Child);
				}
			}
			SetSequentialExecutionConnections(Sequence, Tile.bHasInternalExecutionEntry);
			return;
		}

		if (Tile.ExpressionKind != EVerseExpressionKind::Control)
		{
			return;
		}

		for (const FVerseVisualExpressionDescriptor::FControlRegion& Region :
			Tile.ControlRegions)
		{
			if (Region.Kind == EVerseControlRegionKind::Condition)
			{
				continue;
			}

			TArray<FVerseVisualTile*> Sequence;
			Sequence.Reserve(Region.OperandCount);
			for (int32 Offset = 0; Offset < Region.OperandCount; ++Offset)
			{
				const int32 ChildIndex = Region.FirstOperandIndex + Offset;
				if (Tile.Children.IsValidIndex(ChildIndex)
					&& (Tile.Children[ChildIndex].bHasExecutionInput
						|| Tile.Children[ChildIndex].bHasExecutionOutput))
				{
					Sequence.Add(&Tile.Children[ChildIndex]);
				}
			}
			SetSequentialExecutionConnections(Sequence, true);
		}
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
	Entry.EditableClause = FunctionTile.BodyClause;
	Entry.ClauseItemIndex = INDEX_NONE;
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
	for (int32 ItemIndex = 0; ItemIndex < FunctionTile.BodyClause.Items.Num(); ++ItemIndex)
	{
		const FVerseVisualClauseItemDescriptor& Item = FunctionTile.BodyClause.Items[ItemIndex];
		FVerseVisualTile Expression = MakeExpressionTile(
			Item.Expression,
			Snapshot,
			true,
			Item.bIsFinalValuePosition && bHasReturnValue);
		Expression.ExtraBlankLineCount = Item.ExtraBlankLineCount;
		Expression.EditableClause = FunctionTile.BodyClause;
		Expression.ClauseItemIndex = ItemIndex;
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

	for (FVerseVisualTile& Tile : GraphTiles)
	{
		SetNestedExecutionConnections(Tile);
	}
	TArray<FVerseVisualTile*> RootSequence;
	RootSequence.Reserve(GraphTiles.Num());
	for (FVerseVisualTile& Tile : GraphTiles)
	{
		if (Tile.bHasExecutionInput || Tile.bHasExecutionOutput)
		{
			RootSequence.Add(&Tile);
		}
	}
	SetSequentialExecutionConnections(RootSequence, false);

	if (FunctionTile.BodyClause.Items.IsEmpty() || !bHasReturnValue)
	{
		return GraphTiles;
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
	if (bHasReturnValue)
	{
		FVerseVisualSocket& Socket = Return.ValueInputs.AddDefaulted_GetRef();
		Socket.TypeRange = FunctionTile.TypeRange;
		Socket.bConnected = FunctionTile.BodyClause.Items.ContainsByPredicate(
			[](const FVerseVisualClauseItemDescriptor& Item)
			{
				return Item.bIsFinalValuePosition;
			});
	}
	return GraphTiles;
}
