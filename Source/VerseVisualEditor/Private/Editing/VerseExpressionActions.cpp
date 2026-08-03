#include "Editing/VerseExpressionActions.h"

#include "Internationalization/Text.h"
#include "Editing/VerseBlueprintCallablePresentation.h"
#include "VerseDocument.h"
#include "Document/VerseDocumentSession.h"
#include "VisualModel/VerseFunctionNavigation.h"
#include "Editing/VerseIntrinsicPresentation.h"
#include "VerseParseSnapshotBuilder.h"
#include "Semantics/VerseSemanticCandidates.h"
#include "Semantics/VerseSemanticWorkspace.h"
#include "uLang/Semantics/DataDefinition.h"
#include "uLang/Semantics/SemanticFunction.h"
#include "uLang/Semantics/SemanticProgram.h"
#include "uLang/Semantics/SemanticTypes.h"
#include "uLang/Semantics/TypeVariable.h"

#define LOCTEXT_NAMESPACE "VerseExpressionActions"

namespace
{
	FString SemanticTextToString(uLang::CUTF8StringView Text)
	{
		const FUTF8ToTCHAR Converted(
			reinterpret_cast<const ANSICHAR*>(Text.Data()), Text.ByteLen());
		return FString(Converted.Length(), Converted.Get());
	}

	FText GetDefinitionCategory(const uLang::CDefinition& Definition)
	{
		const uLang::CSemanticProgram& Program = Definition._EnclosingScope.GetProgram();
		const uLang::CClass* CategoryAttribute =
			Program.FindDefinitionByVersePath<uLang::CClass>(
				uLang::CUTF8StringView("/Verse.org/Simulation/category_attribute"));
		if (CategoryAttribute == nullptr)
		{
			return FText::GetEmpty();
		}
		const uLang::CDefinition* Prototype = Definition.GetPrototypeDefinition();
		const uLang::TOptional<uLang::CUTF8String> Value =
			Prototype->GetAttributes().GetAttributeTextValue(CategoryAttribute, Program);
		return Value.IsSet()
			? FText::FromString(SemanticTextToString(Value.GetValue()))
			: FText::GetEmpty();
	}

	FText GetDefinitionDisplayName(const uLang::CDefinition& Definition)
	{
		const uLang::CSemanticProgram& Program = Definition._EnclosingScope.GetProgram();
		const uLang::CClass* DisplayNameAttribute =
			Program.FindDefinitionByVersePath<uLang::CClass>(
				uLang::CUTF8StringView("/Verse.org/Simulation/display_name_attribute"));
		if (DisplayNameAttribute == nullptr)
		{
			return FText::GetEmpty();
		}
		const uLang::CDefinition* Prototype = Definition.GetPrototypeDefinition();
		const uLang::TOptional<uLang::CUTF8String> Value =
			Prototype->GetAttributes().GetAttributeTextValue(DisplayNameAttribute, Program);
		return Value.IsSet()
			? FText::FromString(SemanticTextToString(Value.GetValue()))
			: FText::GetEmpty();
	}

	FText GetModuleCategory(const uLang::CDefinition& Definition)
	{
		const uLang::CModule* Module = Definition._EnclosingScope.GetModule();
		return Module != nullptr
			? FText::FromString(SemanticTextToString(Module->GetScopePath('|')))
			: FText::GetEmpty();
	}

	FString DefaultSourceForType(const uLang::CTypeBase& Type)
	{
		using namespace uLang;
		switch (Type.GetNormalType().GetKind())
		{
		case ETypeKind::Int:
			return TEXT("0");
		case ETypeKind::Float:
			return TEXT("0.0");
		case ETypeKind::Logic:
		case ETypeKind::True:
		case ETypeKind::False:
			return TEXT("false");
		case ETypeKind::Array:
			return TEXT("array{}");
		default:
			break;
		}
		return SemanticTextToString(Type.AsCode()) == TEXT("string") ? TEXT("\"\"") : FString();
	}

	FString AffixSpelling(
		const uLang::CFunction& Function,
		uLang::CUTF8StringView Prefix)
	{
		const uLang::CUTF8StringView Name = Function.GetName().AsStringView();
		if (!Name.StartsWith(Prefix) || Name.ByteLen() <= Prefix.ByteLen() + 1)
		{
			return FString();
		}
		return SemanticTextToString(
			Name.SubView(Prefix.ByteLen(), Name.ByteLen() - Prefix.ByteLen() - 1));
	}

