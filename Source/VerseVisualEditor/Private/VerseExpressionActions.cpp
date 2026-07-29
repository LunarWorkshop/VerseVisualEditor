#include "VerseExpressionActions.h"

#include "Internationalization/Text.h"
#include "VerseDocument.h"
#include "VerseDocumentSession.h"
#include "VerseOperatorTyping.h"
#include "VerseFunctionNavigation.h"
#include "VerseParseSnapshotBuilder.h"
#include "VerseSemanticCandidates.h"
#include "VerseSemanticWorkspace.h"

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

	FString GetTypeName(const FVerseExpressionType& Type, FUtf8StringView Source)
	{
		if (!Type.SourceRange.IsSet())
		{
			return NormalizeActionType(Type.IntrinsicName.ToString());
		}
		if (Type.SourceRange.BeginByte < 0 || Type.SourceRange.EndByte() > Source.Len())
		{
			return FString();
		}
		const FUTF8ToTCHAR Converted(
			reinterpret_cast<const ANSICHAR*>(Source.GetData() + Type.SourceRange.BeginByte),
			Type.SourceRange.NumBytes);
		return NormalizeActionType(FString(Converted.Length(), Converted.Get()));
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

	/** A searchable expression and the type signatures it exposes to graph wiring. */
	struct FExpressionCandidate
	{
		TSharedPtr<FVerseExpressionAction> Action;
		TArray<FVerseExpressionType> InputTypes;
		FVerseExpressionType OutputType;
		TOptional<EVerseOperatorKind> PolymorphicOperator;
		int32 RequiredInputCount = 0;
		bool bHomogeneousInputs = false;
	};

	FString GetDefaultLiteralSource(const FVerseExpressionType& Type, FUtf8StringView Source)
	{
		const FString TypeName = GetTypeName(Type, Source);
		if (TypeName == TEXT("int"))
		{
			return TEXT("0");
		}
		if (TypeName == TEXT("float"))
		{
			return TEXT("0.0");
		}
		if (TypeName == TEXT("string"))
		{
			return TEXT("\"\"");
		}
		if (TypeName.StartsWith(TEXT("[]")))
		{
			return TEXT("array{}");
		}
		return FString();
	}

	bool TypesMatch(
		const FVerseExpressionType& Left,
		const FVerseExpressionType& Right,
		FUtf8StringView Source)
	{
		const FString LeftName = GetTypeName(Left, Source);
		const FString RightName = GetTypeName(Right, Source);
		return !LeftName.IsEmpty() && LeftName == RightName;
	}

	bool CandidateAcceptsInput(
		const FExpressionCandidate& Candidate,
		const FVerseExpressionType& SourceType,
		FUtf8StringView Source)
	{
		if (Candidate.PolymorphicOperator.IsSet())
		{
			return FVerseOperatorTyping::CanAcceptOperand(
				Candidate.PolymorphicOperator.GetValue(), SourceType, Source);
		}
		return Candidate.InputTypes.ContainsByPredicate(
			[&](const FVerseExpressionType& InputType)
			{
				return TypesMatch(InputType, SourceType, Source);
			});
	}

	bool CandidateProducesOutput(
		const FExpressionCandidate& Candidate,
		const FVerseExpressionType& RequiredType,
		FUtf8StringView Source)
	{
		if (Candidate.PolymorphicOperator.IsSet())
		{
			return FVerseOperatorTyping::CanProduceResult(
				Candidate.PolymorphicOperator.GetValue(), RequiredType, Source);
		}
		return Candidate.OutputType.IsResolved()
			&& TypesMatch(Candidate.OutputType, RequiredType, Source);
	}

	TArray<FExpressionCandidate> BuildCandidateRegistry(
		TConstArrayView<FVerseFunctionNavigationParameter> Parameters,
		const FVerseDocument& Document)
	{
		TArray<FExpressionCandidate> Candidates;
		for (const FVerseFunctionNavigationParameter& Parameter : Parameters)
		{
			FExpressionCandidate& Candidate = Candidates.AddDefaulted_GetRef();
			Candidate.Action = MakeShared<FVerseExpressionAction>();
			Candidate.Action->SourceForm =
				EVerseExpressionSourceForm::IdentifierReference;
			Candidate.Action->DisplayName = FText::FromString(
				Document.DecodeOriginalRange(Parameter.NameRange));
			Candidate.Action->Category = LOCTEXT("IdentifiersCategory", "Identifiers");
			Candidate.Action->IdentifierNameRange = Parameter.NameRange;
			Candidate.OutputType = {
				Parameter.TypeRange,
				NAME_None,
				EVerseTypeResolutionProvenance::LocallyInferred};
		}

		FExpressionCandidate& Addition = Candidates.AddDefaulted_GetRef();
		Addition.Action = MakeShared<FVerseExpressionAction>();
		Addition.Action->SourceForm = EVerseExpressionSourceForm::InfixOperator;
		Addition.Action->SourceSpelling = TEXT("+");
		Addition.Action->DisplayName = LOCTEXT("AddAction", "Add (+)");
		Addition.Action->Category = LOCTEXT("OperatorsCategory", "Operators");
		Addition.PolymorphicOperator = EVerseOperatorKind::Addition;
		Addition.RequiredInputCount = 2;
		Addition.bHomogeneousInputs = true;
		return Candidates;
	}
}

