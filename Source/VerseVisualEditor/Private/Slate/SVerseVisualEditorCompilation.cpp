#include "Slate/SVerseVisualEditor.h"

#include "Slate/SVerseLiteralEditor.h"

#include "Slate/SVerseFunctionCanvas.h"
#include "Slate/SVerseFunctionGraphLayout.h"
#include "Slate/SVerseFileCanvas.h"
#include "Slate/SVerseTile.h"

#include "Algo/AllOf.h"
#include "Async/Async.h"
#include "DirectoryWatcherModule.h"
#include "DesktopPlatformModule.h"
#include "EdGraph/EdGraphSchema.h"
#include "Editor/UnrealEdEngine.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "FileHelpers.h"
#include "GraphEditorSettings.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "IDirectoryWatcher.h"
#include "IDesktopPlatform.h"
#include "ISourceControlModule.h"
#include "ISourceControlProvider.h"
#include "ISourceControlState.h"
#include "ISolarisEditorModule.h"
#include "SolarisLoadCompilerModule.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/MessageDialog.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "SGraphActionMenu.h"
#include "SGraphPalette.h"
#include "UnrealEdGlobals.h"
#include "VerseDocument.h"
#include "VerseParseSnapshotBuilder.h"
#include "Editing/VerseClauseEditing.h"
#include "Editing/VerseIntrinsicPresentation.h"
#include "Document/VerseDocumentSession.h"
#include "Slate/VerseDefinitionIcon.h"
#include "Editing/VerseExpressionActions.h"
#include "Document/VerseExternalChange.h"
#include "VisualModel/VerseFunctionNavigation.h"
#include "Editing/VerseIdentifier.h"
#include "Editing/VerseProvisionalState.h"
#include "Semantics/VerseSemanticCandidates.h"
#include "Semantics/VerseSemanticWorkspace.h"
#include "Editing/VerseSpecifier.h"
#include "VisualModel/VerseTileProperties.h"
#include "VisualModel/VerseVisualTile.h"
#include "VerseVisualEditorSettings.h"
#include "Slate/VerseVisualEditorStyle.h"
#include "Infrastructure/VerseVisualEditorLifetimeDiagnostics.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Input/SSearchableComboBox.h"
#include "Widgets/Input/SSegmentedControl.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Views/STableRow.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Text/SMultiLineEditableText.h"
#include "Widgets/Text/STextBlock.h"

#include "Slate/SVerseVisualEditorInternal.h"

#define LOCTEXT_NAMESPACE "SVerseVisualEditor"

using namespace VerseVisualEditorPrivate;

namespace
{
struct FVerseCompilationLifetimeToken
{
	explicit FVerseCompilationLifetimeToken(const FString& SourcePath)
	{
		VerseVisualEditorLifetimeDiagnostics::Track(
			this,
			TEXT("CompileTask"),
			*SourcePath);
	}

	~FVerseCompilationLifetimeToken()
	{
		VerseVisualEditorLifetimeDiagnostics::Untrack(
			this,
			TEXT("CompileTask"));
	}
};

	bool DiagnosticMatchesFile(const FSolDiagnostic& Diagnostic, const FString& FilePath)
	{
		FString DiagnosticPath = Diagnostic.Location.FilePath;
		FString DocumentPath = FilePath;
		FPaths::NormalizeFilename(DiagnosticPath);
		FPaths::NormalizeFilename(DocumentPath);
		if (DiagnosticPath.Equals(DocumentPath, ESearchCase::IgnoreCase))
		{
			return true;
		}

		DiagnosticPath.RemoveFromStart(TEXT("./"));
		return !DiagnosticPath.IsEmpty()
			&& DocumentPath.EndsWith(
				TEXT("/") + DiagnosticPath,
				ESearchCase::IgnoreCase);
	}
}

void SVerseVisualEditor::Tick(
	const FGeometry& AllottedGeometry,
	const double InCurrentTime,
	const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	if (SemanticWorkspace)
	{
		SemanticWorkspace->Tick(FPlatformTime::Seconds());
		PublishCompletedSemanticCompilations();
	}
	const EVerseCompilationMode PreferredMode =
		GetDefault<UVerseVisualEditorSettings>()->CompilationMode;
	if (PreferredMode != CompilationMode)
	{
		SetCompilationMode(PreferredMode);
	}
	for (const TSharedPtr<FOpenVerseDocument>& OpenDocument : OpenDocuments)
	{
		if (OpenDocument.IsValid()
			&& OpenDocument->bCompilationPending
			&& InCurrentTime >= OpenDocument->CompileAfterSeconds)
		{
			StartCompilation(OpenDocument);
		}
	}
}

