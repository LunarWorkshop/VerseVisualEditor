#include "Editing/VerseClauseEditing.h"

#include "Internationalization/Text.h"
#include "Document/VerseDocumentSession.h"
#include "Editing/VerseExpressionActions.h"
#include "Editing/VerseFormattingStyle.h"
#include "VisualModel/VerseFunctionNavigation.h"
#include "VerseParseSnapshotBuilder.h"

#define LOCTEXT_NAMESPACE "VerseClauseEditing"

namespace
{
	FString Decode(const FVerseDocumentSession& Session, FVerseByteRange Range)
	{
		return Range.IsSet()
			? Session.GetParseSnapshot().GetDocument()->DecodeOriginalRange(Range)
			: FString();
	}

	FString DetectLineEnding(FUtf8StringView Source)
	{
		return Source.Find(UTF8TEXTVIEW("\r\n")) != INDEX_NONE
			? TEXT("\r\n")
			: TEXT("\n");
	}

	bool IsClauseWhitespaceOnly(FStringView Text)
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

	FString IndentationAt(FUtf8StringView Source, int32 ByteOffset)
	{
		ByteOffset = FMath::Clamp(ByteOffset, 0, Source.Len());
		int32 LineBegin = ByteOffset;
		while (LineBegin > 0
			&& Source[LineBegin - 1] != static_cast<UTF8CHAR>('\n')
			&& Source[LineBegin - 1] != static_cast<UTF8CHAR>('\r'))
		{
			--LineBegin;
		}
		int32 End = LineBegin;
		while (End < Source.Len()
			&& (Source[End] == static_cast<UTF8CHAR>(' ')
				|| Source[End] == static_cast<UTF8CHAR>('\t')))
		{
			++End;
		}
		const FUTF8ToTCHAR Converted(
			reinterpret_cast<const ANSICHAR*>(Source.GetData() + LineBegin),
			End - LineBegin);
		return FString(Converted.Length(), Converted.Get());
	}

	FUtf8String BuildCandidate(
		const FVerseDocumentSession& Session,
		TConstArrayView<FVerseDocumentEdit> Edits,
		FText& OutError)
	{
		FVerseEditBuffer Buffer(Session.GetParseSnapshot().GetDocument());
		TArray<FVerseDocumentEdit> Sorted(Edits);
		Sorted.Sort([](const FVerseDocumentEdit& Left, const FVerseDocumentEdit& Right)
		{
			return Left.Range.BeginByte > Right.Range.BeginByte;
		});
		for (const FVerseDocumentEdit& Edit : Sorted)
		{
			if (!Buffer.Replace(Edit.Range, Edit.Replacement, OutError))
			{
				return {};
			}
		}
		return Buffer.Materialize();
	}

	const FVerseVisualExpressionDescriptor::FControlRegion* FindRegion(
		TConstArrayView<FVerseVisualTile> Tiles,
		int32 OpeningByte)
	{
		for (const FVerseVisualTile& Tile : Tiles)
		{
			for (const FVerseVisualExpressionDescriptor::FControlRegion& Region :
				Tile.ControlRegions)
			{
				const int32 RegionOpening = Region.OpeningPunctuationRange.IsSet()
					? Region.OpeningPunctuationRange.BeginByte
					: Region.InteriorRange.BeginByte;
				if (RegionOpening == OpeningByte)
				{
					return &Region;
				}
			}
			if (const auto* Nested = FindRegion(Tile.Children, OpeningByte))
			{
				return Nested;
			}
		}
		return nullptr;
	}

	const FVerseVisualClauseDescriptor* FindBodyClause(
		TConstArrayView<FVerseVisualTile> Tiles,
		int32 OpeningByte)
	{
		for (const FVerseVisualTile& Tile : Tiles)
		{
			if (Tile.BodyClause.InteriorRange.IsSet())
			{
				const int32 CandidateOpening = Tile.BodyClause.OpeningPunctuationRange.IsSet()
					? Tile.BodyClause.OpeningPunctuationRange.BeginByte
					: Tile.BodyClause.InteriorRange.BeginByte;
				if (CandidateOpening == OpeningByte)
				{
					return &Tile.BodyClause;
				}
			}
			if (const auto* Nested = FindBodyClause(Tile.Children, OpeningByte))
			{
				return Nested;
			}
		}
		return nullptr;
	}