TArray<TSharedPtr<FVerseExpressionAction>> FVerseExpressionActionQuery::Build(
	TConstArrayView<FVerseFunctionNavigationParameter> Parameters,
	const FVerseVisualSocket& DraggedSocket,
	bool bDraggingFromOutput,
	const FVerseDocument& Document)
{
	TArray<TSharedPtr<FVerseExpressionAction>> Result;
	const FVerseExpressionType SocketType{
		DraggedSocket.TypeRange,
		DraggedSocket.IntrinsicTypeName,
		EVerseTypeResolutionProvenance::LocallyInferred};
	const FUtf8StringView Source = Document.GetOriginalUtf8View();
	for (FExpressionCandidate& Candidate : BuildCandidateRegistry(Parameters, Document))
	{
		const bool bCompatible = bDraggingFromOutput
			? CandidateAcceptsInput(Candidate, SocketType, Source)
			: CandidateProducesOutput(Candidate, SocketType, Source);
		if (bCompatible)
		{
			if (bDraggingFromOutput && Candidate.RequiredInputCount > 1)
			{
				if (!Candidate.bHomogeneousInputs)
				{
					continue;
				}
				const FString DefaultSource = GetDefaultLiteralSource(SocketType, Source);
				if (DefaultSource.IsEmpty())
				{
					continue;
				}
				Candidate.Action->BoundInputIndex = 0;
				Candidate.Action->InputDefaultSources.Init(
					FString(), Candidate.RequiredInputCount);
				for (int32 Index = 1; Index < Candidate.RequiredInputCount; ++Index)
				{
					Candidate.Action->InputDefaultSources[Index] = DefaultSource;
				}
			}
			Result.Add(MoveTemp(Candidate.Action));
		}
	}
	return Result;
}