	TSharedPtr<FVerseExpressionAction> BuildSemanticExpressionAction(
		const FVerseSemanticCandidate& Candidate)
	{
		TSharedPtr<FVerseExpressionAction> Action = MakeShared<FVerseExpressionAction>();
		Action->BoundInputIndex = Candidate.BoundInputIndex;

		if (Candidate.Kind == EVerseSemanticCandidateKind::Identifier)
		{
			if (Candidate.DataDefinition == nullptr || Candidate.DataDefinition->GetType() == nullptr)
			{
				return nullptr;
			}
			Action->SourceForm = EVerseExpressionSourceForm::IdentifierReference;
			Action->SourceSpelling = SemanticTextToString(
				Candidate.DataDefinition->AsNameStringView());
			Action->DisplayName = FText::FromString(Action->SourceSpelling);
			Action->Category = LOCTEXT("VariablesCategory", "Variables");
			Action->ModuleCategory = GetModuleCategory(*Candidate.DataDefinition);
			Action->ResultTypeName = SemanticTextToString(
				Candidate.DataDefinition->GetType()->AsCode());
			return Action;
		}

		const uLang::CFunction* Function = Candidate.Function;
		const uLang::CFunctionType* FunctionType = Candidate.InstantiatedFunctionType;
		if (Function == nullptr || FunctionType == nullptr)
		{
			return nullptr;
		}

		EVerseIntrinsicCallableForm PresentationForm = EVerseIntrinsicCallableForm::Ordinary;
		switch (Candidate.Kind)
		{
		case EVerseSemanticCandidateKind::Function:
			Action->SourceForm = EVerseExpressionSourceForm::OrdinaryCall;
			break;
		case EVerseSemanticCandidateKind::InfixOperator:
			Action->SourceForm = EVerseExpressionSourceForm::InfixOperator;
			PresentationForm = EVerseIntrinsicCallableForm::InfixOperator;
			break;
		case EVerseSemanticCandidateKind::PrefixOperator:
			Action->SourceForm = EVerseExpressionSourceForm::PrefixOperator;
			PresentationForm = EVerseIntrinsicCallableForm::PrefixOperator;
			break;
		case EVerseSemanticCandidateKind::PostfixOperator:
			Action->SourceForm = EVerseExpressionSourceForm::PostfixOperator;
			PresentationForm = EVerseIntrinsicCallableForm::PostfixOperator;
			break;
		default:
			return nullptr;
		}

		const bool bOperator = Candidate.Kind != EVerseSemanticCandidateKind::Function;
		const bool bExtensionMethod = Function->_ExtensionFieldAccessorKind ==
			uLang::EExtensionFieldAccessorKind::ExtensionMethod;
		if (bOperator)
		{
			const uLang::CIntrinsicSymbols& Symbols = Function->GetProgram()._IntrinsicSymbols;
			// Query is semantically classified as postfix by our compatibility layer,
			// but UE 6.0 still spells its compiler symbol `operator'?'`. Select the
			// prefix from the compiler's actual symbol namespace so both today's engine
			// and a future native `postfix'?'` symbol produce the source spelling `?`.
			uLang::CUTF8StringView AffixPrefix("postfix'");
			if (Symbols.IsOperatorOpName(Function->GetName()))
			{
				AffixPrefix = uLang::CUTF8StringView("operator'");
			}
			else if (Symbols.IsPrefixOpName(Function->GetName()))
			{
				AffixPrefix = uLang::CUTF8StringView("prefix'");
			}
			Action->SourceSpelling = AffixSpelling(*Function, AffixPrefix);
		}
		else if (bExtensionMethod)
		{
			Action->SourceSpelling = SemanticTextToString(
				Function->GetProgram()._IntrinsicSymbols.StripExtensionFieldOpName(
					Function->GetName()));
			Action->SourceSpelling.RemoveFromStart(TEXT("."));
		}
		else
		{
			Action->SourceSpelling = SemanticTextToString(Function->AsNameStringView());
		}
		if (Action->SourceSpelling.IsEmpty())
		{
			return nullptr;
		}

		const uLang::CFunctionType::ParamTypes Params = FunctionType->GetParamTypes();
		FVerseIntrinsicPresentationKey PresentationKey;
		PresentationKey.Form = PresentationForm;
		PresentationKey.Spelling = Action->SourceSpelling;
		for (const uLang::CTypeBase* Param : Params)
		{
			PresentationKey.ParameterTypes.Add(SemanticTextToString(Param->AsCode()));
		}
		PresentationKey.ResultType = SemanticTextToString(
			FunctionType->GetReturnType().AsCode());
		const FVerseIntrinsicPresentationDescriptor* IntrinsicPresentation =
			FindVerseIntrinsicPresentation(PresentationKey);
		if (IntrinsicPresentation != nullptr
			&& IntrinsicPresentation->bSymmetricOperands
			&& Candidate.BoundInputIndex > 0)
		{
			return nullptr;
		}
		const TOptional<FVerseBlueprintCallablePresentation> BlueprintPresentation =
			bOperator && (IntrinsicPresentation == nullptr
				|| IntrinsicPresentation->BlueprintLibrary ==
					EVerseIntrinsicBlueprintLibrary::None)
				? TOptional<FVerseBlueprintCallablePresentation>()
				: ResolveVerseBlueprintCallablePresentation(
					Action->SourceSpelling, *FunctionType, IntrinsicPresentation);
		const FVerseResolvedExpressionPresentation Presentation =
			ResolveVerseExpressionPresentation(
				GetDefinitionDisplayName(*Function),
				GetDefinitionCategory(*Function),
				BlueprintPresentation.IsSet()
					? BlueprintPresentation->ExplicitDisplayName
					: FText::GetEmpty(),
				BlueprintPresentation.IsSet()
					? BlueprintPresentation->Category
					: FText::GetEmpty(),
				IntrinsicPresentation,
				Action->SourceSpelling);
		Action->DisplayName = Presentation.DisplayName;
		Action->Category = Presentation.Category;
		Action->ModuleCategory = GetModuleCategory(*Function);
		Action->ResultTypeName = PresentationKey.ResultType;
		Action->bUsesFailureCallSyntax =
			Function->_Signature.GetEffects()[uLang::EEffect::decides];

		const uLang::SSignature::ParamDefinitions& Definitions =
			Function->_Signature.GetParams();
		for (int32 Index = 0; Index < Params.Num(); ++Index)
		{
			Action->InputTypeNames.Add(SemanticTextToString(Params[Index]->AsCode()));
			const uLang::CDataDefinition* Definition = Definitions.IsValidIndex(Index)
				? Definitions[Index]
				: nullptr;
			const uLang::CDefinition* NameDefinition = Definition != nullptr
				&& Definition->_ImplicitParam != nullptr
				? static_cast<const uLang::CDefinition*>(Definition->_ImplicitParam)
				: static_cast<const uLang::CDefinition*>(Definition);
			Action->InputNames.Add(NameDefinition != nullptr
				? SemanticTextToString(NameDefinition->AsNameStringView())
				: FString());
			Action->NamedInputs.Add(Definition != nullptr && Definition->_bNamed);
			if (Index == Candidate.BoundInputIndex)
			{
				Action->InputDefaultSources.Add(FString());
				continue;
			}
			FString Default = DefaultSourceForType(*Params[Index]);
			if (Default.IsEmpty()
				&& IntrinsicPresentation != nullptr
				&& IntrinsicPresentation->DefaultSourceTypeParameterIndices.IsValidIndex(Index))
			{
				const int32 TypeSourceIndex =
					IntrinsicPresentation->DefaultSourceTypeParameterIndices[Index];
				if (Candidate.MatchedSocketType != nullptr
					&& (Candidate.BoundInputIndex == INDEX_NONE
						|| TypeSourceIndex == Candidate.BoundInputIndex))
				{
					Default = DefaultSourceForType(*Candidate.MatchedSocketType);
				}
				else if (TypeSourceIndex >= 0 && TypeSourceIndex < Params.Num())
				{
					Default = DefaultSourceForType(*Params[TypeSourceIndex]);
				}
			}
			if (Default.IsEmpty()
				&& Candidate.MatchedSocketType == nullptr
				&& IntrinsicPresentation != nullptr)
			{
				Default = IntrinsicPresentation->UntypedDefaultSource;
			}
			if (Default.IsEmpty())
			{
				return nullptr;
			}
			Action->InputDefaultSources.Add(MoveTemp(Default));
		}
		return Action;
	}

