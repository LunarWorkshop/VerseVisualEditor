#include "Editing/VerseFormattingEdit.h"

#include "Document/VerseDocumentSession.h"
#include "Editing/VerseFormattingStyle.h"
#include "Internationalization/Text.h"
#include "VerseParseSnapshotBuilder.h"
#include "Algo/Unique.h"

#define LOCTEXT_NAMESPACE "VerseFormattingEdit"

namespace
{
	FString Decode(const FVerseDocumentSession& Session, FVerseTextRange Range)
	{
		return Range.IsSet()
			? Session.GetParseSnapshot().GetDocument()->DecodeOriginalRange(Range)
			: FString();
	}

	bool IsWhitespaceOnly(FStringView Text)
	{
		for (TCHAR Character : Text)
		{
			if (!FChar::IsWhitespace(Character))
			{
				return false;
			}
		}
		return true;
	}

	FVerseDocumentEdit Edit(
		FVerseDocumentRevision Revision,
		FVerseByteRange Range,
		FStringView Replacement)
	{
		return {FVerseTextRange(Revision, Range), FUtf8String(Replacement)};
	}

	void FingerprintExpression(const FVerseExpressionDescriptor& Expression, FString& Out)
	{
		Out += FString::Printf(TEXT("E%d:%d:%s[") ,
			static_cast<int32>(Expression.Kind),
			static_cast<int32>(Expression.ControlKind),
			*Expression.OperatorSpelling);
		for (const FVerseExpressionControlRegion& Region : Expression.ControlRegions)
		{
			Out += FString::Printf(TEXT("R%d{"), static_cast<int32>(Region.Kind));
			for (int32 Index = 0; Index < Region.Items.Num(); ++Index)
			{
				Out += TEXT("I");
			}
			Out += TEXT("}");
		}
		for (const FVerseExpressionDescriptor& Operand : Expression.Operands)
		{
			FingerprintExpression(Operand, Out);
		}
		Out += TEXT("]");
	}

	void FingerprintRegions(TConstArrayView<FVerseSourceRegion> Regions, FString& Out)
	{
		for (const FVerseSourceRegion& Region : Regions)
		{
			if (Region.Kind == EVerseSourceRegionKind::Syntax)
			{
				Out += TEXT("D") + Region.SyntaxKind.ToString() + TEXT("{");
				for (const FVerseClauseItemDescriptor& Item : Region.BodyClause.Items)
				{
					FingerprintExpression(Item.Expression, Out);
					Out += Item.bIsFinalValuePosition ? TEXT("F") : TEXT("S");
				}
				FingerprintRegions(Region.Children, Out);
				Out += TEXT("}");
			}
		}
	}

	FString StructuralFingerprint(const FVerseParseSnapshot& Snapshot)
	{
		FString Result;
		FingerprintRegions(Snapshot.GetSourceRegions(), Result);
		return Result;
	}

	bool ValidateAndCommit(
		FVerseDocumentSession& Session,
		TConstArrayView<FVerseDocumentEdit> Edits,
		FText& OutError)
	{
		if (Edits.IsEmpty())
		{
			OutError = LOCTEXT("NoFormattingEdits", "The selected formatting is already in use.");
			return false;
		}
		TArray<FVerseDocumentEdit> Sorted(Edits);
		Sorted.Sort([](const FVerseDocumentEdit& Left, const FVerseDocumentEdit& Right)
		{
			return Left.Range.BeginByte > Right.Range.BeginByte;
		});
		FVerseEditBuffer Candidate = Session.GetEditBuffer();
		for (const FVerseDocumentEdit& CandidateEdit : Sorted)
		{
			if (CandidateEdit.Range.Revision != Session.GetRevision()
				|| !Candidate.Replace(CandidateEdit.Range, CandidateEdit.Replacement, OutError))
			{
				return false;
			}
		}
		const FUtf8String Source = Candidate.Materialize();
		const TConstArrayView<uint8> Bytes(
			reinterpret_cast<const uint8*>(*Source), Source.Len());
		const TSharedPtr<FVerseDocument> CandidateDocument =
			FVerseDocument::CreateFromBytes(Bytes, OutError);
		if (!CandidateDocument.IsValid())
		{
			return false;
		}
		const FVerseParseSnapshot CandidateSnapshot =
			FVerseParseSnapshotBuilder::Build(CandidateDocument.ToSharedRef());
		if (StructuralFingerprint(Session.GetParseSnapshot())
			!= StructuralFingerprint(CandidateSnapshot))
		{
			OutError = LOCTEXT(
				"FormattingChangedStructure",
				"The proposed formatting did not reparse as the same Verse code, so the source was left unchanged. This formatting form may not be valid for this construct.");
			return false;
		}
		return Session.ReplaceMany(Edits, OutError);
	}

	bool AddWhitespaceEdit(
		FVerseDocumentSession& Session,
		FVerseTextRange Range,
		FStringView Replacement,
		TArray<FVerseDocumentEdit>& OutEdits,
		FText& OutError)
	{
		if (!Range.IsSet() || Range.Revision != Session.GetRevision())
		{
			OutError = LOCTEXT("StaleFormattingRange", "The syntax property belongs to an obsolete source revision.");
			return false;
		}
		if (!IsWhitespaceOnly(Decode(Session, Range)))
		{
			OutError = LOCTEXT(
				"FormattingOwnsComment",
				"This syntax cannot be changed safely because the affected whitespace contains a comment or unsupported token.");
			return false;
		}
		OutEdits.Add(Edit(Session.GetRevision(), Range, Replacement));
		return true;
	}