TArray<TSharedPtr<FVerseExpressionAction>> FVerseExpressionActionQuery::Build(
	TConstArrayView<FVerseFunctionNavigationParameter> Parameters,
	const FVerseVisualSocket& DraggedSocket,
	bool bDraggingFromOutput,
	const FVerseDocument& Document,
	FVerseTextRange ExpressionRange,
	const FString& FilePath,
	TConstArrayView<TSharedPtr<const FVerseSemanticSnapshot>> SemanticSnapshots)
{
	TArray<TSharedPtr<FVerseExpressionAction>> Result;
	const TArray<FVerseSemanticCandidate> SemanticCandidates =
		FVerseSemanticCandidateProvider::Build(
			SemanticSnapshots,
			FilePath,
			ExpressionRange.BeginByte,
			bDraggingFromOutput,
			Document);
	for (const FVerseSemanticCandidate& Candidate : SemanticCandidates)
	{
		TSharedPtr<FVerseExpressionAction> Action = MakeShared<FVerseExpressionAction>();
		Action->DisplayName = FText::FromString(Candidate.DisplayName);
		Action->SourceSpelling = Candidate.SourceSpelling;
		Action->bUsesFailureCallSyntax = Candidate.bUsesFailureCallSyntax;
		Action->BoundInputIndex = Candidate.BoundInputIndex;
		Action->InputDefaultSources = Candidate.UnboundInputDefaults;
		Action->SemanticDataDefinition = Candidate.DataDefinition;
		Action->SemanticFunction = Candidate.Function;
		Action->SemanticSnapshot = Candidate.Snapshot;
		switch (Candidate.Kind)
		{
		case EVerseSemanticCandidateKind::Identifier:
			Action->SourceForm =
				EVerseExpressionSourceForm::IdentifierReference;
			Action->Validation =
				EVerseExpressionActionValidation::ExactSemanticSnapshot;
			Action->Category = LOCTEXT("IdentifiersCategory", "Identifiers");
			break;
		case EVerseSemanticCandidateKind::Function:
			Action->SourceForm = EVerseExpressionSourceForm::OrdinaryCall;
			Action->Validation =
				EVerseExpressionActionValidation::StableSemanticSignature;
			Action->Category = LOCTEXT("FunctionsCategory", "Functions");
			break;
		case EVerseSemanticCandidateKind::InfixOperator:
			Action->SourceForm = EVerseExpressionSourceForm::InfixOperator;
			Action->Validation =
				EVerseExpressionActionValidation::StableSemanticSignature;
			Action->DisplayName = FText::Format(
				LOCTEXT("InfixOperatorAction", "Operator ({0})"),
				FText::FromString(Candidate.SourceSpelling));
			Action->Category = LOCTEXT("OperatorsCategory", "Operators");
			break;
		case EVerseSemanticCandidateKind::PrefixOperator:
			Action->SourceForm = EVerseExpressionSourceForm::PrefixOperator;
			Action->Validation =
				EVerseExpressionActionValidation::StableSemanticSignature;
			Action->Category = LOCTEXT("OperatorsCategory", "Operators");
			break;
		case EVerseSemanticCandidateKind::PostfixOperator:
			Action->SourceForm = EVerseExpressionSourceForm::PostfixOperator;
			Action->Validation =
				EVerseExpressionActionValidation::StableSemanticSignature;
			Action->Category = LOCTEXT("OperatorsCategory", "Operators");
			break;
		}
		Result.Add(MoveTemp(Action));
	}

	const bool bHasExactSemanticSnapshot = SemanticSnapshots.ContainsByPredicate(
		[&FilePath, Revision = ExpressionRange.Revision](
			const TSharedPtr<const FVerseSemanticSnapshot>& Snapshot)
		{
			return Snapshot.IsValid() && Snapshot->Describes(FilePath, Revision);
		});
	if (!bHasExactSemanticSnapshot)
	{
		// A failed unrelated semantic build must not disable source-safe actions.
		// These fallback actions are authorized only by the prospective parser/VST
		// validation in TryApplyVerseExpressionAction.
		Result.Append(Build(Parameters, DraggedSocket, bDraggingFromOutput, Document));
	}
	return Result;
}