TArray<FVerseSemanticDocumentInput> SVerseVisualEditor::CollectSemanticDocumentInputs(
	bool bOnlyCleanDocuments) const
{
	TArray<FVerseSemanticDocumentInput> Documents;
	Documents.Reserve(OpenDocuments.Num());
	for (const TSharedPtr<FOpenVerseDocument>& OpenDocument : OpenDocuments)
	{
		if (!OpenDocument.IsValid()
			|| !OpenDocument->Session.IsValid()
			|| (bOnlyCleanDocuments && OpenDocument->Session->IsDirty()))
		{
			continue;
		}

		FVerseSemanticDocumentInput& Input = Documents.AddDefaulted_GetRef();
		Input.FilePath = OpenDocument->FilePath;
		Input.Source = OpenDocument->Session->GetCurrentUtf8();
		Input.Revision = OpenDocument->Session->GetRevision();
	}
	return Documents;
}

void SVerseVisualEditor::QueueSemanticAnalysis(bool bDebounce)
{
	if (SemanticWorkspace)
	{
		SemanticWorkspace->RequestAnalysis(
			CollectSemanticDocumentInputs(),
			FPlatformTime::Seconds(),
			bDebounce);
	}
}

void SVerseVisualEditor::RequestSemanticCompilation(
	const TSharedPtr<FOpenVerseDocument>& OpenDocument)
{
	if (!SemanticWorkspace || !OpenDocument.IsValid() || !OpenDocument->Session.IsValid())
	{
		return;
	}

	MarkSemanticCompilationPending(OpenDocument);
	QueueSemanticAnalysis(false);
}

void SVerseVisualEditor::MarkSemanticCompilationPending(
	const TSharedPtr<FOpenVerseDocument>& OpenDocument)
{
	if (!OpenDocument.IsValid() || !OpenDocument->Session.IsValid())
	{
		return;
	}

	OpenDocument->SemanticCompilationRevision = OpenDocument->Session->GetRevision();
	OpenDocument->SemanticCompilationDiagnostics.Reset();
	OpenDocument->bSemanticCompilationPending = true;
	OpenDocument->bHasSemanticCompilationResult = false;
}

void SVerseVisualEditor::PublishCompletedSemanticCompilations()
{
	if (!SemanticWorkspace
		|| (SemanticWorkspace->GetState() != EVerseSemanticWorkspaceState::Ready
			&& SemanticWorkspace->GetState() != EVerseSemanticWorkspaceState::Failed))
	{
		return;
	}

	bool bRefreshActiveGraph = false;
	for (const TSharedPtr<FOpenVerseDocument>& OpenDocument : OpenDocuments)
	{
		if (!OpenDocument.IsValid()
			|| !OpenDocument->Session.IsValid()
			|| !OpenDocument->bSemanticCompilationPending
			|| OpenDocument->Session->GetRevision() != OpenDocument->SemanticCompilationRevision
			|| !SemanticWorkspace->LatestAnalysisDescribes(
				OpenDocument->FilePath,
				OpenDocument->SemanticCompilationRevision))
		{
			continue;
		}

		OpenDocument->SemanticCompilationDiagnostics.Reset();
		for (const FVerseSemanticDiagnostic& Diagnostic : SemanticWorkspace->GetDiagnostics())
		{
			if (Diagnostic.AppliesToFile(OpenDocument->FilePath))
			{
				OpenDocument->SemanticCompilationDiagnostics.Add(Diagnostic);
			}
		}
		OpenDocument->bSemanticCompilationPending = false;
		OpenDocument->bHasSemanticCompilationResult = true;
		const bool bActiveFunctionGraphNeedsSemanticRefresh =
			OpenDocument == ActiveDocument
			&& OpenDocument->FunctionTabs.IsValidIndex(
				OpenDocument->ActiveFunctionTabIndex)
			&& (!OpenDocument->FunctionTabs[
					OpenDocument->ActiveFunctionTabIndex].bGraphUsesExactSemanticSnapshot
				|| OpenDocument->FunctionTabs[
					OpenDocument->ActiveFunctionTabIndex].GraphRevision
					!= OpenDocument->SemanticCompilationRevision);
		const TSharedPtr<const FVerseSemanticSnapshot> ExactSnapshot =
			FindExactSemanticSnapshot(SemanticWorkspace.Get(), *OpenDocument);
		ReconcileFunctionTabs(*OpenDocument, ExactSnapshot);
		bRefreshActiveGraph |= bActiveFunctionGraphNeedsSemanticRefresh
			&& ExactSnapshot.IsValid();

		const bool bHasErrors = OpenDocument->SemanticCompilationDiagnostics.ContainsByPredicate(
			[](const FVerseSemanticDiagnostic& Diagnostic)
			{
				return Diagnostic.Severity == ELogVerbosity::Error
					|| Diagnostic.Severity == ELogVerbosity::Fatal;
			});
		if (OpenDocument == ActiveDocument && bHasErrors)
		{
			bLocalCompilePanelOpen = true;
		}
	}
	if (bRefreshActiveGraph)
	{
		// The source and graph identity did not change. Rebind compiler-derived
		// metadata in the existing canvas without replaying entrance motion or
		// replacing the surrounding document UI.
		RefreshActiveDocument(false, false);
	}
}

