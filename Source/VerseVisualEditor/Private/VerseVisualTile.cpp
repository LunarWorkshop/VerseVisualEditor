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

	FVerseVisualTile MakeExpressionTile(
		const FVerseVisualExpressionDescriptor& Descriptor,
		const FVerseParseSnapshot& Snapshot,
		bool bStatementLevel,
		bool bImplicitReturnValue,
		bool bValueConsumed = false)
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
		Tile.bStatementLevel = bStatementLevel;
		Tile.bValueConsumed = bValueConsumed;
		Tile.bProducesValue = Descriptor.Kind == EVerseExpressionKind::Identifier
			|| Descriptor.Kind == EVerseExpressionKind::Literal
			|| IsVerseOperatorExpression(Descriptor.Kind)
			|| Descriptor.Kind == EVerseExpressionKind::Call;
		if (Tile.bProducesValue)
		{
			Tile.bProducesValue = Descriptor.IntrinsicTypeName != TEXT("void")
				&& (!Descriptor.TypeRange.IsSet()
					|| !Snapshot.GetDocument()->DecodeOriginalRange(Descriptor.TypeRange)
						.TrimStartAndEnd().Equals(TEXT("void"), ESearchCase::IgnoreCase));
		}
		if (bStatementLevel)
		{
			Tile.FirstSourceLine = Snapshot.GetDocument()->GetOriginalLineNumber(
				Descriptor.Range.BeginByte);
			Tile.LastSourceLine = Snapshot.GetDocument()->GetOriginalLineNumber(
				FMath::Max(Descriptor.Range.BeginByte, Descriptor.Range.EndByte() - 1));
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
				false,
				true));
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
				FailablePredicate.BodyClause.bRequiresFailablePlaceholder = true;
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
		if (Descriptor.Kind == EVerseExpressionKind::UnaryOperator
			&& Descriptor.OperatorSpelling == TEXT("?"))
		{
			Tile.Outcome = EVerseExpressionOutcome::FailableValue;
		}
		return Tile;
	}
}

const FVerseVisualSocket* FVerseVisualSocketTopology::Find(FVerseVisualSocketId Id) const
{
	const TArray<FVerseVisualSocket>* Collections[] = {
		&ValueInputs, &ValueOutputs, &OtherInputs, &OtherOutputs};
	for (const TArray<FVerseVisualSocket>* Collection : Collections)
	{
		if (const FVerseVisualSocket* Socket = Collection->FindByPredicate(
			[Id](const FVerseVisualSocket& Candidate) { return Candidate.Id == Id; }))
		{
			return Socket;
		}
	}
	return nullptr;
}

class FVerseVisualTopologyBuilder
{
public:
	static void Build(TArray<FVerseVisualTile>& GraphTiles)
	{
		int32 NextTileId = 0;
		for (FVerseVisualTile& Tile : GraphTiles)
		{
			AssignIds(Tile, NextTileId);
		}
		for (FVerseVisualTile& Tile : GraphTiles)
		{
			BuildTile(Tile, false, false);
		}
	}

private:
	static FVerseVisualClauseDescriptor MakeClause(
		const FVerseVisualExpressionDescriptor::FControlRegion& Region)
	{
		FVerseVisualClauseDescriptor Clause;
		Clause.InteriorRange = Region.InteriorRange;
		Clause.OpeningPunctuationRange = Region.OpeningPunctuationRange;
		Clause.ClosingPunctuationRange = Region.ClosingPunctuationRange;
		Clause.PunctuationStyle = Region.PunctuationStyle;
		Clause.EmptyBodyInsertionAnchor = Region.EmptyBodyInsertionAnchor;
		for (const auto& Item : Region.Items)
		{
			FVerseVisualClauseItemDescriptor& ClauseItem =
				Clause.Items.AddDefaulted_GetRef();
			ClauseItem.Expression.Range = Item.ExpressionRange;
			ClauseItem.LeadingTriviaRange = Item.LeadingTriviaRange;
			ClauseItem.TrailingTriviaRange = Item.TrailingTriviaRange;
			ClauseItem.Separator = Item.Separator;
		}
		return Clause;
	}

	static void AddInsertionTarget(
		FVerseVisualTile& Tile,
		FVerseVisualSocketId Socket,
		const FVerseVisualClauseDescriptor& Clause,
		int32 InsertIndex)
	{
		FVerseVisualSocketInsertionTarget& Target =
			Tile.SocketInsertionTargets.AddDefaulted_GetRef();
		Target.Socket = Socket;
		Target.Clause = Clause;
		Target.InsertIndex = InsertIndex;
	}

	static void AddMissingElseTarget(
		FVerseVisualTile& Tile,
		FVerseVisualSocketId Socket,
		const FVerseVisualClauseDescriptor& BodyClause)
	{
		FVerseVisualSocketInsertionTarget& Target =
			Tile.SocketInsertionTargets.AddDefaulted_GetRef();
		Target.Socket = Socket;
		Target.Kind = EVerseVisualSocketInsertionKind::MissingElseClause;
		Target.Clause = BodyClause;
		Target.OwnerExpressionRange = Tile.Range;
		Target.InsertIndex = 0;
	}