bool TryApplyVerseExpressionAction(
	FVerseDocumentSession& Session,
	FVerseTextRange ExpressionRange,
	const FVerseExpressionAction& Action,
	const FVerseExpressionSemanticValidator& SemanticValidator,
	FText& OutError)
{
	if (ExpressionRange.Revision != Session.GetRevision())
	{
		OutError = LOCTEXT("StaleExpression", "The expression belongs to an obsolete document revision.");
		return false;
	}
	const TSharedRef<const FVerseDocument> Document = Session.GetParseSnapshot().GetDocument();
	const FString Existing =
		Document->DecodeOriginalRange(ExpressionRange).TrimStartAndEnd();
	FString Replacement;
	EVerseExpressionKind RequiredKind = EVerseExpressionKind::Unsupported;
	if (Action.SourceForm == EVerseExpressionSourceForm::IdentifierReference)
	{
		Replacement = Action.SourceSpelling.IsEmpty()
			? Document->DecodeOriginalRange(Action.IdentifierNameRange)
			: Action.SourceSpelling;
		RequiredKind = EVerseExpressionKind::Identifier;
	}
	else
	{
		if (Action.SourceSpelling.IsEmpty())
		{
			OutError = LOCTEXT(
				"MissingExpressionSpelling",
				"The selected expression has no source spelling.");
			return false;
		}
		TArray<FString> Inputs = Action.InputDefaultSources;
		if (Action.BoundInputIndex != INDEX_NONE)
		{
			if (!Inputs.IsValidIndex(Action.BoundInputIndex) || Existing.IsEmpty())
			{
				OutError = LOCTEXT(
					"InvalidBoundExpressionInput",
					"The selected expression cannot preserve the dragged input.");
				return false;
			}
			Inputs[Action.BoundInputIndex] = Existing;
		}
		if (Inputs.ContainsByPredicate([](const FString& Input) { return Input.IsEmpty(); }))
		{
			OutError = LOCTEXT(
				"MissingExpressionInputDefault",
				"The selected expression has an input with no source-safe default.");
			return false;
		}
		switch (Action.SourceForm)
		{
		case EVerseExpressionSourceForm::OrdinaryCall:
			Replacement = Action.bUsesFailureCallSyntax
				? FString::Printf(
					TEXT("%s[%s]"),
					*Action.SourceSpelling,
					*FString::Join(Inputs, TEXT(", ")))
				: FString::Printf(
					TEXT("%s(%s)"),
					*Action.SourceSpelling,
					*FString::Join(Inputs, TEXT(", ")));
			break;
		case EVerseExpressionSourceForm::InfixOperator:
			if (Inputs.Num() != 2)
			{
				OutError = LOCTEXT(
					"InvalidInfixInputs",
					"An infix operator requires exactly two operands.");
				return false;
			}
			Replacement = FString::Printf(
				TEXT("%s %s %s"),
				*Inputs[0], *Action.SourceSpelling, *Inputs[1]);
			RequiredKind = Action.SourceSpelling == TEXT("+")
				? EVerseExpressionKind::Addition
				: EVerseExpressionKind::Unsupported;
			break;
		case EVerseExpressionSourceForm::PrefixOperator:
			if (Inputs.Num() != 1)
			{
				OutError = LOCTEXT(
					"InvalidPrefixInputs",
					"A prefix operator requires exactly one operand.");
				return false;
			}
			Replacement = FString::Printf(
				TEXT("%s %s"), *Action.SourceSpelling, *Inputs[0]);
			break;
		case EVerseExpressionSourceForm::PostfixOperator:
			if (Inputs.Num() != 1)
			{
				OutError = LOCTEXT(
					"InvalidPostfixInputs",
					"A postfix operator requires exactly one operand.");
				return false;
			}
			Replacement = FString::Printf(
				TEXT("%s%s"), *Inputs[0], *Action.SourceSpelling);
			break;
		default:
			OutError = LOCTEXT(
				"InvalidExpressionSourceForm",
				"The selected expression source form is unsupported.");
			return false;
		}
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
	if (!SemanticValidator || !SemanticValidator(Candidate, OutError))
	{
		if (OutError.IsEmpty())
		{
			OutError = LOCTEXT(
				"ExpressionSemanticRejection",
				"The expression would introduce a Verse semantic error.");
		}
		return false;
	}
	return Session.Replace(ExpressionRange, ReplacementUtf8, OutError);
}

#undef LOCTEXT_NAMESPACE