bool SVerseVisualEditor::HasLocalCompileDiagnosticsForActiveDocument() const
{
	return ActiveDocument.IsValid()
		&& (!ActiveDocument->LoadError.IsEmpty()
			|| (ActiveDocument->bHasSemanticCompilationResult
				&& !ActiveDocument->SemanticCompilationDiagnostics.IsEmpty()));
}

FText SVerseVisualEditor::GetLocalCompileDiagnosticsText() const
{
	if (!ActiveDocument.IsValid())
	{
		return LOCTEXT("NoLocalCompileErrors", "No local compile errors.");
	}

	TArray<FString> Lines;
	if (!ActiveDocument->LoadError.IsEmpty())
	{
		Lines.Add(FString::Printf(TEXT("Error: %s"), *ActiveDocument->LoadError.ToString()));
	}
	if (ActiveDocument->bHasSemanticCompilationResult)
	{
		for (const FVerseSemanticDiagnostic& Diagnostic : ActiveDocument->SemanticCompilationDiagnostics)
		{
			const TCHAR* Severity = Diagnostic.Severity == ELogVerbosity::Warning
				? TEXT("Warning")
				: Diagnostic.Severity == ELogVerbosity::Error || Diagnostic.Severity == ELogVerbosity::Fatal
					? TEXT("Error")
					: TEXT("Info");
			const FString Location = Diagnostic.RowSpan.X > 0
				? FString::Printf(TEXT(" (L%d)"), Diagnostic.RowSpan.X)
				: FString();
			Lines.Add(FString::Printf(
				TEXT("%s%s: %s"),
				Severity,
				*Location,
				*Diagnostic.Message.ToString()));
		}
	}
	if (Lines.IsEmpty())
	{
		return LOCTEXT("NoLocalCompileErrors", "No local compile errors.");
	}
	return FText::FromString(FString::Join(Lines, TEXT("\n")));
}

FReply SVerseVisualEditor::CloseLocalCompilePanel()
{
	bLocalCompilePanelOpen = false;
	return FReply::Handled();
}