	static FVerseVisualSocket MakeSocket(
		EVerseVisualSocketDirection Direction,
		EVerseVisualSocketRole Role,
		int32 Index,
		FVerseTextRange TypeRange = {},
		FName IntrinsicTypeName = NAME_None,
		FVerseTextRange NameRange = {})
	{
		FVerseVisualSocket Socket;
		Socket.Id = {Direction, Role, Index};
		Socket.NameRange = NameRange;
		Socket.TypeRange = TypeRange;
		Socket.IntrinsicTypeName = IntrinsicTypeName;
		return Socket;
	}

	static FVerseVisualSocket& AddValueInput(
		FVerseVisualTile& Tile,
		FVerseTextRange TypeRange,
		FName IntrinsicTypeName,
		FVerseTextRange NameRange = {})
	{
		const int32 Index = Tile.SocketTopology.ValueInputs.Num();
		return Tile.SocketTopology.ValueInputs.Add_GetRef(MakeSocket(
			EVerseVisualSocketDirection::Input,
			EVerseVisualSocketRole::Value,
			Index,
			TypeRange,
			IntrinsicTypeName,
			NameRange));
	}

	static FVerseVisualSocket& AddValueOutput(
		FVerseVisualTile& Tile,
		FVerseTextRange TypeRange,
		FName IntrinsicTypeName,
		EVerseVisualSocketRole Role = EVerseVisualSocketRole::Value,
		FVerseTextRange NameRange = {})
	{
		int32 Index = 0;
		for (const FVerseVisualSocket& Existing : Tile.SocketTopology.ValueOutputs)
		{
			Index += Existing.Id.Role == Role ? 1 : 0;
		}
		FVerseVisualSocket Socket = MakeSocket(
			EVerseVisualSocketDirection::Output,
			Role,
			Index,
			TypeRange,
			IntrinsicTypeName,
			NameRange);
		Socket.Outcome = Tile.Outcome;
		return Tile.SocketTopology.ValueOutputs.Add_GetRef(MoveTemp(Socket));
	}

	static void AddOther(
		FVerseVisualTile& Tile,
		EVerseVisualSocketDirection Direction,
		EVerseVisualSocketRole Role,
		int32 Index)
	{
		TArray<FVerseVisualSocket>& Collection = Direction == EVerseVisualSocketDirection::Input
			? Tile.SocketTopology.OtherInputs
			: Tile.SocketTopology.OtherOutputs;
		Collection.Add(MakeSocket(Direction, Role, Index));
	}

	static void AssignIds(FVerseVisualTile& Tile, int32& NextTileId)
	{
		Tile.Id.Value = NextTileId++;
		for (FVerseVisualTile& Child : Tile.Children)
		{
			AssignIds(Child, NextTileId);
		}
	}