	bool UsesPreferredUntypedOperand(
		const FVerseExpressionAction& Action,
		FStringView PreferredType)
	{
		return !PreferredType.IsEmpty()
			&& !Action.InputTypeNames.IsEmpty()
			&& !Action.InputTypeNames.ContainsByPredicate(
				[PreferredType](const FString& Type)
				{
					return !FStringView(Type).Equals(
						PreferredType, ESearchCase::IgnoreCase);
				});
	}

	void GroupPolymorphicOperatorActions(
		TArray<TSharedPtr<FVerseExpressionAction>>& Actions)
	{
		TArray<TSharedPtr<FVerseExpressionAction>> Grouped;
		TMap<FString, int32> GroupIndices;
		for (TSharedPtr<FVerseExpressionAction>& Action : Actions)
		{
			if (!Action.IsValid())
			{
				continue;
			}
			EVerseIntrinsicCallableForm Form;
			switch (Action->SourceForm)
			{
			case EVerseExpressionSourceForm::InfixOperator:
				Form = EVerseIntrinsicCallableForm::InfixOperator;
				break;
			case EVerseExpressionSourceForm::PrefixOperator:
				Form = EVerseIntrinsicCallableForm::PrefixOperator;
				break;
			case EVerseExpressionSourceForm::PostfixOperator:
				Form = EVerseIntrinsicCallableForm::PostfixOperator;
				break;
			default:
				Grouped.Add(MoveTemp(Action));
				continue;
			}
			const FVerseIntrinsicPresentationDescriptor* Presentation =
				FindVerseIntrinsicOperatorPresentation(Form, Action->SourceSpelling);
			if (Presentation == nullptr || !Presentation->bGroupOverloadsInActionMenu)
			{
				Grouped.Add(MoveTemp(Action));
				continue;
			}
			const FString Key = FString::Printf(
				TEXT("%d|%s"), static_cast<int32>(Form), *Action->SourceSpelling);
			if (const int32* ExistingIndex = GroupIndices.Find(Key))
			{
				TSharedPtr<FVerseExpressionAction>& Existing = Grouped[*ExistingIndex];
				if (!UsesPreferredUntypedOperand(
						*Existing, Presentation->PreferredUntypedOperandType)
					&& UsesPreferredUntypedOperand(
						*Action, Presentation->PreferredUntypedOperandType))
				{
					Existing = MoveTemp(Action);
				}
				continue;
			}
			GroupIndices.Add(Key, Grouped.Num());
			Grouped.Add(MoveTemp(Action));
		}
		Actions = MoveTemp(Grouped);
	}

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