	FString RebaseMultilineSource(
		FString Source,
		FStringView LineEnding,
		FStringView StatementIndent)
	{
		Source.ReplaceInline(TEXT("\r\n"), TEXT("\n"));
		Source.ReplaceInline(TEXT("\r"), TEXT("\n"));
		Source.ReplaceInline(
			TEXT("\n"), *(FString(LineEnding) + FString(StatementIndent)));
		return Source;
	}

	bool BuildDestinationExpressionSource(
		const FVerseExpressionAction& Action,
		FStringView BoundExpressionSource,
		const FVerseFormattingStyleProfile& Style,
		FString& OutSource,
		FText& OutError)
	{
		if (Action.StructuralKind != EVerseStructuralExpressionKind::If)
		{
			return BuildVerseExpressionActionSource(
				Action, BoundExpressionSource, OutSource, OutError);
		}
		FVerseFormattingStyleProfile CreationStyle = Style;
		// New block punctuation is an explicit project/user default. Local source
		// still supplies indentation and line endings, but must not silently turn
		// a requested colon block back into braces (or vice versa).
		CreationStyle.BodyDelimiter =
			FVerseFormattingStyleResolver::ResolveDefaults().BodyDelimiter;
		OutSource = FVerseSyntaxEmitter::IfTemplate(CreationStyle);
		return true;
	}

	const FVerseVisualTile* FindIfAtOpeningByte(
		TConstArrayView<FVerseVisualTile> Tiles,
		int32 OpeningByte)
	{
		for (const FVerseVisualTile& Tile : Tiles)
		{
			if (Tile.Range.BeginByte == OpeningByte
				&& Tile.ExpressionKind == EVerseExpressionKind::Control
				&& Tile.ControlKind == EVerseControlKind::If)
			{
				return &Tile;
			}
			if (const FVerseVisualTile* Nested =
				FindIfAtOpeningByte(Tile.Children, OpeningByte))
			{
				return Nested;
			}
		}
		return nullptr;
	}

	const FVerseVisualTile* FindIfWithElse(
		TConstArrayView<FVerseVisualTile> Tiles,
		int32 OpeningByte)
	{
		const FVerseVisualTile* IfTile = FindIfAtOpeningByte(Tiles, OpeningByte);
		return IfTile != nullptr
			&& IfTile->ControlRegions.ContainsByPredicate(
				[](const auto& Region)
				{
					return Region.Kind == EVerseControlRegionKind::Else
						&& Region.Items.Num() == 1;
				})
			? IfTile : nullptr;
	}

	FString TileKindName(EVerseVisualTileKind Kind)
	{
		const UEnum* Enum = StaticEnum<EVerseVisualTileKind>();
		return Enum != nullptr
			? Enum->GetNameStringByValue(static_cast<int64>(Kind))
			: TEXT("Unrecognized Tile Kind");
	}

	FString SourceSubstringInRange(
		FUtf8StringView Source,
		FVerseByteRange Range,
		int32 MaxCharacters)
	{
		if (!Range.IsSet()
			|| Range.BeginByte < 0
			|| Range.EndByte() > Source.Len())
		{
			return TEXT("<range is outside candidate source>");
		}
		const FUtf8StringView Substring = Source.Mid(Range.BeginByte, Range.NumBytes);
		const FUTF8ToTCHAR Converted(
			reinterpret_cast<const ANSICHAR*>(Substring.GetData()),
			Substring.Len());
		FString Result(Converted.Length(), Converted.Get());
		Result.LeftInline(MaxCharacters);
		Result.ReplaceInline(TEXT("\r"), TEXT("\\r"));
		Result.ReplaceInline(TEXT("\n"), TEXT("\\n"));
		Result.ReplaceInline(TEXT("\t"), TEXT("\\t"));
		return Result;
	}

	void AppendBodyClauseSearchTiles(
		FString& Out,
		TConstArrayView<FVerseVisualTile> Tiles,
		FUtf8StringView CandidateSource)
	{
		for (const FVerseVisualTile& Tile : Tiles)
		{
			Out += FString::Printf(
				TEXT("\n%s: bodyRange=[%d,%d) text=\"%s\""),
				*TileKindName(Tile.Kind),
				Tile.BodyClause.InteriorRange.BeginByte,
				Tile.BodyClause.InteriorRange.EndByte(),
				*SourceSubstringInRange(
					CandidateSource,
					Tile.BodyClause.InteriorRange,
					40));
			AppendBodyClauseSearchTiles(Out, Tile.Children, CandidateSource);
		}
	}