	static void BuildTile(
		FVerseVisualTile& Tile,
		bool bInsideFailableBlock,
		bool bInlineLiteral)
	{
		Tile.SocketTopology.ValueInputs.Reset();
		Tile.SocketTopology.ValueOutputs.Reset();
		Tile.SocketTopology.OtherInputs.Reset();
		Tile.SocketTopology.OtherOutputs.Reset();
		Tile.SocketInsertionTargets.Reset();
		for (FVerseVisualTile& Child : Tile.Children)
		{
			const bool bChildIsInlineLiteral =
				(IsVerseOperatorExpression(Tile.ExpressionKind)
					|| Tile.ExpressionKind == EVerseExpressionKind::Call
					|| Tile.ExpressionKind == EVerseExpressionKind::Definition)
				&& Child.LiteralKind != EVerseLiteralKind::None;
			BuildTile(
				Child,
				Tile.Kind == EVerseVisualTileKind::FailableBlock,
				bChildIsInlineLiteral);
		}

		if (Tile.Kind == EVerseVisualTileKind::FunctionEntry)
		{
			const FVerseVisualSocketId ExecutionOutput{
				EVerseVisualSocketDirection::Output,
				EVerseVisualSocketRole::Execution, 0};
			AddOther(Tile, ExecutionOutput.Direction, ExecutionOutput.Role, ExecutionOutput.Index);
			if (Tile.EditableClause.IsSet())
			{
				AddInsertionTarget(Tile, ExecutionOutput, Tile.EditableClause.GetValue(), 0);
			}
			for (int32 Index = 0; Index < Tile.FunctionParameters.Num(); ++Index)
			{
				const FVerseVisualFunctionParameter& Parameter = Tile.FunctionParameters[Index];
				AddValueOutput(Tile, Parameter.TypeRange, NAME_None,
					EVerseVisualSocketRole::Value, Parameter.NameRange);
				if (Tile.EditableClause.IsSet())
				{
					AddInsertionTarget(
						Tile,
						{EVerseVisualSocketDirection::Output,
							EVerseVisualSocketRole::Value, Index},
						Tile.EditableClause.GetValue(),
						0);
				}
			}
			return;
		}
		if (Tile.Kind == EVerseVisualTileKind::FunctionReturn)
		{
			if (Tile.TypeRange.IsSet())
			{
				AddValueInput(Tile, Tile.TypeRange, Tile.IntrinsicTypeName);
			}
			return;
		}
		if (Tile.Kind == EVerseVisualTileKind::FailableBlock)
		{
			const FVerseVisualSocketId ClauseInsertion{
				EVerseVisualSocketDirection::Output,
				EVerseVisualSocketRole::ClauseInsertion, 0};
			AddOther(Tile, ClauseInsertion.Direction, ClauseInsertion.Role, ClauseInsertion.Index);
			AddInsertionTarget(Tile, ClauseInsertion, Tile.BodyClause, 0);
			AddOther(Tile, EVerseVisualSocketDirection::Output,
				EVerseVisualSocketRole::FailureContext, 0);
			if (Tile.bProducesValue)
			{
				FVerseVisualSocket& Result = AddValueOutput(
					Tile, Tile.TypeRange, Tile.IntrinsicTypeName);
				Result.SemanticTypeName = Tile.SemanticTypeName;
				Result.SemanticType = Tile.SemanticType;
				Result.SemanticSnapshot = Tile.SemanticSnapshot;
			}
			for (const FVerseVisualTile& Child : Tile.Children)
			{
				if (Child.Kind != EVerseVisualTileKind::Definition
					|| Child.SemanticDataDefinition == nullptr)
				{
					continue;
				}
				FVerseVisualSocket& Binding = AddValueOutput(
					Tile, Child.TypeRange, NAME_None,
					EVerseVisualSocketRole::BoundaryBinding, Child.NameRange);
				Binding.SemanticTypeName = Child.SemanticTypeName;
				Binding.SemanticType = Child.SemanticType;
				Binding.SemanticDataDefinition = Child.SemanticDataDefinition;
				Binding.LegalConsumerScopes = Child.LegalConsumerScopes;
				Binding.SemanticSnapshot = Child.SemanticSnapshot;
				AddInsertionTarget(
					Tile,
					Binding.Id,
					Tile.BodyClause,
					Child.ClauseItemIndex == INDEX_NONE
						? Tile.BodyClause.Items.Num()
						: Child.ClauseItemIndex + 1);
			}
			return;
		}

		if (Tile.bStatementLevel)
		{
			AddOther(Tile, EVerseVisualSocketDirection::Input,
				EVerseVisualSocketRole::Execution, 0);
			const int32 OutputCount = Tile.ExpressionKind == EVerseExpressionKind::Control
				&& Tile.ControlKind == EVerseControlKind::If ? 3 : 1;
			for (int32 Index = 0; Index < OutputCount; ++Index)
			{
				AddOther(Tile, EVerseVisualSocketDirection::Output,
					EVerseVisualSocketRole::Execution, Index);
			}
			if (Tile.EditableClause.IsSet())
			{
				AddInsertionTarget(
					Tile,
					{EVerseVisualSocketDirection::Output,
						EVerseVisualSocketRole::Execution, 0},
					Tile.EditableClause.GetValue(),
					Tile.ClauseItemIndex == INDEX_NONE ? 0 : Tile.ClauseItemIndex + 1);
			}
		}
		if (Tile.ExpressionKind == EVerseExpressionKind::Control
			&& Tile.ControlKind == EVerseControlKind::If)
		{
			AddOther(Tile, EVerseVisualSocketDirection::Input,
				EVerseVisualSocketRole::FailureContext, 0);
			const auto* Body = Tile.ControlRegions.FindByPredicate(
				[](const auto& Region)
				{
					return Region.Kind == EVerseControlRegionKind::Body;
				});
			if (Body != nullptr)
			{
				AddInsertionTarget(
					Tile,
					{EVerseVisualSocketDirection::Output,
						EVerseVisualSocketRole::Execution, 1},
					MakeClause(*Body),
					0);
			}
			const auto* Else = Tile.ControlRegions.FindByPredicate(
				[](const auto& Region)
				{
					return Region.Kind == EVerseControlRegionKind::Else;
				});
			if (Else != nullptr)
			{
				AddInsertionTarget(
					Tile,
					{EVerseVisualSocketDirection::Output,
						EVerseVisualSocketRole::Execution, 2},
					MakeClause(*Else),
					0);
			}
			else
			{
				if (Body != nullptr)
				{
					AddMissingElseTarget(
						Tile,
						{EVerseVisualSocketDirection::Output,
							EVerseVisualSocketRole::Execution, 2},
						MakeClause(*Body));
				}
			}
		}

		if (IsVerseOperatorExpression(Tile.ExpressionKind)
			|| Tile.ExpressionKind == EVerseExpressionKind::Call)
		{
			const int32 InputCount = FMath::Max(
				Tile.Children.Num(), Tile.SemanticInputTypeNames.Num());
			for (int32 Index = 0; Index < InputCount; ++Index)
			{
				const FVerseVisualTile* Child = Tile.Children.IsValidIndex(Index)
					? &Tile.Children[Index] : nullptr;
				FVerseVisualSocket& Input = AddValueInput(
					Tile,
					Child && Child->TypeRange.IsSet() ? Child->TypeRange : Tile.TypeRange,
					Child && !Child->IntrinsicTypeName.IsNone()
						? Child->IntrinsicTypeName : Tile.IntrinsicTypeName);
				if (Tile.SemanticInputNames.IsValidIndex(Index))
				{
					Input.SemanticName = Tile.SemanticInputNames[Index];
				}
				// The resolved callee signature retains abstract constraints such as
				// `comparable` even after an invocation has concrete operands. A
				// connected expression's compiler-resolved result is the effective
				// socket type used for color and interaction; the formal constraint
				// remains available on the owning tile's SemanticInput* arrays.
				if (Child != nullptr && !Child->SemanticTypeName.IsEmpty())
				{
					Input.SemanticTypeName = Child->SemanticTypeName;
					Input.SemanticType = Child->SemanticType;
					Input.SemanticSnapshot = Child->SemanticSnapshot;
				}
				else if (Tile.SemanticInputTypeNames.IsValidIndex(Index))
				{
					Input.SemanticTypeName = Tile.SemanticInputTypeNames[Index];
					if (Tile.SemanticInputTypes.IsValidIndex(Index))
					{
						Input.SemanticType = Tile.SemanticInputTypes[Index];
						Input.SemanticSnapshot = Tile.SemanticSnapshot;
					}
				}
				Input.bNamedParameter = Tile.SemanticInputNamed.IsValidIndex(Index)
					&& Tile.SemanticInputNamed[Index];
				Input.bUsesDeclaredDefault = !Tile.Children.IsValidIndex(Index)
					&& Tile.SemanticInputHasDefault.IsValidIndex(Index)
					&& Tile.SemanticInputHasDefault[Index];
				if (Child && Child->LiteralKind != EVerseLiteralKind::None)
				{
					Input.InlineLiteralRange = Child->Range;
					Input.InlineLiteralKind = Child->LiteralKind;
				}
			}
		}
		else if (Tile.ExpressionKind == EVerseExpressionKind::Definition
			&& Tile.Children.Num() == 1)
		{
			const FVerseVisualTile& Initializer = Tile.Children[0];
			FVerseVisualSocket& Input = AddValueInput(
				Tile, Tile.TypeRange, Initializer.IntrinsicTypeName);
			Input.SemanticTypeName = Tile.SemanticTypeName;
			Input.SemanticType = Tile.SemanticType;
			Input.SemanticSnapshot = Tile.SemanticSnapshot;
			if (Initializer.LiteralKind != EVerseLiteralKind::None)
			{
				Input.InlineLiteralRange = Initializer.Range;
				Input.InlineLiteralKind = Initializer.LiteralKind;
			}
		}
		else if (Tile.ExpressionKind == EVerseExpressionKind::Identifier
			&& Tile.bStatementLevel)
		{
			FVerseVisualSocket& Input =
				AddValueInput(Tile, Tile.TypeRange, Tile.IntrinsicTypeName);
			Input.SemanticTypeName = Tile.SemanticTypeName;
			Input.SemanticType = Tile.SemanticType;
			Input.SemanticSnapshot = Tile.SemanticSnapshot;
		}

		const bool bNeedsOutput = (Tile.bProducesValue
			&& (Tile.bValueConsumed || Tile.bStatementLevel))
			|| Tile.Outcome == EVerseExpressionOutcome::FailureOnly
			|| (Tile.Kind == EVerseVisualTileKind::Definition && bInsideFailableBlock);
		if (bNeedsOutput && !bInlineLiteral)
		{
			const EVerseVisualSocketRole Role = Tile.Kind == EVerseVisualTileKind::Definition
				? EVerseVisualSocketRole::BoundaryBinding
				: EVerseVisualSocketRole::Value;
			FVerseVisualSocket& Output = AddValueOutput(
				Tile, Tile.TypeRange, Tile.IntrinsicTypeName, Role, Tile.NameRange);
			Output.SemanticTypeName = Tile.SemanticTypeName;
			Output.SemanticType = Tile.SemanticType;
			Output.SemanticDataDefinition = Tile.SemanticDataDefinition;
			Output.LegalConsumerScopes = Tile.LegalConsumerScopes;
			Output.SemanticSnapshot = Tile.SemanticSnapshot;
			if (Tile.Kind == EVerseVisualTileKind::Definition
				&& Tile.EditableClause.IsSet())
			{
				AddInsertionTarget(
					Tile,
					Output.Id,
					Tile.EditableClause.GetValue(),
					Tile.ClauseItemIndex == INDEX_NONE
						? Tile.EditableClause->Items.Num()
						: Tile.ClauseItemIndex + 1);
			}
		}
	}
};

