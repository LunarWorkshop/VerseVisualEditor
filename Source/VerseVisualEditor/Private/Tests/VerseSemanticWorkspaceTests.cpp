#if WITH_DEV_AUTOMATION_TESTS

#include "VerseSemanticWorkspace.h"
#include "VerseDocument.h"
#include "VerseDocumentSession.h"
#include "VerseExpressionActions.h"
#include "VerseFunctionNavigation.h"
#include "VerseParseSnapshotBuilder.h"
#include "VerseSemanticCandidates.h"
#include "VerseVisualTile.h"

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
		"AcceptInt(Value : int)<computes> : int = Value\n"
		"PrivateSemanticOverlayOnly(Input : int)<computes> : int = Input + 1\n"
		"PrivateFloatOverlay(Input : float)<computes> : float = Input + 1.0\n"
		"CallAcceptInt(Input : int)<computes> : int = AcceptInt(Input)\n"));
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
	const TArray<FVerseSemanticCandidate> Candidates =
		FVerseSemanticCandidateProvider::Build(
			CandidateSnapshots,
			Document.FilePath,
			ExpressionBeginByte,
			true,
			*ParsedDocument);
	FVerseVisualSocket OutputSocket;
	const TArray<TSharedPtr<FVerseExpressionAction>> Actions =
		FVerseExpressionActionQuery::Build(
			{}, OutputSocket, true, *ParsedDocument,
			FVerseTextRange(Document.Revision, {ExpressionBeginByte, 9}),
			Document.FilePath, CandidateSnapshots);
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
			BoundAdd.ValueInputs.Num() == 2
			&& BoundAdd.ValueInputs[0].SemanticTypeName == TEXT("int")
			&& BoundAdd.ValueInputs[1].SemanticTypeName == TEXT("int"));
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
			&& BoundCall.ValueInputs.Num() == 1
			&& BoundCall.ValueInputs[0].SemanticName == TEXT("Value")
			&& BoundCall.ValueInputs[0].SemanticTypeName == TEXT("int"));
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

#endif