	struct FLiteralActionDescriptor
	{
		EVerseLiteralKind Kind;
		const TCHAR* Source;
		const TCHAR* Type;
		FText DisplayName;
		bool bCanonicalDefault;
	};

	TConstArrayView<FLiteralActionDescriptor> GetLiteralActionDescriptors()
	{
		static const FLiteralActionDescriptor Descriptors[] = {
			{EVerseLiteralKind::Logic, TEXT("true"), TEXT("logic"), LOCTEXT("TrueLiteral", "True"), false},
			{EVerseLiteralKind::Logic, TEXT("false"), TEXT("logic"), LOCTEXT("FalseLiteral", "False"), true},
			{EVerseLiteralKind::Integer, TEXT("0"), TEXT("int"), LOCTEXT("IntegerLiteral", "Integer"), true},
			{EVerseLiteralKind::Float, TEXT("0.0"), TEXT("float"), LOCTEXT("FloatLiteral", "Float"), true},
			{EVerseLiteralKind::String, TEXT("\"\""), TEXT("string"), LOCTEXT("StringLiteral", "String"), true},
			{EVerseLiteralKind::Character, TEXT("'a'"), TEXT("char"), LOCTEXT("CharacterLiteral", "Character"), true},
		};
		return Descriptors;
	}

	void AppendLiteralActions(
		TArray<TSharedPtr<FVerseExpressionAction>>& Actions,
		FString RequiredType = FString())
	{
		RequiredType = NormalizeActionType(MoveTemp(RequiredType));
		for (const FLiteralActionDescriptor& Descriptor : GetLiteralActionDescriptors())
		{
			if (!RequiredType.IsEmpty()
				&& RequiredType != NormalizeActionType(Descriptor.Type))
			{
				continue;
			}
			TSharedPtr<FVerseExpressionAction> Action = MakeShared<FVerseExpressionAction>();
			Action->SourceForm = EVerseExpressionSourceForm::Literal;
			Action->SourceSpelling = Descriptor.Source;
			Action->DisplayName = Descriptor.DisplayName;
			Action->Category = LOCTEXT("LiteralsCategory", "Literals");
			Action->ModuleCategory = LOCTEXT("VerseLanguageModule", "Verse");
			Action->ResultTypeName = Descriptor.Type;
			Actions.Add(MoveTemp(Action));
		}
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
		FVerseExpressionType OutputType;
	};

