#include "VerseSemanticCandidates.h"

#include "VerseDocument.h"
#include "VerseBlueprintCallablePresentation.h"
#include "VerseIntrinsicPresentation.h"
#include "VerseSemanticWorkspace.h"
#include "uLang/Semantics/DataDefinition.h"
#include "uLang/Semantics/Expression.h"
#include "uLang/Semantics/SemanticFunction.h"
#include "uLang/Semantics/SemanticProgram.h"
#include "uLang/Semantics/SemanticScope.h"
#include "uLang/Semantics/SemanticTypes.h"
#include "uLang/Semantics/VisitSet.h"
#include "uLang/SourceProject/UploadedAtFNVersion.h"
#include "uLang/Syntax/VstNode.h"

namespace
{
	uLang::STextPosition ByteOffsetToPosition(FUtf8StringView Source, int32 ByteOffset)
	{
		uLang::STextPosition Result{0, 0};
		const int32 End = FMath::Clamp(ByteOffset, 0, Source.Len());
		for (int32 Index = 0; Index < End; ++Index)
		{
			if (Source[Index] == static_cast<UTF8CHAR>('\r'))
			{
				if (Index + 1 < End && Source[Index + 1] == static_cast<UTF8CHAR>('\n'))
				{
					++Index;
				}
				++Result._Row;
				Result._Column = 0;
			}
			else if (Source[Index] == static_cast<UTF8CHAR>('\n'))
			{
				++Result._Row;
				Result._Column = 0;
			}
			else
			{
				++Result._Column;
			}
		}
		return Result;
	}

	const Verse::Vst::Node* FindSemanticNode(
		const FVerseSemanticSnapshot& Snapshot,
		const FString& FilePath,
		int32 BeginByte,
		const FVerseDocument& Document)
	{
		if (!Snapshot.GetProjectVst().IsValid())
		{
			return nullptr;
		}
		const FTCHARToUTF8 Utf8Path(*FilePath);
		const Verse::Vst::Snippet* Snippet = Snapshot.GetProjectVst()->FindSnippetByFilePath(
			uLang::CUTF8StringView(Utf8Path.Get(), Utf8Path.Length()));
		if (Snippet == nullptr)
		{
			return nullptr;
		}
		return Snippet->FindChildByPosition(ByteOffsetToPosition(
			Document.GetOriginalUtf8View(), BeginByte));
	}

	const uLang::CScope* ScopeFromAst(
		const uLang::CAstNode& Ast,
		const uLang::CSemanticProgram& Program)
	{
		switch (Ast.GetNodeType())
		{
		case uLang::EAstNodeType::Context_Snippet:
			return static_cast<const uLang::CExprSnippet&>(Ast)._SemanticSnippet;
		case uLang::EAstNodeType::Definition_Module:
			return static_cast<const uLang::CExprModuleDefinition&>(Ast)._SemanticModule;
		case uLang::EAstNodeType::Definition_Class:
			return &static_cast<const uLang::CExprClassDefinition&>(Ast)._Class;
		case uLang::EAstNodeType::Definition_Function:
			return static_cast<const uLang::CExprFunctionDefinition&>(Ast)._Function.Get();
		case uLang::EAstNodeType::Flow_CodeBlock:
			return static_cast<const uLang::CExprCodeBlock&>(Ast)._AssociatedScope.Get();
		case uLang::EAstNodeType::Flow_Iteration:
		{
			const uLang::CExprIteration& Iteration =
				static_cast<const uLang::CExprIteration&>(Ast);
			return Iteration._Filters ? Iteration._Filters->_AssociatedScope.Get() : nullptr;
		}
		default:
			return nullptr;
		}
	}

	const uLang::CScope* FindActiveScope(
		const Verse::Vst::Node& Node,
		const uLang::CSemanticProgram& Program)
	{
		for (const Verse::Vst::Node* Current = &Node;
			Current != nullptr;
			Current = Current->GetParent())
		{
			if (const uLang::CAstNode* Ast = Current->GetMappedAstNode())
			{
				if (const uLang::CScope* Scope = ScopeFromAst(*Ast, Program))
				{
					return Scope;
				}
			}
		}
		return nullptr;
	}

