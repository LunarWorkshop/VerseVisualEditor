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
		case EVerseVisualTileKind::FailableBlock:
			return TEXT("Failable Block");
		case EVerseVisualTileKind::Expression:
			return TEXT("Expression");
		case EVerseVisualTileKind::FunctionEntry:
			return TEXT("Function Entry");
		case EVerseVisualTileKind::FunctionReturn:
			return TEXT("Function Return");
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
			const bool bEditableValueType =
				Tile.DefinitionKind == VerseSyntaxKind::Variable
				|| Tile.DefinitionKind == VerseSyntaxKind::Constant;
			Properties.Add({
				TEXT("Type"),
				Snapshot.GetDocument()->DecodeOriginalRange(Tile.TypeRange),
				bEditableValueType,
				bEditableValueType
					? EVerseTilePropertyEditKind::Type
					: EVerseTilePropertyEditKind::None,
				EVerseLiteralKind::None,
				Tile.TypeRange});
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
	else if (Tile.ExpressionKind == EVerseExpressionKind::Literal
		&& Tile.LiteralKind != EVerseLiteralKind::None)
	{
		const FString Type = !Tile.IntrinsicTypeName.IsNone()
			? Tile.IntrinsicTypeName.ToString()
			: Tile.TypeRange.IsSet()
				? Snapshot.GetDocument()->DecodeOriginalRange(Tile.TypeRange)
				: FString();
		if (!Type.IsEmpty())
		{
			Properties.Add({TEXT("Type"), Type});
		}
		Properties.Add({
			TEXT("Value"),
			Snapshot.GetDocument()->DecodeOriginalRange(Tile.Range),
			true,
			EVerseTilePropertyEditKind::Literal,
			Tile.LiteralKind,
			Tile.Range});
	}
	else if (IsVerseOperatorExpression(Tile.ExpressionKind))
	{
		TArray<FString> Inputs;
		for (const FVerseVisualSocket& Socket : Tile.GetValueInputs())
		{
			Inputs.Add(!Socket.SemanticTypeName.IsEmpty()
				? Socket.SemanticTypeName
				: Socket.IntrinsicTypeName.ToString());
		}
		const FString Result = !Tile.SemanticTypeName.IsEmpty()
			? Tile.SemanticTypeName
			: Tile.IntrinsicTypeName.ToString();
		Properties.Add({
			TEXT("Signature"),
			FString::Printf(TEXT("%s -> %s"), *FString::Join(Inputs, TEXT(" x ")), *Result),
			true,
			EVerseTilePropertyEditKind::OperatorSignature});
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