TSharedRef<SWidget> SVerseVisualEditor::BuildToolbar()
{
	FSlimHorizontalToolBarBuilder ToolbarBuilder(nullptr, FMultiBoxCustomization::None);
	ToolbarBuilder.SetStyle(&FAppStyle::Get(), "AssetEditorToolbar");
	ToolbarBuilder.AddToolBarButton(
		FUIAction(
			FExecuteAction::CreateSP(this, &SVerseVisualEditor::SaveActiveDocumentFromMenu),
			FCanExecuteAction::CreateSP(this, &SVerseVisualEditor::CanSaveActiveDocument)),
		NAME_None,
		FText::GetEmpty(),
		LOCTEXT("SaveActiveDocumentTooltip", "Save Active Verse File (Ctrl+S)"),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Save"));

	ToolbarBuilder.AddSeparator();
	ToolbarBuilder.BeginStyleOverride("CalloutToolbar");
	ToolbarBuilder.AddToolBarButton(
		FUIAction(
			FExecuteAction::CreateSP(this, &SVerseVisualEditor::CompileVerseProject),
			FCanExecuteAction::CreateSP(this, &SVerseVisualEditor::CanCompileVerseProject)),
		NAME_None,
		LOCTEXT("CompileActiveDocument", "Compile Verse"),
		TAttribute<FText>::Create(
			TAttribute<FText>::FGetter::CreateSP(this, &SVerseVisualEditor::GetCompileVerseTooltip)),
		TAttribute<FSlateIcon>::Create(
			TAttribute<FSlateIcon>::FGetter::CreateSP(this, &SVerseVisualEditor::GetCompileVerseIcon)));
	ToolbarBuilder.EndStyleOverride();

	ToolbarBuilder.AddWidget(
		SNew(SComboButton)
		.ButtonStyle(FAppStyle::Get(), "SimpleButton")
		.OnGetMenuContent(this, &SVerseVisualEditor::BuildCompilationModeMenu)
		.ButtonContent()
		[
			SNew(STextBlock)
			.Text(this, &SVerseVisualEditor::GetCompilationModeText)
		],
		NAME_None,
		false,
		HAlign_Left);
	return ToolbarBuilder.MakeWidget();
}

void SVerseVisualEditor::CompileVerseProject()
{
	if (ISolarisEditorModule::IsModuleLoaded())
	{
		ISolarisEditorModule::Get().BuildScripts(
			ISolarisEditorModule::EBuildScriptsInstigator::User);
	}
}

bool SVerseVisualEditor::CanCompileVerseProject() const
{
	return ISolarisEditorModule::IsModuleLoaded();
}

FSlateIcon SVerseVisualEditor::GetCompileVerseIcon() const
{
	const TCHAR* IconName = TEXT("SolarisEditor.BuildScripts");
	if (ProjectBuildState == EVerseProjectBuildState::Building
		|| (ActiveDocument.IsValid() && ActiveDocument->bSemanticCompilationPending))
	{
		return FSlateIcon("SolarisEditorStyle", TEXT("SolarisEditor.BuildScriptsLoading"));
	}
	if (ActiveDocument.IsValid()
		&& ActiveDocument->bHasSemanticCompilationResult
		&& ActiveDocument->SemanticCompilationDiagnostics.ContainsByPredicate(
			[](const FVerseSemanticDiagnostic& Diagnostic)
			{
				return Diagnostic.Severity == ELogVerbosity::Error
					|| Diagnostic.Severity == ELogVerbosity::Fatal;
			}))
	{
		return FSlateIcon("SolarisEditorStyle", TEXT("SolarisEditor.BuildScriptsError"));
	}
	switch (ProjectBuildState)
	{
	case EVerseProjectBuildState::Success:
		IconName = TEXT("SolarisEditor.BuildScriptsSuccess");
		break;
	case EVerseProjectBuildState::Warnings:
		IconName = TEXT("SolarisEditor.BuildScriptsWarning");
		break;
	case EVerseProjectBuildState::Errors:
		IconName = TEXT("SolarisEditor.BuildScriptsError");
		break;
	case EVerseProjectBuildState::Unbuilt:
	default:
		break;
	}
	return FSlateIcon("SolarisEditorStyle", IconName);
}

FText SVerseVisualEditor::GetCompileVerseTooltip() const
{
	if (ProjectBuildState == EVerseProjectBuildState::Building
		|| (ActiveDocument.IsValid() && ActiveDocument->bSemanticCompilationPending))
	{
		return LOCTEXT("VerseBuildInProgress", "Build in progress...");
	}
	if (ActiveDocument.IsValid() && ActiveDocument->bHasSemanticCompilationResult)
	{
		int32 LocalErrorCount = 0;
		for (const FVerseSemanticDiagnostic& Diagnostic : ActiveDocument->SemanticCompilationDiagnostics)
		{
			if (Diagnostic.Severity == ELogVerbosity::Error
				|| Diagnostic.Severity == ELogVerbosity::Fatal)
			{
				++LocalErrorCount;
			}
		}
		if (LocalErrorCount > 0)
		{
			return FText::Format(
				LOCTEXT("PrivateVerseBuildErrors", "Built with {0} local {0}|plural(one=error,other=errors)."),
				LocalErrorCount);
		}
	}
	switch (ProjectBuildState)
	{
	case EVerseProjectBuildState::Success:
		return LOCTEXT("VerseBuildSucceeded", "Built successfully.");
	case EVerseProjectBuildState::Warnings:
		return FText::Format(
			LOCTEXT("VerseBuildWarnings", "Built with {0} {0}|plural(one=warning,other=warnings)."),
			ProjectBuildWarningCount);
	case EVerseProjectBuildState::Errors:
		return FText::Format(
			LOCTEXT("VerseBuildErrors", "Built with {0} {0}|plural(one=error,other=errors)."),
			ProjectBuildErrorCount);
	case EVerseProjectBuildState::Unbuilt:
	default:
		return LOCTEXT("CompileVerseProjectTooltip", "Compile all Verse code in project");
	}
}

void SVerseVisualEditor::HandleProjectBuildStarted(
	const TSharedRef<FSolBuildResults>& BuildResults)
{
	ProjectBuildState = EVerseProjectBuildState::Building;
	ProjectBuildWarningCount = 0;
	ProjectBuildErrorCount = 0;
	for (const TSharedPtr<FOpenVerseDocument>& OpenDocument : OpenDocuments)
	{
		if (OpenDocument.IsValid() && OpenDocument->Session.IsValid())
		{
			// Analyze private and unsaved buffers once against the post-build
			// Solaris baseline instead of rebuilding the private environment both
			// before and after the project compile.
			MarkSemanticCompilationPending(OpenDocument);
			if (OpenDocument->Session->IsDirty())
			{
				StartCompilation(OpenDocument, false);
			}
		}
	}
}

void SVerseVisualEditor::HandleProjectBuildComplete(
	const TSharedRef<FSolBuildResults>& BuildResults)
{
	ProjectBuildWarningCount = 0;
	ProjectBuildErrorCount = 0;
	auto CountDiagnostics = [this](TConstArrayView<FSolDiagnostic> Diagnostics)
	{
		for (const FSolDiagnostic& Diagnostic : Diagnostics)
		{
			ProjectBuildWarningCount += Diagnostic.Info.Severity == ELogVerbosity::Warning ? 1 : 0;
			ProjectBuildErrorCount += Diagnostic.IsBuildFailure() ? 1 : 0;
		}
	};
	CountDiagnostics(BuildResults->BuildDiagnostics);
	CountDiagnostics(BuildResults->BuildAssetsDigestDiagnostics);
	ProjectBuildState = ProjectBuildErrorCount > 0
		? EVerseProjectBuildState::Errors
		: ProjectBuildWarningCount > 0
			? EVerseProjectBuildState::Warnings
			: EVerseProjectBuildState::Success;
	if (SemanticWorkspace)
	{
		// A failed user-package build can still leave Solaris with a useful
		// program containing compiled dependencies, native APIs, and intrinsics.
		// Only a successful build may describe exact on-disk document revisions.
		SemanticWorkspace->RefreshCompiledBaseline(
			ProjectBuildErrorCount == 0
				? CollectSemanticDocumentInputs(true)
				: TArray<FVerseSemanticDocumentInput>());
		QueueSemanticAnalysis(false);
	}

	for (const TSharedPtr<FOpenVerseDocument>& OpenDocument : OpenDocuments)
	{
		ApplyProjectDiagnostics(OpenDocument, BuildResults->BuildDiagnostics);
	}
}

void SVerseVisualEditor::ApplyProjectDiagnostics(
	const TSharedPtr<FOpenVerseDocument>& OpenDocument,
	TConstArrayView<FSolDiagnostic> ProjectDiagnostics)
{
	if (!OpenDocument.IsValid()
		|| !OpenDocument->Session.IsValid()
		|| OpenDocument->Session->IsDirty())
	{
		return;
	}

	TArray<FSolDiagnostic> FileDiagnostics;
	for (const FSolDiagnostic& Diagnostic : ProjectDiagnostics)
	{
		if (DiagnosticMatchesFile(Diagnostic, OpenDocument->FilePath))
		{
			FileDiagnostics.Add(Diagnostic);
		}
	}

	FVerseCompilationResult ProjectResult = VerseCompilation::FromProjectBuildDiagnostics(
		FUtf8StringView(OpenDocument->Session->GetCurrentUtf8()),
		OpenDocument->Session->GetRevision(),
		FileDiagnostics);
	FVerseCompilationResult AcceptedResult;
	if (!VerseCompilation::TryAcceptResult(
		MoveTemp(ProjectResult),
		OpenDocument->Session->GetRevision(),
		OpenDocument->Session->GetTiles(),
		AcceptedResult))
	{
		return;
	}

	OpenDocument->CompilationResult = MoveTemp(AcceptedResult);
	OpenDocument->bHasCompilationResult = true;
	if (OpenDocument == ActiveDocument)
	{
		if (!OpenDocument->FunctionTabs.IsValidIndex(
			OpenDocument->ActiveFunctionTabIndex))
		{
			RefreshActiveDocument(false, false);
		}
		else
		{
			RebuildProperties();
		}
	}
}

TSharedRef<SWidget> SVerseVisualEditor::BuildCompilationModeMenu()
{
	FMenuBuilder MenuBuilder(true, nullptr);
	auto AddMode = [this, &MenuBuilder](
		EVerseCompilationMode Mode,
		const FText& Label,
		const FText& ToolTip)
	{
		MenuBuilder.AddMenuEntry(
			Label,
			ToolTip,
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP(this, &SVerseVisualEditor::SetCompilationMode, Mode),
				FCanExecuteAction(),
				FIsActionChecked::CreateLambda([this, Mode]()
				{
					return CompilationMode == Mode;
				})),
			NAME_None,
			EUserInterfaceActionType::RadioButton);
	};

	AddMode(
		EVerseCompilationMode::Continuous,
		LOCTEXT("ContinuousCompilationMode", "Continuous"),
		LOCTEXT("ContinuousCompilationModeTooltip", "Compile shortly after each source edit."));
	AddMode(
		EVerseCompilationMode::OnSave,
		LOCTEXT("OnSaveCompilationMode", "Compile on Save"),
		LOCTEXT("OnSaveCompilationModeTooltip", "Compile after a Verse file is saved."));
	AddMode(
		EVerseCompilationMode::Manual,
		LOCTEXT("ManualCompilationMode", "Manual"),
		LOCTEXT("ManualCompilationModeTooltip", "Compile only when the Compile button is pressed."));
	return MenuBuilder.MakeWidget();
}

