#if WITH_DEV_AUTOMATION_TESTS

#include "VerseSemanticWorkspace.h"
#include "VerseDocument.h"
#include "VerseDocumentSession.h"
#include "VerseExpressionActions.h"
#include "VerseFunctionNavigation.h"
#include "VerseParseSnapshotBuilder.h"
#include "VerseSemanticCandidates.h"
#include "VerseVisualTile.h"

#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "uLang/Semantics/SemanticFunction.h"
#include "uLang/Semantics/SemanticProgram.h"

namespace VerseSemanticWorkspaceTests
{
	FVerseSemanticDocumentInput MakeDocument(const TCHAR* Path, uint64 Revision)
	{
		FVerseSemanticDocumentInput Document;
		Document.FilePath = Path;
		Document.Source = FUtf8String(UTF8TEXT("Value := 1\n"));
		Document.Revision.Value = Revision;
		return Document;
	}

	FVerseSemanticAnalysisResult MakeSuccess(
		TConstArrayView<FVerseSemanticDocumentInput> Documents)
	{
		FVerseSemanticAnalysisResult Result;
		Result.bSucceeded = true;
		Result.Snapshot = FVerseSemanticSnapshot::CreateForTesting(Documents);
		return Result;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVerseSemanticWorkspaceDebounceTest,
	"VerseVisualEditor.Semantics.Workspace.DebouncesAndPublishesExactRevision",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVerseSemanticWorkspaceDebounceTest::RunTest(const FString& Parameters)
{
	int32 AnalysisCount = 0;
	FVerseSemanticWorkspace Workspace(
		[&AnalysisCount](TConstArrayView<FVerseSemanticDocumentInput> Documents)
		{
			++AnalysisCount;
			return VerseSemanticWorkspaceTests::MakeSuccess(Documents);
		},
		0.25);
	const FVerseSemanticDocumentInput Document =
		VerseSemanticWorkspaceTests::MakeDocument(TEXT("C:/Project/Test.verse"), 3);

	Workspace.RequestAnalysis({Document}, 10.0, true);
	Workspace.Tick(10.24);
	TestEqual(TEXT("Analysis waits for the debounce interval"), AnalysisCount, 0);
	TestEqual(TEXT("Workspace remains debouncing"), Workspace.GetState(), EVerseSemanticWorkspaceState::Debouncing);

	Workspace.Tick(10.25);
	TestEqual(TEXT("Analysis runs once after the debounce interval"), AnalysisCount, 1);
	TestEqual(TEXT("Successful analysis becomes ready"), Workspace.GetState(), EVerseSemanticWorkspaceState::Ready);
	TestTrue(TEXT("Published snapshot describes the exact revision"), Workspace.HasExactSnapshot(Document.FilePath, Document.Revision));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVerseSemanticWorkspaceSupersededResultTest,
	"VerseVisualEditor.Semantics.Workspace.RejectsSupersededResult",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVerseSemanticWorkspaceSupersededResultTest::RunTest(const FString& Parameters)
{
	const FVerseSemanticDocumentInput OldDocument =
		VerseSemanticWorkspaceTests::MakeDocument(TEXT("C:/Project/Test.verse"), 4);
	const FVerseSemanticDocumentInput NewDocument =
		VerseSemanticWorkspaceTests::MakeDocument(TEXT("C:/Project/Test.verse"), 5);
	FVerseSemanticWorkspace* WorkspacePointer = nullptr;
	int32 AnalysisCount = 0;
	FVerseSemanticWorkspace Workspace(
		[&](TConstArrayView<FVerseSemanticDocumentInput> Documents)
		{
			++AnalysisCount;
			if (AnalysisCount == 1)
			{
				WorkspacePointer->RequestAnalysis({NewDocument}, 1.0, false);
			}
			return VerseSemanticWorkspaceTests::MakeSuccess(Documents);
		},
		0.0);
	WorkspacePointer = &Workspace;

	Workspace.RequestAnalysis({OldDocument}, 1.0, false);
	Workspace.Tick(1.0);
	TestFalse(TEXT("Superseded result is not published"), Workspace.HasExactSnapshot(OldDocument.FilePath, OldDocument.Revision));
	TestEqual(TEXT("Newer request remains pending"), Workspace.GetState(), EVerseSemanticWorkspaceState::Debouncing);

	Workspace.Tick(1.0);
	TestEqual(TEXT("Both requested analyses ran"), AnalysisCount, 2);
	TestTrue(TEXT("Newest exact revision is published"), Workspace.HasExactSnapshot(NewDocument.FilePath, NewDocument.Revision));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVerseSemanticWorkspaceFailureRetentionTest,
	"VerseVisualEditor.Semantics.Workspace.FailureRetainsLastSuccessfulSnapshot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVerseSemanticWorkspaceFailureRetentionTest::RunTest(const FString& Parameters)
{
	bool bFail = false;
	FVerseSemanticWorkspace Workspace(
		[&bFail](TConstArrayView<FVerseSemanticDocumentInput> Documents)
		{
			if (!bFail)
			{
				return VerseSemanticWorkspaceTests::MakeSuccess(Documents);
			}
			FVerseSemanticAnalysisResult Result;
			FVerseSemanticDiagnostic& Diagnostic = Result.Diagnostics.AddDefaulted_GetRef();
			Diagnostic.Message = FText::FromString(TEXT("Expected semantic failure"));
			Diagnostic.Severity = ELogVerbosity::Error;
			return Result;
		},
		0.0);
	const FVerseSemanticDocumentInput OldDocument =
		VerseSemanticWorkspaceTests::MakeDocument(TEXT("C:/Project/Test.verse"), 8);
	const FVerseSemanticDocumentInput NewDocument =
		VerseSemanticWorkspaceTests::MakeDocument(TEXT("C:/Project/Test.verse"), 9);

	Workspace.RequestAnalysis({OldDocument}, 2.0, false);
	Workspace.Tick(2.0);
	bFail = true;
	Workspace.RequestAnalysis({NewDocument}, 3.0, false);
	Workspace.Tick(3.0);

	TestEqual(TEXT("Failed current analysis is reported"), Workspace.GetState(), EVerseSemanticWorkspaceState::Failed);
	TestTrue(
		TEXT("Failed analysis still records the exact revision it diagnosed"),
		Workspace.LatestAnalysisDescribes(NewDocument.FilePath, NewDocument.Revision));
	TestFalse(
		TEXT("Failed analysis does not describe the previous revision"),
		Workspace.LatestAnalysisDescribes(OldDocument.FilePath, OldDocument.Revision));
	TestTrue(
		TEXT("Last successful snapshot remains available for display"),
		Workspace.GetLastSuccessfulSnapshot().IsValid()
			&& Workspace.GetLastSuccessfulSnapshot()->Describes(
				OldDocument.FilePath,
				OldDocument.Revision));
	const TArray<TSharedPtr<const FVerseSemanticSnapshot>> CandidateSnapshots =
		Workspace.GetCandidateSnapshots();
	TestTrue(
		TEXT("Candidate discovery falls back to the last successful local snapshot"),
		!CandidateSnapshots.IsEmpty()
			&& CandidateSnapshots[0]->Describes(
				OldDocument.FilePath,
				OldDocument.Revision));
	TestFalse(TEXT("Retained display snapshot cannot authorize mutation after failure"), Workspace.HasExactSnapshot(OldDocument.FilePath, OldDocument.Revision));
	TestFalse(TEXT("Stale snapshot cannot authorize the newer revision"), Workspace.HasExactSnapshot(NewDocument.FilePath, NewDocument.Revision));
	TestFalse(TEXT("Failure supplies an explanatory mutation status"), Workspace.GetMutationUnavailableReason(NewDocument.FilePath, NewDocument.Revision).IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVerseSemanticDiagnosticFileOwnershipTest,
	"VerseVisualEditor.Semantics.Workspace.DiagnosticsBelongToTheirDocument",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVerseSemanticDiagnosticFileOwnershipTest::RunTest(const FString& Parameters)
{
	FVerseSemanticDiagnostic Diagnostic;
	Diagnostic.FilePath = TEXT("C:/Project/Folder/SemanticErrors.verse");
	TestTrue(
		TEXT("Normalized matching paths own their diagnostics"),
		Diagnostic.AppliesToFile(TEXT("c:\\project\\folder\\SemanticErrors.verse")));
	TestFalse(
		TEXT("A diagnostic is not shown for a different document"),
		Diagnostic.AppliesToFile(TEXT("C:/Project/Folder/GlobalScopeCorpus.verse")));
	Diagnostic.FilePath.Reset();
	TestTrue(
		TEXT("Workspace-level diagnostics remain visible without a source file"),
		Diagnostic.AppliesToFile(TEXT("C:/Project/Folder/GlobalScopeCorpus.verse")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVerseSemanticWorkspaceIsolatedCompilerTest,
	"VerseVisualEditor.Semantics.Workspace.IsolatedCompilerOverlay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVerseSemanticWorkspaceIsolatedCompilerTest::RunTest(const FString& Parameters)
{
	const FString FilePath = FPaths::ConvertRelativePathToFull(
		FPaths::ProjectPluginsDir()
		/ TEXT("VerseCorpus/Content/ExternalCorpus/UEFNCentralLibrary/Formater.verse"));
	FString Source;
	if (!TestTrue(TEXT("Semantic corpus source can be loaded"), FFileHelper::LoadFileToString(Source, *FilePath)))
	{
		return false;
	}
	Source += TEXT("\n# Unsaved semantic workspace integration test.\n");
	const FTCHARToUTF8 Utf8Source(*Source);
	FVerseSemanticDocumentInput Document;
	Document.FilePath = FilePath;
	Document.Source = FUtf8String(
		FUtf8StringView(
			reinterpret_cast<const UTF8CHAR*>(Utf8Source.Get()),
			Utf8Source.Length()));
	Document.Revision.Value = 77;

	FVerseSemanticWorkspace Workspace(0.0);
	Workspace.RequestAnalysis({Document}, 0.0, false);
	Workspace.Tick(0.0);

	if (!TestEqual(TEXT("Isolated compiler analysis becomes ready"), Workspace.GetState(), EVerseSemanticWorkspaceState::Ready))
	{
		for (const FVerseSemanticDiagnostic& Diagnostic : Workspace.GetDiagnostics())
		{
			AddError(Diagnostic.Message.ToString());
		}
		return false;
	}
	TestTrue(TEXT("Compiler snapshot describes the unsaved exact revision"), Workspace.HasExactSnapshot(Document.FilePath, Document.Revision));
	TestTrue(TEXT("Compiler snapshot owns a semantic program"), Workspace.GetLastSuccessfulSnapshot()->GetProgram().IsValid());
	TestTrue(TEXT("Compiler snapshot owns a project VST"), Workspace.GetLastSuccessfulSnapshot()->GetProjectVst().IsValid());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVerseSemanticWorkspaceUnregisteredFileTest,
	"VerseVisualEditor.Semantics.Workspace.InMemoryUnregisteredFile",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVerseSemanticWorkspaceUnregisteredFileTest::RunTest(const FString& Parameters)
{
	FVerseSemanticDocumentInput Document;
	Document.FilePath = FPaths::ConvertRelativePathToFull(
		FPaths::ProjectPluginsDir()
		/ TEXT("VerseVisualEditor/Content/TestCorpus/PrivateSemanticOverlayOnly.verse"));
	Document.Source = FUtf8String(UTF8TEXT(
		"PrivateValueType := class {}\n"
		"AcceptInt(Value : int)<computes> : int = Value\n"
		"Identity(Value : t where t:type)<computes> : t = Value\n"
		"WithDefault(Required : int, ?Optional : float = 1.0)<computes> : float = Optional\n"
		"PrivateSemanticOverlayOnly(Input : int)<computes> : int = Input + 1\n"
		"PrivateFloatOverlay(Input : float)<computes> : float = Input + 1.0\n"
		"FloatInitializer()<computes> : float =\n"
		"    Value : float = 0.0\n"
		"    Value\n"
		"FloatCondition(Input : float)<computes> : float =\n"
		"    if (Threshold : float = 0.0; Input > Threshold):\n"
		"        Input\n"
		"    else:\n"
		"        0.0\n"
		"ScopedVisibility(Input : float)<computes> : float =\n"
		"    Outer : float = Input\n"
		"    if (Inner : float = Outer; Inner > 0.0):\n"
		"        Inner\n"
		"    Outer\n"
		"CallAcceptInt(Input : int)<computes> : int = AcceptInt(Input)\n"
		"CallGenericInt(Input : int)<computes> : int = Identity(Input)\n"
		"CallGenericFloat(Input : float)<computes> : float = Identity(Input)\n"
		"CallDefault(Input : int)<computes> : float = WithDefault(Input)\n"
		"CompareFloat(Input : float)<computes><decides> : float = Input <> 0.0\n"
		"QueryLogic(Input : logic)<computes><decides> : logic = Input?\n"));
	Document.Revision.Value = 91;

	TestFalse(TEXT("The test document is not registered by existing on-disk source"), FPaths::FileExists(Document.FilePath));

	FVerseSemanticWorkspace Workspace(0.0);
	Workspace.RequestAnalysis({Document}, 0.0, false);
	Workspace.Tick(0.0);

	if (!TestEqual(TEXT("In-memory unregistered source becomes ready"), Workspace.GetState(), EVerseSemanticWorkspaceState::Ready))
	{
		for (const FVerseSemanticDiagnostic& Diagnostic : Workspace.GetDiagnostics())
		{
			AddError(Diagnostic.Message.ToString());
		}
		return false;
	}
	TestTrue(TEXT("Overlay snapshot describes the exact private revision"), Workspace.HasExactSnapshot(Document.FilePath, Document.Revision));
	TestTrue(TEXT("Overlay contributes to the semantic program"), Workspace.GetLastSuccessfulSnapshot()->GetProgram().IsValid());
	TestTrue(TEXT("Overlay contributes to the project VST"), Workspace.GetLastSuccessfulSnapshot()->GetProjectVst().IsValid());

	FText DocumentError;
	const TConstArrayView<uint8> SourceBytes(
		reinterpret_cast<const uint8*>(*Document.Source), Document.Source.Len());
	const TSharedPtr<const FVerseDocument> ParsedDocument =
		FVerseDocument::CreateFromBytes(SourceBytes, DocumentError);
	if (!TestTrue(TEXT("Candidate test source creates a document"), ParsedDocument.IsValid()))
	{
		AddError(DocumentError.ToString());
		return false;
	}
	const FUtf8StringView SourceView = ParsedDocument->GetOriginalUtf8View();
	const int32 ExpressionBeginByte = SourceView.Find(UTF8TEXTVIEW("Input + 1"));
	if (!TestTrue(TEXT("Candidate test locates its expression"), ExpressionBeginByte != INDEX_NONE))
	{
		return false;
	}
	const TArray<TSharedPtr<const FVerseSemanticSnapshot>> CandidateSnapshots =
		Workspace.GetCandidateSnapshots();
	const TArray<FString> VisibleTypeNames =
		FVerseSemanticCandidateProvider::BuildVisibleTypeNames(
			CandidateSnapshots,
			Document.FilePath,
			ExpressionBeginByte,
			*ParsedDocument);
	TestTrue(TEXT("Type discovery retains core scalar types"),
		VisibleTypeNames.Contains(TEXT("int"))
			&& VisibleTypeNames.Contains(TEXT("logic")));
	TestTrue(TEXT("Type discovery includes a class visible in the current module"),
		VisibleTypeNames.Contains(TEXT("PrivateValueType")));
	TestTrue(TEXT("Type discovery retains usable abstract data types"),
		VisibleTypeNames.Contains(TEXT("comparable"))
			&& VisibleTypeNames.Contains(TEXT("cancelable")));
	TestFalse(TEXT("Type discovery excludes attribute and specifier classes"),
		VisibleTypeNames.Contains(TEXT("public"))
			|| VisibleTypeNames.Contains(TEXT("private"))
			|| VisibleTypeNames.Contains(TEXT("protected"))
			|| VisibleTypeNames.Contains(TEXT("override"))
			|| VisibleTypeNames.Contains(TEXT("castable")));
	const FUtf8StringView GenericFunctionPrefix = UTF8TEXTVIEW(
		"Identity(Value : t where t:type)<computes> : t = ");
	const int32 GenericFunctionBeginByte = SourceView.Find(GenericFunctionPrefix);
	const int32 GenericExpressionBeginByte = GenericFunctionBeginByte != INDEX_NONE
		? GenericFunctionBeginByte + GenericFunctionPrefix.Len()
		: INDEX_NONE;
	if (TestTrue(TEXT("Candidate test locates the generic function expression"),
		GenericExpressionBeginByte != INDEX_NONE))
	{
		const TArray<FString> GenericScopeTypeNames =
			FVerseSemanticCandidateProvider::BuildVisibleTypeNames(
				CandidateSnapshots,
				Document.FilePath,
				GenericExpressionBeginByte,
				*ParsedDocument);
		TestTrue(TEXT("A function type variable is available inside its scope"),
			GenericScopeTypeNames.Contains(TEXT("t")));
		TestFalse(TEXT("A function type variable is unavailable outside its scope"),
			VisibleTypeNames.Contains(TEXT("t")));
	}
	const int32 InnerScopeBeginByte =
		SourceView.Find(UTF8TEXTVIEW("        Inner\n    Outer"));
	const int32 ConditionUseBeginByte =
		SourceView.Find(UTF8TEXTVIEW("Inner > 0.0"));
	const int32 OuterScopeBeginByte = InnerScopeBeginByte != INDEX_NONE
		? InnerScopeBeginByte + UTF8TEXTVIEW("        Inner\n    ").Len()
		: INDEX_NONE;
	if (TestTrue(TEXT("Candidate test locates nested and enclosing statements"),
		InnerScopeBeginByte != INDEX_NONE
			&& ConditionUseBeginByte != INDEX_NONE
			&& OuterScopeBeginByte != INDEX_NONE))
	{
		auto HasIdentifier = [](TConstArrayView<FVerseSemanticCandidate> InCandidates,
			uLang::CUTF8StringView Name)
		{
			return InCandidates.ContainsByPredicate(
				[Name](const FVerseSemanticCandidate& Candidate)
				{
					return Candidate.Kind == EVerseSemanticCandidateKind::Identifier
						&& Candidate.DataDefinition != nullptr
						&& Candidate.DataDefinition->AsNameStringView() == Name;
				});
		};
		const TArray<FVerseSemanticCandidate> InnerScopeCandidates =
			FVerseSemanticCandidateProvider::Build(
				CandidateSnapshots, Document.FilePath, InnerScopeBeginByte,
				false, *ParsedDocument);
		const TArray<FVerseSemanticCandidate> ConditionUseCandidates =
			FVerseSemanticCandidateProvider::Build(
				CandidateSnapshots, Document.FilePath, ConditionUseBeginByte,
				false, *ParsedDocument);
		const TArray<FVerseSemanticCandidate> OuterScopeCandidates =
			FVerseSemanticCandidateProvider::Build(
				CandidateSnapshots, Document.FilePath, OuterScopeBeginByte,
				false, *ParsedDocument);
		TestTrue(TEXT("Nested statement sees identifiers introduced by its condition"),
			HasIdentifier(InnerScopeCandidates, uLang::CUTF8StringView("Inner")));
		TestTrue(TEXT("A later condition expression sees an earlier condition definition"),
			HasIdentifier(ConditionUseCandidates, uLang::CUTF8StringView("Inner")));
		TestFalse(TEXT("Condition-local identifiers do not leak past the control scope"),
			HasIdentifier(OuterScopeCandidates, uLang::CUTF8StringView("Inner")));
		TestTrue(TEXT("Both statements retain identifiers from their enclosing function"),
			HasIdentifier(InnerScopeCandidates, uLang::CUTF8StringView("Outer"))
				&& HasIdentifier(
					OuterScopeCandidates, uLang::CUTF8StringView("Outer")));
	}
	const TArray<FVerseSemanticCandidate> Candidates =
		FVerseSemanticCandidateProvider::Build(
			CandidateSnapshots,
			Document.FilePath,
			ExpressionBeginByte,
			true,
			*ParsedDocument);
	const int32 QueryExpressionBeginByte = SourceView.Find(UTF8TEXTVIEW("Input?"));
	const TArray<FVerseSemanticCandidate> QueryCandidates =
		FVerseSemanticCandidateProvider::Build(
			CandidateSnapshots,
			Document.FilePath,
			QueryExpressionBeginByte,
			true,
			*ParsedDocument);
	const TArray<TSharedPtr<FVerseExpressionAction>> UntypedSemanticActions =
		FVerseExpressionActionQuery::BuildAll(
			{},
			*ParsedDocument,
			FVerseTextRange(Document.Revision, {ExpressionBeginByte, 0}),
			Document.FilePath,
			CandidateSnapshots);
	const TArray<TSharedPtr<FVerseExpressionAction>> UntypedAddActions =
		UntypedSemanticActions.FilterByPredicate(
			[](const TSharedPtr<FVerseExpressionAction>& Action)
			{
				return Action.IsValid()
					&& Action->SourceForm == EVerseExpressionSourceForm::InfixOperator
					&& Action->SourceSpelling == TEXT("+");
			});
	TestEqual(TEXT("Untyped semantic search presents one polymorphic Add action"),
		UntypedAddActions.Num(), 1);
	if (!UntypedAddActions.IsEmpty())
	{
		FString UntypedAddSource;
		TestTrue(TEXT("Unconstrained Add has a source-safe default overload"),
			BuildVerseExpressionActionSource(
				*UntypedAddActions[0], FStringView(), UntypedAddSource, DocumentError));
		TestEqual(TEXT("Unconstrained Add starts as integer addition"),
			UntypedAddSource, FString(TEXT("0 + 0")));
	}
	TMap<FString, int32> UntypedOperatorCounts;
	for (const TSharedPtr<FVerseExpressionAction>& Action : UntypedSemanticActions)
	{
		if (!Action.IsValid())
		{
			continue;
		}
		int32 Form = INDEX_NONE;
		switch (Action->SourceForm)
		{
		case EVerseExpressionSourceForm::InfixOperator: Form = 0; break;
		case EVerseExpressionSourceForm::PrefixOperator: Form = 1; break;
		case EVerseExpressionSourceForm::PostfixOperator: Form = 2; break;
		default: break;
		}
		if (Form != INDEX_NONE)
		{
			++UntypedOperatorCounts.FindOrAdd(FString::Printf(
				TEXT("%d|%s"), Form, *Action->SourceSpelling));
		}
	}
	for (const TPair<FString, int32>& Pair : UntypedOperatorCounts)
	{
		TestEqual(
			*FString::Printf(TEXT("Operator action %s appears once"), *Pair.Key),
			Pair.Value,
			1);
	}
	const TSharedPtr<FVerseExpressionAction>* UntypedNotEqualAction =
		UntypedSemanticActions.FindByPredicate(
			[](const TSharedPtr<FVerseExpressionAction>& Action)
			{
				return Action.IsValid() && Action->SourceSpelling == TEXT("<>");
			});
	if (TestNotNull(TEXT("Untyped semantic search offers Not Equal"),
		UntypedNotEqualAction))
	{
		FString UntypedNotEqualSource;
		TestTrue(TEXT("Untyped Not Equal has source-safe operands"),
			BuildVerseExpressionActionSource(
				**UntypedNotEqualAction,
				FStringView(),
				UntypedNotEqualSource,
				DocumentError));
		TestEqual(TEXT("Untyped Not Equal defaults to a valid integer expression"),
			UntypedNotEqualSource,
			FString(TEXT("0 <> 0")));
	}
	FVerseVisualSocket LogicOutputSocket;
	LogicOutputSocket.IntrinsicTypeName = TEXT("logic");
	const TArray<TSharedPtr<FVerseExpressionAction>> QueryActions =
		FVerseExpressionActionQuery::Build(
			{},
			LogicOutputSocket,
			true,
			*ParsedDocument,
			FVerseTextRange(Document.Revision, {QueryExpressionBeginByte, 5}),
			Document.FilePath,
			CandidateSnapshots);
	FVerseVisualSocket OutputSocket;
	const TArray<TSharedPtr<FVerseExpressionAction>> Actions =
		FVerseExpressionActionQuery::Build(
			{}, OutputSocket, true, *ParsedDocument,
			FVerseTextRange(Document.Revision, {ExpressionBeginByte, 9}),
			Document.FilePath, CandidateSnapshots);
	const TSharedPtr<FVerseExpressionAction>* NotEqualAction =
		Actions.FindByPredicate([](const TSharedPtr<FVerseExpressionAction>& Action)
		{
			return Action.IsValid()
				&& Action->SourceForm == EVerseExpressionSourceForm::InfixOperator
				&& Action->SourceSpelling == TEXT("<>");
		});
	TestEqual(TEXT("Symmetric Not Equal contributes one canonical action"),
		Actions.FilterByPredicate([](const TSharedPtr<FVerseExpressionAction>& Action)
		{
			return Action.IsValid() && Action->SourceSpelling == TEXT("<>");
		}).Num(), 1);
	if (TestNotNull(TEXT("Comparable Not Equal is offered for an integer output"),
		NotEqualAction))
	{
		TestEqual(TEXT("Not Equal uses its Blueprint-style searchable name"),
			(*NotEqualAction)->DisplayName.ToString(), FString(TEXT("Not Equal (!=)")));
		TestEqual(TEXT("Not Equal uses the operators category"),
			(*NotEqualAction)->Category.ToString(), FString(TEXT("Utilities|Operators")));
		FString NotEqualSource;
		TestTrue(TEXT("Not Equal materializes source with a concrete RHS default"),
			BuildVerseExpressionActionSource(
				**NotEqualAction, TEXT("Input"), NotEqualSource, DocumentError));
		TestEqual(TEXT("Not Equal writes canonical Verse syntax"),
			NotEqualSource, FString(TEXT("Input <> 0")));
	}
	const FVerseParseSnapshot SyntaxSnapshot =
		FVerseParseSnapshotBuilder::Build(ParsedDocument.ToSharedRef());
	const TArray<FVerseVisualTile> SyntaxTiles =
		FVerseVisualTileBuilder::Build(SyntaxSnapshot, Document.Revision);
	TArray<FVerseFunctionNavigationItem> BoundFunctions =
		FVerseFunctionNavigationBuilder::Build(SyntaxTiles, SyntaxSnapshot);
	FVerseFunctionNavigationItem* BoundIntFunction = BoundFunctions.FindByPredicate(
		[](const FVerseFunctionNavigationItem& Function)
		{
			return Function.Name == TEXT("PrivateSemanticOverlayOnly");
		});
	if (TestNotNull(TEXT("Semantic binding fixture function exists"), BoundIntFunction)
		&& TestTrue(TEXT("Semantic binding fixture has a statement"),
			BoundIntFunction->GraphTiles.Num() >= 3))
	{
		FVerseSemanticCandidateProvider::BindFunctionGraph(
			BoundIntFunction->GraphTiles,
			Workspace.GetLastSuccessfulSnapshot(),
			Document.FilePath,
			*ParsedDocument);
		const FVerseVisualTile& BoundAdd = BoundIntFunction->GraphTiles[1];
		TestNotNull(TEXT("Parsed Add is bound to its compiler CFunction"),
			BoundAdd.SemanticFunction);
		TestEqual(TEXT("Compiler supplies Add's concrete result type"),
			BoundAdd.SemanticTypeName, FString(TEXT("int")));
		TestTrue(TEXT("Compiler supplies both ordered Add parameter types"),
			BoundAdd.GetValueInputs().Num() == 2
			&& BoundAdd.GetValueInputs()[0].SemanticTypeName == TEXT("int")
			&& BoundAdd.GetValueInputs()[1].SemanticTypeName == TEXT("int"));
		if (BoundAdd.GetValueInputs().Num() == 2
			&& BoundAdd.Children.Num() == 2)
		{
			const TArray<TSharedPtr<FVerseExpressionAction>> IntProviders =
				FVerseExpressionActionQuery::Build(
					{},
					BoundAdd.GetValueInputs()[1],
					false,
					*ParsedDocument,
					BoundAdd.Children[1].Range,
					Document.FilePath,
					CandidateSnapshots);
			TestTrue(TEXT("An integer literal operand can be replaced by an int identifier"),
				IntProviders.ContainsByPredicate(
					[](const TSharedPtr<FVerseExpressionAction>& Action)
					{
						return Action.IsValid()
							&& Action->SourceForm
								== EVerseExpressionSourceForm::IdentifierReference
							&& Action->SourceSpelling == TEXT("Input");
					}));
		}
		const TArray<FVerseOperatorSignature> AddSignatures =
			FVerseSemanticCandidateProvider::BuildOperatorSignatures(
				CandidateSnapshots,
				Document.FilePath,
				BoundAdd.Range.BeginByte,
				*ParsedDocument,
				BoundAdd.OperatorSpelling,
				2,
				{},
				{});
		TestTrue(TEXT("Concrete arithmetic retains its result-bearing signature"),
			AddSignatures.ContainsByPredicate([](const FVerseOperatorSignature& Signature)
			{
				return Signature.DisplayText == TEXT("int x int -> int");
			}));
	}
	FVerseFunctionNavigationItem* BoundCallFunction = BoundFunctions.FindByPredicate(
		[](const FVerseFunctionNavigationItem& Function)
		{
			return Function.Name == TEXT("CallAcceptInt");
		});
	if (TestNotNull(TEXT("Semantic call binding fixture exists"), BoundCallFunction)
		&& TestTrue(TEXT("Semantic call fixture has a statement"),
			BoundCallFunction->GraphTiles.Num() >= 3))
	{
		FVerseSemanticCandidateProvider::BindFunctionGraph(
			BoundCallFunction->GraphTiles,
			Workspace.GetLastSuccessfulSnapshot(),
			Document.FilePath,
			*ParsedDocument);
		const FVerseVisualTile& BoundCall = BoundCallFunction->GraphTiles[1];
		TestTrue(TEXT("Call binds to the selected compiler signature"),
			BoundCall.ExpressionKind == EVerseExpressionKind::Call
			&& BoundCall.SemanticFunction != nullptr
			&& BoundCall.SemanticTypeName == TEXT("int")
			&& BoundCall.GetValueInputs().Num() == 1
			&& BoundCall.GetValueInputs()[0].SemanticName == TEXT("Value")
			&& BoundCall.GetValueInputs()[0].SemanticTypeName == TEXT("int"));
	}
	auto BindNamedFunction = [&](const TCHAR* Name) -> FVerseVisualTile*
	{
		FVerseFunctionNavigationItem* Function = BoundFunctions.FindByPredicate(
			[Name](const FVerseFunctionNavigationItem& Candidate)
			{
				return Candidate.Name == Name;
			});
		if (Function == nullptr || Function->GraphTiles.Num() < 3)
		{
			return nullptr;
		}
		FVerseSemanticCandidateProvider::BindFunctionGraph(
			Function->GraphTiles,
			Workspace.GetLastSuccessfulSnapshot(),
			Document.FilePath,
			*ParsedDocument);
		return &Function->GraphTiles[1];
	};
	FVerseVisualTile* GenericInt = BindNamedFunction(TEXT("CallGenericInt"));
	FVerseVisualTile* GenericFloat = BindNamedFunction(TEXT("CallGenericFloat"));
	if (TestNotNull(TEXT("Generic int invocation binds"), GenericInt)
		&& TestNotNull(TEXT("Generic float invocation binds"), GenericFloat))
	{
		TestTrue(TEXT("Each generic instantiation keeps one stable formal socket"),
			GenericInt->GetValueInputs().Num() == 1
			&& GenericFloat->GetValueInputs().Num() == 1
			&& GenericInt->GetValueInputs()[0].Id
				== GenericFloat->GetValueInputs()[0].Id);
		TestTrue(TEXT("Generic inference changes type metadata, not topology"),
			GenericInt->GetValueInputs()[0].SemanticTypeName == TEXT("int")
			&& GenericFloat->GetValueInputs()[0].SemanticTypeName == TEXT("float"));
	}
	FVerseVisualTile* ComparableFloat = BindNamedFunction(TEXT("CompareFloat"));
	if (TestNotNull(TEXT("Comparable float invocation binds"), ComparableFloat))
	{
		TestEqual(TEXT("Comparable invocation retains its concrete result type"),
			ComparableFloat->SemanticTypeName,
			FString(TEXT("float")));
		TestTrue(TEXT("Connected comparable operands use their resolved expression types"),
			ComparableFloat->GetValueInputs().Num() == 2
			&& ComparableFloat->GetValueInputs()[0].SemanticTypeName == TEXT("float")
			&& ComparableFloat->GetValueInputs()[1].SemanticTypeName == TEXT("float"));
		if (ComparableFloat->GetValueInputs().Num() == 2
			&& ComparableFloat->Children.Num() == 2)
		{
			const TArray<TSharedPtr<FVerseExpressionAction>> FloatProviders =
				FVerseExpressionActionQuery::Build(
					{},
					ComparableFloat->GetValueInputs()[1],
					false,
					*ParsedDocument,
					ComparableFloat->Children[1].Range,
					Document.FilePath,
					CandidateSnapshots);
			TestTrue(TEXT("A float literal operand can be replaced by a float identifier"),
				FloatProviders.ContainsByPredicate(
					[](const TSharedPtr<FVerseExpressionAction>& Action)
					{
						return Action.IsValid()
							&& Action->SourceForm
								== EVerseExpressionSourceForm::IdentifierReference
							&& Action->SourceSpelling == TEXT("Input");
					}));
		}
		const TArray<FVerseOperatorSignature> Signatures =
			FVerseSemanticCandidateProvider::BuildOperatorSignatures(
				CandidateSnapshots,
				Document.FilePath,
				ComparableFloat->Range.BeginByte,
				*ParsedDocument,
				ComparableFloat->OperatorSpelling,
				2,
				{},
				{});
		TestTrue(TEXT("Comparable operator signatures include concrete float operands"),
			Signatures.ContainsByPredicate([](const FVerseOperatorSignature& Signature)
			{
				return Signature.OperandTypeNames.Num() == 2
					&& Signature.OperandTypeNames[0] == TEXT("float")
					&& Signature.OperandTypeNames[1] == TEXT("float");
			}));
		TestTrue(TEXT("Comparable operator signatures include concrete integer operands"),
			Signatures.ContainsByPredicate([](const FVerseOperatorSignature& Signature)
			{
				return Signature.OperandTypeNames.Num() == 2
					&& Signature.OperandTypeNames[0] == TEXT("int")
					&& Signature.OperandTypeNames[1] == TEXT("int");
			}));
		TestTrue(TEXT("Comparable signature labels omit compiler constraint results"),
			Signatures.ContainsByPredicate([](const FVerseOperatorSignature& Signature)
			{
				return Signature.DisplayText == TEXT("float x float");
			})
			&& Signatures.ContainsByPredicate([](const FVerseOperatorSignature& Signature)
			{
				return Signature.DisplayText == TEXT("int x int");
			})
			&& !Signatures.ContainsByPredicate([](const FVerseOperatorSignature& Signature)
			{
				return Signature.DisplayText.Contains(TEXT("comparable"))
					|| Signature.DisplayText.Contains(TEXT("false"))
					|| Signature.DisplayText.Contains(TEXT("type{"));
			}));
		TestTrue(TEXT("Comparable presentation retains concrete results internally"),
			Signatures.ContainsByPredicate([](const FVerseOperatorSignature& Signature)
			{
				return Signature.DisplayText == TEXT("float x float")
					&& Signature.ResultTypeName == TEXT("float");
			})
			&& Signatures.ContainsByPredicate([](const FVerseOperatorSignature& Signature)
			{
				return Signature.DisplayText == TEXT("int x int")
					&& Signature.ResultTypeName == TEXT("int");
			}));
		TestFalse(TEXT("No compiler-internal type enters a resolved signature"),
			Signatures.ContainsByPredicate([](const FVerseOperatorSignature& Signature)
			{
				auto IsInternal = [](const FString& TypeName)
				{
					return TypeName.Contains(TEXT("comparable"))
						|| TypeName.Contains(TEXT("unknown"))
						|| TypeName.Contains(TEXT("type{"));
				};
				return IsInternal(Signature.ResultTypeName)
					|| Signature.OperandTypeNames.ContainsByPredicate(IsInternal);
			}));
		TSet<FString> ResolvedSignatureKeys;
		for (const FVerseOperatorSignature& Signature : Signatures)
		{
			ResolvedSignatureKeys.Add(FString::Printf(
				TEXT("%s -> %s"),
				*FString::Join(Signature.OperandTypeNames, TEXT(" x ")),
				*Signature.ResultTypeName));
		}
		TestEqual(TEXT("Resolved signatures are deduplicated semantically"),
			ResolvedSignatureKeys.Num(), Signatures.Num());
		if (TestTrue(TEXT("Comparable fixture exposes its connected left operand"),
			ComparableFloat->Children.Num() == 2
			&& ComparableFloat->Children[0].GetValueOutputs().Num() == 1))
		{
			const TArray<const FVerseVisualSocket*> ConnectedOperands = {
				&ComparableFloat->Children[0].GetValueOutputs()[0], nullptr};
			const TArray<FVerseOperatorSignature> InputCompatibleSignatures =
				FVerseSemanticCandidateProvider::BuildOperatorSignatures(
					CandidateSnapshots,
					Document.FilePath,
					ComparableFloat->Range.BeginByte,
					*ParsedDocument,
					ComparableFloat->OperatorSpelling,
					2,
					ConnectedOperands,
					{});
			TestTrue(TEXT("One connected operand constrains its signature position"),
				!InputCompatibleSignatures.IsEmpty()
				&& InputCompatibleSignatures.ContainsByPredicate(
					[](const FVerseOperatorSignature& Signature)
					{
						return Signature.OperandTypeNames[0] == TEXT("float");
					})
				&& !InputCompatibleSignatures.ContainsByPredicate(
					[](const FVerseOperatorSignature& Signature)
					{
						return Signature.OperandTypeNames[0] == TEXT("int");
					}));
		}

		FVerseVisualSocket FloatConsumer;
		FloatConsumer.SemanticTypeName = TEXT("float");
		FloatConsumer.SemanticType = ComparableFloat->SemanticType;
		FloatConsumer.SemanticSnapshot = ComparableFloat->SemanticSnapshot;
		const TArray<const FVerseVisualSocket*> Consumers = {&FloatConsumer};
		const TArray<FVerseOperatorSignature> OutputCompatibleSignatures =
			FVerseSemanticCandidateProvider::BuildOperatorSignatures(
				CandidateSnapshots,
				Document.FilePath,
				ComparableFloat->Range.BeginByte,
				*ParsedDocument,
				ComparableFloat->OperatorSpelling,
				2,
				{},
				Consumers);
		TestTrue(TEXT("An output consumer removes incompatible operator results"),
			!OutputCompatibleSignatures.IsEmpty()
			&& OutputCompatibleSignatures.ContainsByPredicate(
				[](const FVerseOperatorSignature& Signature)
				{
					return Signature.ResultTypeName == TEXT("float");
				})
			&& !OutputCompatibleSignatures.ContainsByPredicate(
				[](const FVerseOperatorSignature& Signature)
				{
					return Signature.ResultTypeName == TEXT("int");
				}));
	}
	FVerseVisualTile* DefaultCall = BindNamedFunction(TEXT("CallDefault"));
	if (TestNotNull(TEXT("Default-parameter invocation binds"), DefaultCall))
	{
		TestTrue(TEXT("Omitted fixed parameters retain an immutable socket"),
			DefaultCall->GetValueInputs().Num() == 2
			&& DefaultCall->GetValueInputs()[1].bNamedParameter
			&& DefaultCall->GetValueInputs()[1].bUsesDeclaredDefault);
	}
	FVerseVisualTile* FloatDefinition = BindNamedFunction(TEXT("FloatInitializer"));
	if (TestNotNull(TEXT("Float initializer definition binds"), FloatDefinition)
		&& TestTrue(TEXT("Float initializer has one declared input socket"),
			FloatDefinition->ExpressionKind == EVerseExpressionKind::Definition
			&& FloatDefinition->GetValueInputs().Num() == 1
			&& FloatDefinition->Children.Num() == 1))
	{
		const FVerseVisualSocket& FloatInput = FloatDefinition->GetValueInputs()[0];
		TestTrue(TEXT("Input socket retains the compiler-owned declared float type"),
			FloatInput.SemanticType != nullptr
			&& FloatInput.SemanticTypeName == TEXT("float")
			&& FloatInput.SemanticSnapshot == Workspace.GetLastSuccessfulSnapshot());
		const TArray<TSharedPtr<FVerseExpressionAction>> FloatProviders =
			FVerseExpressionActionQuery::Build(
				{},
				FloatInput,
				false,
				*ParsedDocument,
				FloatDefinition->Children[0].Range,
				Document.FilePath,
				CandidateSnapshots);
		TestTrue(TEXT("Polymorphic Add is offered as an action providing float"),
			FloatProviders.ContainsByPredicate(
				[](const TSharedPtr<FVerseExpressionAction>& Action)
				{
					return Action.IsValid()
						&& Action->SourceForm
							== EVerseExpressionSourceForm::InfixOperator
						&& Action->SourceSpelling == TEXT("+")
						&& Action->ResultTypeName == TEXT("float");
				}));
	}
	FVerseFunctionNavigationItem* FloatCondition = BoundFunctions.FindByPredicate(
		[](const FVerseFunctionNavigationItem& Function)
		{
			return Function.Name == TEXT("FloatCondition");
		});
	if (TestNotNull(TEXT("Failable float-definition fixture binds"), FloatCondition))
	{
		FVerseSemanticCandidateProvider::BindFunctionGraph(
			FloatCondition->GraphTiles,
			Workspace.GetLastSuccessfulSnapshot(),
			Document.FilePath,
			*ParsedDocument);
		const FVerseVisualTile* IfTile = FloatCondition->GraphTiles.FindByPredicate(
			[](const FVerseVisualTile& Tile)
			{
				return Tile.ControlKind == EVerseControlKind::If;
			});
		const FVerseVisualTile* Failable = IfTile != nullptr
			? IfTile->Children.FindByPredicate(
				[](const FVerseVisualTile& Tile)
				{
					return Tile.Kind == EVerseVisualTileKind::FailableBlock;
				})
			: nullptr;
		const FVerseVisualTile* Threshold = Failable != nullptr
			? Failable->Children.FindByPredicate(
				[](const FVerseVisualTile& Tile)
				{
					return Tile.ExpressionKind == EVerseExpressionKind::Definition;
				})
			: nullptr;
		if (TestNotNull(TEXT("Float definition is inside the failable context"), Threshold)
			&& TestTrue(TEXT("Failable definition exposes its float initializer socket"),
				Threshold->GetValueInputs().Num() == 1
				&& Threshold->Children.Num() == 1))
		{
			const TArray<TSharedPtr<FVerseExpressionAction>> Providers =
				FVerseExpressionActionQuery::Build(
					{},
					Threshold->GetValueInputs()[0],
					false,
					*ParsedDocument,
					Threshold->Children[0].Range,
					Document.FilePath,
					CandidateSnapshots);
			TestTrue(TEXT("Failable-context float input offers Add as a provider"),
				Providers.ContainsByPredicate(
					[](const TSharedPtr<FVerseExpressionAction>& Action)
					{
						return Action.IsValid()
							&& Action->SourceSpelling == TEXT("+")
							&& Action->ResultTypeName == TEXT("float");
					}));
		}
	}
	FVerseFunctionNavigationItem* ScopedVisibility = BoundFunctions.FindByPredicate(
		[](const FVerseFunctionNavigationItem& Function)
		{
			return Function.Name == TEXT("ScopedVisibility");
		});
	if (TestNotNull(TEXT("Scoped visibility fixture binds"), ScopedVisibility))
	{
		FVerseSemanticCandidateProvider::BindFunctionGraph(
			ScopedVisibility->GraphTiles,
			Workspace.GetLastSuccessfulSnapshot(),
			Document.FilePath,
			*ParsedDocument);
		const FVerseVisualTile* IfTile = ScopedVisibility->GraphTiles.FindByPredicate(
			[](const FVerseVisualTile& Tile)
			{
				return Tile.ControlKind == EVerseControlKind::If;
			});
		const FVerseVisualTile* Failable = IfTile != nullptr
			? IfTile->Children.FindByPredicate(
				[](const FVerseVisualTile& Tile)
				{
					return Tile.Kind == EVerseVisualTileKind::FailableBlock;
				})
			: nullptr;
		const FVerseVisualTile* Comparison = Failable != nullptr
			? Failable->Children.FindByPredicate(
				[](const FVerseVisualTile& Tile)
				{
					return Tile.OperatorSpelling == TEXT(">");
				})
			: nullptr;
		if (TestNotNull(TEXT("Scoped comparison follows its local definition"), Comparison)
			&& TestTrue(TEXT("Scoped comparison retains its literal operand socket"),
				Comparison->GetValueInputs().Num() == 2
				&& Comparison->Children.Num() == 2))
		{
			const TArray<TSharedPtr<FVerseExpressionAction>> Providers =
				FVerseExpressionActionQuery::Build(
					{},
					Comparison->GetValueInputs()[1],
					false,
					*ParsedDocument,
					Comparison->Children[1].Range,
					Document.FilePath,
					CandidateSnapshots);
			TestTrue(TEXT("A comparison literal can be replaced by its condition-local float"),
				Providers.ContainsByPredicate(
					[](const TSharedPtr<FVerseExpressionAction>& Action)
					{
						return Action.IsValid()
							&& Action->SourceForm
								== EVerseExpressionSourceForm::IdentifierReference
							&& Action->SourceSpelling == TEXT("Inner");
					}));
		}
	}
	TestTrue(
		TEXT("Compiler intrinsics contribute the polymorphic Add overload for int"),
		Candidates.ContainsByPredicate([](const FVerseSemanticCandidate& Candidate)
		{
			return Candidate.Kind == EVerseSemanticCandidateKind::InfixOperator
				&& Candidate.Function != nullptr
				&& Candidate.InstantiatedFunctionType != nullptr
				&& Candidate.BoundInputIndex != INDEX_NONE
				&& Candidate.Snapshot.IsValid();
		}));
	const FVerseSemanticCandidate* QueryCandidate = QueryCandidates.FindByPredicate(
		[](const FVerseSemanticCandidate& Candidate)
		{
			return Candidate.Kind == EVerseSemanticCandidateKind::PostfixOperator
				&& Candidate.Function != nullptr
				&& Candidate.Function->GetName()
					== Candidate.Function->GetProgram()._IntrinsicSymbols._OpNameQuery
				&& Candidate.BoundInputIndex == 0;
		});
	TestNotNull(TEXT("Compiler query intrinsic is exposed as a postfix logic action"),
		QueryCandidate);
	if (QueryCandidate != nullptr)
	{
		const uLang::CIntrinsicSymbols& Symbols =
			QueryCandidate->Function->GetProgram()._IntrinsicSymbols;
		// INTENTIONAL ENGINE-UPGRADE TRIPWIRE: UE 6.0 declares `_OpNameQuery` as
		// `operator'?'`, despite query being postfix syntax. When Epic changes it to
		// native postfix classification this assertion should fail loudly. Remove
		// the two exact `_OpNameQuery` branches in VerseSemanticCandidates, retain
		// the generic IsPostfixOpName path, and update this test for the new contract.
		TestTrue(
			TEXT("Maintenance guard: remove the UE 6.0 query classification shim when this fails"),
			Symbols.IsOperatorOpName(QueryCandidate->Function->GetName())
				&& !Symbols.IsPostfixOpName(QueryCandidate->Function->GetName()));
	}
	TestTrue(TEXT("Logic query action emits postfix question-mark source"),
		QueryActions.ContainsByPredicate([](const TSharedPtr<FVerseExpressionAction>& Action)
		{
			return Action.IsValid()
				&& Action->SourceForm == EVerseExpressionSourceForm::PostfixOperator
				&& Action->SourceSpelling == TEXT("?")
				&& Action->BoundInputIndex == 0;
		}));
	const FVerseSemanticCandidate* AcceptIntCandidate = Candidates.FindByPredicate(
		[](const FVerseSemanticCandidate& Candidate)
		{
			return Candidate.Kind == EVerseSemanticCandidateKind::Function
				&& Candidate.Function != nullptr
				&& Candidate.Function->AsNameStringView()
					== uLang::CUTF8StringView("AcceptInt")
				&& Candidate.InstantiatedFunctionType != nullptr;
		});
	TestNotNull(
		TEXT("A function declared only in the private overlay is discovered dynamically"),
		AcceptIntCandidate);
	if (AcceptIntCandidate != nullptr)
	{
		TestTrue(TEXT("Raw function candidates retain compiler-owned match state"),
			AcceptIntCandidate->BoundInputIndex != INDEX_NONE
			&& AcceptIntCandidate->Snapshot.IsValid());
	}
	const TSharedPtr<FVerseExpressionAction>* AbsoluteInteger = Actions.FindByPredicate(
		[](const TSharedPtr<FVerseExpressionAction>& Action)
		{
			return Action.IsValid()
				&& Action->SourceSpelling == TEXT("Abs")
				&& Action->DisplayName.ToString()
					== TEXT("Absolute (Integer)");
		});
	TestNotNull(
		TEXT("Verse Abs(int) resolves to Blueprint's integer function presentation"),
		AbsoluteInteger);
	if (AbsoluteInteger != nullptr)
	{
		TestEqual(
			TEXT("Absolute Integer uses Blueprint's Math Integer category"),
			(*AbsoluteInteger)->Category.ToString(),
			FString(TEXT("Math|Integer")));
		TestTrue(
			TEXT("Absolute Integer retains its semantic module hierarchy"),
			(*AbsoluteInteger)->ModuleCategory.ToString().Contains(TEXT("Verse")));
	}
	TestFalse(
		TEXT("Output-side filtering does not need an identifier exclusion rule"),
		Candidates.ContainsByPredicate([](const FVerseSemanticCandidate& Candidate)
		{
			return Candidate.Kind == EVerseSemanticCandidateKind::Identifier;
		}));
	TestFalse(
		TEXT("Compiler invocation plumbing is not offered as an expression action"),
		Candidates.ContainsByPredicate([](const FVerseSemanticCandidate& Candidate)
		{
			return Candidate.Function != nullptr
				&& Candidate.Function->GetName()
					== Candidate.Function->GetProgram()._IntrinsicSymbols._OpNameCall;
		}));
	const auto HasCategory = [&Actions](const TCHAR* Spelling, const TCHAR* Category)
	{
		return Actions.ContainsByPredicate(
			[Spelling, Category](const TSharedPtr<FVerseExpressionAction>& Action)
			{
				return Action.IsValid()
					&& Action->SourceSpelling == Spelling
					&& Action->Category.ToString() == Category;
			});
	};
	TestTrue(TEXT("BitAnd uses Blueprint's integer category"),
		HasCategory(TEXT("BitAnd"), TEXT("Math|Integer")));
	TestTrue(TEXT("BitOr uses Blueprint's integer category"),
		HasCategory(TEXT("BitOr"), TEXT("Math|Integer")));
	TestTrue(TEXT("BitXor uses Blueprint's integer category"),
		HasCategory(TEXT("BitXor"), TEXT("Math|Integer")));
	TestTrue(TEXT("BitNot uses Blueprint's integer category"),
		HasCategory(TEXT("BitNot"), TEXT("Math|Integer")));
	TestTrue(TEXT("Mod uses Blueprint's integer category"),
		HasCategory(TEXT("Mod"), TEXT("Math|Integer")));
	TestTrue(TEXT("Quotient uses Blueprint's integer category"),
		HasCategory(TEXT("Quotient"), TEXT("Math|Integer")));
	TestTrue(TEXT("Integer ToString uses Blueprint's string category"),
		HasCategory(TEXT("ToString"), TEXT("Utilities|String")));
	TestTrue(TEXT("Array Find uses Blueprint's array category"),
		HasCategory(TEXT("Find"), TEXT("Utilities|Array")));
	TestTrue(TEXT("Array RemoveAllElements uses Blueprint's array category"),
		HasCategory(TEXT("RemoveAllElements"), TEXT("Utilities|Array")));
	TestTrue(TEXT("Array RemoveElement uses Blueprint's array category"),
		HasCategory(TEXT("RemoveElement"), TEXT("Utilities|Array")));
	TestTrue(TEXT("Array Slice uses Blueprint's array category"),
		HasCategory(TEXT("Slice"), TEXT("Utilities|Array")));
	TestTrue(TEXT("ToDiagnostic is grouped with string conversion"),
		HasCategory(TEXT("ToDiagnostic"), TEXT("Utilities|String")));

	const int32 FloatExpressionBeginByte =
		SourceView.Find(UTF8TEXTVIEW("Input + 1.0"));
	const TArray<TSharedPtr<FVerseExpressionAction>> FloatActions =
		FVerseExpressionActionQuery::Build(
			{}, OutputSocket, true, *ParsedDocument,
			FVerseTextRange(Document.Revision, {FloatExpressionBeginByte, 11}),
			Document.FilePath, CandidateSnapshots);
	const auto HasFloatCategory = [&FloatActions](
		const TCHAR* Spelling, const TCHAR* Category)
	{
		return FloatActions.ContainsByPredicate(
			[Spelling, Category](const TSharedPtr<FVerseExpressionAction>& Action)
			{
				return Action.IsValid()
					&& Action->SourceSpelling == Spelling
					&& Action->Category.ToString() == Category;
			});
	};
	TestTrue(TEXT("Ceil uses Blueprint's float category"),
		HasFloatCategory(TEXT("Ceil"), TEXT("Math|Float")));
	TestTrue(TEXT("Floor uses Blueprint's float category"),
		HasFloatCategory(TEXT("Floor"), TEXT("Math|Float")));
	TestTrue(TEXT("Sin uses Blueprint's trig category"),
		HasFloatCategory(TEXT("Sin"), TEXT("Math|Trig")));
	TestTrue(TEXT("Float ToString uses Blueprint's string category"),
		HasFloatCategory(TEXT("ToString"), TEXT("Utilities|String")));

	FVerseExpressionAction NamedCallAction;
	NamedCallAction.SourceForm = EVerseExpressionSourceForm::OrdinaryCall;
	NamedCallAction.SourceSpelling = TEXT("AcceptNamed");
	NamedCallAction.BoundInputIndex = 0;
	NamedCallAction.InputDefaultSources.Add(FString());
	NamedCallAction.InputNames.Add(TEXT("Value"));
	NamedCallAction.NamedInputs.Add(true);
	FVerseDocumentSession NamedCallSession(ParsedDocument.ToSharedRef());
	const bool bNamedCallApplied = TryApplyVerseExpressionAction(
		NamedCallSession,
		FVerseTextRange(NamedCallSession.GetRevision(), {ExpressionBeginByte, 9}),
		NamedCallAction,
		DocumentError);
	TestTrue(TEXT("Generic call creation preserves named selected-signature arguments"),
		bNamedCallApplied
		&& FString(UTF8_TO_TCHAR(*NamedCallSession.GetCurrentUtf8())).Contains(
			TEXT("AcceptNamed(?Value := Input + 1)")));

	FVerseSemanticDocumentInput InvalidDocument = Document;
	InvalidDocument.Source = FUtf8String(UTF8TEXT(
		"AcceptInt(Value : int)<computes> : int = Value\n"
		"BrokenValue : int = \"not an int\"\n"
		"PrivateSemanticOverlayOnly(Input : int)<computes> : int = Input + 1\n"));
	InvalidDocument.Revision.Value = 92;
	Workspace.RequestAnalysis({InvalidDocument}, 1.0, false);
	Workspace.Tick(1.0);
	if (!TestEqual(
		TEXT("An unrelated local semantic error fails exact analysis"),
		Workspace.GetState(),
		EVerseSemanticWorkspaceState::Failed))
	{
		return false;
	}
	TestTrue(
		TEXT("Compiler diagnostics retain ownership of the invalid document"),
		Workspace.GetDiagnostics().ContainsByPredicate(
			[&InvalidDocument](const FVerseSemanticDiagnostic& Diagnostic)
			{
				return Diagnostic.Severity == ELogVerbosity::Error
					&& !Diagnostic.FilePath.IsEmpty()
					&& Diagnostic.AppliesToFile(InvalidDocument.FilePath);
			}));
	TestFalse(
		TEXT("Failed local analysis cannot authorize exact semantic mutation"),
		Workspace.HasExactSnapshot(InvalidDocument.FilePath, InvalidDocument.Revision));

	const TConstArrayView<uint8> InvalidBytes(
		reinterpret_cast<const uint8*>(*InvalidDocument.Source),
		InvalidDocument.Source.Len());
	const TSharedPtr<const FVerseDocument> InvalidParsedDocument =
		FVerseDocument::CreateFromBytes(InvalidBytes, DocumentError);
	if (!TestTrue(
		TEXT("Semantically invalid candidate source remains syntactically usable"),
		InvalidParsedDocument.IsValid()))
	{
		return false;
	}
	const int32 InvalidExpressionBeginByte =
		InvalidParsedDocument->GetOriginalUtf8View().Find(UTF8TEXTVIEW("Input + 1"));
	const TArray<FVerseSemanticCandidate> DegradedCandidates =
		FVerseSemanticCandidateProvider::Build(
			Workspace.GetCandidateSnapshots(),
			InvalidDocument.FilePath,
			InvalidExpressionBeginByte,
			true,
			*InvalidParsedDocument);
	TestTrue(
		TEXT("Compiler Add remains discoverable after an unrelated local error"),
		DegradedCandidates.ContainsByPredicate([](const FVerseSemanticCandidate& Candidate)
		{
			return Candidate.Kind == EVerseSemanticCandidateKind::InfixOperator
				&& Candidate.Function != nullptr;
		}));
	TestTrue(
		TEXT("Callable signatures remain discoverable after an unrelated local error"),
		DegradedCandidates.ContainsByPredicate([](const FVerseSemanticCandidate& Candidate)
		{
			return Candidate.Kind == EVerseSemanticCandidateKind::Function
				&& Candidate.Function != nullptr
				&& Candidate.Function->AsNameStringView()
					== uLang::CUTF8StringView("AcceptInt");
		}));
	const TArray<TSharedPtr<FVerseExpressionAction>> DegradedActions =
		FVerseExpressionActionQuery::Build(
			{},
			OutputSocket,
			true,
			*InvalidParsedDocument,
			FVerseTextRange(
				InvalidDocument.Revision,
				{InvalidExpressionBeginByte, 5}),
			InvalidDocument.FilePath,
			Workspace.GetCandidateSnapshots());
	const TSharedPtr<FVerseExpressionAction>* CallableAction =
		DegradedActions.FindByPredicate([](
			const TSharedPtr<FVerseExpressionAction>& Action)
		{
			return Action.IsValid()
				&& Action->SourceForm == EVerseExpressionSourceForm::OrdinaryCall
				&& Action->SourceSpelling == TEXT("AcceptInt");
		});
	TestTrue(
		TEXT("Integer unary minus uses Blueprint's Negate Int action name"),
		DegradedActions.ContainsByPredicate([](
			const TSharedPtr<FVerseExpressionAction>& Action)
		{
			return Action.IsValid()
				&& Action->SourceForm == EVerseExpressionSourceForm::PrefixOperator
				&& Action->SourceSpelling == TEXT("-")
				&& Action->DisplayName.ToString() == TEXT("Negate Int");
		}));
	TestNotNull(
		TEXT("Failed local analysis still exposes callable actions to expression search"),
		CallableAction);
	if (CallableAction != nullptr)
	{
		TestEqual(
			TEXT("A callable without display metadata keeps its Verse definition name"),
			(*CallableAction)->DisplayName.ToString(),
			FString(TEXT("AcceptInt")));
		TestEqual(
			TEXT("Callables without category metadata use the editor's fallback category"),
			(*CallableAction)->Category.ToString(),
			FString(TEXT("Uncategorized")));
		TestFalse(
			TEXT("Callable module grouping is kept separate from its category"),
			(*CallableAction)->ModuleCategory.IsEmpty());
		FVerseDocumentSession InvalidSession(InvalidParsedDocument.ToSharedRef());
		const FVerseTextRange SessionExpressionRange(
			InvalidSession.GetRevision(), {InvalidExpressionBeginByte, 5});
		const bool bApplied = TryApplyVerseExpressionAction(
			InvalidSession,
			SessionExpressionRange,
			**CallableAction,
			DocumentError);
		TestTrue(
			TEXT("A generic call action applies despite unrelated semantic errors"),
			bApplied);
		TestTrue(
			TEXT("The generic call preserves the dragged expression as its bound input"),
			FString(UTF8_TO_TCHAR(*InvalidSession.GetCurrentUtf8())).Contains(
				TEXT("AcceptInt(Input)")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVerseSemanticFailureOutcomeBindingTest,
	"VerseVisualEditor.Semantics.Workspace.BindsFailureOutcomes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVerseSemanticFailureOutcomeBindingTest::RunTest(const FString& Parameters)
{
	const TSharedPtr<IPlugin> Plugin =
		IPluginManager::Get().FindPlugin(TEXT("VerseVisualEditor"));
	if (!TestTrue(TEXT("VerseVisualEditor plugin is discoverable"), Plugin.IsValid()))
	{
		return false;
	}
	FString FilePath = FPaths::ConvertRelativePathToFull(FPaths::Combine(
		Plugin->GetBaseDir(), TEXT("Tests/Fixtures/failure_contexts.verse")));
	FPaths::NormalizeFilename(FilePath);
	FString Source;
	if (!TestTrue(
		TEXT("Fixed failure-context fixture loads"),
		FFileHelper::LoadFileToString(Source, *FilePath)))
	{
		return false;
	}

	const FTCHARToUTF8 Utf8Source(*Source);
	FVerseSemanticDocumentInput Input;
	Input.FilePath = FilePath;
	Input.Source = FUtf8String(FUtf8StringView(
		reinterpret_cast<const UTF8CHAR*>(Utf8Source.Get()),
		Utf8Source.Length()));
	Input.Revision.Value = 1161;

	FVerseSemanticWorkspace Workspace(
		EVerseSemanticDependencyPolicy::PublicApiOnly,
		0.0);
	Workspace.RequestAnalysis({Input}, 0.0, false);
	Workspace.Tick(0.0);
	for (const FVerseSemanticDiagnostic& Diagnostic : Workspace.GetDiagnostics())
	{
		if (Diagnostic.Severity == ELogVerbosity::Error
			&& Diagnostic.AppliesToFile(FilePath))
		{
			AddError(FString::Printf(
				TEXT("%s L%d:%d %s"),
				*Diagnostic.FilePath,
				Diagnostic.RowSpan.X,
				Diagnostic.ColumnSpan.X,
				*Diagnostic.Message.ToString()));
		}
	}
	const TArray<TSharedPtr<const FVerseSemanticSnapshot>> CandidateSnapshots =
		Workspace.GetCandidateSnapshots();
	const TSharedPtr<const FVerseSemanticSnapshot>* ExactSnapshot =
		CandidateSnapshots.FindByPredicate(
			[&FilePath, &Input](const TSharedPtr<const FVerseSemanticSnapshot>& Candidate)
			{
				return Candidate.IsValid()
					&& Candidate->Describes(FilePath, Input.Revision);
			});
	if (!TestNotNull(
		TEXT("Compiler publishes an exact isolated fixture snapshot"),
		ExactSnapshot))
	{
		return false;
	}

	FText Error;
	const TConstArrayView<uint8> Bytes(
		reinterpret_cast<const uint8*>(*Input.Source), Input.Source.Len());
	const TSharedPtr<const FVerseDocument> Document =
		FVerseDocument::CreateFromBytes(Bytes, Error);
	if (!TestTrue(TEXT("Failure-context fixture parses"), Document.IsValid()))
	{
		AddError(Error.ToString());
		return false;
	}
	const FVerseParseSnapshot ParseSnapshot =
		FVerseParseSnapshotBuilder::Build(Document.ToSharedRef());
	const TArray<FVerseVisualTile> SyntaxTiles =
		FVerseVisualTileBuilder::Build(ParseSnapshot, Input.Revision);
	TArray<FVerseFunctionNavigationItem> Functions =
		FVerseFunctionNavigationBuilder::Build(SyntaxTiles, ParseSnapshot);

	auto BindStatement = [&](
		const TCHAR* FunctionName) -> const FVerseVisualTile*
	{
		FVerseFunctionNavigationItem* Function = Functions.FindByPredicate(
			[FunctionName](const FVerseFunctionNavigationItem& Candidate)
			{
				return Candidate.Name == FunctionName;
			});
		if (!TestNotNull(FunctionName, Function)
			|| !TestTrue(
				*FString::Printf(TEXT("%s has a statement tile"), FunctionName),
				Function->GraphTiles.Num() >= 2))
		{
			return nullptr;
		}
		FVerseSemanticCandidateProvider::BindFunctionGraph(
			Function->GraphTiles,
			*ExactSnapshot,
			FilePath,
			*Document);
		return &Function->GraphTiles[1];
	};

	const FVerseVisualTile* NonFailableCall = BindStatement(TEXT("NonFailableCall"));
	if (NonFailableCall != nullptr)
	{
		TestEqual(
			TEXT("Ordinary call is compiler-classified as non-failable"),
			NonFailableCall->Outcome,
			EVerseExpressionOutcome::Ordinary);
		TestTrue(
			TEXT("Ordinary call retains its typed round output"),
			NonFailableCall->GetValueOutputs().Num() == 1
			&& NonFailableCall->GetValueOutputs()[0].Outcome
				== EVerseExpressionOutcome::Ordinary
			&& NonFailableCall->GetValueOutputs()[0].SemanticTypeName == TEXT("int"));
	}

	const FVerseVisualTile* FailableCall = BindStatement(TEXT("FailableCall"));
	if (FailableCall != nullptr)
	{
		TestTrue(
			TEXT("Decides call carries an int through a failable socket"),
			FailableCall->Outcome == EVerseExpressionOutcome::FailableValue
			&& FailableCall->GetValueOutputs().Num() == 1
			&& FailableCall->GetValueOutputs()[0].Outcome
				== EVerseExpressionOutcome::FailableValue
			&& FailableCall->GetValueOutputs()[0].SemanticTypeName == TEXT("int"));
	}

	const FVerseVisualTile* FailableVoidCall =
		BindStatement(TEXT("FailableVoidCall"));
	if (FailableVoidCall != nullptr)
	{
		TestEqual(TEXT("Decides void call is classified as failure-only"),
			FailableVoidCall->Outcome, EVerseExpressionOutcome::FailureOnly);
		TestEqual(TEXT("Failure-only call exposes exactly one failure socket"),
			FailableVoidCall->GetValueOutputs().Num(), 1);
		if (FailableVoidCall->GetValueOutputs().Num() == 1)
		{
			TestEqual(TEXT("Failure-only socket retains its outcome"),
				FailableVoidCall->GetValueOutputs()[0].Outcome,
				EVerseExpressionOutcome::FailureOnly);
			TestTrue(TEXT("Failure-only socket has no value type"),
				FailableVoidCall->GetValueOutputs()[0].SemanticTypeName.IsEmpty());
		}
	}

	const FVerseVisualTile* NonFailableOperator =
		BindStatement(TEXT("NonFailableOperator"));
	if (NonFailableOperator != nullptr)
	{
		TestEqual(
			TEXT("Arithmetic operator is not guessed to be failable"),
			NonFailableOperator->Outcome,
			EVerseExpressionOutcome::Ordinary);
	}

	const FVerseVisualTile* FailableOperator =
		BindStatement(TEXT("FailableOperator"));
	if (FailableOperator != nullptr)
	{
		TestTrue(
			TEXT("Comparison carries its compiler-resolved value through failure"),
			FailableOperator->Outcome == EVerseExpressionOutcome::FailableValue
			&& FailableOperator->GetValueOutputs().Num() == 1
			&& FailableOperator->GetValueOutputs()[0].Outcome
				== EVerseExpressionOutcome::FailableValue
			&& FailableOperator->GetValueOutputs()[0].SemanticTypeName == TEXT("int"));
	}

	const FVerseVisualTile* FailableCast = BindStatement(TEXT("FailableCast"));
	if (FailableCast != nullptr)
	{
		TestTrue(
			TEXT("Failable cast carries the compiler-resolved int type"),
			FailableCast->Outcome == EVerseExpressionOutcome::FailableValue
			&& FailableCast->GetValueOutputs().Num() == 1
			&& FailableCast->GetValueOutputs()[0].SemanticTypeName == TEXT("int"));
	}

	const FVerseVisualTile* PredicateBinding =
		BindStatement(TEXT("PredicateBinding"));
	if (PredicateBinding != nullptr)
	{
		const FVerseVisualExpressionDescriptor::FControlRegion* ConditionRegion =
			PredicateBinding->ControlRegions.FindByPredicate(
				[](const FVerseVisualExpressionDescriptor::FControlRegion& Region)
				{
					return Region.Kind == EVerseControlRegionKind::Condition;
				});
		const FVerseVisualTile* Predicate = ConditionRegion != nullptr
			&& PredicateBinding->Children.IsValidIndex(
				ConditionRegion->FirstOperandIndex)
			? &PredicateBinding->Children[ConditionRegion->FirstOperandIndex]
			: nullptr;
		if (TestNotNull(TEXT("If exposes its external failure context"), Predicate)
			&& TestTrue(TEXT("Predicate bindings are compiler-backed"),
				Predicate->GetValueOutputs().Num() == 2
				&& Predicate->GetValueOutputs()[0].SemanticDataDefinition != nullptr))
		{
			const FVerseVisualSocket& Boundary = Predicate->GetValueOutputs()[0];
			const FVerseVisualSocket& MutableBoundary = Predicate->GetValueOutputs()[1];
			TestTrue(TEXT("Predicate boundary carries the exact typed binding"),
				Boundary.SemanticName == TEXT("Value")
				&& Boundary.SemanticTypeName == TEXT("int")
				&& Boundary.SemanticSnapshot == *ExactSnapshot);
			TestTrue(TEXT("Variable predicate boundary uses its user-facing value type"),
				MutableBoundary.SemanticName == TEXT("MutableValue")
				&& MutableBoundary.SemanticTypeName == TEXT("int")
				&& MutableBoundary.SemanticDataDefinition != nullptr);
			TestTrue(TEXT("Predicate binding is limited to the successful body scope"),
				Boundary.LegalConsumerScopes.Num() == 1
				&& MutableBoundary.LegalConsumerScopes.Num() == 1);
			TestTrue(TEXT("Defining tile shares the boundary's compiler identity"),
				Predicate->Children.Num() == 2
				&& Predicate->Children[0].GetValueOutputs().Num() == 1
				&& Predicate->Children[0].GetValueOutputs()[0].SemanticDataDefinition
					== Boundary.SemanticDataDefinition
				&& Predicate->Children[1].DefinitionKind == VerseSyntaxKind::Variable
				&& Predicate->Children[1].GetValueOutputs().Num() == 1
				&& Predicate->Children[1].GetValueOutputs()[0].SemanticDataDefinition
					== MutableBoundary.SemanticDataDefinition);

			const FUtf8StringView SourceView = Document->GetOriginalUtf8View();
			const int32 ThenValueByte = SourceView.Find(UTF8TEXTVIEW("        Value"));
			const int32 ElseValueByte = SourceView.Find(UTF8TEXTVIEW("        0"));
			const auto HasBoundaryBinding = [](
				const FVerseVisualSocket& Expected,
				TConstArrayView<FVerseSemanticCandidate> Candidates)
			{
				return Candidates.ContainsByPredicate(
					[&Expected](const FVerseSemanticCandidate& Candidate)
					{
						return Candidate.DataDefinition
							== Expected.SemanticDataDefinition;
					});
			};
			if (TestTrue(TEXT("Fixture locates both if branches"),
				ThenValueByte != INDEX_NONE && ElseValueByte != INDEX_NONE))
			{
				const TArray<FVerseSemanticCandidate> ThenCandidates =
					FVerseSemanticCandidateProvider::Build(
						CandidateSnapshots,
						FilePath,
						ThenValueByte,
						false,
						*Document);
				const TArray<FVerseSemanticCandidate> ElseCandidates =
					FVerseSemanticCandidateProvider::Build(
						CandidateSnapshots,
						FilePath,
						ElseValueByte,
						false,
						*Document);
				TestTrue(TEXT("Constant binding is available in the successful body"),
					HasBoundaryBinding(Boundary, ThenCandidates));
				TestTrue(TEXT("Variable binding is available in the successful body"),
					HasBoundaryBinding(MutableBoundary, ThenCandidates));
				TestFalse(TEXT("Constant binding is unavailable in the else body"),
					HasBoundaryBinding(Boundary, ElseCandidates));
				TestFalse(TEXT("Variable binding is unavailable in the else body"),
					HasBoundaryBinding(MutableBoundary, ElseCandidates));
			}
		}
	}
	return true;
}

#endif