	void AppendRegionSearchTiles(
		FString& Out,
		TConstArrayView<FVerseVisualTile> Tiles,
		int32 Depth)
	{
		for (int32 Index = 0; Index < Tiles.Num(); ++Index)
		{
			const FVerseVisualTile& Tile = Tiles[Index];
			Out += FString::Printf(
				TEXT("\n%stile[%d]: kind=%d controlKind=%d range=[%d,%d) regions=%d"),
				*FString::ChrN(Depth * 2, TEXT(' ')),
				Index,
				static_cast<int32>(Tile.Kind),
				static_cast<int32>(Tile.ControlKind),
				Tile.Range.BeginByte,
				Tile.Range.EndByte(),
				Tile.ControlRegions.Num());
			for (int32 RegionIndex = 0; RegionIndex < Tile.ControlRegions.Num(); ++RegionIndex)
			{
				const FVerseVisualExpressionDescriptor::FControlRegion& Region =
					Tile.ControlRegions[RegionIndex];
				const int32 RegionOpening = Region.OpeningPunctuationRange.IsSet()
					? Region.OpeningPunctuationRange.BeginByte
					: Region.InteriorRange.BeginByte;
				Out += FString::Printf(
					TEXT("\n%s  region[%d]: kind=%d opening=%d items=%d"),
					*FString::ChrN(Depth * 2, TEXT(' ')),
					RegionIndex,
					static_cast<int32>(Region.Kind),
					RegionOpening,
					Region.Items.Num());
			}
			AppendRegionSearchTiles(Out, Tile.Children, Depth + 1);
		}
	}

	FString SourceSubstringAt(FUtf8StringView Source, int32 BeginByte)
	{
		if (BeginByte < 0 || BeginByte > Source.Len())
		{
			return TEXT("<opening byte is outside candidate source>");
		}
		const FUtf8StringView Substring = Source.Mid(BeginByte);
		const FUTF8ToTCHAR Converted(
			reinterpret_cast<const ANSICHAR*>(Substring.GetData()),
			Substring.Len());
		FString Result(Converted.Length(), Converted.Get());
		Result.LeftInline(40);
		Result.ReplaceInline(TEXT("\r"), TEXT("\\r"));
		Result.ReplaceInline(TEXT("\n"), TEXT("\\n"));
		Result.ReplaceInline(TEXT("\t"), TEXT("\\t"));
		return Result;
	}

