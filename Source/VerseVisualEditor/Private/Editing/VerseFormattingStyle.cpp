#include "Editing/VerseFormattingStyle.h"

#include "VerseDocument.h"
#include "VisualModel/VerseVisualTile.h"

namespace
{
	template <typename T>
	TOptional<T> Dominant(const TMap<T, int32>& Counts)
	{
		TOptional<T> Result;
		int32 Highest = 0;
		bool bTie = false;
		for (const TPair<T, int32>& Pair : Counts)
		{
			if (Pair.Value > Highest)
			{
				Result = Pair.Key;
				Highest = Pair.Value;
				bTie = false;
			}
			else if (Pair.Value == Highest && Highest > 0)
			{
				bTie = true;
			}
		}
		return bTie ? TOptional<T>() : Result;
	}

	void AccumulateClause(
		const FVerseClauseDescriptor& Clause,
		TMap<EVerseClauseDelimiter, int32>& Delimiters,
		TMap<EVerseBracePlacement, int32>& Braces,
		TMap<FString, int32>& Indentation,
		TMap<EVerseSeparatorLayout, int32>& Separators)
	{
		if (Clause.Syntax.Delimiter != EVerseClauseDelimiter::None)
		{
			++Delimiters.FindOrAdd(Clause.Syntax.Delimiter);
		}
		if (Clause.Syntax.BracePlacement != EVerseBracePlacement::NotApplicable)
		{
			++Braces.FindOrAdd(Clause.Syntax.BracePlacement);
		}
		if (!Clause.Syntax.IndentationUnit.IsEmpty())
		{
			++Indentation.FindOrAdd(Clause.Syntax.IndentationUnit);
		}
		for (const FVerseClauseItemDescriptor& Item : Clause.Items)
		{
			if (!Item.Separator.bIsEndOfClause
				&& Item.Separator.Layout != EVerseSeparatorLayout::None)
			{
				++Separators.FindOrAdd(Item.Separator.Layout);
			}
		}
	}

	void AccumulateRegions(
		TConstArrayView<FVerseSourceRegion> Regions,
		TMap<EVerseClauseDelimiter, int32>& Delimiters,
		TMap<EVerseBracePlacement, int32>& Braces,
		TMap<FString, int32>& Indentation,
		TMap<EVerseSeparatorLayout, int32>& Separators)
	{
		for (const FVerseSourceRegion& Region : Regions)
		{
			if (Region.BodyClause.IsSet())
			{
				AccumulateClause(Region.BodyClause, Delimiters, Braces, Indentation, Separators);
			}
			AccumulateRegions(Region.Children, Delimiters, Braces, Indentation, Separators);
		}
	}

	EVerseLineEnding ProjectLineEnding(EVerseFormattingLineEnding Value)
	{
		switch (Value)
		{
		case EVerseFormattingLineEnding::CrLf: return EVerseLineEnding::CrLf;
		case EVerseFormattingLineEnding::Cr: return EVerseLineEnding::Cr;
		default: return EVerseLineEnding::Lf;
		}
	}

	EVerseClauseDelimiter ProjectDelimiter(EVerseFormattingBodySyntax Value)
	{
		switch (Value)
		{
		case EVerseFormattingBodySyntax::Braces: return EVerseClauseDelimiter::Braces;
		case EVerseFormattingBodySyntax::BareIndentation: return EVerseClauseDelimiter::BareIndentation;
		case EVerseFormattingBodySyntax::Dot: return EVerseClauseDelimiter::Dot;
		default: return EVerseClauseDelimiter::Colon;
		}
	}

	void ApplyStatementSeparator(
		EVerseFormattingStatementSeparator Value,
		EVerseSeparatorToken& OutToken,
		EVerseSeparatorLayout& OutLayout)
	{
		switch (Value)
		{
		case EVerseFormattingStatementSeparator::SemicolonSpace:
			OutToken = EVerseSeparatorToken::Semicolon;
			OutLayout = EVerseSeparatorLayout::TokenAndSpace;
			break;
		case EVerseFormattingStatementSeparator::SemicolonNewline:
			OutToken = EVerseSeparatorToken::Semicolon;
			OutLayout = EVerseSeparatorLayout::TokenAndNewline;
			break;
		default:
			OutToken = EVerseSeparatorToken::None;
			OutLayout = EVerseSeparatorLayout::Newline;
			break;
		}
	}
}

