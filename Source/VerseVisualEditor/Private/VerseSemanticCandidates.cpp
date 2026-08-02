#include "VerseSemanticCandidates.h"

#include "VerseDocument.h"
#include "VerseIdentifier.h"
#include "VerseIntrinsicPresentation.h"
#include "VerseSemanticWorkspace.h"
#include "VerseVisualTile.h"
#include "uLang/Semantics/DataDefinition.h"
#include "uLang/Semantics/Expression.h"
#include "uLang/Semantics/SemanticClass.h"
#include "uLang/Semantics/SemanticEnumeration.h"
#include "uLang/Semantics/SemanticFunction.h"
#include "uLang/Semantics/SemanticProgram.h"
#include "uLang/Semantics/SemanticScope.h"
#include "uLang/Semantics/SemanticTypes.h"
#include "uLang/Semantics/TypeAlias.h"
#include "uLang/Semantics/TypeVariable.h"
#include "uLang/Semantics/SemanticUnion.h"
#include "uLang/Semantics/VisitSet.h"
#include "uLang/SourceProject/UploadedAtFNVersion.h"
#include "uLang/Syntax/VstNode.h"
#include "Misc/ScopeLock.h"
#include "Misc/ScopeExit.h"

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
		const uLang::STextPosition TargetPosition = Node.Whence().GetBegin();
		const uLang::CUTF8StringView TargetPath = Node.GetSnippetPath();
		const uLang::CAstNode* SearchRoot = nullptr;
		const uLang::CScope* VstScope = nullptr;
		for (const Verse::Vst::Node* Current = &Node;
			Current != nullptr;
			Current = Current->GetParent())
		{
			if (const uLang::CAstNode* Ast = Current->GetMappedAstNode())
			{
				if (VstScope == nullptr)
				{
					VstScope = ScopeFromAst(*Ast, Program);
				}
				SearchRoot = Ast;
				if (Ast->GetNodeType() == uLang::EAstNodeType::Definition_Function)
				{
					break;
				}
			}
		}
		if (VstScope != nullptr && VstScope->IsControlScope())
		{
			return VstScope;
		}

		// Synthetic Verse code blocks (notably an if's failable condition) own
		// real compiler scopes but are mapped non-reciprocally: walking only VST
		// parents skips them. Search the containing function AST and select the
		// deepest scope whose children contain this source position.
		if (SearchRoot != nullptr)
		{
			struct FScopeSearchResult
			{
				const uLang::CScope* Scope = nullptr;
				int32 ScopeDepth = INDEX_NONE;
				bool bContainsTarget = false;
			};
			TSet<const uLang::CAstNode*> Visited;
			TFunction<FScopeSearchResult(const uLang::CAstNode&, int32)> Search;
			Search = [&](const uLang::CAstNode& Ast, int32 Depth)
			{
				FScopeSearchResult Result;
				if (Visited.Contains(&Ast))
				{
					return Result;
				}
				Visited.Add(&Ast);

				Ast.VisitChildrenLambda(
					[&](uLang::SAstVisitor&, uLang::CAstNode& Child)
					{
						const FScopeSearchResult ChildResult = Search(Child, Depth + 1);
						Result.bContainsTarget |= ChildResult.bContainsTarget;
						if (ChildResult.Scope != nullptr
							&& ChildResult.ScopeDepth > Result.ScopeDepth)
						{
							Result.Scope = ChildResult.Scope;
							Result.ScopeDepth = ChildResult.ScopeDepth;
						}
					});

				// A code block's own non-reciprocal mapping can cover the whole macro,
				// including sibling clauses. Its precise containment comes from its
				// children; ordinary AST nodes may use their own source locus.
				if (Ast.GetNodeType() != uLang::EAstNodeType::Flow_CodeBlock)
				{
					if (const Verse::Vst::Node* Mapped = Ast.GetMappedVstNode())
					{
						Result.bContainsTarget |= Mapped->GetSnippetPath() == TargetPath
							&& Mapped->Whence().IsInRangeInclusive(TargetPosition);
					}
				}
				if (Result.bContainsTarget)
				{
					if (const uLang::CScope* Scope = ScopeFromAst(Ast, Program);
						Scope != nullptr && Depth > Result.ScopeDepth)
					{
						Result.Scope = Scope;
						Result.ScopeDepth = Depth;
					}
				}
				return Result;
			};

			const FScopeSearchResult AstResult = Search(*SearchRoot, 0);
			if (AstResult.Scope != nullptr)
			{
				return AstResult.Scope;
			}
		}

		return VstScope;
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
			Tile.bProducesValue = true;
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
				Tile.SemanticType = GetDataValueType(*Tile.SemanticDataDefinition);
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
			Tile.SemanticType = ResultType;
			Tile.SemanticSnapshot = Snapshot;
			Tile.SemanticTypeName = ToFString(ResultType->AsCode());
			Tile.TypeProvenance = EVerseTypeResolutionProvenance::CompilerResolved;
			if (Tile.SemanticTypeName.Equals(TEXT("void"), ESearchCase::IgnoreCase))
			{
				Tile.bProducesValue = false;
				if (bCanFail.Get(false))
				{
					Tile.Outcome = EVerseExpressionOutcome::FailureOnly;
				}
			}
			else
			{
				Tile.bProducesValue = true;
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
					for (FVerseVisualTile& Binding : Predicate.Children)
					{
						if (ThenScope != nullptr
							&& Binding.SemanticDataDefinition != nullptr)
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
			Tile.SemanticInputNames.Reset();
			Tile.SemanticInputTypeNames.Reset();
			Tile.SemanticInputTypes.Reset();
			Tile.SemanticInputNamed.Reset();
			Tile.SemanticInputHasDefault.Reset();
			for (int32 Index = 0; Index < Params.Num(); ++Index)
			{
				const uLang::CDataDefinition* Param = ParamDefinitions != nullptr
					&& ParamDefinitions->IsValidIndex(Index)
					? (*ParamDefinitions)[Index]
					: nullptr;
				const uLang::CDefinition* NameDefinition = Param != nullptr
					&& Param->_ImplicitParam != nullptr
					? static_cast<const uLang::CDefinition*>(Param->_ImplicitParam)
					: static_cast<const uLang::CDefinition*>(Param);
				Tile.SemanticInputNames.Add(NameDefinition != nullptr
					? ToFString(NameDefinition->AsNameStringView())
					: FString());
				Tile.SemanticInputTypeNames.Add(ToFString(Params[Index]->AsCode()));
				Tile.SemanticInputTypes.Add(Params[Index]);
				Tile.SemanticInputNamed.Add(Param != nullptr && Param->_bNamed);
				Tile.SemanticInputHasDefault.Add(
					Param != nullptr && Param->GetAstNode() != nullptr
						&& Param->GetAstNode()->Value());
			}
			Tile.SemanticTypeName = ToFString(FunctionType->GetReturnType().AsCode());
			Tile.SemanticType = &FunctionType->GetReturnType();
			Tile.SemanticSnapshot = Snapshot;
			if (Tile.SemanticTypeName.Equals(TEXT("void"), ESearchCase::IgnoreCase))
			{
				Tile.bProducesValue = false;
				if (bCanFail.Get(false))
				{
					Tile.Outcome = EVerseExpressionOutcome::FailureOnly;
				}
			}
			else
			{
				Tile.bProducesValue = true;
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

	bool IsUsableBareDataType(const uLang::CDefinition& Definition)
	{
		switch (Definition.GetKind())
		{
		case uLang::CDefinition::EKind::Class:
		{
			const uLang::CClass& Class = Definition.AsChecked<uLang::CClass>();
			// Verse represents specifiers and effects such as public, override,
			// castable, decides, and computes as classes derived from attribute.
			// They are valid semantic definitions but not data-type annotations.
			// Parametric classes are also not usable by their bare name: their
			// required type arguments need a future structured type-expression UI.
			return !uLang::SemanticTypeUtils::IsAttributeType(&Class)
				&& !Class.IsParametric();
		}
		case uLang::CDefinition::EKind::Enumeration:
		case uLang::CDefinition::EKind::TypeVariable:
			return true;
		case uLang::CDefinition::EKind::TypeAlias:
		{
			const uLang::CTypeAlias& Alias =
				Definition.AsChecked<uLang::CTypeAlias>();
			return Alias.IsInitialized()
				&& !uLang::SemanticTypeUtils::IsAttributeType(Alias.GetType());
		}
		default:
			return false;
		}
	}

	const uLang::CTypeBase* DefinitionAsDataType(const uLang::CDefinition& Definition)
	{
		switch (Definition.GetKind())
		{
		case uLang::CDefinition::EKind::Class:
			return &Definition.AsChecked<uLang::CClass>();
		case uLang::CDefinition::EKind::Enumeration:
			return &Definition.AsChecked<uLang::CEnumeration>();
		case uLang::CDefinition::EKind::TypeAlias:
			return Definition.AsChecked<uLang::CTypeAlias>().GetType();
		case uLang::CDefinition::EKind::TypeVariable:
			return &Definition.AsChecked<uLang::CTypeVariable>();
		case uLang::CDefinition::EKind::Union:
			return &Definition.AsChecked<uLang::CUnion>();
		default:
			return nullptr;
		}
	}

	FString GetOperatorSpelling(const uLang::CFunction& Function)
	{
		const uLang::CIntrinsicSymbols& Symbols = Function.GetProgram()._IntrinsicSymbols;
		const uLang::CUTF8StringView Name = Function.GetName().AsStringView();
		uLang::CUTF8StringView Prefix("postfix'");
		if (Symbols.IsOperatorOpName(Function.GetName()))
		{
			Prefix = uLang::CUTF8StringView("operator'");
		}
		else if (Symbols.IsPrefixOpName(Function.GetName()))
		{
			Prefix = uLang::CUTF8StringView("prefix'");
		}
		if (!Name.StartsWith(Prefix) || Name.ByteLen() <= Prefix.ByteLen() + 1)
		{
			return FString();
		}
		return ToFString(Name.SubView(
			Prefix.ByteLen(), Name.ByteLen() - Prefix.ByteLen() - 1));
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
			// A provider search is overload resolution with the socket as the
			// expected negative type. Matches constrains freshly instantiated
			// generic return variables (for example operator'+' t -> t) while a
			// plain subtype query rejects them before t can become float/int/etc.
			if (!uLang::SemanticTypeUtils::Matches(
				&FunctionType->GetReturnType(), &SocketType, UploadedVersion))
			{
				return;
			}
			FVerseSemanticCandidate Candidate;
			Candidate.Kind = Kind;
			Candidate.Function = &Function;
			Candidate.InstantiatedFunctionType = FunctionType;
			Candidate.MatchedSocketType = &SocketType;
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
			Candidate.MatchedSocketType = &SocketType;
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
	const FVerseDocument& Document,
	const FVerseVisualSocket* DraggedSocket)
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
		const bool bSocketBelongsToSnapshot = DraggedSocket != nullptr
			&& DraggedSocket->SemanticType != nullptr
			&& DraggedSocket->SemanticSnapshot.Get() == Snapshot.Get();
		const uLang::CTypeBase* SocketType = bSocketBelongsToSnapshot
			? DraggedSocket->SemanticType
			: FindExpressionType(*Node, Program);
		// A numeric literal expression has a singleton compiler result such as
		// type{0.0}. When its parent input socket is being replaced, candidates
		// must match the full primitive operand type rather than that one value.
		if (DraggedSocket != nullptr)
		{
			switch (DraggedSocket->InlineLiteralKind)
			{
			case EVerseLiteralKind::Integer:
				SocketType = Program._intType;
				break;
			case EVerseLiteralKind::Float:
				SocketType = Program._floatType;
				break;
			default:
				break;
			}
		}
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
			if (IsUsableBareDataType(*Definition))
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

TArray<FVerseOperatorSignature> FVerseSemanticCandidateProvider::BuildOperatorSignatures(
	TConstArrayView<TSharedPtr<const FVerseSemanticSnapshot>> Snapshots,
	const FString& FilePath,
	int32 ExpressionBeginByte,
	const FVerseDocument& Document,
	FStringView OperatorSpelling,
	int32 OperandCount,
	TConstArrayView<const FVerseVisualSocket*> ConnectedOperands,
	TConstArrayView<const FVerseVisualSocket*> OutputConsumers)
{
	TArray<FVerseOperatorSignature> Result;
	TSet<FString> SeenSemanticSignatures;

	struct FVisibleType
	{
		FString Name;
		const uLang::CTypeBase* Type = nullptr;
	};
	struct FCachedSignatures
	{
		TWeakPtr<const FVerseSemanticSnapshot> Snapshot;
		TArray<FVerseOperatorSignature> Signatures;
	};
	static FCriticalSection CacheMutex;
	static TMap<FString, FCachedSignatures> Cache;

	auto GetConcreteTypeName = [](
		const uLang::CTypeBase& Type,
		uint32 UploadedVersion) -> TOptional<FString>
	{
		TSet<const uLang::CTypeBase*> Visiting;
		TFunction<TOptional<FString>(const uLang::CTypeBase&)> Format;
		Format = [&Visiting, &Format, UploadedVersion](
			const uLang::CTypeBase& Current) -> TOptional<FString>
		{
			if (Visiting.Contains(&Current))
			{
				return {};
			}
			if (const uLang::CFlowType* Flow = Current.AsFlowType())
			{
				const uLang::CTypeBase& Unwrapped =
					uLang::SemanticTypeUtils::SkipIdentityFlowType(
						Current, uLang::ETypePolarity::Positive, UploadedVersion);
				if (&Unwrapped != &Current)
				{
					return Format(Unwrapped);
				}

				// AsPositive can retain a constraint-flow wrapper after inference. Its
				// normal type is the materialized positive bound. Validate and format that
				// bound recursively; unresolved comparable/variable/unknown bounds are
				// rejected by the normal-type cases below.
				return Format(Flow->GetNormalType());
			}
			Visiting.Add(&Current);
			ON_SCOPE_EXIT { Visiting.Remove(&Current); };

			// Preserve a source-spellable alias rather than exposing its implementation.
			if (const uLang::CAliasType* Alias = Current.AsAliasType())
			{
				return Format(*Alias->GetAliasedType()).IsSet()
					? TOptional<FString>(ToFString(Current.AsCode()))
					: TOptional<FString>();
			}
			const uLang::CNormalType& Normal = Current.GetNormalType();
			const uLang::CNormalType& Stripped =
				uLang::SemanticTypeUtils::StripVariableAndConstraints(Normal);
			if (&Stripped != &Normal)
			{
				return Format(Stripped);
			}
			switch (Normal.GetKind())
			{
			case uLang::ETypeKind::Int:
				return FString(TEXT("int"));
			case uLang::ETypeKind::Float:
				return FString(TEXT("float"));
			case uLang::ETypeKind::Array:
			{
				const TOptional<FString> Element = Format(
					*Normal.AsChecked<uLang::CArrayType>().GetElementType());
				return Element.IsSet()
					? TOptional<FString>(TEXT("[]") + Element.GetValue())
					: TOptional<FString>();
			}
			case uLang::ETypeKind::Option:
			{
				const TOptional<FString> Value = Format(
					*Normal.AsChecked<uLang::COptionType>().GetValueType());
				return Value.IsSet()
					? TOptional<FString>(TEXT("?") + Value.GetValue())
					: TOptional<FString>();
			}
			case uLang::ETypeKind::Map:
			{
				const uLang::CMapType& Map = Normal.AsChecked<uLang::CMapType>();
				const TOptional<FString> Key = Format(*Map.GetKeyType());
				const TOptional<FString> Value = Format(*Map.GetValueType());
				return Key.IsSet() && Value.IsSet() && !Map.IsWeak()
					? TOptional<FString>(FString::Printf(
						TEXT("[%s]%s"), *Key.GetValue(), *Value.GetValue()))
					: TOptional<FString>();
			}
			case uLang::ETypeKind::Tuple:
			{
				const uLang::CTupleType& Tuple = Normal.AsChecked<uLang::CTupleType>();
				if (Tuple.GetFirstNamedIndex() != Tuple.Num())
				{
					return {};
				}
				TArray<FString> Elements;
				for (const uLang::CTypeBase* ElementType : Tuple.GetElements())
				{
					const TOptional<FString> Element = ElementType
						? Format(*ElementType) : TOptional<FString>();
					if (!Element.IsSet())
					{
						return {};
					}
					Elements.Add(Element.GetValue());
				}
				return FString::Printf(TEXT("(%s)"), *FString::Join(Elements, TEXT(", ")));
			}
			case uLang::ETypeKind::Unknown:
			case uLang::ETypeKind::False:
			case uLang::ETypeKind::True:
			case uLang::ETypeKind::Any:
			case uLang::ETypeKind::Comparable:
			case uLang::ETypeKind::Range:
			case uLang::ETypeKind::Type:
			case uLang::ETypeKind::Module:
			case uLang::ETypeKind::Function:
			case uLang::ETypeKind::Variable:
			case uLang::ETypeKind::Persistable:
			case uLang::ETypeKind::Castable:
			case uLang::ETypeKind::Concrete:
				return {};
			default:
				break;
			}

			bool bContainsInternalType = false;
			uLang::SemanticTypeUtils::ForEachTypeRecursive(
				&Current,
				uLang::ETypePolarity::Positive,
				[&bContainsInternalType](
					const uLang::CTypeBase* Nested,
					uLang::ETypePolarity)
				{
					if (Nested == nullptr || Nested->AsFlowType() != nullptr)
					{
						bContainsInternalType = true;
						return;
					}
					switch (Nested->GetNormalType().GetKind())
					{
					case uLang::ETypeKind::Unknown:
					case uLang::ETypeKind::False:
					case uLang::ETypeKind::True:
					case uLang::ETypeKind::Any:
					case uLang::ETypeKind::Comparable:
					case uLang::ETypeKind::Range:
					case uLang::ETypeKind::Variable:
						bContainsInternalType = true;
						break;
					default:
						break;
					}
				});
			return bContainsInternalType
				? TOptional<FString>()
				: TOptional<FString>(ToFString(Current.AsCode()));
		};
		return Format(Type);
	};

	const EVerseIntrinsicCallableForm PresentationForm = OperandCount == 1
		? EVerseIntrinsicCallableForm::PrefixOperator
		: EVerseIntrinsicCallableForm::InfixOperator;
	const FVerseIntrinsicPresentationDescriptor* Presentation =
		FindVerseIntrinsicOperatorPresentation(PresentationForm, OperatorSpelling);
	const bool bSymmetricOperands = Presentation != nullptr
		&& Presentation->bSymmetricOperands;
	const bool bOmitResult = Presentation != nullptr
		&& Presentation->bOmitResultInSignaturePicker;

	auto MakeSemanticKey = [](const FVerseOperatorSignature& Signature)
	{
		return FString::Printf(
			TEXT("%s -> %s"),
			*FString::Join(Signature.OperandTypeNames, TEXT(" x ")),
			*Signature.ResultTypeName);
	};
	auto AddResult = [&](FVerseOperatorSignature Signature)
	{
		const FString SemanticKey = MakeSemanticKey(Signature);
		if (!SeenSemanticSignatures.Contains(SemanticKey))
		{
			SeenSemanticSignatures.Add(SemanticKey);
			Result.Add(MoveTemp(Signature));
		}
	};

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

		TArray<FVisibleType> VisibleTypes;
		auto AddVisibleType = [
			&VisibleTypes,
			&GetConcreteTypeName,
			UploadedVersion](
			FString Name,
			const uLang::CTypeBase* Type)
		{
			if (Type == nullptr || !GetConcreteTypeName(*Type, UploadedVersion).IsSet()
				|| VisibleTypes.ContainsByPredicate(
					[&Name](const FVisibleType& Existing) { return Existing.Name == Name; }))
			{
				return;
			}
			VisibleTypes.Add({MoveTemp(Name), Type});
		};
		AddVisibleType(TEXT("logic"), &Program._logicType);
		AddVisibleType(TEXT("int"), Program._intType);
		AddVisibleType(TEXT("float"), Program._floatType);
		AddVisibleType(TEXT("string"),
			Program._stringAlias ? Program._stringAlias->GetType() : nullptr);
		AddVisibleType(TEXT("char"), &Program._char32Type);
		for (const FVerseVisualSocket* Source : ConnectedOperands)
		{
			if (Source != nullptr && Source->SemanticType != nullptr
				&& Source->SemanticSnapshot.Get() == Snapshot.Get())
			{
				const TOptional<FString> Name = GetConcreteTypeName(
					*Source->SemanticType, UploadedVersion);
				if (Name.IsSet())
				{
					AddVisibleType(Name.GetValue(), Source->SemanticType);
				}
			}
		}
		for (const uLang::CDefinition* Definition :
			CollectVisibleDefinitions(*ActiveScope, Program))
		{
			if (Definition == nullptr
				|| !Definition->IsAccessibleFrom(*ActiveScope)
				|| !IsVisibleAtCurrentPackageVersion(*Definition, *ActiveScope, Package)
				|| !IsUsableBareDataType(*Definition))
			{
				continue;
			}
			const FString Name = ToFString(Definition->AsNameStringView());
			if (ValidateVerseIdentifier(Name).IsEmpty())
			{
				AddVisibleType(Name, DefinitionAsDataType(*Definition));
			}
		}

		for (const uLang::CDefinition* Definition :
			CollectVisibleDefinitions(*ActiveScope, Program))
		{
			const uLang::CFunction* Function = Definition != nullptr
				? Definition->AsNullable<uLang::CFunction>() : nullptr;
			if (Function == nullptr
				|| !Function->IsAccessibleFrom(*ActiveScope)
				|| Function->IsInstanceMember()
				|| Function->_Signature.GetFunctionType() == nullptr
				|| GetOperatorSpelling(*Function) != OperatorSpelling)
			{
				continue;
			}

			FString CacheKey = FString::Printf(
				TEXT("%p|%p|%p|%d|%d|%d"),
				Snapshot.Get(), ActiveScope, Function, OperandCount,
				bSymmetricOperands, bOmitResult);
			for (const FVerseVisualSocket* Source : ConnectedOperands)
			{
				CacheKey += TEXT("|in:") + (Source
					? Source->SemanticTypeName : FString(TEXT("-")));
			}
			for (const FVerseVisualSocket* Consumer : OutputConsumers)
			{
				CacheKey += TEXT("|out:") + (Consumer
					? Consumer->SemanticTypeName : FString(TEXT("-")));
			}
			{
				FScopeLock Lock(&CacheMutex);
				for (auto It = Cache.CreateIterator(); It; ++It)
				{
					if (!It.Value().Snapshot.IsValid())
					{
						It.RemoveCurrent();
					}
				}
				if (const FCachedSignatures* Cached = Cache.Find(CacheKey))
				{
					for (FVerseOperatorSignature Signature : Cached->Signatures)
					{
						Signature.Snapshot = Snapshot;
						AddResult(MoveTemp(Signature));
					}
					continue;
				}
			}

			TArray<FVerseOperatorSignature> FunctionSignatures;
			auto AddFunctionSignature = [&](TArray<FString> OperandNames, FString ResultName)
			{
				FVerseOperatorSignature Signature;
				Signature.OperandTypeNames = MoveTemp(OperandNames);
				Signature.ResultTypeName = MoveTemp(ResultName);
				Signature.DisplayText = FString::Join(
					Signature.OperandTypeNames, TEXT(" x "));
				if (!bOmitResult)
				{
					Signature.DisplayText += TEXT(" -> ") + Signature.ResultTypeName;
				}
				Signature.Snapshot = Snapshot;
				const FString SemanticKey = MakeSemanticKey(Signature);
				if (!FunctionSignatures.ContainsByPredicate(
					[&SemanticKey, &MakeSemanticKey](const FVerseOperatorSignature& Existing)
					{
						return MakeSemanticKey(Existing) == SemanticKey;
					}))
				{
					FunctionSignatures.Add(MoveTemp(Signature));
				}
			};

			const uLang::CFunctionType* DeclaredType =
				Function->_Signature.GetFunctionType();
			if (DeclaredType->GetParamTypes().Num() != OperandCount)
			{
				continue;
			}

			if (DeclaredType->GetTypeVariables().IsEmpty())
			{
				const uLang::CFunctionType* Concrete =
					uLang::SemanticTypeUtils::Instantiate(DeclaredType, UploadedVersion);
				if (Concrete != nullptr)
				{
					TArray<FString> OperandNames;
					bool bConcrete = true;
					for (int32 Index = 0; Index < OperandCount; ++Index)
					{
						const uLang::CTypeBase* Parameter = Concrete->GetParamTypes()[Index];
						const TOptional<FString> Name = Parameter
							? GetConcreteTypeName(*Parameter, UploadedVersion)
							: TOptional<FString>();
						const FVerseVisualSocket* Source = ConnectedOperands.IsValidIndex(Index)
							? ConnectedOperands[Index] : nullptr;
						bConcrete &= Name.IsSet() && (Source == nullptr
							|| Source->SemanticTypeName.IsEmpty()
							|| (Source->SemanticType != nullptr
								&& Source->SemanticSnapshot.Get() == Snapshot.Get()
								? uLang::SemanticTypeUtils::IsSubtype(
									Source->SemanticType, Parameter, UploadedVersion)
								: Source->SemanticTypeName == Name.Get(FString())));
						if (Name.IsSet())
						{
							OperandNames.Add(Name.GetValue());
						}
					}
					const TOptional<FString> ResultName =
						GetConcreteTypeName(Concrete->GetReturnType(), UploadedVersion);
					for (const FVerseVisualSocket* Consumer : OutputConsumers)
					{
						if (!bConcrete || Consumer == nullptr
							|| Consumer->SemanticTypeName.IsEmpty())
						{
							continue;
						}
						bConcrete = Consumer->SemanticType != nullptr
							&& Consumer->SemanticSnapshot.Get() == Snapshot.Get()
							? uLang::SemanticTypeUtils::IsSubtype(
								&Concrete->GetReturnType(), Consumer->SemanticType, UploadedVersion)
							: ResultName.IsSet()
								&& ResultName.GetValue() == Consumer->SemanticTypeName;
					}
					if (bConcrete && ResultName.IsSet())
					{
						AddFunctionSignature(MoveTemp(OperandNames), ResultName.GetValue());
					}
				}
			}
			else
			{
				TArray<const FVisibleType*> Tuple;
				Tuple.Reserve(OperandCount);
				TFunction<void(int32)> Enumerate = [&](int32 Index)
				{
					if (Index < OperandCount)
					{
						if (bSymmetricOperands && !Tuple.IsEmpty())
						{
							const FVisibleType* SymmetricType = Tuple[0];
							Tuple.Add(SymmetricType);
							Enumerate(Index + 1);
							Tuple.Pop();
							return;
						}
						for (const FVisibleType& Type : VisibleTypes)
						{
							const FVerseVisualSocket* Source =
								ConnectedOperands.IsValidIndex(Index)
									? ConnectedOperands[Index] : nullptr;
							if (Source != nullptr && !Source->SemanticTypeName.IsEmpty())
							{
								const bool bCompatible = Source->SemanticType != nullptr
									&& Source->SemanticSnapshot.Get() == Snapshot.Get()
									? uLang::SemanticTypeUtils::IsSubtype(
										Source->SemanticType, Type.Type, UploadedVersion)
									: Source->SemanticTypeName == Type.Name;
								if (!bCompatible)
								{
									continue;
								}
							}
							Tuple.Add(&Type);
							Enumerate(Index + 1);
							Tuple.Pop();
						}
						return;
					}

					uLang::TArray<uLang::STypeVariableSubstitution> Substitutions;
					const uLang::CFunctionType* Instantiated =
						uLang::SemanticTypeUtils::Instantiate(
							DeclaredType, UploadedVersion, Substitutions);
					if (Instantiated == nullptr)
					{
						return;
					}
					uLang::CTupleType::ElementArray ArgumentTypes;
					for (const FVisibleType* Type : Tuple)
					{
						ArgumentTypes.Add(Type->Type);
					}
					const uLang::CTypeBase* ArgumentTuple =
						uLang::CFunctionType::GetOrCreateParamType(
							Instantiated->GetProgram(), MoveTemp(ArgumentTypes));
					if (ArgumentTuple == nullptr
						|| !uLang::SemanticTypeUtils::ConstrainCanonicalized(
							ArgumentTuple,
							&Instantiated->GetParamsType(),
							UploadedVersion))
					{
						return;
					}
					for (const FVerseVisualSocket* Consumer : OutputConsumers)
					{
						if (Consumer != nullptr && Consumer->SemanticType != nullptr
							&& Consumer->SemanticSnapshot.Get() == Snapshot.Get()
							&& !uLang::SemanticTypeUtils::ConstrainCanonicalized(
								&Instantiated->GetReturnType(),
								&Consumer->SemanticType->GetNormalType(),
								UploadedVersion))
						{
							return;
						}
					}
					const uLang::TArray<uLang::SInstantiatedTypeVariable> InstantiatedVariables =
						uLang::SemanticTypeUtils::AsInstantiatedTypeVariables(Substitutions);
					const uLang::CTypeBase& ResolvedResult =
						uLang::SemanticTypeUtils::AsPositive(
							Instantiated->GetReturnType(), InstantiatedVariables);
					const TOptional<FString> ResultName =
						GetConcreteTypeName(ResolvedResult, UploadedVersion);
					if (!ResultName.IsSet())
					{
						return;
					}
					for (const FVerseVisualSocket* Consumer : OutputConsumers)
					{
						if (Consumer == nullptr || Consumer->SemanticTypeName.IsEmpty())
						{
							continue;
						}
						const bool bCompatible = Consumer->SemanticType != nullptr
							&& Consumer->SemanticSnapshot.Get() == Snapshot.Get()
							? uLang::SemanticTypeUtils::IsSubtype(
								&ResolvedResult, Consumer->SemanticType, UploadedVersion)
							: ResultName.GetValue() == Consumer->SemanticTypeName;
						if (!bCompatible)
						{
							return;
						}
					}
					TArray<FString> OperandNames;
					// The complete argument tuple is the concrete source signature selected
					// by inference. Some constrained declarations intentionally retain their
					// abstract formal (for example, `comparable`) even after a concrete tuple
					// has satisfied it, so displaying the formal would leak compiler bounds.
					for (const FVisibleType* Type : Tuple)
					{
						const TOptional<FString> Name = GetConcreteTypeName(
							*Type->Type, UploadedVersion);
						if (!Name.IsSet())
						{
							return;
						}
						OperandNames.Add(Name.GetValue());
					}
					AddFunctionSignature(MoveTemp(OperandNames), ResultName.GetValue());
				};
				Enumerate(0);
			}

			{
				FScopeLock Lock(&CacheMutex);
				FCachedSignatures Cached;
				Cached.Snapshot = Snapshot;
				Cached.Signatures = FunctionSignatures;
				for (FVerseOperatorSignature& Signature : Cached.Signatures)
				{
					Signature.Snapshot.Reset();
				}
				Cache.Add(CacheKey, MoveTemp(Cached));
			}
			for (FVerseOperatorSignature& Signature : FunctionSignatures)
			{
				AddResult(MoveTemp(Signature));
			}
		}
	}
	Result.Sort([](const FVerseOperatorSignature& Left, const FVerseOperatorSignature& Right)
	{
		return Left.DisplayText.Compare(Right.DisplayText, ESearchCase::IgnoreCase) < 0;
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
	FVerseVisualTileBuilder::FinalizeSocketTopology(GraphTiles);
}