	bool ValidateCandidate(
		const FVerseDocumentSession& Session,
		const FVerseVisualClauseDescriptor& Clause,
		TConstArrayView<FVerseDocumentEdit> Edits,
		int32 ExpectedItemCount,
		FText& OutError)
	{
		const FUtf8String Candidate = BuildCandidate(Session, Edits, OutError);
		if (Candidate.IsEmpty() && Session.GetCurrentUtf8().Len() != 0)
		{
			return false;
		}
		const TConstArrayView<uint8> Bytes(
			reinterpret_cast<const uint8*>(*Candidate), Candidate.Len());
		const TSharedPtr<const FVerseDocument> Document =
			FVerseDocument::CreateFromBytes(Bytes, OutError);
		if (!Document.IsValid())
		{
			return false;
		}
		const FVerseParseSnapshot Snapshot =
			FVerseParseSnapshotBuilder::Build(Document.ToSharedRef());
		const TArray<FVerseVisualTile> Tiles = FVerseVisualTileBuilder::Build(Snapshot);
		const int32 OpeningByte = Clause.OpeningPunctuationRange.IsSet()
			? Clause.OpeningPunctuationRange.BeginByte
			: Clause.InteriorRange.BeginByte;
		const FVerseVisualClauseDescriptor* MatchedBodyClause =
			FindBodyClause(Tiles, OpeningByte);
		if (MatchedBodyClause != nullptr)
		{
			if (MatchedBodyClause->Items.Num() == ExpectedItemCount)
			{
				return true;
			}
		}
		const TArray<FVerseFunctionNavigationItem> Functions =
			FVerseFunctionNavigationBuilder::Build(Tiles, Snapshot);
		const FVerseVisualExpressionDescriptor::FControlRegion* MatchedRegion = nullptr;
		for (const FVerseFunctionNavigationItem& Function : Functions)
		{
			if (const auto* Region = FindRegion(Function.GraphTiles, OpeningByte))
			{
				MatchedRegion = Region;
				if (Region->Items.Num() == ExpectedItemCount)
				{
					return true;
				}
			}
		}

		if (MatchedBodyClause == nullptr && MatchedRegion == nullptr)
		{
			FString Message = FString::Printf(
				TEXT("The edit was rejected because the edited Verse clause could not be found after reparsing the proposed source.\nCandidate source: \"%s\"\nSearch for byte %d in the following body-clause tiles:"),
				*SourceSubstringAt(FUtf8StringView(*Candidate, Candidate.Len()), OpeningByte),
				OpeningByte);
			AppendBodyClauseSearchTiles(
				Message,
				Tiles,
				FUtf8StringView(*Candidate, Candidate.Len()));
			Message += TEXT("\nFunctions searched for a matching control region:");
			if (Functions.IsEmpty())
			{
				Message += TEXT("\n<none>");
			}
			for (int32 FunctionIndex = 0; FunctionIndex < Functions.Num(); ++FunctionIndex)
			{
				const FVerseFunctionNavigationItem& Function = Functions[FunctionIndex];
				Message += FString::Printf(
					TEXT("\nfunction[%d]: scope=%s name=%s range=[%d,%d) graphTiles=%d"),
					FunctionIndex,
					*FString::Join(Function.ScopePath, TEXT(".")),
					*Function.Name,
					Function.FunctionRange.BeginByte,
					Function.FunctionRange.EndByte(),
					Function.GraphTiles.Num());
				AppendRegionSearchTiles(Message, Function.GraphTiles, 1);
			}
			OutError = FText::FromString(MoveTemp(Message));
			return false;
		}

		const int32 ActualItemCount = MatchedBodyClause != nullptr
			? MatchedBodyClause->Items.Num()
			: MatchedRegion->Items.Num();
		OutError = FText::Format(
			LOCTEXT(
				"ClauseItemCountMismatch",
				"The edit was rejected because the edited Verse clause had {0} ordered items before the edit, {1} afterward, and was expected to have {2}."),
			FText::AsNumber(Clause.Items.Num()),
			FText::AsNumber(ActualItemCount),
			FText::AsNumber(ExpectedItemCount));
		return false;
	}

	FVerseDocumentEdit MakeEdit(
		FVerseDocumentRevision Revision,
		FVerseByteRange Range,
		FStringView Replacement)
	{
		return {
			FVerseTextRange(Revision, Range),
			FUtf8String(Replacement)};
	}
}