TArray<FVerseVisualTile> FVerseVisualTileBuilder::Build(
	const FVerseParseSnapshot& Snapshot,
	FVerseDocumentRevision Revision)
{
	TArray<FVerseVisualTile> Tiles = BuildTiles(Snapshot, Snapshot.GetSourceRegions(), Revision);
	FinalizeSocketTopology(Tiles);
	return Tiles;
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
	Entry.FunctionParameters = FunctionTile.FunctionParameters;
	Entry.EditableClause = FunctionTile.BodyClause;
	Entry.ClauseItemIndex = INDEX_NONE;

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
		}
		GraphTiles.Add(MoveTemp(Expression));
	}

	if (FunctionTile.BodyClause.Items.IsEmpty() || !bHasReturnValue)
	{
		FinalizeSocketTopology(GraphTiles);
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
	FinalizeSocketTopology(GraphTiles);
	return GraphTiles;
}

void FVerseVisualTileBuilder::FinalizeSocketTopology(TArray<FVerseVisualTile>& GraphTiles)
{
	FVerseVisualTopologyBuilder::Build(GraphTiles);
}

namespace
{
	void AddVisualConnection(
		TArray<FVerseVisualConnection>& Connections,
		const FVerseVisualTile& SourceTile,
		FVerseVisualSocketId SourceSocket,
		const FVerseVisualTile& TargetTile,
		FVerseVisualSocketId TargetSocket,
		EVerseVisualConnectionAxis Axis,
		EVerseExpressionOutcome Outcome = EVerseExpressionOutcome::Unresolved,
		int32 ExtraBlankLines = 0,
		FVerseGraphRenderScopeId RenderScope = FVerseGraphRenderScopeId::Root())
	{
		if (!ensureMsgf(SourceSocket.Direction == EVerseVisualSocketDirection::Output
				&& TargetSocket.Direction == EVerseVisualSocketDirection::Input,
			TEXT("Verse graph connections must run from an output socket to an input socket."))
			|| !ensureMsgf(SourceTile.FindSocket(SourceSocket) != nullptr,
				TEXT("Verse graph connection references a missing source socket."))
			|| !ensureMsgf(TargetTile.FindSocket(TargetSocket) != nullptr,
				TEXT("Verse graph connection references a missing target socket.")))
		{
			return;
		}
		Connections.Add({
			{SourceTile.Id, SourceSocket},
			{TargetTile.Id, TargetSocket},
			Axis,
			Outcome,
			ExtraBlankLines,
			RenderScope});
	}

