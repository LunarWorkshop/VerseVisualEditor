#include "VerseSemanticCandidates.h"

#include "VerseDocument.h"
#include "VerseIdentifier.h"
#include "VerseSemanticWorkspace.h"
#include "VerseVisualTile.h"
#include "uLang/Semantics/DataDefinition.h"
#include "uLang/Semantics/Expression.h"
#include "uLang/Semantics/SemanticFunction.h"
#include "uLang/Semantics/SemanticProgram.h"
#include "uLang/Semantics/SemanticScope.h"
#include "uLang/Semantics/SemanticTypes.h"
#include "uLang/Semantics/TypeVariable.h"
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

	const Verse::Vst::Node* FindExactSemanticNode(
		const FVerseSemanticSnapshot& Snapshot,
		const FString& FilePath,
		FVerseTextRange Range,
		const FVerseDocument& Document)
	{
		const Verse::Vst::Node* Node = FindSemanticNode(
			Snapshot, FilePath, Range.BeginByte, Document);
		const uLang::STextPosition Begin = ByteOffsetToPosition(
			Document.GetOriginalUtf8View(), Range.BeginByte);
		const uLang::STextPosition End = ByteOffsetToPosition(
			Document.GetOriginalUtf8View(), Range.EndByte());
		for (; Node != nullptr; Node = Node->GetParent())
		{
			if (Node->Whence().GetBegin() == Begin && Node->Whence().GetEnd() == End)
			{
				return Node;
			}
		}
		return nullptr;
	}

	const uLang::CExpressionBase* FindMappedExpression(const Verse::Vst::Node& Node)
	{
		if (const uLang::CAstNode* Ast = Node.GetMappedAstNode())
		{
			if (const uLang::CExpressionBase* Expression = Ast->AsExpression())
			{
				return Expression;
			}
		}
		for (const Verse::Vst::TNodeRef<Verse::Vst::Node>& Child : Node.GetChildren())
		{
			if (const uLang::CExpressionBase* Expression = FindMappedExpression(*Child))
			{
				return Expression;
			}
		}
		return nullptr;
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

	const uLang::CExprInvocation* AsInvocation(const uLang::CExpressionBase& Expression)
	{
		switch (Expression.GetNodeType())
		{
		case uLang::EAstNodeType::Invoke_Invocation:
		case uLang::EAstNodeType::Invoke_UnaryArithmetic:
		case uLang::EAstNodeType::Invoke_BinaryArithmetic:
		case uLang::EAstNodeType::Invoke_Comparison:
		case uLang::EAstNodeType::Invoke_QueryValue:
			return static_cast<const uLang::CExprInvocation*>(&Expression);
		default:
			return nullptr;
		}
	}

	const uLang::CExprInvocation* FindMappedInvocation(const Verse::Vst::Node& Node)
	{
		if (const uLang::CAstNode* Ast = Node.GetMappedAstNode())
		{
			if (const uLang::CExpressionBase* Expression = Ast->AsExpression())
			{
				if (const uLang::CExprInvocation* Invocation = AsInvocation(*Expression))
				{
					return Invocation;
				}
			}
		}
		for (const Verse::Vst::TNodeRef<Verse::Vst::Node>& Child : Node.GetChildren())
		{
			if (const uLang::CExprInvocation* Invocation = FindMappedInvocation(*Child))
			{
				return Invocation;
			}
		}
		return nullptr;
	}

	const uLang::CExprDataDefinition* FindMappedDataDefinition(
		const Verse::Vst::Node& Node)
	{
		if (const uLang::CAstNode* Ast = Node.GetMappedAstNode();
			Ast != nullptr && Ast->GetNodeType() == uLang::EAstNodeType::Definition_Data)
		{
			return static_cast<const uLang::CExprDataDefinition*>(Ast);
		}
		for (const Verse::Vst::TNodeRef<Verse::Vst::Node>& Child : Node.GetChildren())
		{
			if (const uLang::CExprDataDefinition* Definition =
				FindMappedDataDefinition(*Child))
			{
				return Definition;
			}
		}
		return nullptr;
	}

	const uLang::CTypeBase* GetDataValueType(
		const uLang::CDataDefinition& Definition)
	{
		if (Definition.GetType() == nullptr)
		{
			return nullptr;
		}
		const uLang::CNormalType& NormalType = Definition.GetType()->GetNormalType();
		if (const uLang::CPointerType* PointerType =
			NormalType.AsNullable<uLang::CPointerType>())
		{
			return PointerType->PositiveValueType();
		}
		return Definition.GetType();
	}

	FString GetUserFacingDataType(const uLang::CDataDefinition& Definition)
	{
		const uLang::CTypeBase* ValueType = GetDataValueType(Definition);
		return ValueType != nullptr ? ToFString(ValueType->AsCode()) : FString();
	}

	void BindExpressionTile(
		FVerseVisualTile& Tile,
		const TSharedPtr<const FVerseSemanticSnapshot>& Snapshot,
		const FString& FilePath,
		const FVerseDocument& Document)
	{
		for (FVerseVisualTile& Child : Tile.Children)
		{
			BindExpressionTile(Child, Snapshot, FilePath, Document);
		}
		if (!Snapshot.IsValid()
			|| !Snapshot->GetProgram().IsValid()
			|| !Snapshot->Describes(FilePath, Tile.Range.Revision))
		{
			return;
		}

		if (Tile.Kind == EVerseVisualTileKind::FailableBlock)
		{
			Tile.ValueOutputs.Reset();
			for (FVerseVisualTile& Child : Tile.Children)
			{
				if (Child.Kind != EVerseVisualTileKind::Definition
					|| Child.SemanticDataDefinition == nullptr)
				{
					continue;
				}
				FVerseVisualSocket Binding;
				Binding.NameRange = Child.NameRange;
				Binding.TypeRange = Child.TypeRange;
				Binding.SemanticName = ToFString(
					Child.SemanticDataDefinition->AsNameStringView());
				Binding.SemanticTypeName = GetUserFacingDataType(
					*Child.SemanticDataDefinition);
				// Boundary binding pins are future drag sources, not an internal
				// visualization of the declaration that introduced them.
				Binding.bConnected = false;
				Binding.Outcome = EVerseExpressionOutcome::Ordinary;
				Binding.SemanticDataDefinition = Child.SemanticDataDefinition;
				Binding.SemanticSnapshot = Snapshot;
				Tile.ValueOutputs.Add(Binding);

				Child.ValueOutputs.Reset();
				Child.ValueOutputs.Add(MoveTemp(Binding));
			}
			Tile.SemanticSnapshot = Snapshot;
			return;
		}

		if (Tile.Kind != EVerseVisualTileKind::Expression
			&& Tile.Kind != EVerseVisualTileKind::Definition)
		{
			return;
		}
		if (Tile.ExpressionKind == EVerseExpressionKind::Literal)
		{
			// The syntax kind fully determines the type of every supported literal.
			// Recursive VST-to-AST lookup can encounter a surrounding type expression,
			// so it must not replace `int`, `float`, etc. with `type{...}` here.
			Tile.SemanticTypeName = Tile.IntrinsicTypeName.ToString();
			Tile.Outcome = EVerseExpressionOutcome::Ordinary;
			for (FVerseVisualSocket& Output : Tile.ValueOutputs)
			{
				Output.IntrinsicTypeName = Tile.IntrinsicTypeName;
				Output.SemanticTypeName = Tile.SemanticTypeName;
				Output.Outcome = Tile.Outcome;
			}
			return;
		}

		const Verse::Vst::Node* Node = FindExactSemanticNode(
			*Snapshot, FilePath, Tile.Range, Document);
		if (Tile.Kind == EVerseVisualTileKind::Definition)
		{
			const uLang::CExprDataDefinition* Definition = Node != nullptr
				? FindMappedDataDefinition(*Node)
				: nullptr;
			if (Definition != nullptr)
			{
				Tile.SemanticDataDefinition = Definition->_DataMember.Get();
				Tile.SemanticSnapshot = Snapshot;
				Tile.SemanticTypeName = GetUserFacingDataType(*Tile.SemanticDataDefinition);
				Tile.TypeProvenance = EVerseTypeResolutionProvenance::CompilerResolved;
			}
			return;
		}
		const uLang::CExprInvocation* Invocation = Node
			? FindMappedInvocation(*Node)
			: nullptr;
		const bool bInvocationIsTileExpression =
			Tile.ExpressionKind == EVerseExpressionKind::Call
			|| IsVerseOperatorExpression(Tile.ExpressionKind);
		const uLang::CExpressionBase* Expression = bInvocationIsTileExpression
			? static_cast<const uLang::CExpressionBase*>(Invocation)
			: Node != nullptr
				? FindMappedExpression(*Node)
				: nullptr;
		if (Expression == nullptr)
		{
			return;
		}

		const uLang::CSemanticProgram& Program = *Snapshot->GetProgram();
		const uLang::CScope* ActiveScope = FindActiveScope(*Node, Program);
		const uLang::CAstPackage* Package = ActiveScope != nullptr
			? ActiveScope->GetPackage()
			: nullptr;
		const TOptional<bool> bCanFail = Package != nullptr
			? TOptional<bool>(Expression->CanFail(Package))
			: TOptional<bool>();
		if (bCanFail.IsSet())
		{
			Tile.Outcome = bCanFail.GetValue()
				? EVerseExpressionOutcome::FailableValue
				: EVerseExpressionOutcome::Ordinary;
		}
		if (const uLang::CTypeBase* ResultType = Expression->GetResultType(Program))
		{
			Tile.SemanticTypeName = ToFString(ResultType->AsCode());
			Tile.TypeProvenance = EVerseTypeResolutionProvenance::CompilerResolved;
			for (FVerseVisualSocket& Output : Tile.ValueOutputs)
			{
				Output.SemanticTypeName = Tile.SemanticTypeName;
				Output.Outcome = Tile.Outcome;
			}
			if (Tile.SemanticTypeName.Equals(TEXT("void"), ESearchCase::IgnoreCase))
			{
				Tile.ValueOutputs.Reset();
				if (bCanFail.Get(false))
				{
					FVerseVisualSocket& FailureSocket = Tile.ValueOutputs.AddDefaulted_GetRef();
					FailureSocket.Outcome = EVerseExpressionOutcome::FailureOnly;
					Tile.Outcome = EVerseExpressionOutcome::FailureOnly;
				}
			}
			else if (Tile.ValueOutputs.IsEmpty())
			{
				FVerseVisualSocket& Output = Tile.ValueOutputs.AddDefaulted_GetRef();
				Output.SemanticTypeName = Tile.SemanticTypeName;
				Output.Outcome = Tile.Outcome;
			}
		}
		if (Tile.ExpressionKind == EVerseExpressionKind::Control
			&& Tile.ControlKind == EVerseControlKind::If)
		{
			const FVerseVisualExpressionDescriptor::FControlRegion* ThenRegion =
				Tile.ControlRegions.FindByPredicate(
					[](const FVerseVisualExpressionDescriptor::FControlRegion& Region)
					{
						return Region.Kind == EVerseControlRegionKind::Body;
					});
			const FVerseVisualExpressionDescriptor::FControlRegion* ConditionRegion =
				Tile.ControlRegions.FindByPredicate(
					[](const FVerseVisualExpressionDescriptor::FControlRegion& Region)
					{
						return Region.Kind == EVerseControlRegionKind::Condition;
					});
			if (ThenRegion != nullptr && ConditionRegion != nullptr
				&& Tile.Children.IsValidIndex(ConditionRegion->FirstOperandIndex))
			{
				const int32 ThenProbeByte = ThenRegion->OperandCount > 0
					&& Tile.Children.IsValidIndex(ThenRegion->FirstOperandIndex)
					? Tile.Children[ThenRegion->FirstOperandIndex].Range.BeginByte
					: (ThenRegion->InteriorRange.IsSet()
						? ThenRegion->InteriorRange.BeginByte
						: ThenRegion->Range.BeginByte);
				const Verse::Vst::Node* ThenNode = FindSemanticNode(
					*Snapshot, FilePath, ThenProbeByte, Document);
				const uLang::CScope* ThenScope = ThenNode != nullptr
					? FindActiveScope(*ThenNode, Program)
					: nullptr;
				FVerseVisualTile& Predicate =
					Tile.Children[ConditionRegion->FirstOperandIndex];
				if (Predicate.Kind == EVerseVisualTileKind::FailableBlock)
				{
					for (FVerseVisualSocket& Binding : Predicate.ValueOutputs)
					{
						if (ThenScope != nullptr)
						{
							Binding.LegalConsumerScopes.AddUnique(ThenScope);
						}
					}
				}
			}
		}
		if (Tile.ExpressionKind != EVerseExpressionKind::Call
			&& !IsVerseOperatorExpression(Tile.ExpressionKind))
		{
			return;
		}

		if (Invocation == nullptr)
		{
			return;
		}
		if (const uLang::TSPtr<uLang::CExpressionBase>& Callee = Invocation->GetCallee();
			Callee && Callee->GetNodeType() == uLang::EAstNodeType::Identifier_Function)
		{
			Tile.SemanticFunction =
				&static_cast<const uLang::CExprIdentifierFunction&>(*Callee)._Function;
			Tile.SemanticSnapshot = Snapshot;
		}
		if (const uLang::CFunctionType* FunctionType = Invocation->GetResolvedCalleeType())
		{
			const uLang::CFunctionType::ParamTypes Params = FunctionType->GetParamTypes();
			const uLang::SSignature::ParamDefinitions* ParamDefinitions =
				Tile.SemanticFunction != nullptr
				? &Tile.SemanticFunction->_Signature.GetParams()
				: nullptr;
			for (int32 Index = 0; Index < Tile.ValueInputs.Num() && Index < Params.Num(); ++Index)
			{
				FVerseVisualSocket& Input = Tile.ValueInputs[Index];
				Input.SemanticTypeName = ToFString(Params[Index]->AsCode());
				const uLang::CDataDefinition* Param = ParamDefinitions != nullptr
					&& ParamDefinitions->IsValidIndex(Index)
					? (*ParamDefinitions)[Index]
					: nullptr;
				const uLang::CDefinition* NameDefinition = Param != nullptr
					&& Param->_ImplicitParam != nullptr
					? static_cast<const uLang::CDefinition*>(Param->_ImplicitParam)
					: static_cast<const uLang::CDefinition*>(Param);
				Input.SemanticName = NameDefinition != nullptr
					? ToFString(NameDefinition->AsNameStringView())
					: FString();
			}
			Tile.SemanticTypeName = ToFString(FunctionType->GetReturnType().AsCode());
			if (Tile.SemanticTypeName.Equals(TEXT("void"), ESearchCase::IgnoreCase))
			{
				Tile.ValueOutputs.Reset();
				if (bCanFail.Get(false))
				{
					FVerseVisualSocket& FailureSocket = Tile.ValueOutputs.AddDefaulted_GetRef();
					FailureSocket.Outcome = EVerseExpressionOutcome::FailureOnly;
					Tile.Outcome = EVerseExpressionOutcome::FailureOnly;
				}
			}
			for (FVerseVisualSocket& Output : Tile.ValueOutputs)
			{
				Output.SemanticTypeName =
					Output.Outcome == EVerseExpressionOutcome::FailureOnly
					? FString()
					: Tile.SemanticTypeName;
				Output.Outcome = Tile.Outcome;
			}
		}
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
		// UE 6.0 interns query as `operator'?'`, so IsOperatorOpName reports it as
		// infix even though Verse syntax and CExprQueryValue are postfix. Keep the
		// exact intrinsic identity override until Epic moves _OpNameQuery under the
		// postfix namespace. The semantic workspace test intentionally fails when
		// that engine taxonomy changes so this compatibility branch gets removed.
		const bool bQuery = Function.GetName()
			== Function.GetProgram()._IntrinsicSymbols._OpNameQuery;
		const bool bInfix = !bQuery && !bExtensionMethod
			&& Function.GetProgram()._IntrinsicSymbols.IsOperatorOpName(
				Function.GetName());
		const bool bPrefix = Function.GetProgram()._IntrinsicSymbols.IsPrefixOpName(
			Function.GetName());
		const bool bPostfix = Function.GetProgram()._IntrinsicSymbols.IsPostfixOpName(
			Function.GetName());
		// operator'()' is compiler plumbing for non-function invocation (array/map
		// access), not an expression the user can create from the action menu.
		if (Function.GetName() == Function.GetProgram()._IntrinsicSymbols._OpNameCall)
		{
			return;
		}
		EVerseSemanticCandidateKind Kind = EVerseSemanticCandidateKind::Function;
		if (bQuery || bPostfix)
		{
			Kind = EVerseSemanticCandidateKind::PostfixOperator;
		}
		else if (bInfix)
		{
			Kind = EVerseSemanticCandidateKind::InfixOperator;
		}
		else if (bPrefix)
		{
			Kind = EVerseSemanticCandidateKind::PrefixOperator;
		}

		if (!bDraggingFromOutput)
		{
			if (!uLang::SemanticTypeUtils::IsSubtype(
				&FunctionType->GetReturnType(), &SocketType, UploadedVersion))
			{
				return;
			}
			FVerseSemanticCandidate Candidate;
			Candidate.Kind = Kind;
			Candidate.Function = &Function;
			Candidate.InstantiatedFunctionType = FunctionType;
			Candidate.Snapshot = Snapshot;
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
			Candidate.Kind = Kind;
			Candidate.BoundInputIndex = BoundIndex;
			Candidate.Function = &Function;
			Candidate.InstantiatedFunctionType = FunctionType;
			Candidate.Snapshot = Snapshot;
			Out.Add(MoveTemp(Candidate));

			// Homogeneous overloads do not need indistinguishable left/right rows.
			if (Params.Num() == 2
				&& uLang::SemanticTypeUtils::IsEquivalent(
					Params[0], Params[1], UploadedVersion))
			{
				break;
			}
		}
	}

	FString SemanticCandidateKey(const FVerseSemanticCandidate& Candidate)
	{
		const uLang::CDefinition* Definition = Candidate.Function != nullptr
			? static_cast<const uLang::CDefinition*>(Candidate.Function)
			: static_cast<const uLang::CDefinition*>(Candidate.DataDefinition);
		const FString Name = Definition != nullptr
			? ToFString(Definition->GetName().AsStringView())
			: FString();
		const FString ScopePath = Definition != nullptr
			? ToFString(Definition->_EnclosingScope.GetScopePath('/'))
			: FString();
		const FString Signature = Candidate.InstantiatedFunctionType != nullptr
			? ToFString(Candidate.InstantiatedFunctionType->AsCode())
			: (Candidate.DataDefinition != nullptr
				? ToFString(Candidate.DataDefinition->GetType()->AsCode())
				: FString());
		return FString::Printf(
			TEXT("%d|%s|%s|%d|%s"),
			static_cast<int32>(Candidate.Kind), *ScopePath, *Name,
			Candidate.BoundInputIndex, *Signature);
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
				const uLang::CTypeBase* DataValueType = GetDataValueType(*Data);
				if (!bDraggingFromOutput
					&& !Data->IsInstanceMember()
					&& Data->IsAccessibleFrom(*ActiveScope)
					&& DataValueType != nullptr
					&& uLang::SemanticTypeUtils::IsSubtype(
						DataValueType, SocketType, UploadedVersion))
				{
					FVerseSemanticCandidate& Candidate = Result.AddDefaulted_GetRef();
					Candidate.Kind = EVerseSemanticCandidateKind::Identifier;
					Candidate.DataDefinition = Data;
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
				const FString Key = SemanticCandidateKey(Candidate);
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

TArray<FVerseSemanticCandidate> FVerseSemanticCandidateProvider::BuildAll(
	TConstArrayView<TSharedPtr<const FVerseSemanticSnapshot>> Snapshots,
	const FString& FilePath,
	int32 ExpressionBeginByte,
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
		if (ActiveScope == nullptr)
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
			if (!IsVisibleAtCurrentPackageVersion(*Definition, *ActiveScope, Package))
			{
				continue;
			}
			FVerseSemanticCandidate Candidate;
			if (const uLang::CDataDefinition* Data =
				Definition->AsNullable<uLang::CDataDefinition>())
			{
				if (Data->IsInstanceMember() || !Data->IsAccessibleFrom(*ActiveScope)
					|| GetDataValueType(*Data) == nullptr)
				{
					continue;
				}
				Candidate.Kind = EVerseSemanticCandidateKind::Identifier;
				Candidate.DataDefinition = Data;
			}
			else if (const uLang::CFunction* Function =
				Definition->AsNullable<uLang::CFunction>())
			{
				if (!Function->IsAccessibleFrom(*ActiveScope)
					|| Function->IsInstanceMember()
					|| Function->_Signature.GetFunctionType() == nullptr
					|| Function->GetName() == Program._IntrinsicSymbols._OpNameCall)
				{
					continue;
				}
				Candidate.Function = Function;
				Candidate.InstantiatedFunctionType = uLang::SemanticTypeUtils::Instantiate(
					Function->_Signature.GetFunctionType(), UploadedVersion);
				if (Candidate.InstantiatedFunctionType == nullptr)
				{
					continue;
				}
				const bool bExtensionMethod = Function->_ExtensionFieldAccessorKind ==
					uLang::EExtensionFieldAccessorKind::ExtensionMethod;
				Candidate.Kind = EVerseSemanticCandidateKind::Function;
				// See AddFunctionCandidates above. BuildAll also walks compiler functions,
				// so it needs the same UE 6.0 _OpNameQuery compatibility classification.
				// Remove both exact-identity branches together when the maintenance
				// tripwire reports that IsPostfixOpName handles query natively.
				if (Function->GetName() == Program._IntrinsicSymbols._OpNameQuery)
				{
					Candidate.Kind = EVerseSemanticCandidateKind::PostfixOperator;
				}
				else if (!bExtensionMethod
					&& Program._IntrinsicSymbols.IsOperatorOpName(Function->GetName()))
				{
					Candidate.Kind = EVerseSemanticCandidateKind::InfixOperator;
				}
				else if (Program._IntrinsicSymbols.IsPrefixOpName(Function->GetName()))
				{
					Candidate.Kind = EVerseSemanticCandidateKind::PrefixOperator;
				}
				else if (Program._IntrinsicSymbols.IsPostfixOpName(Function->GetName()))
				{
					Candidate.Kind = EVerseSemanticCandidateKind::PostfixOperator;
				}
			}
			else
			{
				continue;
			}
			Candidate.Snapshot = Snapshot;
			const FString Key = SemanticCandidateKey(Candidate);
			if (!Seen.Contains(Key))
			{
				Seen.Add(Key);
				Result.Add(MoveTemp(Candidate));
			}
		}
	}
	return Result;
}

TArray<FString> FVerseSemanticCandidateProvider::BuildVisibleTypeNames(
	TConstArrayView<TSharedPtr<const FVerseSemanticSnapshot>> Snapshots,
	const FString& FilePath,
	int32 ExpressionBeginByte,
	const FVerseDocument& Document)
{
	// These scalar types remain source-safe even while the private semantic
	// snapshot is rebuilding or the current buffer has semantic errors.
	TSet<FString> Names = {
		TEXT("logic"),
		TEXT("int"),
		TEXT("float"),
		TEXT("string"),
		TEXT("char")};
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
		if (ActiveScope == nullptr)
		{
			continue;
		}
		const uLang::CAstPackage* Package = ActiveScope->GetPackage();
		for (const uLang::CDefinition* Definition :
			CollectVisibleDefinitions(*ActiveScope, Program))
		{
			if (Definition == nullptr
				|| !Definition->IsAccessibleFrom(*ActiveScope)
				|| !IsVisibleAtCurrentPackageVersion(
					*Definition, *ActiveScope, Package))
			{
				continue;
			}
			const uLang::CDefinition::EKind Kind = Definition->GetKind();
			if (Kind == uLang::CDefinition::EKind::Class
				|| Kind == uLang::CDefinition::EKind::Enumeration
				|| Kind == uLang::CDefinition::EKind::TypeAlias
				|| Kind == uLang::CDefinition::EKind::TypeVariable)
			{
				const FString TypeName = ToFString(Definition->AsNameStringView());
				if (ValidateVerseIdentifier(TypeName).IsEmpty())
				{
					Names.Add(TypeName);
				}
			}
		}
	}

	TArray<FString> Result = Names.Array();
	Result.Sort([](const FString& Left, const FString& Right)
	{
		return Left.Compare(Right, ESearchCase::IgnoreCase) < 0;
	});
	return Result;
}

void FVerseSemanticCandidateProvider::BindFunctionGraph(
	TArray<FVerseVisualTile>& GraphTiles,
	const TSharedPtr<const FVerseSemanticSnapshot>& Snapshot,
	const FString& FilePath,
	const FVerseDocument& Document)
{
	if (!Snapshot.IsValid())
	{
		return;
	}
	for (FVerseVisualTile& Tile : GraphTiles)
	{
		BindExpressionTile(Tile, Snapshot, FilePath, Document);
	}
}