	const uLang::CTypeBase* FindExpressionType(
		const Verse::Vst::Node& Node,
		const uLang::CSemanticProgram& Program)
	{
		for (const Verse::Vst::Node* Current = &Node;
			Current != nullptr;
			Current = Current->GetParent())
		{
			if (const uLang::CAstNode* Ast = Current->GetMappedAstNode())
			{
				if (const uLang::CExpressionBase* Expression = Ast->AsExpression())
				{
					if (const uLang::CTypeBase* Type = Expression->GetResultType(Program))
					{
						return Type;
					}
				}
			}
		}
		return nullptr;
	}

	FString ToFString(uLang::CUTF8StringView Text)
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
			? FText::FromString(ToFString(Value.GetValue()))
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
			? FText::FromString(ToFString(Value.GetValue()))
			: FText::GetEmpty();
	}

	FText GetModuleCategory(const uLang::CDefinition& Definition)
	{
		const uLang::CModule* Module = Definition._EnclosingScope.GetModule();
		if (Module == nullptr)
		{
			return FText::GetEmpty();
		}
		return FText::FromString(ToFString(Module->GetScopePath('|')));
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
		const FString TypeCode = ToFString(Type.AsCode());
		return TypeCode == TEXT("string") ? TEXT("\"\"") : FString();
	}

	void CollectUsingScope(
		const uLang::CLogicalScope& Scope,
		TSet<const uLang::CLogicalScope*>& VisitedScopes,
		uLang::CVisitSet& VisitedDefinitions,
		TArray<const uLang::CDefinition*>& Definitions)
	{
		if (VisitedScopes.Contains(&Scope))
		{
			return;
		}
		VisitedScopes.Add(&Scope);
		for (const uLang::TSRef<uLang::CDefinition>& Definition : Scope.GetDefinitions())
		{
			if (Definition->TryMarkOverriddenAndConstrainedDefinitionsVisited(
				VisitedDefinitions))
			{
				Definitions.Add(Definition.Get());
			}
		}
		for (const uLang::CLogicalScope* UsingScope : Scope.GetUsingScopes())
		{
			if (UsingScope != nullptr)
			{
				CollectUsingScope(
					*UsingScope, VisitedScopes, VisitedDefinitions, Definitions);
			}
		}
	}

	TArray<const uLang::CDefinition*> CollectVisibleDefinitions(
		const uLang::CScope& ActiveScope,
		const uLang::CSemanticProgram& Program)
	{
		TArray<const uLang::CDefinition*> Definitions;
		TSet<const uLang::CLogicalScope*> VisitedScopes;
		uLang::CVisitSet VisitedDefinitions;
		for (const uLang::CScope* Scope = &ActiveScope;
			Scope != nullptr;
			Scope = Scope->GetParentScope())
		{
			if (const uLang::CLogicalScope* Logical = Scope->AsLogicalScopeNullable())
			{
				CollectUsingScope(
					*Logical, VisitedScopes, VisitedDefinitions, Definitions);
			}
		}

		// Operators participate in overload resolution as intrinsic/global
		// functions even when their defining module is not a normal completion
		// scope. Visibility and access are still checked below.
		Program.IterateRecurseLogicalScopes(
			[&Definitions, &Program, &VisitedDefinitions](const uLang::CLogicalScope& Scope)
			{
				for (const uLang::CFunction* Function :
					Scope.GetDefinitionsOfKind<uLang::CFunction>())
				{
					if (Program._IntrinsicSymbols.IsOperatorOpName(Function->GetName())
						|| Program._IntrinsicSymbols.IsPrefixOpName(Function->GetName())
						|| Program._IntrinsicSymbols.IsPostfixOpName(Function->GetName()))
					{
						if (Function->TryMarkOverriddenAndConstrainedDefinitionsVisited(
							VisitedDefinitions))
						{
							Definitions.Add(Function);
						}
					}
				}
				return uLang::EVisitResult::Continue;
			});
		return Definitions;
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
		return ToFString(Name.SubView(Prefix.ByteLen(), Name.ByteLen() - Prefix.ByteLen() - 1));
	}

	bool IsVisibleAtCurrentPackageVersion(
		const uLang::CDefinition& Definition,
		const uLang::CScope& ActiveScope,
		const uLang::CAstPackage* ContextPackage)
	{
		const uLang::CDefinition* Prototype = Definition.GetPrototypeDefinition();
		for (const uLang::SResolvedDefinition& Resolved :
			ActiveScope.ResolveDefinition(
				Definition.GetName(), uLang::SQualifier::Unknown(), ContextPackage))
		{
			if (Resolved._Definition != nullptr
				&& Resolved._Definition->GetPrototypeDefinition() == Prototype)
			{
				return true;
			}
		}
		return false;
	}

	void AddFunctionCandidates(
		const uLang::CFunction& Function,
		const uLang::CScope& ActiveScope,
		const uLang::CTypeBase& SocketType,
		bool bDraggingFromOutput,
		uint32 UploadedVersion,
		const TSharedPtr<const FVerseSemanticSnapshot>& Snapshot,
		TArray<FVerseSemanticCandidate>& Out)
	{
		if (!Function.IsAccessibleFrom(ActiveScope)
			|| Function.IsInstanceMember()
			|| Function._Signature.GetFunctionType() == nullptr)
		{
			return;
		}
		const uLang::CFunctionType* FunctionType = uLang::SemanticTypeUtils::Instantiate(
			Function._Signature.GetFunctionType(), UploadedVersion);
		if (FunctionType == nullptr)
		{
			return;
		}
		const uLang::CFunctionType::ParamTypes Params = FunctionType->GetParamTypes();
		const bool bExtensionMethod = Function._ExtensionFieldAccessorKind ==
			uLang::EExtensionFieldAccessorKind::ExtensionMethod;
		const bool bInfix = !bExtensionMethod
			&& Function.GetProgram()._IntrinsicSymbols.IsOperatorOpName(
				Function.GetName());
		const bool bPrefix = Function.GetProgram()._IntrinsicSymbols.IsPrefixOpName(
			Function.GetName());
		const bool bPostfix = Function.GetProgram()._IntrinsicSymbols.IsPostfixOpName(
			Function.GetName());
		const bool bOperator = bInfix || bPrefix || bPostfix;
		// operator'()' is compiler plumbing for non-function invocation (array/map
		// access), not an expression the user can create from the action menu.
		if (Function.GetName() == Function.GetProgram()._IntrinsicSymbols._OpNameCall)
		{
			return;
		}
		const bool bUsesFailureCallSyntax =
			Function._Signature.GetEffects()[uLang::EEffect::decides];
		FString Spelling;
		if (bOperator)
		{
			Spelling = AffixSpelling(
				Function,
				bInfix
					? uLang::CUTF8StringView("operator'")
					: (bPrefix
						? uLang::CUTF8StringView("prefix'")
						: uLang::CUTF8StringView("postfix'")));
		}
		else if (bExtensionMethod)
		{
			Spelling = ToFString(
				Function.GetProgram()._IntrinsicSymbols.StripExtensionFieldOpName(
					Function.GetName()));
			Spelling.RemoveFromStart(TEXT("."));
		}
		else
		{
			Spelling = ToFString(Function.AsNameStringView());
		}
		if (Spelling.IsEmpty())
		{
			return;
		}
		const FText DefinitionCategory = GetDefinitionCategory(Function);
		const FText DefinitionDisplayName = GetDefinitionDisplayName(Function);
		FVerseIntrinsicPresentationKey PresentationKey;
		PresentationKey.Form = bInfix
			? EVerseIntrinsicCallableForm::InfixOperator
			: (bPrefix
				? EVerseIntrinsicCallableForm::PrefixOperator
				: (bPostfix
					? EVerseIntrinsicCallableForm::PostfixOperator
					: EVerseIntrinsicCallableForm::Ordinary));
		PresentationKey.Spelling = Spelling;
		for (const uLang::CTypeBase* Param : Params)
		{
			PresentationKey.ParameterTypes.Add(ToFString(Param->AsCode()));
		}
		PresentationKey.ResultType = ToFString(FunctionType->GetReturnType().AsCode());
		const FVerseIntrinsicPresentationDescriptor* IntrinsicPresentation =
			FindVerseIntrinsicPresentation(PresentationKey);
		const TOptional<FVerseBlueprintCallablePresentation> BlueprintPresentation =
			bOperator && (IntrinsicPresentation == nullptr
				|| IntrinsicPresentation->BlueprintLibrary ==
					EVerseIntrinsicBlueprintLibrary::None)
				? TOptional<FVerseBlueprintCallablePresentation>()
				: ResolveVerseBlueprintCallablePresentation(
					Spelling, *FunctionType, IntrinsicPresentation);
		const FVerseResolvedExpressionPresentation ResolvedPresentation =
			ResolveVerseExpressionPresentation(
				DefinitionDisplayName,
				DefinitionCategory,
				BlueprintPresentation.IsSet()
					? BlueprintPresentation->ExplicitDisplayName
					: FText::GetEmpty(),
				BlueprintPresentation.IsSet()
					? BlueprintPresentation->Category
					: FText::GetEmpty(),
				IntrinsicPresentation,
				Spelling);
		const FText ModuleCategory = GetModuleCategory(Function);

		if (!bDraggingFromOutput)
		{
			if (!uLang::SemanticTypeUtils::IsSubtype(
				&FunctionType->GetReturnType(), &SocketType, UploadedVersion))
			{
				return;
			}
			FVerseSemanticCandidate Candidate;
			Candidate.Kind = bInfix
				? EVerseSemanticCandidateKind::InfixOperator
				: (bPrefix
					? EVerseSemanticCandidateKind::PrefixOperator
					: (bPostfix
						? EVerseSemanticCandidateKind::PostfixOperator
						: EVerseSemanticCandidateKind::Function));
			Candidate.DisplayName = Spelling;
			Candidate.Category = ResolvedPresentation.Category;
			Candidate.ModuleCategory = ModuleCategory;
			Candidate.PresentationDisplayName = ResolvedPresentation.DisplayName;
			Candidate.SourceSpelling = Spelling;
			Candidate.bUsesFailureCallSyntax = bUsesFailureCallSyntax;
			Candidate.Function = &Function;
			Candidate.ResultType = &FunctionType->GetReturnType();
			Candidate.ResultTypeName = ToFString(FunctionType->GetReturnType().AsCode());
			Candidate.Snapshot = Snapshot;
			for (const uLang::CTypeBase* Param : Params)
			{
				FString Default = DefaultSourceForType(*Param);
				if (Default.IsEmpty())
				{
					return;
				}
				Candidate.UnboundInputDefaults.Add(MoveTemp(Default));
			}
			Out.Add(MoveTemp(Candidate));
			return;
		}

		for (int32 BoundIndex = 0; BoundIndex < Params.Num(); ++BoundIndex)
		{
			if (!uLang::SemanticTypeUtils::Matches(
				&SocketType, Params[BoundIndex], UploadedVersion))
			{
				continue;
			}
			FVerseSemanticCandidate Candidate;
			Candidate.Kind = bInfix
				? EVerseSemanticCandidateKind::InfixOperator
				: (bPrefix
					? EVerseSemanticCandidateKind::PrefixOperator
					: (bPostfix
						? EVerseSemanticCandidateKind::PostfixOperator
						: EVerseSemanticCandidateKind::Function));
			Candidate.DisplayName = Spelling;
			Candidate.Category = ResolvedPresentation.Category;
			Candidate.ModuleCategory = ModuleCategory;
			Candidate.PresentationDisplayName = ResolvedPresentation.DisplayName;
			Candidate.SourceSpelling = Spelling;
			Candidate.bUsesFailureCallSyntax = bUsesFailureCallSyntax;
			Candidate.BoundInputIndex = BoundIndex;
			Candidate.Function = &Function;
			Candidate.ResultType = &FunctionType->GetReturnType();
			Candidate.ResultTypeName = ToFString(FunctionType->GetReturnType().AsCode());
			Candidate.Snapshot = Snapshot;
			bool bHasAllDefaults = true;
			for (int32 ParamIndex = 0; ParamIndex < Params.Num(); ++ParamIndex)
			{
				if (ParamIndex == BoundIndex)
				{
					Candidate.UnboundInputDefaults.Add(FString());
					continue;
				}
				FString Default = DefaultSourceForType(*Params[ParamIndex]);
				if (Default.IsEmpty())
				{
					bHasAllDefaults = false;
					break;
				}
				Candidate.UnboundInputDefaults.Add(MoveTemp(Default));
			}
			if (bHasAllDefaults)
			{
				Out.Add(MoveTemp(Candidate));
			}

			// Homogeneous overloads do not need indistinguishable left/right rows.
			if (Params.Num() == 2
				&& uLang::SemanticTypeUtils::IsEquivalent(
					Params[0], Params[1], UploadedVersion))
			{
				break;
			}
		}
	}
}