void SVerseVisualEditor::SetCompilationMode(EVerseCompilationMode Mode)
{
	if (CompilationMode == Mode)
	{
		return;
	}

	CompilationMode = Mode;
	UVerseVisualEditorSettings* Settings = GetMutableDefault<UVerseVisualEditorSettings>();
	Settings->CompilationMode = Mode;
	Settings->SaveConfig();
	for (const TSharedPtr<FOpenVerseDocument>& OpenDocument : OpenDocuments)
	{
		if (OpenDocument.IsValid())
		{
			OpenDocument->bCompilationPending = false;
			if (Mode == EVerseCompilationMode::Continuous)
			{
				QueueCompilation(OpenDocument, true);
			}
		}
	}
}

FText SVerseVisualEditor::GetCompilationModeText() const
{
	switch (CompilationMode)
	{
	case EVerseCompilationMode::Continuous:
		return LOCTEXT("ContinuousCompilationModeButton", "Continuous");
	case EVerseCompilationMode::OnSave:
		return LOCTEXT("OnSaveCompilationModeButton", "On Save");
	case EVerseCompilationMode::Manual:
	default:
		return LOCTEXT("ManualCompilationModeButton", "Manual");
	}
}

void SVerseVisualEditor::QueueCompilation(
	const TSharedPtr<FOpenVerseDocument>& OpenDocument,
	bool bDebounce)
{
	if (!OpenDocument.IsValid() || !OpenDocument->Session.IsValid())
	{
		return;
	}
	if (!bDebounce)
	{
		StartCompilation(OpenDocument);
		return;
	}

	OpenDocument->bCompilationPending = true;
	OpenDocument->CompileAfterSeconds = FPlatformTime::Seconds() + 0.35;
}