bool FVerseClauseEditing::InsertExpression(
	FVerseDocumentSession& Session,
	const FVerseVisualClauseDescriptor& Clause,
	int32 InsertIndex,
	const FVerseExpressionAction& Action,
	FText& OutError,
	FVerseTextRange* OutInsertedRange,
	FStringView BoundExpressionSource)
{
	if (Clause.InteriorRange.Revision != Session.GetRevision()
		|| InsertIndex < 0 || InsertIndex > Clause.Items.Num())
	{
		OutError = LOCTEXT("InvalidClauseInsertion", "The insertion point is no longer valid.");
		return false;
	}
	const FUtf8StringView Source(*Session.GetCurrentUtf8(), Session.GetCurrentUtf8().Len());
	const FVerseFormattingStyleProfile Style = FVerseFormattingStyleResolver::Resolve(
		*Session.GetParseSnapshot().GetDocument(), Session.GetParseSnapshot(), Clause);
	const FString LineEnding = FVerseSyntaxEmitter::LineEnding(Style);
	FString ExpressionSource;
	if (!BuildDestinationExpressionSource(
		Action, BoundExpressionSource, Style, ExpressionSource, OutError))
	{
		return false;
	}
	const EVerseSeparatorToken SeparatorToken = Clause.bRequiresFailablePlaceholder
		? Style.FailureSeparatorToken : Style.StatementSeparatorToken;
	const EVerseSeparatorLayout SeparatorLayout = Clause.bRequiresFailablePlaceholder
		? Style.FailureSeparatorLayout : Style.StatementSeparatorLayout;
	int32 InsertionByte = Clause.EmptyBodyInsertionAnchor.IsSet()
		? Clause.EmptyBodyInsertionAnchor.BeginByte
		: Clause.InteriorRange.BeginByte;
	FVerseByteRange EditedRange(InsertionByte, 0);
	FString Replacement;
	int32 ExpressionOffsetCharacters = 0;
	if (!Clause.Items.IsEmpty())
	{
		if (InsertIndex < Clause.Items.Num())
		{
			InsertionByte = Clause.Items[InsertIndex].Expression.Range.BeginByte;
			EditedRange = FVerseByteRange(InsertionByte, 0);
			const FString StatementIndent = IndentationAt(Source, InsertionByte);
			ExpressionSource = RebaseMultilineSource(
				MoveTemp(ExpressionSource), LineEnding, StatementIndent);
			if (Clause.Syntax.Delimiter == EVerseClauseDelimiter::Colon
				|| Clause.Syntax.Delimiter == EVerseClauseDelimiter::BareIndentation)
			{
				Replacement = ExpressionSource + LineEnding + StatementIndent;
			}
			else
			{
				Replacement = ExpressionSource + FVerseSyntaxEmitter::Separator(
					SeparatorToken, SeparatorLayout, 0, Style,
					StatementIndent);
			}
		}
		else
		{
			InsertionByte = Clause.Items.Last().Expression.Range.EndByte();
			EditedRange = FVerseByteRange(InsertionByte, 0);
			const FString StatementIndent = IndentationAt(
				Source, Clause.Items.Last().Expression.Range.BeginByte);
			ExpressionSource = RebaseMultilineSource(
				MoveTemp(ExpressionSource), LineEnding, StatementIndent);
			if (Clause.Syntax.Delimiter == EVerseClauseDelimiter::Colon
				|| Clause.Syntax.Delimiter == EVerseClauseDelimiter::BareIndentation)
			{
				Replacement = LineEnding + StatementIndent + ExpressionSource;
				ExpressionOffsetCharacters = Replacement.Len() - ExpressionSource.Len();
			}
			else
			{
				Replacement = FVerseSyntaxEmitter::Separator(
					SeparatorToken, SeparatorLayout, 0, Style,
					StatementIndent)
					+ ExpressionSource;
				ExpressionOffsetCharacters = Replacement.Len() - ExpressionSource.Len();
			}
		}
	}
	else if (Clause.Syntax.Delimiter == EVerseClauseDelimiter::Colon
		|| Clause.Syntax.Delimiter == EVerseClauseDelimiter::BareIndentation)
	{
		const FString StatementIndent = Clause.Syntax.IndentationPrefix
			+ (Clause.Syntax.IndentationUnit.IsEmpty()
				? Style.IndentationUnit : Clause.Syntax.IndentationUnit);
		ExpressionSource = RebaseMultilineSource(
			MoveTemp(ExpressionSource), LineEnding, StatementIndent);
		EditedRange = Clause.Syntax.LeadingWhitespaceRange.IsSet()
			? FVerseByteRange(
				Clause.Syntax.LeadingWhitespaceRange.BeginByte,
				Clause.Syntax.LeadingWhitespaceRange.NumBytes)
			: FVerseByteRange(InsertionByte, 0);
		InsertionByte = EditedRange.BeginByte;
		Replacement = LineEnding + StatementIndent + ExpressionSource;
		ExpressionOffsetCharacters = Replacement.Len() - ExpressionSource.Len();
	}
	else
	{
		const bool bMultiline = Clause.Syntax.Layout == EVerseSyntaxLayout::Multiline;
		const FString StatementIndent = Clause.Syntax.IndentationPrefix
			+ (Clause.Syntax.IndentationUnit.IsEmpty()
				? Style.IndentationUnit : Clause.Syntax.IndentationUnit);
		ExpressionSource = RebaseMultilineSource(
			MoveTemp(ExpressionSource), LineEnding, StatementIndent);
		EditedRange = Clause.Syntax.LeadingWhitespaceRange.IsSet()
			? FVerseByteRange(
				Clause.Syntax.LeadingWhitespaceRange.BeginByte,
				Clause.Syntax.LeadingWhitespaceRange.NumBytes)
			: FVerseByteRange(InsertionByte, 0);
		InsertionByte = EditedRange.BeginByte;
		if (bMultiline)
		{
			Replacement = LineEnding + StatementIndent + ExpressionSource
				+ LineEnding + Clause.Syntax.IndentationPrefix;
			ExpressionOffsetCharacters = LineEnding.Len() + StatementIndent.Len();
		}
		else
		{
			Replacement = TEXT(" ") + ExpressionSource + TEXT(" ");
			ExpressionOffsetCharacters = 1;
		}
	}

	const FVerseDocumentEdit Edit = MakeEdit(
		Session.GetRevision(), EditedRange, Replacement);
	if (!ValidateCandidate(Session, Clause, MakeArrayView(&Edit, 1), Clause.Items.Num() + 1, OutError))
	{
		return false;
	}
	if (!Session.ReplaceMany(MakeArrayView(&Edit, 1), OutError))
	{
		return false;
	}
	if (OutInsertedRange != nullptr)
	{
		const FTCHARToUTF8 PrefixUtf8(*Replacement, ExpressionOffsetCharacters);
		const FTCHARToUTF8 ExpressionUtf8(*ExpressionSource);
		*OutInsertedRange = FVerseTextRange(
			Session.GetRevision(),
			FVerseByteRange(
				InsertionByte + PrefixUtf8.Length(), ExpressionUtf8.Length()));
	}
	return true;
}

