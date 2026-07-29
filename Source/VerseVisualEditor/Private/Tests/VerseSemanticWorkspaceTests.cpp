#if WITH_DEV_AUTOMATION_TESTS

#include "VerseSemanticWorkspace.h"

#include "Misc/FileHelper.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"

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
		"PrivateSemanticOverlayOnly(Input : int)<computes> : int = Input + 1\n"));
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
	return true;
}

#endif
