#include "VerseExpressionActions.h"

#include "Internationalization/Text.h"
#include "VerseDocument.h"
#include "VerseDocumentSession.h"
#include "VerseOperatorTyping.h"
#include "VerseFunctionNavigation.h"
#include "VerseParseSnapshotBuilder.h"

#define LOCTEXT_NAMESPACE "VerseExpressionActions"

namespace
{
	FString NormalizeActionType(FString Type)
	{
		Type.TrimStartAndEndInline();
		Type.ReplaceInline(TEXT(" "), TEXT(""));
		Type.ReplaceInline(TEXT("\t"), TEXT(""));
		return Type.ToLower();
	}

	FString GetTypeName(const FVerseTextRange& Range, FName Intrinsic, const FVerseDocument& Document)
	{
		return NormalizeActionType(Range.IsSet() ? Document.DecodeOriginalRange(Range) : Intrinsic.ToString());
	}

	bool ContainsExpressionAt(
		TConstArrayView<FVerseVisualTile> Tiles,
		int32 BeginByte,
		EVerseExpressionKind Kind)
	{
		for (const FVerseVisualTile& Tile : Tiles)
		{
			if (Tile.Kind == EVerseVisualTileKind::Expression
				&& Tile.Range.BeginByte == BeginByte
				&& Tile.ExpressionKind == Kind)
			{
				return true;
			}
			if (ContainsExpressionAt(Tile.Children, BeginByte, Kind))
			{
				return true;
			}
		}
		return false;
	}
}

TArray<TSharedPtr<FVerseExpressionAction>> FVerseExpressionActionQuery::Build(
	TConstArrayView<FVerseFunctionNavigationParameter> Parameters,
	const FVerseVisualTile& DraggedExpression,
	const FVerseDocument& Document)
{
	TArray<TSharedPtr<FVerseExpressionAction>> Result;
	const FString ExpectedType = GetTypeName(
		DraggedExpression.TypeRange,
		DraggedExpression.IntrinsicTypeName,
		Document);
	for (const FVerseFunctionNavigationParameter& Parameter : Parameters)
	{
		if (ExpectedType.IsEmpty()
			|| GetTypeName(Parameter.TypeRange, NAME_None, Document) != ExpectedType)
		{
			continue;
		}
		TSharedPtr<FVerseExpressionAction> Action = MakeShared<FVerseExpressionAction>();
		Action->Kind = EVerseExpressionActionKind::Identifier;
		Action->DisplayName = FText::FromString(Document.DecodeOriginalRange(Parameter.NameRange));
		Action->Category = LOCTEXT("IdentifiersCategory", "Identifiers");
		Action->IdentifierNameRange = Parameter.NameRange;
		Result.Add(MoveTemp(Action));
	}

	// The expression registry currently has one operator. It is offered only when
	// duplicating the dragged terminal is a locally provable, valid addition.
	if (DraggedExpression.ExpressionKind == EVerseExpressionKind::Identifier
		&& !ExpectedType.IsEmpty())
	{
		FVerseExpressionType Evidence;
		Evidence.SourceRange = DraggedExpression.TypeRange;
		Evidence.IntrinsicName = DraggedExpression.IntrinsicTypeName;
		const FVerseExpressionType Operands[] = {Evidence, Evidence};
		const FVerseExpressionType Resolved = FVerseOperatorTyping::Resolve(
			EVerseOperatorKind::Addition,
			Operands,
			FVerseExpressionType(),
			Document.GetOriginalUtf8View());
		if (Resolved.IsResolved())
		{
			TSharedPtr<FVerseExpressionAction> Action = MakeShared<FVerseExpressionAction>();
			Action->Kind = EVerseExpressionActionKind::Addition;
			Action->DisplayName = LOCTEXT("AddAction", "Add (+)");
			Action->Category = LOCTEXT("OperatorsCategory", "Operators");
			Result.Add(MoveTemp(Action));
		}
	}
	return Result;
}

bool TryApplyVerseExpressionAction(
	FVerseDocumentSession& Session,
	FVerseTextRange ExpressionRange,
	const FVerseExpressionAction& Action,
	FText& OutError)
{
	if (ExpressionRange.Revision != Session.GetRevision())
	{
		OutError = LOCTEXT("StaleExpression", "The expression belongs to an obsolete document revision.");
		return false;
	}
	const TSharedRef<const FVerseDocument> Document = Session.GetParseSnapshot().GetDocument();
	FString Replacement;
	EVerseExpressionKind RequiredKind = EVerseExpressionKind::Identifier;
	if (Action.Kind == EVerseExpressionActionKind::Identifier)
	{
		Replacement = Document->DecodeOriginalRange(Action.IdentifierNameRange);
	}
	else
	{
		const FString Existing = Document->DecodeOriginalRange(ExpressionRange).TrimStartAndEnd();
		if (Existing.IsEmpty())
		{
			OutError = LOCTEXT("EmptyAddOperand", "Add requires a valid source expression.");
			return false;
		}
		Replacement = FString::Printf(TEXT("%s + %s"), *Existing, *Existing);
		RequiredKind = EVerseExpressionKind::Addition;
	}

	FUtf8String ReplacementUtf8(Replacement);
	const FUtf8String& Current = Session.GetCurrentUtf8();
	FUtf8String Candidate;
	Candidate.Append(FUtf8StringView(*Current, ExpressionRange.BeginByte));
	Candidate.Append(ReplacementUtf8);
	Candidate.Append(FUtf8StringView(
		*Current + ExpressionRange.EndByte(),
		Current.Len() - ExpressionRange.EndByte()));
	const TConstArrayView<uint8> CandidateBytes(
		reinterpret_cast<const uint8*>(*Candidate), Candidate.Len());
	TSharedPtr<const FVerseDocument> CandidateDocument = FVerseDocument::CreateFromBytes(CandidateBytes, OutError);
	if (!CandidateDocument.IsValid())
	{
		return false;
	}
	const FVerseParseSnapshot CandidateSnapshot = FVerseParseSnapshotBuilder::Build(CandidateDocument.ToSharedRef());
	const TArray<FVerseVisualTile> CandidateTiles = FVerseVisualTileBuilder::Build(CandidateSnapshot);
	const TArray<FVerseFunctionNavigationItem> Functions = FVerseFunctionNavigationBuilder::Build(
		CandidateTiles, CandidateSnapshot);
	const bool bRecognizedAtReplacement = Functions.ContainsByPredicate(
		[&](const FVerseFunctionNavigationItem& Function)
		{
			return ContainsExpressionAt(Function.GraphTiles, ExpressionRange.BeginByte, RequiredKind);
		});
	if (!bRecognizedAtReplacement)
	{
		OutError = LOCTEXT("ExpressionRejected", "The expression would not produce a valid supported Verse structure.");
		return false;
	}
	return Session.Replace(ExpressionRange, ReplacementUtf8, OutError);
}

#undef LOCTEXT_NAMESPACE