	bool TypesMatch(
		const FVerseExpressionType& Left,
		const FVerseExpressionType& Right,
		FUtf8StringView Source)
	{
		const FString LeftName = GetTypeName(Left, Source);
		const FString RightName = GetTypeName(Right, Source);
		return !LeftName.IsEmpty() && LeftName == RightName;
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
			Candidate.Action->Category = LOCTEXT("VariablesCategory", "Variables");
			Candidate.Action->ModuleCategory = LOCTEXT("CurrentModuleCategory", "Current Module");
			Candidate.Action->IdentifierNameRange = Parameter.NameRange;
			Candidate.Action->SourceSpelling =
				Document.DecodeOriginalRange(Parameter.NameRange);
			Candidate.OutputType = {
				Parameter.TypeRange,
				NAME_None,
				EVerseTypeResolutionProvenance::LocallyInferred};
		}
		return Candidates;
	}
}

TOptional<FString> GetDefaultVerseLiteralSourceForType(FStringView TypeName)
{
	const FString NormalizedType = NormalizeActionType(FString(TypeName));
	for (const FLiteralActionDescriptor& Descriptor : GetLiteralActionDescriptors())
	{
		if (Descriptor.bCanonicalDefault
			&& NormalizedType == NormalizeActionType(Descriptor.Type))
		{
			return FString(Descriptor.Source);
		}
	}
	return {};
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
		// Syntax-only fallback can safely offer identifiers as values, but it
		// cannot authorize any callable or operator signature. Those come only
		// from FVerseSemanticCandidateProvider.
		const bool bCompatible = !bDraggingFromOutput
			&& Candidate.OutputType.IsResolved()
			&& TypesMatch(Candidate.OutputType, SocketType, Source);
		if (bCompatible)
		{
			Candidate.Action->ResultTypeName = GetTypeName(Candidate.OutputType, Source);
			Result.Add(MoveTemp(Candidate.Action));
		}
	}
	if (!bDraggingFromOutput)
	{
		AppendLiteralActions(Result,
			!DraggedSocket.SemanticTypeName.IsEmpty()
				? DraggedSocket.SemanticTypeName
				: GetTypeName(SocketType, Source));
	}
	return Result;
}

