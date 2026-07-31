#include "VerseClauseEditing.h"

#include "Internationalization/Text.h"
#include "VerseDocumentSession.h"
#include "VerseExpressionActions.h"
#include "VerseFunctionNavigation.h"
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
				if (Region.OpeningPunctuationRange.IsSet()
					&& Region.OpeningPunctuationRange.BeginByte == OpeningByte)
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
		if (const FVerseVisualClauseDescriptor* BodyClause =
			FindBodyClause(Tiles, OpeningByte))
		{
			if (BodyClause->Items.Num() == ExpectedItemCount)
			{
				return true;
			}
		}
		const TArray<FVerseFunctionNavigationItem> Functions =
			FVerseFunctionNavigationBuilder::Build(Tiles, Snapshot);
		for (const FVerseFunctionNavigationItem& Function : Functions)
		{
			if (const auto* Region = FindRegion(Function.GraphTiles, OpeningByte))
			{
				if (Region->Items.Num() == ExpectedItemCount)
				{
					return true;
				}
			}
		}
		OutError = LOCTEXT(
			"ClauseEditRejected",
			"The edit would not produce a valid ordered Verse clause.");
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
	FText& OutError)
{
	if (Clause.InteriorRange.Revision != Session.GetRevision()
		|| InsertIndex < 0 || InsertIndex > Clause.Items.Num())
	{
		OutError = LOCTEXT("InvalidClauseInsertion", "The insertion point is no longer valid.");
		return false;
	}
	FString ExpressionSource;
	if (!BuildVerseExpressionActionSource(Action, FStringView(), ExpressionSource, OutError))
	{
		return false;
	}

	const FUtf8StringView Source(*Session.GetCurrentUtf8(), Session.GetCurrentUtf8().Len());
	const FString LineEnding = DetectLineEnding(Source);
	int32 InsertionByte = Clause.EmptyBodyInsertionAnchor.IsSet()
		? Clause.EmptyBodyInsertionAnchor.BeginByte
		: Clause.InteriorRange.BeginByte;
	FString Replacement;
	if (!Clause.Items.IsEmpty())
	{
		if (InsertIndex < Clause.Items.Num())
		{
			InsertionByte = Clause.Items[InsertIndex].Expression.Range.BeginByte;
			if (Clause.PunctuationStyle == EVerseClausePunctuationStyle::ColonOrIndentation)
			{
				Replacement = ExpressionSource + LineEnding + IndentationAt(Source, InsertionByte);
			}
			else
			{
				Replacement = ExpressionSource + TEXT("; ");
			}
		}
		else
		{
			InsertionByte = Clause.Items.Last().Expression.Range.EndByte();
			if (Clause.PunctuationStyle == EVerseClausePunctuationStyle::ColonOrIndentation)
			{
				Replacement = LineEnding + IndentationAt(
					Source, Clause.Items.Last().Expression.Range.BeginByte) + ExpressionSource;
			}
			else
			{
				Replacement = TEXT("; ") + ExpressionSource;
			}
		}
	}
	else if (Clause.PunctuationStyle == EVerseClausePunctuationStyle::ColonOrIndentation)
	{
		const int32 HeaderByte = Clause.OpeningPunctuationRange.IsSet()
			? Clause.OpeningPunctuationRange.BeginByte
			: Clause.InteriorRange.BeginByte;
		Replacement = LineEnding + IndentationAt(Source, HeaderByte) + TEXT("    ") + ExpressionSource;
	}
	else
	{
		Replacement = ExpressionSource;
	}

	const FVerseDocumentEdit Edit = MakeEdit(
		Session.GetRevision(), FVerseByteRange(InsertionByte, 0), Replacement);
	if (!ValidateCandidate(Session, Clause, MakeArrayView(&Edit, 1), Clause.Items.Num() + 1, OutError))
	{
		return false;
	}
	return Session.ReplaceMany(MakeArrayView(&Edit, 1), OutError);
}

bool FVerseClauseEditing::DeleteExpression(
	FVerseDocumentSession& Session,
	const FVerseVisualClauseDescriptor& Clause,
	int32 ItemIndex,
	FText& OutError)
{
	if (Clause.InteriorRange.Revision != Session.GetRevision()
		|| !Clause.Items.IsValidIndex(ItemIndex))
	{
		OutError = LOCTEXT("InvalidClauseDeletion", "The selected expression is no longer in this clause.");
		return false;
	}
	TArray<FVerseDocumentEdit> Edits;
	Edits.Add(MakeEdit(Session.GetRevision(), Clause.Items[ItemIndex].Expression.Range, FStringView()));

	// Remove only a separator token. Trivia and VST-owned/ambiguous comments stay fixed.
	if (Clause.Items.Num() > 1
		&& Clause.PunctuationStyle != EVerseClausePunctuationStyle::ColonOrIndentation)
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