	const FVerseVisualSocket* FirstValueOutput(const FVerseVisualTile& Tile)
	{
		return Tile.GetValueOutputs().IsEmpty() ? nullptr : &Tile.GetValueOutputs()[0];
	}

	void AddSequentialConnections(
		TArray<FVerseVisualConnection>& Connections,
		TConstArrayView<const FVerseVisualTile*> Sequence,
		const FVerseVisualTile* InitialSource,
		FVerseVisualSocketId InitialSocket,
		FVerseGraphRenderScopeId RenderScope = FVerseGraphRenderScopeId::Root())
	{
		const FVerseVisualTile* Previous = InitialSource;
		FVerseVisualSocketId PreviousSocket = InitialSocket;
		for (const FVerseVisualTile* Current : Sequence)
		{
			if (Previous != nullptr)
			{
				AddVisualConnection(
					Connections,
					*Previous,
					PreviousSocket,
					*Current,
					{EVerseVisualSocketDirection::Input,
						EVerseVisualSocketRole::Execution, 0},
					EVerseVisualConnectionAxis::Vertical,
					EVerseExpressionOutcome::Unresolved,
					Previous == InitialSource ? 0 : Previous->ExtraBlankLineCount,
					RenderScope);
			}
			Previous = Current;
			PreviousSocket = {EVerseVisualSocketDirection::Output,
				EVerseVisualSocketRole::Execution, 0};
		}
	}

	void BuildNestedConnections(
		const FVerseVisualTile& Tile,
		TArray<FVerseVisualConnection>& Connections,
		FVerseGraphRenderScopeId ParentScope)
	{
		const FVerseGraphRenderScopeId ContentScope =
			Tile.Kind == EVerseVisualTileKind::FailableBlock
				? FVerseGraphRenderScopeId::ForTile(Tile.Id)
				: ParentScope;
		for (const FVerseVisualTile& Child : Tile.Children)
		{
			BuildNestedConnections(Child, Connections, ContentScope);
		}

		if (IsVerseOperatorExpression(Tile.ExpressionKind)
			|| Tile.ExpressionKind == EVerseExpressionKind::Call
			|| Tile.ExpressionKind == EVerseExpressionKind::Definition)
		{
			for (int32 Index = 0; Index < Tile.Children.Num(); ++Index)
			{
				const FVerseVisualTile& Child = Tile.Children[Index];
				const FVerseVisualSocket* Output = FirstValueOutput(Child);
				const FVerseVisualSocketId InputId{
					EVerseVisualSocketDirection::Input,
					EVerseVisualSocketRole::Value,
					Index};
				if (Child.LiteralKind == EVerseLiteralKind::None
					&& Output != nullptr && Tile.FindSocket(InputId) != nullptr)
				{
					AddVisualConnection(
						Connections, Child, Output->Id, Tile, InputId,
						EVerseVisualConnectionAxis::Horizontal, Output->Outcome,
						0, ContentScope);
				}
			}
		}

		if (Tile.Kind == EVerseVisualTileKind::FailableBlock)
		{
			TArray<const FVerseVisualTile*> Sequence;
			for (const FVerseVisualTile& Child : Tile.Children)
			{
				if (Child.FindSocket({EVerseVisualSocketDirection::Input,
					EVerseVisualSocketRole::Execution, 0}))
				{
					Sequence.Add(&Child);
				}
			}
			AddSequentialConnections(
				Connections,
				Sequence,
				&Tile,
				{EVerseVisualSocketDirection::Output,
					EVerseVisualSocketRole::ClauseInsertion, 0},
				ContentScope);
			return;
		}

		if (Tile.ExpressionKind == EVerseExpressionKind::Control
			&& Tile.ControlKind == EVerseControlKind::If)
		{
			const FVerseVisualExpressionDescriptor::FControlRegion* Condition =
				Tile.ControlRegions.FindByPredicate(
					[](const auto& Region)
					{
						return Region.Kind == EVerseControlRegionKind::Condition
							&& Region.OperandCount > 0;
					});
			if (Condition && Tile.Children.IsValidIndex(Condition->FirstOperandIndex))
			{
				const FVerseVisualTile& Predicate = Tile.Children[Condition->FirstOperandIndex];
				AddVisualConnection(
					Connections,
					Predicate,
					{EVerseVisualSocketDirection::Output,
						EVerseVisualSocketRole::FailureContext, 0},
					Tile,
					{EVerseVisualSocketDirection::Input,
						EVerseVisualSocketRole::FailureContext, 0},
					EVerseVisualConnectionAxis::Horizontal,
					EVerseExpressionOutcome::FailureOnly,
					0,
					ParentScope);
			}
			for (const auto& Region : Tile.ControlRegions)
			{
				const int32 OutputIndex = Region.Kind == EVerseControlRegionKind::Body ? 1
					: Region.Kind == EVerseControlRegionKind::Else ? 2 : INDEX_NONE;
				if (OutputIndex == INDEX_NONE)
				{
					continue;
				}
				TArray<const FVerseVisualTile*> Sequence;
				for (int32 Offset = 0; Offset < Region.OperandCount; ++Offset)
				{
					const int32 ChildIndex = Region.FirstOperandIndex + Offset;
					if (Tile.Children.IsValidIndex(ChildIndex))
					{
						Sequence.Add(&Tile.Children[ChildIndex]);
					}
				}
				AddSequentialConnections(
					Connections,
					Sequence,
					&Tile,
					{EVerseVisualSocketDirection::Output,
						EVerseVisualSocketRole::Execution, OutputIndex},
					ParentScope);
			}
		}
	}