FVerseFormattingStyleEvidence FVerseFormattingStyleAnalyzer::Analyze(
	const FVerseDocument& Document,
	const FVerseParseSnapshot& Snapshot)
{
	FVerseFormattingStyleEvidence Result;
	TMap<EVerseClauseDelimiter, int32> Delimiters;
	TMap<EVerseBracePlacement, int32> Braces;
	TMap<FString, int32> Indentation;
	TMap<EVerseSeparatorLayout, int32> Separators;
	AccumulateRegions(
		Snapshot.GetSourceRegions(), Delimiters, Braces, Indentation, Separators);
	Result.BodyDelimiter = Dominant(Delimiters);
	Result.BracePlacement = Dominant(Braces);
	Result.IndentationUnit = Dominant(Indentation);
	if (const TOptional<EVerseSeparatorLayout> Layout = Dominant(Separators))
	{
		FVerseSeparatorDescriptor Separator;
		Separator.Layout = Layout.GetValue();
		Separator.Token = Separator.Layout == EVerseSeparatorLayout::TokenAndSpace
			|| Separator.Layout == EVerseSeparatorLayout::TokenAndNewline
			? EVerseSeparatorToken::Semicolon : EVerseSeparatorToken::None;
		Result.StatementSeparator = Separator;
	}
	if (Document.GetLineEnding() != EVerseLineEnding::None
		&& Document.GetLineEnding() != EVerseLineEnding::Mixed)
	{
		Result.LineEnding = Document.GetLineEnding();
	}
	return Result;
}

FVerseFormattingStyleProfile FVerseFormattingStyleResolver::ResolveDefaults()
{
	const UVerseVisualEditorProjectSettings* Project =
		GetDefault<UVerseVisualEditorProjectSettings>();
	const UVerseVisualEditorSettings* User = GetDefault<UVerseVisualEditorSettings>();
	FVerseFormattingStyleProfile Result;
	const bool bUser = User != nullptr && User->bOverrideProjectFormatting;
	const EVerseFormattingIndentation Indentation = bUser
		? User->IndentationOverride : Project->Indentation;
	const int32 IndentationWidth = FMath::Clamp(
		bUser ? User->IndentationWidthOverride : Project->IndentationWidth, 1, 8);
	Result.IndentationUnit = Indentation == EVerseFormattingIndentation::Tabs
		? TEXT("\t") : FString::ChrN(IndentationWidth, TEXT(' '));
	Result.LineEnding = ProjectLineEnding(
		bUser ? User->LineEndingOverride : Project->LineEnding);
	Result.BodyDelimiter = ProjectDelimiter(
		bUser ? User->BodySyntaxOverride : Project->BodySyntax);
	Result.BracePlacement = (bUser ? User->BracePlacementOverride : Project->BracePlacement)
		== EVerseFormattingBracePlacement::SameLine
		? EVerseBracePlacement::SameLine : EVerseBracePlacement::NextLine;
	ApplyStatementSeparator(
		bUser ? User->StatementSeparatorOverride : Project->StatementSeparator,
		Result.StatementSeparatorToken,
		Result.StatementSeparatorLayout);
	ApplyStatementSeparator(
		Project->FailurePredicateSeparator,
		Result.FailureSeparatorToken,
		Result.FailureSeparatorLayout);
	Result.bSpaceAroundOperators = Project->bSpaceAroundOperators;
	Result.bSpaceAfterComma = Project->bSpaceAfterComma;
	Result.bSpaceAfterSemicolon = Project->bSpaceAfterSemicolon;
	Result.bSpaceInsideParentheses = Project->bSpaceInsideParentheses;

	return Result;
}

FVerseFormattingStyleProfile FVerseFormattingStyleResolver::Resolve(
	const FVerseDocument& Document,
	const FVerseParseSnapshot& Snapshot,
	const FVerseClauseSyntaxDescriptor* DestinationClause)
{
	FVerseFormattingStyleProfile Result = ResolveDefaults();
	const FVerseFormattingStyleEvidence Evidence =
		FVerseFormattingStyleAnalyzer::Analyze(Document, Snapshot);
	if (Evidence.BodyDelimiter.IsSet()) Result.BodyDelimiter = Evidence.BodyDelimiter.GetValue();
	if (Evidence.LineEnding.IsSet()) Result.LineEnding = Evidence.LineEnding.GetValue();
	if (Evidence.BracePlacement.IsSet()) Result.BracePlacement = Evidence.BracePlacement.GetValue();
	if (Evidence.IndentationUnit.IsSet()) Result.IndentationUnit = Evidence.IndentationUnit.GetValue();
	if (Evidence.StatementSeparator.IsSet())
	{
		Result.StatementSeparatorToken = Evidence.StatementSeparator->Token;
		Result.StatementSeparatorLayout = Evidence.StatementSeparator->Layout;
	}
	if (DestinationClause != nullptr)
	{
		if (DestinationClause->Delimiter != EVerseClauseDelimiter::None)
		{
			Result.BodyDelimiter = DestinationClause->Delimiter;
		}
		if (DestinationClause->LineEnding != EVerseLineEnding::None
			&& DestinationClause->LineEnding != EVerseLineEnding::Mixed)
		{
			Result.LineEnding = DestinationClause->LineEnding;
		}
		if (!DestinationClause->IndentationUnit.IsEmpty())
		{
			Result.IndentationUnit = DestinationClause->IndentationUnit;
		}
		if (DestinationClause->BracePlacement != EVerseBracePlacement::NotApplicable)
		{
			Result.BracePlacement = DestinationClause->BracePlacement;
		}
	}
	return Result;
}