void SVerseVisualEditor::StartCompilation(
	const TSharedPtr<FOpenVerseDocument>& OpenDocument,
	bool bRequestSemanticAnalysis)
{
	if (!OpenDocument.IsValid() || !OpenDocument->Session.IsValid())
	{
		return;
	}
	if (bRequestSemanticAnalysis)
	{
		RequestSemanticCompilation(OpenDocument);
	}

	OpenDocument->bCompilationPending = false;
	OpenDocument->bCompilationInFlight = true;
	const uint64 RequestId = ++OpenDocument->CompilationRequestId;
	const FVerseDocumentRevision Revision = OpenDocument->Session->GetRevision();
	FUtf8String Source = OpenDocument->Session->GetCurrentUtf8();
	FString SourcePath = OpenDocument->FilePath;
	const TWeakPtr<SVerseVisualEditor> WeakEditor = SharedThis(this);
	const TWeakPtr<FOpenVerseDocument> WeakDocument = OpenDocument;
	const TSharedRef<FVerseCompilationLifetimeToken> LifetimeToken =
		MakeShared<FVerseCompilationLifetimeToken>(SourcePath);

	(void)Async(EAsyncExecution::ThreadPool,
		[WeakEditor,
		 WeakDocument,
		 LifetimeToken,
		 RequestId,
		 Revision,
		 Source = MoveTemp(Source),
		 SourcePath = MoveTemp(SourcePath)]() mutable
		{
			FVerseCompilationResult Result = VerseCompilation::Compile(
				MoveTemp(Source),
				Revision,
				MoveTemp(SourcePath));
			AsyncTask(ENamedThreads::GameThread,
				[WeakEditor,
				 WeakDocument,
				 LifetimeToken,
				 RequestId,
				 Result = MoveTemp(Result)]() mutable
				{
					(void)LifetimeToken;
					const TSharedPtr<SVerseVisualEditor> Editor = WeakEditor.Pin();
					const TSharedPtr<FOpenVerseDocument> Document = WeakDocument.Pin();
					if (Editor.IsValid() && Document.IsValid())
					{
						Editor->ApplyCompilationResult(Document, RequestId, MoveTemp(Result));
					}
				});
		});
}