	void GatherRenderScopes(
		const FVerseVisualTile& Tile,
		FVerseGraphRenderScopeId ParentScope,
		TArray<FVerseGraphRenderScope>& Scopes)
	{
		FVerseGraphRenderScopeId ContentScope = ParentScope;
		if (Tile.Kind == EVerseVisualTileKind::FailableBlock)
		{
			ContentScope = FVerseGraphRenderScopeId::ForTile(Tile.Id);
			Scopes.Add({
				ContentScope,
				ParentScope,
				Tile.Id,
				EVerseGraphRenderScopeBackground::Failable,
				true});
		}
		for (const FVerseVisualTile& Child : Tile.Children)
		{
			GatherRenderScopes(Child, ContentScope, Scopes);
		}
	}
}

TArray<FVerseGraphRenderScope> FVerseVisualTileBuilder::BuildRenderScopes(
	TConstArrayView<FVerseVisualTile> GraphTiles)
{
	TArray<FVerseGraphRenderScope> Scopes;
	Scopes.Add({
		FVerseGraphRenderScopeId::Root(),
		{},
		{},
		EVerseGraphRenderScopeBackground::Root,
		false});
	for (const FVerseVisualTile& Tile : GraphTiles)
	{
		GatherRenderScopes(Tile, FVerseGraphRenderScopeId::Root(), Scopes);
	}
	return Scopes;
}

TArray<FVerseVisualConnection> FVerseVisualTileBuilder::BuildConnections(
	TConstArrayView<FVerseVisualTile> GraphTiles)
{
	TArray<FVerseVisualConnection> Connections;
	for (const FVerseVisualTile& Tile : GraphTiles)
	{
		BuildNestedConnections(Tile, Connections, FVerseGraphRenderScopeId::Root());
	}

	TArray<const FVerseVisualTile*> RootStatements;
	const FVerseVisualTile* Entry = nullptr;
	for (const FVerseVisualTile& Tile : GraphTiles)
	{
		if (Tile.Kind == EVerseVisualTileKind::FunctionEntry)
		{
			Entry = &Tile;
		}
		else if (Tile.FindSocket({EVerseVisualSocketDirection::Input,
			EVerseVisualSocketRole::Execution, 0}))
		{
			RootStatements.Add(&Tile);
		}
	}
	if (Entry != nullptr)
	{
		AddSequentialConnections(
			Connections,
			RootStatements,
			Entry,
			{EVerseVisualSocketDirection::Output,
				EVerseVisualSocketRole::Execution, 0});
	}

	for (int32 Index = 0; Index + 1 < GraphTiles.Num(); ++Index)
	{
		const FVerseVisualTile& Source = GraphTiles[Index];
		const FVerseVisualTile& Target = GraphTiles[Index + 1];
		if (Source.bImplicitReturnValue && Target.Kind == EVerseVisualTileKind::FunctionReturn)
		{
			const FVerseVisualSocket* Output = FirstValueOutput(Source);
			const FVerseVisualSocketId InputId{
				EVerseVisualSocketDirection::Input, EVerseVisualSocketRole::Value, 0};
			if (Output && Target.FindSocket(InputId))
			{
				AddVisualConnection(
					Connections, Source, Output->Id, Target, InputId,
					EVerseVisualConnectionAxis::Horizontal, Output->Outcome);
			}
		}
	}
	FString Diagnostic;
	ensureMsgf(ValidateConnections(GraphTiles, Connections, &Diagnostic),
		TEXT("Invalid immutable Verse graph topology: %s"), *Diagnostic);
	const TArray<FVerseGraphRenderScope> RenderScopes = BuildRenderScopes(GraphTiles);
	ensureMsgf(ValidateRenderScopes(GraphTiles, RenderScopes, Connections, &Diagnostic),
		TEXT("Invalid Verse graph render scopes: %s"), *Diagnostic);
	return Connections;
}

