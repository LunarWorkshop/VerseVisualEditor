#include "VisualModel/VerseTileProperties.h"

#include "VerseParseSnapshot.h"
#include "VerseParseSnapshotBuilder.h"
#include "VisualModel/VerseVisualTile.h"

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

	FString FormatSeparator(const FVerseVisualSeparatorDescriptor& Separator)
	{
		if (Separator.Token == EVerseSeparatorToken::Semicolon)
		{
			return Separator.Layout == EVerseSeparatorLayout::TokenAndNewline
				? TEXT("Semicolon + newline") : TEXT("Semicolon + space");
		}
		return TEXT("Newline");
	}

	FString FormatDelimiter(EVerseClauseDelimiter Delimiter)
	{
		switch (Delimiter)
		{
		case EVerseClauseDelimiter::Braces: return TEXT("Braces");
		case EVerseClauseDelimiter::Colon: return TEXT("Colon");
		case EVerseClauseDelimiter::BareIndentation: return TEXT("Indentation");
		case EVerseClauseDelimiter::Parentheses: return TEXT("Parentheses");
		case EVerseClauseDelimiter::Dot: return TEXT("Dot");
		default: return TEXT("None");
		}
	}

	FString DefinitionHead(const FVerseVisualTile& Tile)
	{
		if (Tile.Kind != EVerseVisualTileKind::Definition)
		{
			return TEXT("Body");
		}
		if (Tile.DefinitionKind == VerseSyntaxKind::Function) return TEXT("Function() =");
		if (Tile.DefinitionKind == VerseSyntaxKind::Module) return TEXT("Module := module");
		if (Tile.DefinitionKind == VerseSyntaxKind::Class) return TEXT("Class := class");
		if (Tile.DefinitionKind == VerseSyntaxKind::Struct) return TEXT("Struct := struct");
		if (Tile.DefinitionKind == VerseSyntaxKind::Interface) return TEXT("Interface := interface");
		if (Tile.DefinitionKind == VerseSyntaxKind::Enum) return TEXT("Enum := enum");
		return TEXT("Definition");
	}

	enum class EExampleClause : uint8
	{
		Definition,
		Condition,
		TrueBody,
		FalseBody,
	};

	EExampleClause GetExampleClause(FStringView PropertyName)
	{
		if (PropertyName.StartsWith(TEXTVIEW("Condition"))) return EExampleClause::Condition;
		if (PropertyName.StartsWith(TEXTVIEW("True Body"))) return EExampleClause::TrueBody;
		if (PropertyName.StartsWith(TEXTVIEW("False Body"))) return EExampleClause::FalseBody;
		return EExampleClause::Definition;
	}

	FString ClauseKeyword(EExampleClause Clause)
	{
		switch (Clause)
		{
		case EExampleClause::Condition: return TEXT("if");
		case EExampleClause::TrueBody: return TEXT("then");
		case EExampleClause::FalseBody: return TEXT("else");
		default: return FString();
		}
	}

	FString MultilineClauseExample(
		const FVerseVisualTile& Tile,
		EExampleClause Clause,
		EVerseClauseDelimiter Delimiter,
		FStringView Indentation = TEXTVIEW("    "))
	{
		const FString Head = Clause == EExampleClause::Definition
			? DefinitionHead(Tile) : ClauseKeyword(Clause);
		const FString First = Clause == EExampleClause::Condition
			? TEXT("FirstCondition") : TEXT("FirstExpression");
		const FString Second = Clause == EExampleClause::Condition
			? TEXT("SecondCondition") : TEXT("SecondExpression");
		const FString Opening = Delimiter == EVerseClauseDelimiter::Braces
			? TEXT(" {\n") : TEXT(":\n");
		const FString Closing = Delimiter == EVerseClauseDelimiter::Braces
			? TEXT("\n}") : FString();
		return Head + Opening + FString(Indentation) + First
			+ TEXT("\n") + FString(Indentation) + Second + Closing;
	}

	FString SyntaxExample(
		const FVerseVisualTile& Tile,
		FStringView PropertyName,
		FStringView Value,
		EVerseSyntaxControlKind Control,
		int32 RegionIndex)
	{
		const EExampleClause Clause = GetExampleClause(PropertyName);
		const EVerseClauseDelimiter ExistingDelimiter = Tile.ControlRegions.IsValidIndex(RegionIndex)
			? Tile.ControlRegions[RegionIndex].Syntax.Delimiter
			: Tile.BodyClause.Syntax.Delimiter;
		switch (Control)
		{
		case EVerseSyntaxControlKind::GroupingLayers:
			return Value == TEXTVIEW("1") ? TEXT("(Expression)") : TEXT("Expression");
		case EVerseSyntaxControlKind::StatementSeparator:
			if (Value == TEXTVIEW("Semicolon + space")) return TEXT("First; Second");
			if (Value == TEXTVIEW("Semicolon + newline")) return TEXT("First;\nSecond");
			return TEXT("First\nSecond");
		case EVerseSyntaxControlKind::BlankLinesAfter:
			return TEXT("First") + FString::ChrN(
				FMath::Clamp(FCString::Atoi(*FString(Value)) + 1, 1, 9), TEXT('\n')) + TEXT("Second");
		case EVerseSyntaxControlKind::ConditionSyntax:
			return Value == TEXTVIEW("Parentheses")
				? TEXT("if (Condition) {}")
				: TEXT("if:\n    Condition\nthen:");
		case EVerseSyntaxControlKind::BodyDelimiter:
		{
			if (Clause == EExampleClause::Condition)
			{
				return Value == TEXTVIEW("Braces")
					? TEXT("if (Condition) {}") : TEXT("if (Condition) :");
			}
			const FString Head = Clause == EExampleClause::Definition
				? DefinitionHead(Tile) : ClauseKeyword(Clause);
			return Value == TEXTVIEW("Braces") ? Head + TEXT(" {}") : Head + TEXT(":");
		}
		case EVerseSyntaxControlKind::BodyLayout:
		{
			if (Value == TEXTVIEW("Multiline"))
			{
				return MultilineClauseExample(Tile, Clause, ExistingDelimiter);
			}
			const FString Head = Clause == EExampleClause::Definition
				? DefinitionHead(Tile) : ClauseKeyword(Clause);
			const FString First = Clause == EExampleClause::Condition
				? TEXT("FirstCondition") : TEXT("FirstExpression");
			const FString Second = Clause == EExampleClause::Condition
				? TEXT("SecondCondition") : TEXT("SecondExpression");
			return ExistingDelimiter == EVerseClauseDelimiter::Braces
				? Head + TEXT(" { ") + First + TEXT("; ") + Second + TEXT(" }")
				: Head + TEXT(": ") + First + TEXT("; ") + Second;
		}
		case EVerseSyntaxControlKind::BracePlacement:
		{
			const FString Head = DefinitionHead(Tile);
			return Value == TEXTVIEW("Next line")
				? Head + TEXT("\n{\n}") : Head + TEXT(" {\n}");
		}
		case EVerseSyntaxControlKind::Indentation:
		{
			const FString Unit = Value == TEXTVIEW("Tabs")
				? TEXT("\t") : FString::ChrN(FMath::Clamp(FCString::Atoi(*FString(Value)), 1, 8), TEXT(' '));
			return MultilineClauseExample(Tile, Clause, ExistingDelimiter, Unit);
		}
		case EVerseSyntaxControlKind::OperatorSpacing:
			if (Value == TEXTVIEW("None")) return TEXT("Left+Right");
			if (Value == TEXTVIEW("Newline")) return TEXT("Left\n    +\n    Right");
			return TEXT("Left + Right");
		case EVerseSyntaxControlKind::TypeColonSpacing:
			return Value == TEXTVIEW("Compact") ? TEXT("Name:type") : TEXT("Name : type");
		case EVerseSyntaxControlKind::InitializerSpacing:
			return Value == TEXTVIEW("Compact") ? TEXT("Name=Value") : TEXT("Name = Value");
		case EVerseSyntaxControlKind::CallSpacing:
			if (Value == TEXTVIEW("Compact")) return TEXT("Call(First,Second)");
			if (Value == TEXTVIEW("Spaced")) return TEXT("Call( First, Second )");
			if (Value == TEXTVIEW("Wrapped")) return TEXT("Call(\n    First,\n    Second\n)");
			return TEXT("Call(First, Second)");
		case EVerseSyntaxControlKind::CommentOpenerSpacing:
			return Value == TEXTVIEW("One space") ? TEXT("# Comment") : TEXT("#Comment");
		case EVerseSyntaxControlKind::CommentStyle:
			return Value == TEXTVIEW("Block") ? TEXT("<# Comment #>") : TEXT("# Comment");
		default:
			return FString();
		}
	}

	FString FormatOperatorSpacing(
		const FVerseVisualTile& Tile,
		const FVerseParseSnapshot& Snapshot)
	{
		if (Tile.Children.Num() < 2 || !Tile.OperatorRange.IsSet())
		{
			return TEXT("Custom (preserved)");
		}
		const FString Left = Snapshot.GetDocument()->DecodeOriginalRange(
			FVerseTextRange(Tile.Range.Revision, FVerseByteRange::FromBounds(
				Tile.Children[0].Range.EndByte(), Tile.OperatorRange.BeginByte)));
		const FString Right = Snapshot.GetDocument()->DecodeOriginalRange(
			FVerseTextRange(Tile.Range.Revision, FVerseByteRange::FromBounds(
				Tile.OperatorRange.EndByte(), Tile.Children[1].Range.BeginByte)));
		if (Left.IsEmpty() && Right.IsEmpty()) return TEXT("None");
		if (Left == TEXT(" ") && Right == TEXT(" ")) return TEXT("Space");
		if ((Left.Contains(TEXT("\n")) || Left.Contains(TEXT("\r")))
			&& (Right.Contains(TEXT("\n")) || Right.Contains(TEXT("\r"))))
		{
			return TEXT("Newline");
		}
		return TEXT("Custom (preserved)");
	}

	void AddSyntaxProperty(
		TArray<FVerseTileProperty>& Properties,
		FString Name,
		FString Value,
		EVerseSyntaxControlKind Control,
		TArray<FString> Options,
		int32 RegionIndex = INDEX_NONE)
	{
		FVerseTileProperty& Property = Properties.AddDefaulted_GetRef();
		Property.Name = MoveTemp(Name);
		Property.Value = MoveTemp(Value);
		Property.bEditable = Options.Num() > 1;
		Property.EditKind = EVerseTilePropertyEditKind::Syntax;
		Property.SyntaxControl = Control;
		Property.SyntaxRegionIndex = RegionIndex;
		Property.Options = MoveTemp(Options);
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

	if (Tile.ExpressionKind != EVerseExpressionKind::Unsupported
		&& !Tile.bStatementLevel)
	{
		AddSyntaxProperty(Properties, TEXT("Grouping Parentheses"),
			Tile.GroupingLayers.IsEmpty() ? TEXT("0") : TEXT("1"),
			EVerseSyntaxControlKind::GroupingLayers, {TEXT("0"), TEXT("1")});
	}

	if (Tile.EditableClause.IsSet()
		&& Tile.EditableClause->Items.IsValidIndex(Tile.ClauseItemIndex))
	{
		const FVerseVisualSeparatorDescriptor& Separator =
			Tile.EditableClause->Items[Tile.ClauseItemIndex].Separator;
		if (!Separator.bIsEndOfClause)
		{
			AddSyntaxProperty(Properties, TEXT("Statement Separator"),
				FormatSeparator(Separator), EVerseSyntaxControlKind::StatementSeparator,
				{TEXT("Newline"), TEXT("Semicolon + space"), TEXT("Semicolon + newline")});
			TArray<FString> BlankOptions;
			for (int32 Count = 0; Count <= 8; ++Count)
			{
				BlankOptions.Add(FString::FromInt(Count));
			}
			AddSyntaxProperty(Properties, TEXT("Blank Lines After"),
				FString::FromInt(Separator.BlankLineCount),
				EVerseSyntaxControlKind::BlankLinesAfter, MoveTemp(BlankOptions));
		}
	}

	if (Tile.BodyClause.InteriorRange.IsSet()
		&& (Tile.BodyClause.Syntax.Delimiter == EVerseClauseDelimiter::Braces
			|| Tile.BodyClause.Syntax.Delimiter == EVerseClauseDelimiter::Colon))
	{
		const auto& Syntax = Tile.BodyClause.Syntax;
		AddSyntaxProperty(Properties, TEXT("Body Syntax"),
			FormatDelimiter(Syntax.Delimiter), EVerseSyntaxControlKind::BodyDelimiter,
			{TEXT("Braces"), TEXT("Colon")});
		AddSyntaxProperty(Properties, TEXT("Body Layout"),
			Syntax.Layout == EVerseSyntaxLayout::Multiline ? TEXT("Multiline") : TEXT("Inline"),
			EVerseSyntaxControlKind::BodyLayout, {TEXT("Inline"), TEXT("Multiline")});
		if (Syntax.Delimiter == EVerseClauseDelimiter::Braces)
		{
			AddSyntaxProperty(Properties, TEXT("Brace Placement"),
				Syntax.BracePlacement == EVerseBracePlacement::NextLine ? TEXT("Next line") : TEXT("Same line"),
				EVerseSyntaxControlKind::BracePlacement, {TEXT("Same line"), TEXT("Next line")});
		}
	}
	for (int32 RegionIndex = 0; RegionIndex < Tile.ControlRegions.Num(); ++RegionIndex)
	{
		const auto& Region = Tile.ControlRegions[RegionIndex];
		const FString Prefix = Region.Kind == EVerseControlRegionKind::Condition
			? TEXT("Condition") : Region.Kind == EVerseControlRegionKind::Else
				? TEXT("False Body") : TEXT("True Body");
		const bool bIfCondition = Tile.ControlKind == EVerseControlKind::If
			&& Region.Kind == EVerseControlRegionKind::Condition;
		if (bIfCondition
			&& (Region.Syntax.Delimiter == EVerseClauseDelimiter::Parentheses
				|| Region.Syntax.Delimiter == EVerseClauseDelimiter::Colon))
		{
			AddSyntaxProperty(Properties, TEXT("Condition Syntax"),
				FormatDelimiter(Region.Syntax.Delimiter),
				EVerseSyntaxControlKind::ConditionSyntax,
				{TEXT("Parentheses"), TEXT("Colon")}, RegionIndex);
		}
		else if (Region.Syntax.Delimiter == EVerseClauseDelimiter::Braces
			|| Region.Syntax.Delimiter == EVerseClauseDelimiter::Colon)
		{
			AddSyntaxProperty(Properties, Prefix + TEXT(" Syntax"),
				FormatDelimiter(Region.Syntax.Delimiter), EVerseSyntaxControlKind::BodyDelimiter,
				{TEXT("Braces"), TEXT("Colon")}, RegionIndex);
		}
		if (!bIfCondition)
		{
			AddSyntaxProperty(Properties, Prefix + TEXT(" Layout"),
				Region.Syntax.Layout == EVerseSyntaxLayout::Multiline ? TEXT("Multiline") : TEXT("Inline"),
				EVerseSyntaxControlKind::BodyLayout, {TEXT("Inline"), TEXT("Multiline")}, RegionIndex);
		}
	}

	if (IsVerseBinaryOperatorExpression(Tile.ExpressionKind))
	{
		AddSyntaxProperty(Properties, TEXT("Operator Spacing"),
			FormatOperatorSpacing(Tile, Snapshot),
			EVerseSyntaxControlKind::OperatorSpacing,
			{TEXT("None"), TEXT("Space"), TEXT("Newline")});
	}
	if (Tile.Kind == EVerseVisualTileKind::Definition
		&& (Tile.DefinitionKind == VerseSyntaxKind::Variable
			|| Tile.DefinitionKind == VerseSyntaxKind::Constant))
	{
		const FString Source = Snapshot.GetDocument()->DecodeOriginalRange(Tile.Range);
		const FString ColonSpacing = Source.Contains(TEXT(" : "))
			? TEXT("Standard") : Source.Contains(TEXT(":"))
				? TEXT("Compact") : TEXT("Custom (preserved)");
		const FString InitializerSpacing = Source.Contains(TEXT(" = "))
			|| Source.Contains(TEXT(" := ")) ? TEXT("Standard")
			: Source.Contains(TEXT("=")) ? TEXT("Compact") : TEXT("Custom (preserved)");
		AddSyntaxProperty(Properties, TEXT("Type Colon Spacing"), ColonSpacing,
			EVerseSyntaxControlKind::TypeColonSpacing, {TEXT("Compact"), TEXT("Standard")});
		AddSyntaxProperty(Properties, TEXT("Initializer Spacing"), InitializerSpacing,
			EVerseSyntaxControlKind::InitializerSpacing, {TEXT("Compact"), TEXT("Standard")});
	}
	if (Tile.ExpressionKind == EVerseExpressionKind::Call)
	{
		const FString Source = Snapshot.GetDocument()->DecodeOriginalRange(Tile.Range);
		const FString Layout = Source.Contains(TEXT("\n")) || Source.Contains(TEXT("\r"))
			? TEXT("Wrapped") : Source.Contains(TEXT("( ")) || Source.Contains(TEXT("[ "))
				? TEXT("Spaced") : Source.Contains(TEXT(", "))
					? TEXT("Standard") : TEXT("Compact");
		AddSyntaxProperty(Properties, TEXT("Call Layout"), Layout,
			EVerseSyntaxControlKind::CallSpacing,
			{TEXT("Compact"), TEXT("Standard"), TEXT("Spaced"), TEXT("Wrapped")});
	}
	if (Tile.Kind == EVerseVisualTileKind::Comment
		&& (Tile.CommentKind == EVerseCommentKind::Line
			|| Tile.CommentKind == EVerseCommentKind::Block))
	{
		const FString Source = Snapshot.GetDocument()->DecodeOriginalRange(Tile.Range);
		const int32 Opener = Source.StartsWith(TEXT("<#")) ? 2 : 1;
		const bool bHasSpace = Source.Len() > Opener && Source[Opener] == TEXT(' ');
		AddSyntaxProperty(Properties, TEXT("Space After Comment Opener"),
			bHasSpace ? TEXT("One space") : TEXT("None"),
			EVerseSyntaxControlKind::CommentOpenerSpacing,
			{TEXT("None"), TEXT("One space")});
		const bool bSingleLine = !Source.Contains(TEXT("\n")) && !Source.Contains(TEXT("\r"));
		AddSyntaxProperty(Properties, TEXT("Comment Syntax"), GetCommentKind(Tile.CommentKind),
			EVerseSyntaxControlKind::CommentStyle,
			bSingleLine ? TArray<FString>{TEXT("Line"), TEXT("Block")}
				: TArray<FString>{GetCommentKind(Tile.CommentKind)});
	}

	for (FVerseTileProperty& Property : Properties)
	{
		if (Property.EditKind == EVerseTilePropertyEditKind::Syntax)
		{
			Property.Example = SyntaxExample(
				Tile, Property.Name, Property.Value, Property.SyntaxControl,
				Property.SyntaxRegionIndex);
		}
	}

	// Identity is always presented first, followed by the source location. All
	// editable and syntax-specific properties belong below this stable header.
	const int32 LinesIndex = Properties.Num() > 1 && Properties[1].Name == TEXT("Kind")
		? 2
		: 1;
	Properties.Insert({TEXT("Lines"), GetSourceLines(Tile)}, LinesIndex);
	return Properties;
}

bool FVerseTileProperties::MatchesFilter(const FVerseTileProperty& Property, const FString& Filter)
{
	return Filter.IsEmpty()
		|| Property.Name.Contains(Filter, ESearchCase::IgnoreCase)
		|| Property.Value.Contains(Filter, ESearchCase::IgnoreCase);
}