bool FVerseClauseEditing::ReplaceExpression(
	FVerseDocumentSession& Session,
	const FVerseVisualClauseDescriptor& Clause,
	int32 ItemIndex,
	const FVerseExpressionAction& Action,
	FText& OutError,
	FVerseTextRange* OutReplacementRange)
{
	if (Clause.InteriorRange.Revision != Session.GetRevision()
		|| !Clause.Items.IsValidIndex(ItemIndex))
	{
		OutError = LOCTEXT(
			"InvalidClauseReplacement",
			"The provisional expression is no longer in this clause.");
		return false;
	}
	FString ExpressionSource;
	if (!BuildVerseExpressionActionSource(Action, FStringView(), ExpressionSource, OutError))
	{
		return false;
	}
	const FVerseByteRange ReplacedRange = Clause.Items[ItemIndex].Expression.Range;
	const FVerseDocumentEdit Edit = MakeEdit(
		Session.GetRevision(), ReplacedRange, ExpressionSource);
	if (!ValidateCandidate(Session, Clause, MakeArrayView(&Edit, 1), Clause.Items.Num(), OutError)
		|| !Session.ReplaceMany(MakeArrayView(&Edit, 1), OutError))
	{
		return false;
	}
	if (OutReplacementRange != nullptr)
	{
		const FTCHARToUTF8 ExpressionUtf8(*ExpressionSource);
		*OutReplacementRange = FVerseTextRange(
			Session.GetRevision(),
			FVerseByteRange(ReplacedRange.BeginByte, ExpressionUtf8.Length()));
	}
	return true;
}