bool FVerseVisualTileBuilder::ValidateRenderScopes(
	TConstArrayView<FVerseVisualTile> GraphTiles,
	TConstArrayView<FVerseGraphRenderScope> Scopes,
	TConstArrayView<FVerseVisualConnection> Connections,
	FString* OutDiagnostic)
{
	auto Fail = [OutDiagnostic](FString Diagnostic)
	{
		if (OutDiagnostic != nullptr)
		{
			*OutDiagnostic = MoveTemp(Diagnostic);
		}
		return false;
	};

	TMap<FVerseGraphRenderScopeId, const FVerseGraphRenderScope*> ScopesById;
	for (const FVerseGraphRenderScope& Scope : Scopes)
	{
		if (!Scope.Id.IsValid() || ScopesById.Contains(Scope.Id))
		{
			return Fail(TEXT("A render scope has a duplicate or invalid id."));
		}
		ScopesById.Add(Scope.Id, &Scope);
	}
	const FVerseGraphRenderScope* const* Root =
		ScopesById.Find(FVerseGraphRenderScopeId::Root());
	if (Root == nullptr || (*Root)->Parent.IsValid() || (*Root)->OwnerTile.IsValid())
	{
		return Fail(TEXT("The render-scope tree has no valid root."));
	}
	for (const FVerseGraphRenderScope& Scope : Scopes)
	{
		if (!(Scope.Id == FVerseGraphRenderScopeId::Root())
			&& !ScopesById.Contains(Scope.Parent))
		{
			return Fail(TEXT("A render scope references a missing parent."));
		}
		TSet<FVerseGraphRenderScopeId> Ancestors;
		const FVerseGraphRenderScope* Cursor = &Scope;
		while (Cursor != nullptr && Cursor->Parent.IsValid())
		{
			if (Ancestors.Contains(Cursor->Id))
			{
				return Fail(TEXT("The render-scope tree contains a cycle."));
			}
			Ancestors.Add(Cursor->Id);
			const FVerseGraphRenderScope* const* Parent = ScopesById.Find(Cursor->Parent);
			Cursor = Parent != nullptr ? *Parent : nullptr;
		}
	}

	TMap<FVerseVisualTileId, FVerseGraphRenderScopeId> TileScopes;
	TMap<FVerseVisualTileId, const FVerseVisualTile*> TilesById;
	TFunction<void(const FVerseVisualTile&, FVerseGraphRenderScopeId)> RegisterTiles =
		[&](const FVerseVisualTile& Tile, FVerseGraphRenderScopeId ContainingScope)
		{
			TileScopes.Add(Tile.Id, ContainingScope);
			TilesById.Add(Tile.Id, &Tile);
			const FVerseGraphRenderScopeId ContentScope =
				Tile.Kind == EVerseVisualTileKind::FailableBlock
					? FVerseGraphRenderScopeId::ForTile(Tile.Id)
					: ContainingScope;
			for (const FVerseVisualTile& Child : Tile.Children)
			{
				RegisterTiles(Child, ContentScope);
			}
		};
	for (const FVerseVisualTile& Tile : GraphTiles)
	{
		RegisterTiles(Tile, FVerseGraphRenderScopeId::Root());
	}

	auto EndpointScope = [&](const FVerseVisualSocketEndpoint& Endpoint)
	{
		const FVerseVisualTile* const* Tile = TilesById.Find(Endpoint.Tile);
		const FVerseGraphRenderScopeId* ContainingScope = TileScopes.Find(Endpoint.Tile);
		if (Tile != nullptr && ContainingScope != nullptr
			&& (*Tile)->Kind == EVerseVisualTileKind::FailableBlock
			&& Endpoint.Socket.Role == EVerseVisualSocketRole::ClauseInsertion)
		{
			return FVerseGraphRenderScopeId::ForTile(Endpoint.Tile);
		}
		return ContainingScope != nullptr ? *ContainingScope : FVerseGraphRenderScopeId{};
	};
	auto NearestCommonScope = [&](FVerseGraphRenderScopeId Left, FVerseGraphRenderScopeId Right)
	{
		TSet<FVerseGraphRenderScopeId> LeftAncestors;
		for (FVerseGraphRenderScopeId Cursor = Left; Cursor.IsValid();)
		{
			LeftAncestors.Add(Cursor);
			const FVerseGraphRenderScope* const* Scope = ScopesById.Find(Cursor);
			Cursor = Scope != nullptr ? (*Scope)->Parent : FVerseGraphRenderScopeId{};
		}
		for (FVerseGraphRenderScopeId Cursor = Right; Cursor.IsValid();)
		{
			if (LeftAncestors.Contains(Cursor))
			{
				return Cursor;
			}
			const FVerseGraphRenderScope* const* Scope = ScopesById.Find(Cursor);
			Cursor = Scope != nullptr ? (*Scope)->Parent : FVerseGraphRenderScopeId{};
		}
		return FVerseGraphRenderScopeId{};
	};

	for (const FVerseVisualConnection& Connection : Connections)
	{
		if (!ScopesById.Contains(Connection.RenderScope))
		{
			return Fail(TEXT("A connection references a missing render scope."));
		}
		const FVerseGraphRenderScopeId Expected = NearestCommonScope(
			EndpointScope(Connection.Source), EndpointScope(Connection.Target));
		if (!(Expected == Connection.RenderScope))
		{
			return Fail(FString::Printf(
				TEXT("Connection scope %d is not the endpoints' nearest common scope %d."),
				Connection.RenderScope.Value, Expected.Value));
		}
	}
	if (OutDiagnostic != nullptr)
	{
		OutDiagnostic->Reset();
	}
	return true;
}