TArray<TSharedPtr<FVerseExpressionAction>> FVerseExpressionActionQuery::BuildAll(
	TConstArrayView<FVerseFunctionNavigationParameter> Parameters,
	const FVerseDocument& Document,
	FVerseTextRange ScopeAnchorRange,
	const FString& FilePath,
	TConstArrayView<TSharedPtr<const FVerseSemanticSnapshot>> SemanticSnapshots)
{
	TArray<TSharedPtr<FVerseExpressionAction>> Result;
	for (const FVerseSemanticCandidate& Candidate :
		FVerseSemanticCandidateProvider::BuildAll(
			SemanticSnapshots,
			FilePath,
			ScopeAnchorRange.BeginByte,
			Document))
	{
		TSharedPtr<FVerseExpressionAction> Action =
			BuildSemanticExpressionAction(Candidate);
		if (Action.IsValid())
		{
			if (Action->ModuleCategory.IsEmpty())
			{
				Action->ModuleCategory = LOCTEXT("CurrentModuleCategory", "Current Module");
			}
			Result.Add(MoveTemp(Action));
		}
	}

	// Syntax-only identifiers remain safe when the exact semantic snapshot is
	// unavailable; insertion is still prospectively parsed before mutation.
	if (!SemanticSnapshots.ContainsByPredicate(
		[&FilePath, Revision = ScopeAnchorRange.Revision](const auto& Snapshot)
		{
			return Snapshot.IsValid() && Snapshot->Describes(FilePath, Revision);
		}))
	{
		for (const FVerseFunctionNavigationParameter& Parameter : Parameters)
		{
			TSharedPtr<FVerseExpressionAction> Action = MakeShared<FVerseExpressionAction>();
			Action->SourceForm = EVerseExpressionSourceForm::IdentifierReference;
			Action->SourceSpelling = Document.DecodeOriginalRange(Parameter.NameRange);
			Action->DisplayName = FText::FromString(Action->SourceSpelling);
			Action->Category = LOCTEXT("VariablesCategory", "Variables");
			Action->ModuleCategory = LOCTEXT("CurrentModuleCategory", "Current Module");
			Result.Add(MoveTemp(Action));
		}
	}
	AppendLiteralActions(Result);
	TSharedPtr<FVerseExpressionAction> IfAction = MakeShared<FVerseExpressionAction>();
	IfAction->SourceForm = EVerseExpressionSourceForm::StructuralExpression;
	IfAction->SourceSpelling = TEXT("if (true?) {}");
	IfAction->ProvisionalContentTarget =
		EVerseProvisionalContentTarget::FirstConditionExpression;
	IfAction->DisplayName = LOCTEXT("CreateIfExpression", "If");
	IfAction->Category = LOCTEXT("FlowControlCategory", "Flow Control");
	IfAction->ModuleCategory = LOCTEXT("CurrentModuleCategory", "Current Module");
	Result.Add(MoveTemp(IfAction));

	TSharedPtr<FVerseExpressionAction> VariableAction = MakeShared<FVerseExpressionAction>();
	VariableAction->SourceForm = EVerseExpressionSourceForm::Definition;
	VariableAction->SourceSpelling = TEXT("var NewVariable : int = 0");
	VariableAction->DisplayName = LOCTEXT("CreateVariableDefinition", "Variable Definition");
	VariableAction->Category = LOCTEXT("VariablesCategory", "Variables");
	VariableAction->ModuleCategory = LOCTEXT("CurrentModuleCategory", "Current Module");
	Result.Add(MoveTemp(VariableAction));

	TSharedPtr<FVerseExpressionAction> ConstantAction = MakeShared<FVerseExpressionAction>();
	ConstantAction->SourceForm = EVerseExpressionSourceForm::Definition;
	ConstantAction->SourceSpelling = TEXT("NewConstant : int = 0");
	ConstantAction->DisplayName = LOCTEXT("CreateConstantDefinition", "Constant Definition");
	ConstantAction->Category = LOCTEXT("VariablesCategory", "Variables");
	ConstantAction->ModuleCategory = LOCTEXT("CurrentModuleCategory", "Current Module");
	Result.Add(MoveTemp(ConstantAction));
	GroupPolymorphicOperatorActions(Result);
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
			Document,
			&DraggedSocket);
	for (const FVerseSemanticCandidate& Candidate : SemanticCandidates)
	{
		TSharedPtr<FVerseExpressionAction> Action =
			BuildSemanticExpressionAction(Candidate);
		if (!Action.IsValid())
		{
			continue;
		}
		if (Action->ModuleCategory.IsEmpty())
		{
			Action->ModuleCategory = LOCTEXT("CurrentModuleCategory", "Current Module");
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
	else if (!bDraggingFromOutput)
	{
		const FVerseExpressionType SocketType{
			DraggedSocket.TypeRange,
			DraggedSocket.IntrinsicTypeName,
			EVerseTypeResolutionProvenance::LocallyInferred};
		AppendLiteralActions(Result,
			!DraggedSocket.SemanticTypeName.IsEmpty()
				? DraggedSocket.SemanticTypeName
				: GetTypeName(SocketType, Document.GetOriginalUtf8View()));
	}
	GroupPolymorphicOperatorActions(Result);
	return Result;
}

bool BuildVerseExpressionActionSource(
	const FVerseExpressionAction& Action,
	FStringView BoundExpressionSource,
	FString& OutSource,
	FText& OutError)
{
	if (Action.SourceForm == EVerseExpressionSourceForm::IdentifierReference)
	{
		if (Action.SourceSpelling.IsEmpty())
		{
			OutError = LOCTEXT("MissingIdentifierSpelling", "The selected identifier has no source spelling.");
			return false;
		}
		OutSource = Action.SourceSpelling;
		return true;
	}
	if (Action.SourceForm == EVerseExpressionSourceForm::Literal)
	{
		if (Action.SourceSpelling.IsEmpty())
		{
			OutError = LOCTEXT("MissingLiteralSource", "The selected literal has no source value.");
			return false;
		}
		OutSource = Action.SourceSpelling;
		return true;
	}
	if (Action.SourceForm == EVerseExpressionSourceForm::StructuralExpression
		|| Action.SourceForm == EVerseExpressionSourceForm::Definition)
	{
		if (Action.SourceSpelling.IsEmpty())
		{
			OutError = LOCTEXT("MissingStructuralSource", "The selected structural expression has no source template.");
			return false;
		}
		OutSource = Action.SourceSpelling;
		return true;
	}
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
		if (!Inputs.IsValidIndex(Action.BoundInputIndex)
			|| BoundExpressionSource.TrimStartAndEnd().IsEmpty())
		{
			OutError = LOCTEXT(
				"InvalidBoundExpressionInput",
				"The selected expression cannot preserve the dragged input.");
			return false;
		}
		Inputs[Action.BoundInputIndex] = FString(BoundExpressionSource).TrimStartAndEnd();
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
	{
		TArray<FString> Arguments;
		Arguments.Reserve(Inputs.Num());
		for (int32 Index = 0; Index < Inputs.Num(); ++Index)
		{
			const bool bNamed = Action.NamedInputs.IsValidIndex(Index)
				&& Action.NamedInputs[Index];
			const FString Name = Action.InputNames.IsValidIndex(Index)
				? Action.InputNames[Index]
				: FString();
			Arguments.Add(bNamed && !Name.IsEmpty()
				? FString::Printf(TEXT("?%s := %s"), *Name, *Inputs[Index])
				: Inputs[Index]);
		}
		OutSource = Action.bUsesFailureCallSyntax
			? FString::Printf(TEXT("%s[%s]"), *Action.SourceSpelling, *FString::Join(Arguments, TEXT(", ")))
			: FString::Printf(TEXT("%s(%s)"), *Action.SourceSpelling, *FString::Join(Arguments, TEXT(", ")));
		return true;
	}
	case EVerseExpressionSourceForm::InfixOperator:
		if (Inputs.Num() == 2)
		{
			OutSource = FString::Printf(TEXT("%s %s %s"), *Inputs[0], *Action.SourceSpelling, *Inputs[1]);
			return true;
		}
		OutError = LOCTEXT("InvalidInfixInputs", "An infix operator requires exactly two operands.");
		return false;
	case EVerseExpressionSourceForm::PrefixOperator:
		if (Inputs.Num() == 1)
		{
			OutSource = FString::Printf(TEXT("%s %s"), *Action.SourceSpelling, *Inputs[0]);
			return true;
		}
		OutError = LOCTEXT("InvalidPrefixInputs", "A prefix operator requires exactly one operand.");
		return false;
	case EVerseExpressionSourceForm::PostfixOperator:
		if (Inputs.Num() == 1)
		{
			OutSource = FString::Printf(TEXT("%s%s"), *Inputs[0], *Action.SourceSpelling);
			return true;
		}
		OutError = LOCTEXT("InvalidPostfixInputs", "A postfix operator requires exactly one operand.");
		return false;
	case EVerseExpressionSourceForm::Literal:
		return true;
	default:
		OutError = LOCTEXT("InvalidExpressionSourceForm", "The selected expression source form is unsupported.");
		return false;
	}
}