TArray<FVerseSemanticCandidate> FVerseSemanticCandidateProvider::Build(
	TConstArrayView<TSharedPtr<const FVerseSemanticSnapshot>> Snapshots,
	const FString& FilePath,
	int32 ExpressionBeginByte,
	bool bDraggingFromOutput,
	const FVerseDocument& Document)
{
	TArray<FVerseSemanticCandidate> Result;
	TSet<FString> Seen;
	for (const TSharedPtr<const FVerseSemanticSnapshot>& Snapshot : Snapshots)
	{
		if (!Snapshot.IsValid() || !Snapshot->GetProgram().IsValid())
		{
			continue;
		}
		const Verse::Vst::Node* Node = FindSemanticNode(
			*Snapshot, FilePath, ExpressionBeginByte, Document);
		if (Node == nullptr)
		{
			continue;
		}
		const uLang::CSemanticProgram& Program = *Snapshot->GetProgram();
		const uLang::CScope* ActiveScope = FindActiveScope(*Node, Program);
		const uLang::CTypeBase* SocketType = FindExpressionType(*Node, Program);
		if (ActiveScope == nullptr || SocketType == nullptr)
		{
			continue;
		}
		const uLang::CAstPackage* Package = ActiveScope->GetPackage();
		const uint32 UploadedVersion = Package
			? Package->_UploadedAtFNVersion
			: VerseFN::UploadedAtFNVersion::Latest;

		for (const uLang::CDefinition* Definition :
			CollectVisibleDefinitions(*ActiveScope, Program))
		{
			if (!IsVisibleAtCurrentPackageVersion(
				*Definition, *ActiveScope, Package))
			{
				continue;
			}
			const int32 Before = Result.Num();
			if (const uLang::CDataDefinition* Data =
				Definition->AsNullable<uLang::CDataDefinition>())
			{
				if (!bDraggingFromOutput
					&& !Data->IsInstanceMember()
					&& Data->IsAccessibleFrom(*ActiveScope)
					&& uLang::SemanticTypeUtils::IsSubtype(
						Data->GetType(), SocketType, UploadedVersion))
				{
					FVerseSemanticCandidate& Candidate = Result.AddDefaulted_GetRef();
					Candidate.Kind = EVerseSemanticCandidateKind::Identifier;
					Candidate.DisplayName = ToFString(Data->AsNameStringView());
					Candidate.Category = GetDefinitionCategory(*Data);
					Candidate.ModuleCategory = GetModuleCategory(*Data);
					Candidate.SourceSpelling = Candidate.DisplayName;
					Candidate.DataDefinition = Data;
					Candidate.ResultType = Data->GetType();
					Candidate.ResultTypeName = ToFString(Data->GetType()->AsCode());
					Candidate.Snapshot = Snapshot;
				}
			}
			else if (const uLang::CFunction* Function =
				Definition->AsNullable<uLang::CFunction>())
			{
				AddFunctionCandidates(
					*Function, *ActiveScope, *SocketType, bDraggingFromOutput,
					UploadedVersion, Snapshot, Result);
			}

			for (int32 Index = Result.Num() - 1; Index >= Before; --Index)
			{
				const FVerseSemanticCandidate& Candidate = Result[Index];
				const FString Signature = Candidate.Function
					&& Candidate.Function->_Signature.GetFunctionType()
						? ToFString(Candidate.Function->_Signature.GetFunctionType()->AsCode())
						: FString();
				const FString Key = FString::Printf(
					TEXT("%d|%s|%d|%s"), static_cast<int32>(Candidate.Kind),
					*Candidate.SourceSpelling, Candidate.BoundInputIndex, *Signature);
				if (Seen.Contains(Key))
				{
					Result.RemoveAt(Index);
				}
				else
				{
					Seen.Add(Key);
				}
			}
		}
	}
	return Result;
}