	const FVerseVisualClauseDescriptor* GetOwnedClause(
		const FVerseVisualTile& Tile,
		int32 ControlRegionIndex,
		FVerseVisualClauseDescriptor& Scratch)
	{
		if (Tile.ControlRegions.IsValidIndex(ControlRegionIndex))
		{
			const auto& Region = Tile.ControlRegions[ControlRegionIndex];
			Scratch.InteriorRange = Region.InteriorRange;
			Scratch.OpeningPunctuationRange = Region.OpeningPunctuationRange;
			Scratch.ClosingPunctuationRange = Region.ClosingPunctuationRange;
			Scratch.Syntax = Region.Syntax;
			Scratch.EmptyBodyInsertionAnchor = Region.EmptyBodyInsertionAnchor;
			for (const auto& Item : Region.Items)
			{
				auto& Copy = Scratch.Items.AddDefaulted_GetRef();
				Copy.Expression.Range = Item.ExpressionRange;
				Copy.LeadingTriviaRange = Item.LeadingTriviaRange;
				Copy.TrailingTriviaRange = Item.TrailingTriviaRange;
				Copy.Separator = Item.Separator;
			}
			return &Scratch;
		}
		if (Tile.BodyClause.InteriorRange.IsSet()) return &Tile.BodyClause;
		if (Tile.EditableClause.IsSet()) return &Tile.EditableClause.GetValue();
		return nullptr;
	}

	bool AddClauseLayoutEdits(
		FVerseDocumentSession& Session,
		const FVerseVisualClauseDescriptor& Clause,
		bool bMultiline,
		const FVerseFormattingStyleProfile& Style,
		TArray<FVerseDocumentEdit>& OutEdits,
		FText& OutError)
	{
		if (Clause.Items.IsEmpty())
		{
			OutError = LOCTEXT("NoBodyLayout", "This body has no expressions to lay out.");
			return false;
		}
		const FString ChildIndent = Clause.Syntax.IndentationPrefix
			+ (Clause.Syntax.IndentationUnit.IsEmpty()
				? Style.IndentationUnit : Clause.Syntax.IndentationUnit);
		const FString Leading = bMultiline
			? FVerseSyntaxEmitter::LineEnding(Style) + ChildIndent : TEXT(" ");
		if (!AddWhitespaceEdit(Session, Clause.Syntax.LeadingWhitespaceRange,
			Leading, OutEdits, OutError))
		{
			return false;
		}
		for (int32 Index = 0; Index + 1 < Clause.Items.Num(); ++Index)
		{
			const FVerseVisualSeparatorDescriptor& Separator = Clause.Items[Index].Separator;
			if (Separator.bIsEndOfClause)
			{
				continue;
			}
			const int32 Begin = Separator.TokenRange.IsSet()
				? Separator.TokenRange.BeginByte : Separator.WhitespaceRange.BeginByte;
			int32 End = Separator.WhitespaceRange.IsSet()
				? Separator.WhitespaceRange.EndByte() : Separator.TokenRange.EndByte();
			const FString Trivia = Decode(Session, Separator.WhitespaceRange);
			if (!IsWhitespaceOnly(Trivia))
			{
				int32 WhitespaceCharacters = 0;
				while (WhitespaceCharacters < Trivia.Len()
					&& FChar::IsWhitespace(Trivia[WhitespaceCharacters]))
				{
					++WhitespaceCharacters;
				}
				const FString ExpressionPrefix = Trivia.Mid(WhitespaceCharacters);
				bool bOnlyGroupingOpeners = !ExpressionPrefix.IsEmpty();
				for (TCHAR Character : ExpressionPrefix)
				{
					if (Character != TEXT('('))
					{
						bOnlyGroupingOpeners = false;
						break;
					}
				}
				if (!bOnlyGroupingOpeners)
				{
					OutError = LOCTEXT("BodyLayoutContainsComment", "A statement separator contains preserved trivia, so this body cannot be reformatted safely.");
					return false;
				}
				const FTCHARToUTF8 WhitespaceUtf8(*Trivia.Left(WhitespaceCharacters));
				End = Separator.WhitespaceRange.BeginByte + WhitespaceUtf8.Length();
			}
			OutEdits.Add(Edit(Session.GetRevision(), FVerseByteRange::FromBounds(Begin, End),
				bMultiline
					? FVerseSyntaxEmitter::LineEnding(Style) + ChildIndent
					: TEXT("; ")));
		}
		if (Clause.Syntax.Delimiter == EVerseClauseDelimiter::Braces)
		{
			const FString Trailing = bMultiline
				? FVerseSyntaxEmitter::LineEnding(Style) + Clause.Syntax.IndentationPrefix
				: TEXT(" ");
			if (!AddWhitespaceEdit(Session, Clause.Syntax.TrailingWhitespaceRange,
				Trailing, OutEdits, OutError))
			{
				return false;
			}
		}
		return true;
	}

	TOptional<int32> ParseInteger(FStringView Value)
	{
		int32 Parsed = 0;
		return LexTryParseString(Parsed, *FString(Value))
			? TOptional<int32>(Parsed) : TOptional<int32>();
	}

	FString ReplaceLineEndings(FString Text, FStringView LineEnding)
	{
		Text.ReplaceInline(TEXT("\r\n"), TEXT("\n"));
		Text.ReplaceInline(TEXT("\r"), TEXT("\n"));
		Text.ReplaceInline(TEXT("\n"), *FString(LineEnding));
		return Text;
	}