FVerseFormattingStyleProfile FVerseFormattingStyleResolver::Resolve(
	const FVerseDocument& Document,
	const FVerseParseSnapshot& Snapshot,
	const FVerseVisualClauseDescriptor& DestinationClause)
{
	FVerseFormattingStyleProfile Result = Resolve(Document, Snapshot, nullptr);
	if (DestinationClause.Syntax.Delimiter != EVerseClauseDelimiter::None)
	{
		Result.BodyDelimiter = DestinationClause.Syntax.Delimiter;
	}
	if (DestinationClause.Syntax.LineEnding != EVerseLineEnding::None
		&& DestinationClause.Syntax.LineEnding != EVerseLineEnding::Mixed)
	{
		Result.LineEnding = DestinationClause.Syntax.LineEnding;
	}
	if (!DestinationClause.Syntax.IndentationUnit.IsEmpty())
	{
		Result.IndentationUnit = DestinationClause.Syntax.IndentationUnit;
	}
	if (DestinationClause.Syntax.BracePlacement != EVerseBracePlacement::NotApplicable)
	{
		Result.BracePlacement = DestinationClause.Syntax.BracePlacement;
	}
	if (!DestinationClause.Items.IsEmpty())
	{
		for (const FVerseVisualClauseItemDescriptor& Item : DestinationClause.Items)
		{
			if (!Item.Separator.bIsEndOfClause
				&& Item.Separator.Layout != EVerseSeparatorLayout::None)
			{
				Result.StatementSeparatorToken = Item.Separator.Token;
				Result.StatementSeparatorLayout = Item.Separator.Layout;
				break;
			}
		}
	}
	return Result;
}

FString FVerseSyntaxEmitter::LineEnding(const FVerseFormattingStyleProfile& Style)
{
	switch (Style.LineEnding)
	{
	case EVerseLineEnding::CrLf: return TEXT("\r\n");
	case EVerseLineEnding::Cr: return TEXT("\r");
	default: return TEXT("\n");
	}
}

FString FVerseSyntaxEmitter::Separator(
	EVerseSeparatorToken Token,
	EVerseSeparatorLayout Layout,
	int32 BlankLineCount,
	const FVerseFormattingStyleProfile& Style,
	FStringView FollowingIndentation)
{
	FString Result;
	if (Token == EVerseSeparatorToken::Comma) Result += TEXT(",");
	else if (Token == EVerseSeparatorToken::Semicolon) Result += TEXT(";");
	if (Layout == EVerseSeparatorLayout::HorizontalSpace
		|| Layout == EVerseSeparatorLayout::TokenAndSpace)
	{
		Result += TEXT(" ");
	}
	else if (Layout == EVerseSeparatorLayout::Newline
		|| Layout == EVerseSeparatorLayout::TokenAndNewline)
	{
		const FString Newline = LineEnding(Style);
		Result += Newline;
		for (int32 Index = 0; Index < BlankLineCount; ++Index) Result += Newline;
		Result += FString(FollowingIndentation);
	}
	return Result;
}

FString FVerseSyntaxEmitter::Infix(
	FStringView Left, FStringView Operator, FStringView Right,
	const FVerseFormattingStyleProfile& Style)
{
	const TCHAR* Space = Style.bSpaceAroundOperators ? TEXT(" ") : TEXT("");
	return FString::Printf(TEXT("%s%s%s%s%s"),
		*FString(Left), Space, *FString(Operator), Space, *FString(Right));
}

FString FVerseSyntaxEmitter::Prefix(
	FStringView Operator, FStringView Operand,
	const FVerseFormattingStyleProfile& Style)
{
	return FString::Printf(TEXT("%s%s%s"), *FString(Operator),
		Style.bSpaceAroundOperators ? TEXT(" ") : TEXT(""), *FString(Operand));
}

FString FVerseSyntaxEmitter::Arguments(
	TConstArrayView<FString> Arguments,
	bool bFailureCall,
	const FVerseFormattingStyleProfile& Style)
{
	const FString Separator = Style.bSpaceAfterComma ? TEXT(", ") : TEXT(",");
	const FString Padding = Style.bSpaceInsideParentheses && !Arguments.IsEmpty()
		? TEXT(" ") : TEXT("");
	const FString Joined = FString::Join(Arguments, *Separator);
	return bFailureCall
		? FString::Printf(TEXT("[%s%s%s]"), *Padding, *Joined, *Padding)
		: FString::Printf(TEXT("(%s%s%s)"), *Padding, *Joined, *Padding);
}

FString FVerseSyntaxEmitter::Definition(
	bool bMutable, FStringView Name, FStringView Type, FStringView Initializer,
	const FVerseFormattingStyleProfile& Style)
{
	const TCHAR* Space = Style.bSpaceAroundOperators ? TEXT(" ") : TEXT("");
	return FString::Printf(TEXT("%s%s : %s%s=%s%s"),
		bMutable ? TEXT("var ") : TEXT(""), *FString(Name), *FString(Type),
		Space, Space, *FString(Initializer));
}