void SVerseVisualEditor::ApplyCompilationResult(
	const TSharedPtr<FOpenVerseDocument>& OpenDocument,
	uint64 RequestId,
	FVerseCompilationResult Result)
{
	if (!OpenDocument.IsValid()
		|| !OpenDocument->Session.IsValid()
		|| !OpenDocuments.Contains(OpenDocument)
		|| OpenDocument->CompilationRequestId != RequestId)
	{
		return;
	}

	OpenDocument->bCompilationInFlight = false;
	FVerseCompilationResult AcceptedResult;
	if (!VerseCompilation::TryAcceptResult(
		MoveTemp(Result),
		OpenDocument->Session->GetRevision(),
		OpenDocument->Session->GetTiles(),
		AcceptedResult))
	{
		return;
	}

	OpenDocument->CompilationResult = MoveTemp(AcceptedResult);
	OpenDocument->bHasCompilationResult = true;
	if (OpenDocument == ActiveDocument)
	{
		if (!OpenDocument->FunctionTabs.IsValidIndex(
			OpenDocument->ActiveFunctionTabIndex))
		{
			RefreshActiveDocument(false, false);
		}
		else
		{
			RebuildProperties();
		}
	}
}

void SVerseVisualEditor::InvalidateCompilationResult(
	const TSharedPtr<FOpenVerseDocument>& OpenDocument)
{
	if (!OpenDocument.IsValid())
	{
		return;
	}
	++OpenDocument->CompilationRequestId;
	OpenDocument->CompilationResult = {};
	OpenDocument->bHasCompilationResult = false;
	OpenDocument->bCompilationInFlight = false;
	OpenDocument->SemanticCompilationDiagnostics.Reset();
	OpenDocument->bSemanticCompilationPending = false;
	OpenDocument->bHasSemanticCompilationResult = false;
}


#undef LOCTEXT_NAMESPACE