	void GatherClauseWhitespaceRanges(
		const FVerseVisualClauseDescriptor& Clause,
		TArray<FVerseTextRange>& OutRanges)
	{
		if (Clause.Syntax.LeadingWhitespaceRange.IsSet())
		{
			OutRanges.Add(Clause.Syntax.LeadingWhitespaceRange);
		}
		for (const FVerseVisualClauseItemDescriptor& Item : Clause.Items)
		{
			if (Item.Separator.WhitespaceRange.IsSet())
			{
				OutRanges.Add(Item.Separator.WhitespaceRange);
			}
		}
		if (Clause.Syntax.TrailingWhitespaceRange.IsSet())
		{
			OutRanges.Add(Clause.Syntax.TrailingWhitespaceRange);
		}
	}

	int32 FindByte(
		FUtf8StringView Source,
		UTF8CHAR Needle,
		int32 Begin,
		int32 End)
	{
		for (int32 Index = FMath::Max(0, Begin); Index < FMath::Min(End, Source.Len()); ++Index)
		{
			if (Source[Index] == Needle)
			{
				return Index;
			}
		}
		return INDEX_NONE;
	}

	int32 FindLastByte(
		FUtf8StringView Source,
		UTF8CHAR Needle,
		int32 Begin,
		int32 End)
	{
		for (int32 Index = FMath::Min(End, Source.Len()) - 1; Index >= FMath::Max(0, Begin); --Index)
		{
			if (Source[Index] == Needle)
			{
				return Index;
			}
		}
		return INDEX_NONE;
	}

	bool IsWhitespaceAndOptionalComma(FStringView Text)
	{
		int32 Commas = 0;
		for (TCHAR Character : Text)
		{
			if (Character == TEXT(','))
			{
				++Commas;
			}
			else if (!FChar::IsWhitespace(Character))
			{
				return false;
			}
		}
		return Commas <= 1;
	}
}