bool FVerseClauseEditing::AddElseExpression(
	FVerseDocumentSession& Session,
	FVerseTextRange IfExpressionRange,
	EVerseClauseDelimiter BodyStyle,
	const FVerseExpressionAction& Action,
	FText& OutError,
	FVerseTextRange* OutInsertedRange,
	FStringView BoundExpressionSource)
{
	if (IfExpressionRange.Revision != Session.GetRevision()
		|| !IfExpressionRange.IsSet())
	{
		OutError = LOCTEXT("InvalidMissingElseInsertion", "The if expression is no longer valid.");
		return false;
	}
	FString ExpressionSource;
	if (!BuildVerseExpressionActionSource(
		Action, BoundExpressionSource, ExpressionSource, OutError))
	{
		return false;
	}

	const FUtf8StringView Source(*Session.GetCurrentUtf8(), Session.GetCurrentUtf8().Len());
	const FVerseFormattingStyleProfile Style = FVerseFormattingStyleResolver::Resolve(
		*Session.GetParseSnapshot().GetDocument(), Session.GetParseSnapshot());
	const FString LineEnding = FVerseSyntaxEmitter::LineEnding(Style);
	const FString Indentation = IndentationAt(Source, IfExpressionRange.BeginByte);
	const bool bBraces = BodyStyle == EVerseClauseDelimiter::Braces;
	int32 InsertionByte = IfExpressionRange.EndByte();
	const TArray<FVerseFunctionNavigationItem> CurrentFunctions =
		FVerseFunctionNavigationBuilder::Build(
			Session.GetTiles(), Session.GetParseSnapshot());
	for (const FVerseFunctionNavigationItem& Function : CurrentFunctions)
	{
		const FVerseVisualTile* CurrentIf =
			FindIfAtOpeningByte(Function.GraphTiles, IfExpressionRange.BeginByte);
		if (CurrentIf == nullptr)
		{
			continue;
		}
		const auto* Body = CurrentIf->ControlRegions.FindByPredicate(
			[](const auto& Region)
			{
				return Region.Kind == EVerseControlRegionKind::Body;
			});
		// Insert before the true body's trailing trivia. That trivia belongs after
		// the complete if, so adding else here naturally moves it behind else.
		if (Body != nullptr && Body->Syntax.TrailingWhitespaceRange.IsSet())
		{
			InsertionByte = Body->Syntax.TrailingWhitespaceRange.BeginByte;
		}
		break;
	}
	const FString Replacement = bBraces
		? FString::Printf(TEXT(" else { %s }"), *ExpressionSource)
		: LineEnding + Indentation + TEXT("else:") + LineEnding
			+ Indentation + Style.IndentationUnit + ExpressionSource;
	const int32 ExpressionOffsetCharacters = bBraces
		? FString(TEXT(" else { ")).Len()
		: Replacement.Len() - ExpressionSource.Len();
	const FVerseDocumentEdit Edit = MakeEdit(
		Session.GetRevision(),
		FVerseByteRange(InsertionByte, 0),
		Replacement);
	const FUtf8String Candidate = BuildCandidate(Session, MakeArrayView(&Edit, 1), OutError);
	if (Candidate.IsEmpty() && Session.GetCurrentUtf8().Len() != 0)
	{
		return false;
	}
	const TConstArrayView<uint8> Bytes(
		reinterpret_cast<const uint8*>(*Candidate), Candidate.Len());
	const TSharedPtr<const FVerseDocument> Document =
		FVerseDocument::CreateFromBytes(Bytes, OutError);
	if (!Document.IsValid())
	{
		return false;
	}
	const FVerseParseSnapshot Snapshot = FVerseParseSnapshotBuilder::Build(Document.ToSharedRef());
	const TArray<FVerseVisualTile> FileTiles = FVerseVisualTileBuilder::Build(Snapshot);
	const TArray<FVerseFunctionNavigationItem> Functions =
		FVerseFunctionNavigationBuilder::Build(FileTiles, Snapshot);
	const bool bFound = Functions.ContainsByPredicate(
		[OpeningByte = IfExpressionRange.BeginByte](const FVerseFunctionNavigationItem& Function)
		{
			return FindIfWithElse(Function.GraphTiles, OpeningByte) != nullptr;
		});
	if (!bFound)
	{
		OutError = LOCTEXT(
			"MissingElseRejected",
			"The edit was rejected because it did not produce a valid else clause.");
		return false;
	}
	if (!Session.ReplaceMany(MakeArrayView(&Edit, 1), OutError))
	{
		return false;
	}
	if (OutInsertedRange != nullptr)
	{
		const FTCHARToUTF8 PrefixUtf8(*Replacement, ExpressionOffsetCharacters);
		const FTCHARToUTF8 ExpressionUtf8(*ExpressionSource);
		*OutInsertedRange = FVerseTextRange(
			Session.GetRevision(),
			FVerseByteRange(
				InsertionByte + PrefixUtf8.Length(),
				ExpressionUtf8.Length()));
	}
	return true;
}