namespace
{
	bool TryReplaceExpressionSource(
		FVerseDocumentSession& Session,
		FVerseTextRange ExpressionRange,
		FStringView Replacement,
		EVerseExpressionKind RequiredKind,
		FText& OutError)
	{
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
		TSharedPtr<const FVerseDocument> CandidateDocument =
			FVerseDocument::CreateFromBytes(CandidateBytes, OutError);
		if (!CandidateDocument.IsValid())
		{
			return false;
		}
		const FVerseParseSnapshot CandidateSnapshot =
			FVerseParseSnapshotBuilder::Build(CandidateDocument.ToSharedRef());
		const TArray<FVerseVisualTile> CandidateTiles =
			FVerseVisualTileBuilder::Build(CandidateSnapshot);
		const TArray<FVerseFunctionNavigationItem> Functions =
			FVerseFunctionNavigationBuilder::Build(CandidateTiles, CandidateSnapshot);
		const bool bRecognizedAtReplacement = Functions.ContainsByPredicate(
			[&](const FVerseFunctionNavigationItem& Function)
			{
				return ContainsExpressionAt(
					Function.GraphTiles, ExpressionRange.BeginByte, RequiredKind);
			});
		if (!bRecognizedAtReplacement)
		{
			OutError = LOCTEXT(
				"ExpressionRejected",
				"The expression would not produce a valid supported Verse structure.");
			return false;
		}
		return Session.Replace(ExpressionRange, ReplacementUtf8, OutError);
	}
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
	const FString Existing = Document->DecodeOriginalRange(ExpressionRange).TrimStartAndEnd();
	FString Replacement;
	if (!BuildVerseExpressionActionSource(Action, Existing, Replacement, OutError))
	{
		return false;
	}
	EVerseExpressionKind RequiredKind = EVerseExpressionKind::Unsupported;
	switch (Action.SourceForm)
	{
	case EVerseExpressionSourceForm::IdentifierReference: RequiredKind = EVerseExpressionKind::Identifier; break;
	case EVerseExpressionSourceForm::OrdinaryCall: RequiredKind = EVerseExpressionKind::Call; break;
	case EVerseExpressionSourceForm::InfixOperator: RequiredKind = EVerseExpressionKind::BinaryOperator; break;
	case EVerseExpressionSourceForm::PrefixOperator:
	case EVerseExpressionSourceForm::PostfixOperator: RequiredKind = EVerseExpressionKind::UnaryOperator; break;
	case EVerseExpressionSourceForm::Literal: RequiredKind = EVerseExpressionKind::Literal; break;
	case EVerseExpressionSourceForm::StructuralExpression: RequiredKind = EVerseExpressionKind::Control; break;
	case EVerseExpressionSourceForm::Definition: RequiredKind = EVerseExpressionKind::Definition; break;
	default: break;
	}