bool FVerseFormattingEditService::Apply(
	FVerseDocumentSession& Session,
	const FVerseVisualTile& Tile,
	EVerseSyntaxControlKind Control,
	FStringView Value,
	FText& OutError,
	int32 ControlRegionIndex)
{
	if (Tile.Range.Revision != Session.GetRevision())
	{
		OutError = LOCTEXT("StaleFormattingTile", "The selected tile belongs to an obsolete source revision.");
		return false;
	}
	const FVerseFormattingStyleProfile Style = FVerseFormattingStyleResolver::Resolve(
		*Session.GetParseSnapshot().GetDocument(), Session.GetParseSnapshot());
	TArray<FVerseDocumentEdit> Edits;
	FVerseVisualClauseDescriptor ScratchClause;

	if (Control == EVerseSyntaxControlKind::GroupingLayers)
	{
		const TOptional<int32> Requested = ParseInteger(Value);
		if (!Requested.IsSet() || Requested.GetValue() < 0 || Requested.GetValue() > 8)
		{
			OutError = LOCTEXT("InvalidGroupingCount", "Grouping parentheses must be between 0 and 8 layers.");
			return false;
		}
		const int32 Current = Tile.GroupingLayers.Num();
		if (Requested.GetValue() > Current)
		{
			const int32 Added = Requested.GetValue() - Current;
			Edits.Add(Edit(Session.GetRevision(), {Tile.Range.BeginByte, 0}, FString::ChrN(Added, TEXT('('))));
			Edits.Add(Edit(Session.GetRevision(), {Tile.Range.EndByte(), 0}, FString::ChrN(Added, TEXT(')'))));
		}
		else
		{
			for (int32 Index = 0; Index < Current - Requested.GetValue(); ++Index)
			{
				const auto& Layer = Tile.GroupingLayers[Index];
				Edits.Add(Edit(Session.GetRevision(), Layer.OpeningRange, FStringView()));
				Edits.Add(Edit(Session.GetRevision(), Layer.ClosingRange, FStringView()));
			}
		}
	}
	else if (Control == EVerseSyntaxControlKind::StatementSeparator
		|| Control == EVerseSyntaxControlKind::BlankLinesAfter)
	{
		if (!Tile.EditableClause.IsSet()
			|| !Tile.EditableClause->Items.IsValidIndex(Tile.ClauseItemIndex))
		{
			OutError = LOCTEXT("NoEditableSeparator", "This tile has no editable statement separator.");
			return false;
		}
		const FVerseVisualSeparatorDescriptor& Separator =
			Tile.EditableClause->Items[Tile.ClauseItemIndex].Separator;
		if (Separator.bIsEndOfClause)
		{
			OutError = LOCTEXT("FinalSeparator", "The final value in this clause cannot have this separator.");
			return false;
		}
		EVerseSeparatorToken Token = Separator.Token;
		EVerseSeparatorLayout Layout = Separator.Layout;
		int32 BlankLines = Separator.BlankLineCount;
		if (Control == EVerseSyntaxControlKind::BlankLinesAfter)
		{
			const TOptional<int32> Requested = ParseInteger(Value);
			if (!Requested.IsSet() || Requested.GetValue() < 0 || Requested.GetValue() > 8)
			{
				OutError = LOCTEXT("InvalidBlankLines", "Blank-line count must be between 0 and 8.");
				return false;
			}
			BlankLines = Requested.GetValue();
			if (Layout != EVerseSeparatorLayout::Newline
				&& Layout != EVerseSeparatorLayout::TokenAndNewline)
			{
				Layout = Token == EVerseSeparatorToken::None
					? EVerseSeparatorLayout::Newline
					: EVerseSeparatorLayout::TokenAndNewline;
			}
		}
		else if (Value == TEXTVIEW("Newline"))
		{
			Token = EVerseSeparatorToken::None;
			Layout = EVerseSeparatorLayout::Newline;
		}
		else if (Value == TEXTVIEW("Semicolon + space"))
		{
			Token = EVerseSeparatorToken::Semicolon;
			Layout = EVerseSeparatorLayout::TokenAndSpace;
			BlankLines = 0;
		}
		else if (Value == TEXTVIEW("Semicolon + newline"))
		{
			Token = EVerseSeparatorToken::Semicolon;
			Layout = EVerseSeparatorLayout::TokenAndNewline;
		}
		else
		{
			OutError = LOCTEXT("UnsupportedSeparator", "That separator is not legal in this clause.");
			return false;
		}
		const int32 Begin = Separator.TokenRange.IsSet()
			? Separator.TokenRange.BeginByte : Separator.WhitespaceRange.BeginByte;
		const int32 End = Separator.WhitespaceRange.IsSet()
			? Separator.WhitespaceRange.EndByte()
			: Separator.TokenRange.EndByte();
		const FVerseTextRange Range(Session.GetRevision(), FVerseByteRange::FromBounds(Begin, End));
		if (!IsWhitespaceOnly(Decode(Session, Separator.WhitespaceRange)))
		{
			OutError = LOCTEXT("SeparatorContainsComment", "This separator contains a comment and is preserved.");
			return false;
		}
		const FString Indent = Tile.EditableClause->Syntax.IndentationPrefix
			+ Tile.EditableClause->Syntax.IndentationUnit;
		Edits.Add(Edit(Session.GetRevision(), Range,
			FVerseSyntaxEmitter::Separator(Token, Layout, BlankLines, Style, Indent)));
	}
	else if (Control == EVerseSyntaxControlKind::OperatorSpacing)
	{
		if (!Tile.OperatorRange.IsSet() || Tile.Children.Num() < 2)
		{
			OutError = LOCTEXT("NoOperatorSpacing", "This operator has no editable binary spacing.");
			return false;
		}
		FString Gap = Value == TEXTVIEW("None") ? FString()
			: Value == TEXTVIEW("Newline")
				? FVerseSyntaxEmitter::LineEnding(Style) + Style.IndentationUnit
				: TEXT(" ");
		const FVerseTextRange LeftGap(
			Session.GetRevision(), FVerseByteRange::FromBounds(
				Tile.Children[0].Range.EndByte(), Tile.OperatorRange.BeginByte));
		const FVerseTextRange RightGap(
			Session.GetRevision(), FVerseByteRange::FromBounds(
				Tile.OperatorRange.EndByte(), Tile.Children[1].Range.BeginByte));
		if (!AddWhitespaceEdit(Session, LeftGap, Gap, Edits, OutError)
			|| !AddWhitespaceEdit(Session, RightGap, Gap, Edits, OutError))
		{
			return false;
		}
	}
	else if (Control == EVerseSyntaxControlKind::ConditionSyntax)
	{
		if (Tile.ControlKind != EVerseControlKind::If
			|| !Tile.ControlRegions.IsValidIndex(ControlRegionIndex))
		{
			OutError = LOCTEXT("NoConditionSyntax", "This tile has no editable if-condition syntax.");
			return false;
		}
		const auto& Condition = Tile.ControlRegions[ControlRegionIndex];
		if (Condition.Kind != EVerseControlRegionKind::Condition
			|| !Condition.OpeningPunctuationRange.IsSet())
		{
			OutError = LOCTEXT("NoConditionDelimiter", "The if condition has no exact editable delimiter.");
			return false;
		}
		const bool bWantParentheses = Value == TEXTVIEW("Parentheses");
		const bool bWantColon = Value == TEXTVIEW("Colon");
		if (!bWantParentheses && !bWantColon)
		{
			OutError = LOCTEXT("UnsupportedConditionSyntax", "That if-condition syntax is not supported.");
			return false;
		}
		FVerseVisualClauseDescriptor ConditionClause;
		if (GetOwnedClause(Tile, ControlRegionIndex, ConditionClause) == nullptr
			|| !AddClauseLayoutEdits(
				Session, ConditionClause, bWantColon, Style, Edits, OutError))
		{
			return false;
		}
		if (bWantParentheses
			&& Condition.Syntax.Delimiter == EVerseClauseDelimiter::Colon)
		{
			Edits.Add(Edit(Session.GetRevision(),
				FVerseByteRange::FromBounds(
					Tile.Range.BeginByte, Condition.OpeningPunctuationRange.EndByte()),
				TEXTVIEW("if (")));
			const int32 CloseByte = Condition.Syntax.TrailingWhitespaceRange.IsSet()
				? Condition.Syntax.TrailingWhitespaceRange.BeginByte
				: Condition.InteriorRange.EndByte();
			Edits.Add(Edit(Session.GetRevision(), {CloseByte, 0}, TEXTVIEW(")")));
		}
		else if (bWantColon
			&& Condition.Syntax.Delimiter == EVerseClauseDelimiter::Parentheses)
		{
			if (!Condition.ClosingPunctuationRange.IsSet())
			{
				OutError = LOCTEXT("MissingConditionClose", "The parenthesized if condition has no exact closing-parenthesis range.");
				return false;
			}
			Edits.Add(Edit(Session.GetRevision(),
				FVerseByteRange::FromBounds(
					Tile.Range.BeginByte, Condition.OpeningPunctuationRange.EndByte()),
				TEXTVIEW("if:")));

			const auto* Body = Tile.ControlRegions.FindByPredicate(
				[](const auto& Region)
				{
					return Region.Kind == EVerseControlRegionKind::Body;
				});
			if (Body != nullptr
				&& Body->Syntax.Keyword == EVerseClauseKeyword::None
				&& Body->OpeningPunctuationRange.IsSet())
			{
				FString ThenPrefix = FVerseSyntaxEmitter::LineEnding(Style)
					+ Condition.Syntax.IndentationPrefix + TEXT("then");
				if (Body->Syntax.Delimiter == EVerseClauseDelimiter::Braces)
				{
					ThenPrefix += TEXT(" ");
				}
				Edits.Add(Edit(Session.GetRevision(),
					FVerseByteRange::FromBounds(
						Condition.ClosingPunctuationRange.BeginByte,
						Body->OpeningPunctuationRange.BeginByte),
					ThenPrefix));
			}
			else
			{
				Edits.Add(Edit(Session.GetRevision(),
					Condition.ClosingPunctuationRange, FStringView()));
			}
		}
	}
	else if (Control == EVerseSyntaxControlKind::BodyDelimiter)
	{
		const FVerseVisualClauseDescriptor* Clause = GetOwnedClause(Tile, ControlRegionIndex, ScratchClause);
		if (Clause == nullptr || !Clause->OpeningPunctuationRange.IsSet())
		{
			OutError = LOCTEXT("NoBodyDelimiter", "This tile has no editable body delimiter.");
			return false;
		}
		const bool bWantBraces = Value == TEXTVIEW("Braces");
		const bool bWantColon = Value == TEXTVIEW("Colon");
		if (!bWantBraces && !bWantColon)
		{
			OutError = LOCTEXT("UnsupportedBodyDelimiter", "That body syntax is not supported here.");
			return false;
		}
		if (bWantBraces && Clause->Syntax.Delimiter != EVerseClauseDelimiter::Braces)
		{
			Edits.Add(Edit(Session.GetRevision(), Clause->OpeningPunctuationRange, TEXTVIEW(" {")));
			const bool bMultiline =
				Clause->Syntax.Layout == EVerseSyntaxLayout::Multiline;
			// Close immediately after the clause's last owned content. Any trailing
			// blank lines belong outside the new brace body and remain byte-exact.
			const int32 ClosingByte = bMultiline
				&& Clause->Syntax.TrailingWhitespaceRange.IsSet()
					? Clause->Syntax.TrailingWhitespaceRange.BeginByte
					: Clause->InteriorRange.EndByte();
			const FString Closing = bMultiline
				? FVerseSyntaxEmitter::LineEnding(Style)
					+ Clause->Syntax.IndentationPrefix + TEXT("}")
				: TEXT(" }");
			const auto* ElseRegion = Tile.ControlRegions.FindByPredicate(
				[](const auto& Region)
				{
					return Region.Kind == EVerseControlRegionKind::Else;
				});
			const bool bTrueBodyBeforeElse = bMultiline
				&& Tile.ControlKind == EVerseControlKind::If
				&& Tile.ControlRegions.IsValidIndex(ControlRegionIndex)
				&& Tile.ControlRegions[ControlRegionIndex].Kind
					== EVerseControlRegionKind::Body
				&& ElseRegion != nullptr;
			if (bTrueBodyBeforeElse
				&& Clause->Syntax.TrailingWhitespaceRange.IsSet())
			{
				// `else` is structurally adjacent to the completed true body. Do not
				// leave blank-line trivia between the new closing brace and `else`.
				const int32 GapEnd = ElseRegion->Syntax.KeywordRange.IsSet()
					? ElseRegion->Syntax.KeywordRange.BeginByte
					: Clause->Syntax.TrailingWhitespaceRange.EndByte();
				Edits.Add(Edit(
					Session.GetRevision(),
					FVerseTextRange(
						Session.GetRevision(),
						FVerseByteRange::FromBounds(
							Clause->Syntax.TrailingWhitespaceRange.BeginByte,
							GapEnd)),
					Closing + FVerseSyntaxEmitter::LineEnding(Style)
						+ Clause->Syntax.IndentationPrefix));
			}
			else
			{
				Edits.Add(Edit(Session.GetRevision(), {ClosingByte, 0}, Closing));
			}
		}
		else if (bWantColon && Clause->Syntax.Delimiter == EVerseClauseDelimiter::Braces)
		{
			if (!Clause->ClosingPunctuationRange.IsSet())
			{
				OutError = LOCTEXT("MissingClosingBrace", "The body does not have an exact closing brace range.");
				return false;
			}
			Edits.Add(Edit(Session.GetRevision(), Clause->OpeningPunctuationRange, TEXTVIEW(":")));
			Edits.Add(Edit(Session.GetRevision(), Clause->ClosingPunctuationRange, FStringView()));
		}
	}
	else if (Control == EVerseSyntaxControlKind::BodyLayout)
	{
		const FVerseVisualClauseDescriptor* Clause = GetOwnedClause(Tile, ControlRegionIndex, ScratchClause);
		if (Clause == nullptr)
		{
			OutError = LOCTEXT("NoBodyLayout", "This body has no expressions to lay out.");
			return false;
		}
		const bool bMultiline = Value == TEXTVIEW("Multiline");
		const bool bInline = Value == TEXTVIEW("Inline");
		if (!bMultiline && !bInline)
		{
			OutError = LOCTEXT("UnsupportedBodyLayout", "That body layout is not supported.");
			return false;
		}
		if (!AddClauseLayoutEdits(Session, *Clause, bMultiline, Style, Edits, OutError))
		{
			return false;
		}
	}
	else if (Control == EVerseSyntaxControlKind::BracePlacement)
	{
		const FVerseVisualClauseDescriptor* Clause = GetOwnedClause(Tile, ControlRegionIndex, ScratchClause);
		if (Clause == nullptr
			|| Clause->Syntax.Delimiter != EVerseClauseDelimiter::Braces
			|| !Clause->OpeningPunctuationRange.IsSet()
			|| !Tile.HeaderRange.IsSet())
		{
			OutError = LOCTEXT("NoBracePlacement", "This tile has no editable opening-brace placement.");
			return false;
		}
		const FVerseTextRange Gap(Session.GetRevision(), FVerseByteRange::FromBounds(
			Tile.HeaderRange.EndByte(), Clause->OpeningPunctuationRange.BeginByte));
		const FString Replacement = Value == TEXTVIEW("Next line")
			? FVerseSyntaxEmitter::LineEnding(Style) + Clause->Syntax.IndentationPrefix
			: TEXT(" ");
		if (!AddWhitespaceEdit(Session, Gap, Replacement, Edits, OutError))
		{
			return false;
		}
	}
	else if (Control == EVerseSyntaxControlKind::LineEnding)
	{
		const FVerseVisualClauseDescriptor* Clause = GetOwnedClause(Tile, ControlRegionIndex, ScratchClause);
		if (Clause == nullptr)
		{
			OutError = LOCTEXT("NoLineEndingClause", "This tile has no owned clause line endings.");
			return false;
		}
		const FString NewLineEnding = Value == TEXTVIEW("CRLF") ? TEXT("\r\n")
			: Value == TEXTVIEW("CR") ? TEXT("\r") : TEXT("\n");
		TArray<FVerseTextRange> Ranges;
		GatherClauseWhitespaceRanges(*Clause, Ranges);
		for (const FVerseTextRange Range : Ranges)
		{
			const FString Existing = Decode(Session, Range);
			if (!IsWhitespaceOnly(Existing))
			{
				OutError = LOCTEXT("LineEndingOwnsTrivia", "A line-ending range contains preserved trivia and cannot be rewritten safely.");
				return false;
			}
			const FString Replacement = ReplaceLineEndings(Existing, NewLineEnding);
			if (Replacement != Existing)
			{
				Edits.Add(Edit(Session.GetRevision(), Range, Replacement));
			}
		}
	}
	else if (Control == EVerseSyntaxControlKind::Indentation)
	{
		const FVerseVisualClauseDescriptor* Clause = GetOwnedClause(Tile, ControlRegionIndex, ScratchClause);
		if (Clause == nullptr || !Clause->InteriorRange.IsSet())
		{
			OutError = LOCTEXT("NoIndentationClause", "This tile has no owned block indentation.");
			return false;
		}
		FString NewUnit;
		if (Value == TEXTVIEW("Tabs"))
		{
			NewUnit = TEXT("\t");
		}
		else
		{
			const int32 Space = FString(Value).Find(TEXT(" "));
			const TOptional<int32> Width = ParseInteger(
				Space == INDEX_NONE ? Value : FStringView(Value.GetData(), Space));
			if (!Width.IsSet() || Width.GetValue() < 1 || Width.GetValue() > 8)
			{
				OutError = LOCTEXT("InvalidIndentation", "Indentation must be tabs or 1–8 spaces.");
				return false;
			}
			NewUnit = FString::ChrN(Width.GetValue(), TEXT(' '));
		}
		const FString OldUnit = Clause->Syntax.IndentationUnit.IsEmpty()
			? Style.IndentationUnit : Clause->Syntax.IndentationUnit;
		if (OldUnit == NewUnit)
		{
			OutError = LOCTEXT("IndentationAlreadyUsed", "The selected indentation is already in use.");
			return false;
		}
		const FTCHARToUTF8 ParentUtf8(*Clause->Syntax.IndentationPrefix);
		const FTCHARToUTF8 OldUtf8(*OldUnit);
		const FUtf8StringView Source = Session.GetParseSnapshot().GetDocument()->GetOriginalUtf8View();
		auto AddLineEdit = [&](int32 LineStart)
		{
			const int32 UnitStart = LineStart + ParentUtf8.Length();
			if (UnitStart >= Clause->InteriorRange.EndByte()
				|| UnitStart + OldUtf8.Length() > Source.Len())
			{
				return;
			}
			for (int32 Offset = 0; Offset < OldUtf8.Length(); ++Offset)
			{
				if (Source[UnitStart + Offset]
					!= static_cast<UTF8CHAR>(OldUtf8.Get()[Offset]))
				{
					return;
				}
			}
			Edits.Add(Edit(Session.GetRevision(),
				{UnitStart, OldUtf8.Length()}, NewUnit));
		};
		int32 LineStart = Clause->InteriorRange.BeginByte;
		if (LineStart == 0 || (Source[LineStart - 1] != static_cast<UTF8CHAR>('\n')
			&& Source[LineStart - 1] != static_cast<UTF8CHAR>('\r')))
		{
			while (LineStart > 0 && Source[LineStart - 1] != static_cast<UTF8CHAR>('\n')
				&& Source[LineStart - 1] != static_cast<UTF8CHAR>('\r'))
			{
				--LineStart;
			}
		}
		AddLineEdit(LineStart);
		for (int32 Byte = Clause->InteriorRange.BeginByte; Byte < Clause->InteriorRange.EndByte(); ++Byte)
		{
			if (Source[Byte] == static_cast<UTF8CHAR>('\n')
				|| Source[Byte] == static_cast<UTF8CHAR>('\r'))
			{
				if (Source[Byte] == static_cast<UTF8CHAR>('\r')
					&& Byte + 1 < Source.Len()
					&& Source[Byte + 1] == static_cast<UTF8CHAR>('\n'))
				{
					++Byte;
				}
				AddLineEdit(Byte + 1);
			}
		}
	}
	else if (Control == EVerseSyntaxControlKind::TypeColonSpacing)
	{
		if (!Tile.NameRange.IsSet() || !Tile.TypeRange.IsSet())
		{
			OutError = LOCTEXT("NoTypeColon", "This definition has no editable type colon.");
			return false;
		}
		const FUtf8StringView Source = Session.GetParseSnapshot().GetDocument()->GetOriginalUtf8View();
		const int32 Colon = FindByte(Source, static_cast<UTF8CHAR>(':'),
			Tile.NameRange.EndByte(), Tile.TypeRange.BeginByte);
		if (Colon == INDEX_NONE)
		{
			OutError = LOCTEXT("TypeColonNotFound", "The exact type colon could not be located.");
			return false;
		}
		const FString Gap = Value == TEXTVIEW("Standard") ? TEXT(" ") : FString();
		if (!AddWhitespaceEdit(Session,
			FVerseTextRange(Session.GetRevision(), FVerseByteRange::FromBounds(Tile.NameRange.EndByte(), Colon)),
			Gap, Edits, OutError)
			|| !AddWhitespaceEdit(Session,
				FVerseTextRange(Session.GetRevision(), FVerseByteRange::FromBounds(Colon + 1, Tile.TypeRange.BeginByte)),
				Gap, Edits, OutError))
		{
			return false;
		}
	}
	else if (Control == EVerseSyntaxControlKind::InitializerSpacing)
	{
		int32 ValueBegin = INDEX_NONE;
		for (const FVerseVisualSocket& Input : Tile.GetValueInputs())
		{
			if (Input.InlineLiteralRange.IsSet())
			{
				ValueBegin = Input.InlineLiteralRange.BeginByte;
				break;
			}
		}
		if (ValueBegin == INDEX_NONE && !Tile.Children.IsEmpty())
		{
			ValueBegin = Tile.Children[0].Range.BeginByte;
		}
		const int32 SearchBegin = Tile.TypeRange.IsSet()
			? Tile.TypeRange.EndByte() : Tile.NameRange.EndByte();
		if (ValueBegin == INDEX_NONE || SearchBegin > ValueBegin)
		{
			OutError = LOCTEXT("NoInitializerSpacing", "This definition has no editable initializer gap.");
			return false;
		}
		const FUtf8StringView Source = Session.GetParseSnapshot().GetDocument()->GetOriginalUtf8View();
		const int32 Equal = FindLastByte(Source, static_cast<UTF8CHAR>('='), SearchBegin, ValueBegin);
		if (Equal == INDEX_NONE)
		{
			OutError = LOCTEXT("InitializerOperatorNotFound", "The exact initializer operator could not be located.");
			return false;
		}
		const int32 OperatorBegin = Equal > SearchBegin
			&& Source[Equal - 1] == static_cast<UTF8CHAR>(':') ? Equal - 1 : Equal;
		const FString Gap = Value == TEXTVIEW("Standard") ? TEXT(" ") : FString();
		if (!AddWhitespaceEdit(Session,
			FVerseTextRange(Session.GetRevision(), FVerseByteRange::FromBounds(SearchBegin, OperatorBegin)),
			Gap, Edits, OutError)
			|| !AddWhitespaceEdit(Session,
				FVerseTextRange(Session.GetRevision(), FVerseByteRange::FromBounds(Equal + 1, ValueBegin)),
				Gap, Edits, OutError))
		{
			return false;
		}
	}
	else if (Control == EVerseSyntaxControlKind::CallSpacing)
	{
		if (!Tile.OperatorRange.IsSet())
		{
			OutError = LOCTEXT("NoCallSpacing", "This call has no exact callee range.");
			return false;
		}
		const FUtf8StringView Source = Session.GetParseSnapshot().GetDocument()->GetOriginalUtf8View();
		const int32 OpenParen = FindByte(Source, static_cast<UTF8CHAR>('('),
			Tile.OperatorRange.EndByte(), Tile.Range.EndByte());
		const int32 OpenBracket = FindByte(Source, static_cast<UTF8CHAR>('['),
			Tile.OperatorRange.EndByte(), Tile.Range.EndByte());
		const int32 Open = OpenParen == INDEX_NONE ? OpenBracket
			: OpenBracket == INDEX_NONE ? OpenParen : FMath::Min(OpenParen, OpenBracket);
		const UTF8CHAR Closing = Open != INDEX_NONE
			&& Source[Open] == static_cast<UTF8CHAR>('[')
			? static_cast<UTF8CHAR>(']') : static_cast<UTF8CHAR>(')');
		const int32 Close = Open == INDEX_NONE ? INDEX_NONE
			: FindLastByte(Source, Closing, Open + 1, Tile.Range.EndByte());
		if (Open == INDEX_NONE || Close == INDEX_NONE)
		{
			OutError = LOCTEXT("CallDelimitersNotFound", "The exact call delimiters could not be located.");
			return false;
		}
		const bool bWrapped = Value == TEXTVIEW("Wrapped");
		const bool bSpaced = Value == TEXTVIEW("Spaced");
		const bool bStandard = Value == TEXTVIEW("Standard");
		const FString LineIndent = FVerseSyntaxEmitter::LineEnding(Style) + Style.IndentationUnit;
		const FString Inner = bWrapped ? LineIndent : bSpaced ? TEXT(" ") : FString();
		const FString Between = bWrapped ? TEXT(",") + LineIndent
			: (bStandard || bSpaced) ? TEXT(", ") : TEXT(",");
		TArray<FVerseTextRange> Arguments;
		for (const FVerseVisualTile& Child : Tile.Children)
		{
			Arguments.Add(Child.Range);
		}
		for (const FVerseVisualSocket& Input : Tile.GetValueInputs())
		{
			if (Input.InlineLiteralRange.IsSet())
			{
				Arguments.Add(Input.InlineLiteralRange);
			}
		}
		Arguments.Sort([](const FVerseTextRange& Left, const FVerseTextRange& Right)
		{
			return Left.BeginByte < Right.BeginByte;
		});
		Arguments.SetNum(Algo::UniqueBy(Arguments, [](const FVerseTextRange& Range)
		{
			return Range.BeginByte;
		}));
		if (!AddWhitespaceEdit(Session,
			FVerseTextRange(Session.GetRevision(), FVerseByteRange::FromBounds(
				Tile.OperatorRange.EndByte(), Open)),
			bSpaced ? TEXTVIEW(" ") : FStringView(), Edits, OutError))
		{
			return false;
		}
		if (Arguments.IsEmpty())
		{
			if (!AddWhitespaceEdit(Session,
				FVerseTextRange(Session.GetRevision(), FVerseByteRange::FromBounds(Open + 1, Close)),
				bSpaced ? TEXTVIEW("  ") : FStringView(), Edits, OutError))
			{
				return false;
			}
		}
		else
		{
			if (!AddWhitespaceEdit(Session,
				FVerseTextRange(Session.GetRevision(), FVerseByteRange::FromBounds(Open + 1, Arguments[0].BeginByte)),
				Inner, Edits, OutError))
			{
				return false;
			}
			for (int32 Index = 0; Index + 1 < Arguments.Num(); ++Index)
			{
				const FVerseTextRange Gap(Session.GetRevision(), FVerseByteRange::FromBounds(
					Arguments[Index].EndByte(), Arguments[Index + 1].BeginByte));
				if (!IsWhitespaceAndOptionalComma(Decode(Session, Gap)))
				{
					OutError = LOCTEXT("CallSeparatorOwnsTrivia", "An argument separator contains preserved trivia and cannot be rewritten safely.");
					return false;
				}
				Edits.Add(Edit(Session.GetRevision(), Gap, Between));
			}
			const FString Trailing = bWrapped
				? FVerseSyntaxEmitter::LineEnding(Style) : Inner;
			if (!AddWhitespaceEdit(Session,
				FVerseTextRange(Session.GetRevision(), FVerseByteRange::FromBounds(Arguments.Last().EndByte(), Close)),
				Trailing, Edits, OutError))
			{
				return false;
			}
		}
	}
	else if (Control == EVerseSyntaxControlKind::CommentStyle)
	{
		const FString Source = Decode(Session, Tile.Range);
		if (Source.Contains(TEXT("\n")) || Source.Contains(TEXT("\r")))
		{
			OutError = LOCTEXT("MultilineCommentConversion", "Multiline comment conversion is preserved because it could move or reinterpret content.");
			return false;
		}
		if (Value == TEXTVIEW("Block") && Source.StartsWith(TEXT("#"))
			&& !Source.StartsWith(TEXT("<#")))
		{
			Edits.Add(Edit(Session.GetRevision(), Tile.Range,
				TEXT("<#") + Source.Mid(1) + TEXT("#>")));
		}
		else if (Value == TEXTVIEW("Line") && Source.StartsWith(TEXT("<#"))
			&& Source.EndsWith(TEXT("#>")))
		{
			Edits.Add(Edit(Session.GetRevision(), Tile.Range,
				TEXT("#") + Source.Mid(2, Source.Len() - 4)));
		}
		else
		{
			OutError = LOCTEXT("UnsupportedCommentConversion", "That comment conversion is already in use or is not source-safe.");
			return false;
		}
	}
	else if (Control == EVerseSyntaxControlKind::CommentOpenerSpacing)
	{
		const FString Source = Decode(Session, Tile.Range);
		int32 OpenerLength = Source.StartsWith(TEXT("<#")) ? 2
			: Source.StartsWith(TEXT("#")) ? 1 : 0;
		if (OpenerLength == 0)
		{
			OutError = LOCTEXT("UnsupportedCommentSpacing", "This comment form is preserved exactly.");
			return false;
		}
		int32 Content = OpenerLength;
		while (Content < Source.Len() && (Source[Content] == TEXT(' ') || Source[Content] == TEXT('\t')))
		{
			++Content;
		}
		const FTCHARToUTF8 Prefix(*Source.Left(OpenerLength));
		const FTCHARToUTF8 Existing(*Source.Mid(OpenerLength, Content - OpenerLength));
		Edits.Add(Edit(Session.GetRevision(), {
			Tile.Range.BeginByte + Prefix.Length(), Existing.Length()},
			Value == TEXTVIEW("One space") ? TEXTVIEW(" ") : FStringView()));
	}
	else
	{
		OutError = LOCTEXT("FormattingControlPending", "This syntax control is not available for the selected tile.");
		return false;
	}

	return ValidateAndCommit(Session, Edits, OutError);
}

#undef LOCTEXT_NAMESPACE