bool FVerseClauseEditing::DeleteExpression(
	FVerseDocumentSession& Session,
	const FVerseVisualClauseDescriptor& Clause,
	int32 ItemIndex,
	FText& OutError,
	FVerseTextRange* OutProvisionalReplacementRange)
{
	if (Clause.InteriorRange.Revision != Session.GetRevision()
		|| !Clause.Items.IsValidIndex(ItemIndex))
	{
		OutError = LOCTEXT("InvalidClauseDeletion", "The selected expression is no longer in this clause.");
		return false;
	}
	if (OutProvisionalReplacementRange != nullptr)
	{
		*OutProvisionalReplacementRange = {};
	}
	if (Clause.bRequiresFailablePlaceholder && Clause.Items.Num() == 1)
	{
		static constexpr FStringView Placeholder = TEXTVIEW("true?");
		const FVerseByteRange ReplacedRange = Clause.Items[0].Expression.Range;
		const FVerseDocumentEdit Edit = MakeEdit(
			Session.GetRevision(), ReplacedRange, Placeholder);
		if (!ValidateCandidate(Session, Clause, MakeArrayView(&Edit, 1), 1, OutError)
			|| !Session.ReplaceMany(MakeArrayView(&Edit, 1), OutError))
		{
			return false;
		}
		if (OutProvisionalReplacementRange != nullptr)
		{
			const FTCHARToUTF8 PlaceholderUtf8(Placeholder.GetData(), Placeholder.Len());
			*OutProvisionalReplacementRange = FVerseTextRange(
				Session.GetRevision(),
				FVerseByteRange(ReplacedRange.BeginByte, PlaceholderUtf8.Length()));
		}
		return true;
	}
	TArray<FVerseDocumentEdit> Edits;
	FVerseTextRange DeletedRange = Clause.Items[ItemIndex].Expression.Range;
	const FVerseTextRange LeadingTrivia =
		Clause.Items[ItemIndex].LeadingTriviaRange;
	if (Clause.Syntax.Layout == EVerseSyntaxLayout::Multiline
		&& LeadingTrivia.IsSet()
		&& IsClauseWhitespaceOnly(Decode(Session, LeadingTrivia)))
	{
		// Delete the whole source line prefix owned by this item. Removing only
		// the expression would strand its indentation as a whitespace-only line.
		DeletedRange = FVerseTextRange(
			Session.GetRevision(),
			FVerseByteRange::FromBounds(
				LeadingTrivia.BeginByte,
				Clause.Items[ItemIndex].Expression.Range.EndByte()));
	}
	Edits.Add(MakeEdit(Session.GetRevision(), DeletedRange, FStringView()));

	// Remove only a separator token. Trivia and VST-owned/ambiguous comments stay fixed.
	if (Clause.Items.Num() > 1
		&& Clause.Syntax.Delimiter != EVerseClauseDelimiter::Colon
		&& Clause.Syntax.Delimiter != EVerseClauseDelimiter::BareIndentation)
	{
		const bool bUseTrailing = ItemIndex + 1 < Clause.Items.Num();
		const FVerseTextRange Trivia = bUseTrailing
			? Clause.Items[ItemIndex].TrailingTriviaRange
			: Clause.Items[ItemIndex].LeadingTriviaRange;
		const FString TriviaText = Decode(Session, Trivia);
		const int32 SeparatorCharacter = bUseTrailing
			? TriviaText.Find(TEXT(";"))
			: TriviaText.Find(TEXT(";"), ESearchCase::CaseSensitive, ESearchDir::FromEnd);
		if (SeparatorCharacter != INDEX_NONE)
		{
			const FTCHARToUTF8 Prefix(*TriviaText.Left(SeparatorCharacter));
			Edits.Add(MakeEdit(
				Session.GetRevision(),
				FVerseByteRange(Trivia.BeginByte + Prefix.Length(), 1),
				FStringView()));
		}
	}
	if (!ValidateCandidate(Session, Clause, Edits, Clause.Items.Num() - 1, OutError))
	{
		return false;
	}
	return Session.ReplaceMany(Edits, OutError);
}

bool FVerseClauseEditing::ReorderExpression(
	FVerseDocumentSession& Session,
	const FVerseVisualClauseDescriptor& Clause,
	int32 FromIndex,
	int32 ToIndex,
	FText& OutError)
{
	if (Clause.InteriorRange.Revision != Session.GetRevision()
		|| !Clause.Items.IsValidIndex(FromIndex)
		|| !Clause.Items.IsValidIndex(ToIndex))
	{
		OutError = LOCTEXT("InvalidClauseReorder", "The reorder destination is outside this clause.");
		return false;
	}
	if (FromIndex == ToIndex)
	{
		OutError = FText::GetEmpty();
		return true;
	}

	TArray<FString> Expressions;
	Expressions.Reserve(Clause.Items.Num());
	for (const FVerseVisualClauseItemDescriptor& Item : Clause.Items)
	{
		Expressions.Add(Decode(Session, Item.Expression.Range));
	}
	const FString Moved = Expressions[FromIndex];
	Expressions.RemoveAt(FromIndex);
	Expressions.Insert(Moved, ToIndex);

	TArray<FVerseDocumentEdit> Edits;
	const int32 Begin = FMath::Min(FromIndex, ToIndex);
	const int32 End = FMath::Max(FromIndex, ToIndex);
	for (int32 Index = Begin; Index <= End; ++Index)
	{
		Edits.Add(MakeEdit(
			Session.GetRevision(), Clause.Items[Index].Expression.Range, Expressions[Index]));
	}
	if (!ValidateCandidate(Session, Clause, Edits, Clause.Items.Num(), OutError))
	{
		return false;
	}
	return Session.ReplaceMany(Edits, OutError);
}

#undef LOCTEXT_NAMESPACE