	return TryReplaceExpressionSource(
		Session, ExpressionRange, Replacement, RequiredKind, OutError);
}

bool TryMaterializeVerseNamedInput(
	FVerseDocumentSession& Session,
	FVerseTextRange CallRange,
	FStringView InputName,
	const FVerseExpressionAction& Action,
	FText& OutError)
{
	if (CallRange.Revision != Session.GetRevision()
		|| InputName.TrimStartAndEnd().IsEmpty())
	{
		OutError = LOCTEXT(
			"InvalidNamedInputMaterialization",
			"The omitted named input is no longer valid.");
		return false;
	}
	FString ProviderSource;
	if (!BuildVerseExpressionActionSource(
		Action, FStringView(), ProviderSource, OutError))
	{
		return false;
	}
	const TSharedRef<const FVerseDocument> Document =
		Session.GetParseSnapshot().GetDocument();
	FString CallSource = Document->DecodeOriginalRange(CallRange).TrimStartAndEnd();
	int32 ClosingIndex = CallSource.Len() - 1;
	while (ClosingIndex >= 0 && FChar::IsWhitespace(CallSource[ClosingIndex]))
	{
		--ClosingIndex;
	}
	if (ClosingIndex < 0
		|| (CallSource[ClosingIndex] != TEXT(')')
			&& CallSource[ClosingIndex] != TEXT(']')))
	{
		OutError = LOCTEXT(
			"MissingCallDelimiterForNamedInput",
			"The call's closing delimiter could not be found.");
		return false;
	}
	const int32 OpeningIndex = CallSource.Find(
		CallSource[ClosingIndex] == TEXT(')') ? TEXT("(") : TEXT("["));
	const bool bHasArguments = OpeningIndex != INDEX_NONE
		&& !CallSource.Mid(OpeningIndex + 1, ClosingIndex - OpeningIndex - 1)
			.TrimStartAndEnd().IsEmpty();
	const FString Argument = FString::Printf(
		TEXT("?%s := %s"),
		*FString(InputName).TrimStartAndEnd(),
		*ProviderSource);
	CallSource.InsertAt(
		ClosingIndex,
		(bHasArguments ? TEXT(", ") : TEXT("")) + Argument);
	return TryReplaceExpressionSource(
		Session,
		CallRange,
		CallSource,
		EVerseExpressionKind::Call,
		OutError);
}

#undef LOCTEXT_NAMESPACE