bool FVerseVisualTileBuilder::ValidateConnections(
	TConstArrayView<FVerseVisualTile> GraphTiles,
	TConstArrayView<FVerseVisualConnection> Connections,
	FString* OutDiagnostic)
{
	auto Fail = [OutDiagnostic](FString Diagnostic)
	{
		if (OutDiagnostic != nullptr)
		{
			*OutDiagnostic = MoveTemp(Diagnostic);
		}
		return false;
	};
	TMap<FVerseVisualTileId, const FVerseVisualTile*> TilesById;
	TFunction<bool(const FVerseVisualTile&)> RegisterTile =
		[&](const FVerseVisualTile& Tile)
		{
			if (!Tile.Id.IsValid() || TilesById.Contains(Tile.Id))
			{
				return Fail(FString::Printf(
					TEXT("Duplicate or invalid tile id %d."), Tile.Id.Value));
			}
			TilesById.Add(Tile.Id, &Tile);
			TSet<FVerseVisualSocketId> SocketIds;
			const TConstArrayView<FVerseVisualSocket> Collections[] = {
				Tile.SocketTopology.GetValueInputs(),
				Tile.SocketTopology.GetValueOutputs(),
				Tile.SocketTopology.GetOtherInputs(),
				Tile.SocketTopology.GetOtherOutputs()};
			for (const TConstArrayView<FVerseVisualSocket> Collection : Collections)
			{
				for (const FVerseVisualSocket& Socket : Collection)
				{
					if (!Socket.Id.IsValid() || SocketIds.Contains(Socket.Id))
					{
						return Fail(FString::Printf(
							TEXT("Tile %d has a duplicate or invalid socket id."),
							Tile.Id.Value));
					}
					SocketIds.Add(Socket.Id);
				}
			}
			TSet<FVerseVisualSocketId> InsertionSockets;
			for (const FVerseVisualSocketInsertionTarget& Target :
				Tile.SocketInsertionTargets)
			{
				const FVerseVisualSocket* Socket = Tile.FindSocket(Target.Socket);
				if (Socket == nullptr
					|| Socket->Id.Direction != EVerseVisualSocketDirection::Output)
				{
					return Fail(FString::Printf(
						TEXT("Tile %d has an insertion target for a missing or non-output socket."),
						Tile.Id.Value));
				}
				if (InsertionSockets.Contains(Target.Socket))
				{
					return Fail(FString::Printf(
						TEXT("Tile %d has duplicate insertion targets for one socket."),
						Tile.Id.Value));
				}
				InsertionSockets.Add(Target.Socket);
			}
			for (const FVerseVisualTile& Child : Tile.Children)
			{
				if (!RegisterTile(Child))
				{
					return false;
				}
			}
			return true;
		};
	for (const FVerseVisualTile& Tile : GraphTiles)
	{
		if (!RegisterTile(Tile))
		{
			return false;
		}
	}

	TSet<FVerseVisualSocketEndpoint> OccupiedInputs;
	TSet<FVerseVisualSocketEndpoint> OccupiedSingleOutputs;
	for (const FVerseVisualConnection& Connection : Connections)
	{
		const FVerseVisualTile* const* SourceTile = TilesById.Find(Connection.Source.Tile);
		const FVerseVisualTile* const* TargetTile = TilesById.Find(Connection.Target.Tile);
		if (SourceTile == nullptr || TargetTile == nullptr)
		{
			return Fail(TEXT("A connection references a missing tile."));
		}
		const FVerseVisualSocket* SourceSocket = (*SourceTile)->FindSocket(Connection.Source.Socket);
		const FVerseVisualSocket* TargetSocket = (*TargetTile)->FindSocket(Connection.Target.Socket);
		if (SourceSocket == nullptr || TargetSocket == nullptr)
		{
			return Fail(TEXT("A connection references a missing socket."));
		}
		if (SourceSocket->Id.Direction != EVerseVisualSocketDirection::Output
			|| TargetSocket->Id.Direction != EVerseVisualSocketDirection::Input)
		{
			return Fail(TEXT("A connection has incompatible endpoint directions."));
		}
		if (OccupiedInputs.Contains(Connection.Target))
		{
			return Fail(TEXT("More than one connection targets the same input socket."));
		}
		OccupiedInputs.Add(Connection.Target);
		const bool bSingleConnectionOutput =
			SourceSocket->Id.Role == EVerseVisualSocketRole::Execution
			|| SourceSocket->Id.Role == EVerseVisualSocketRole::FailureContext
			|| SourceSocket->Id.Role == EVerseVisualSocketRole::ClauseInsertion;
		if (bSingleConnectionOutput && OccupiedSingleOutputs.Contains(Connection.Source))
		{
			return Fail(TEXT("A single-cardinality output has more than one connection."));
		}
		if (bSingleConnectionOutput)
		{
			OccupiedSingleOutputs.Add(Connection.Source);
		}
	}
	if (OutDiagnostic != nullptr)
	{
		OutDiagnostic->Reset();
	}
	return true;
}

bool FVerseVisualTileBuilder::IsSocketConnected(
	TConstArrayView<FVerseVisualConnection> Connections,
	FVerseVisualSocketEndpoint EndpointToFind)
{
	return Connections.ContainsByPredicate(
		[EndpointToFind](const FVerseVisualConnection& Connection)
		{
			return Connection.Source == EndpointToFind
				|| Connection.Target == EndpointToFind;
		});
}
