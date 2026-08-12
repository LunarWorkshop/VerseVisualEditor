#include "VerseVisualEditorModule.h"

#include "Framework/Commands/Commands.h"
#include "Framework/Commands/InputBindingManager.h"
#include "Framework/Commands/UICommandList.h"
#include "Framework/Commands/GenericCommands.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "Interfaces/IMainFrameModule.h"
#include "Misc/CoreDelegates.h"
#include "Modules/ModuleManager.h"
#include "Slate/SVerseVisualEditor.h"
#include "Styling/AppStyle.h"
#include "Textures/SlateIcon.h"
#include "ToolMenus.h"
#include "Slate/VerseVisualEditorStyle.h"
#include "Infrastructure/VerseVisualEditorLifetimeDiagnostics.h"
#include "Widgets/Docking/SDockTab.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"

#define LOCTEXT_NAMESPACE "VerseVisualEditorModule"

namespace VerseVisualEditorModule
{
	const FName MainTabId(TEXT("VerseVisualEditor"));
	const FName MainMenuName(TEXT("VerseVisualEditor.MainMenu"));
	const FName FileMenuName(TEXT("VerseVisualEditor.MainMenu.File"));
}

class FVerseVisualEditorCommands final : public TCommands<FVerseVisualEditorCommands>
{
public:
	FVerseVisualEditorCommands()
		: TCommands<FVerseVisualEditorCommands>(
			TEXT("VerseVisualEditor"),
			LOCTEXT("VerseVisualEditorCommandContext", "Verse Visual Editor"),
			NAME_None,
			FAppStyle::GetAppStyleSetName())
	{
	}

	virtual void RegisterCommands() override
	{
		UI_COMMAND(Reload, "Reload", "Reload the active Verse file from disk and discard local changes.",
			EUserInterfaceActionType::Button, FInputChord());
		UI_COMMAND(Close, "Close", "Close the active Verse file.", EUserInterfaceActionType::Button,
			FInputChord(EModifierKey::Control, EKeys::W));
	}

	TSharedPtr<FUICommandInfo> Reload;
	TSharedPtr<FUICommandInfo> Close;
};

void FVerseVisualEditorModule::StartupModule()
{
	VerseVisualEditorStyle::Initialize();
	VerseVisualEditorLifetimeDiagnostics::Track(
		this,
		TEXT("PluginModule"));
	EnginePreExitHandle = FCoreDelegates::OnEnginePreExit.AddRaw(
		this,
		&FVerseVisualEditorModule::HandleEnginePreExit);
	FVerseVisualEditorCommands::Register();
	UToolMenus::RegisterStartupCallback(
		FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FVerseVisualEditorModule::RegisterMenus));

	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
		VerseVisualEditorModule::MainTabId,
		FOnSpawnTab::CreateRaw(this, &FVerseVisualEditorModule::SpawnVerseVisualEditorTab))
		.SetDisplayName(LOCTEXT("MainTabTitle", "Verse Visual Editor"))
		.SetTooltipText(LOCTEXT("MainTabTooltip", "Open the Verse Visual Editor."))
		.SetGroup(WorkspaceMenu::GetMenuStructure().GetToolsCategory())
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Edit"));
}

void FVerseVisualEditorModule::ShutdownModule()
{
	VerseVisualEditorLifetimeDiagnostics::Dump(
		TEXT("VerseVisualEditor module shutdown begin"));
	FCoreDelegates::OnEnginePreExit.Remove(EnginePreExitHandle);
	EnginePreExitHandle.Reset();
	UToolMenus::UnRegisterStartupCallback(this);
	UToolMenus::UnregisterOwner(this);
	FVerseVisualEditorCommands::Unregister();
	VerseEditorTabManager.Reset();

	if (FSlateApplication::IsInitialized())
	{
		FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(VerseVisualEditorModule::MainTabId);
	}
	VerseVisualEditorStyle::Shutdown();
	VerseVisualEditorLifetimeDiagnostics::Dump(
		TEXT("VerseVisualEditor module shutdown end"));
	VerseVisualEditorLifetimeDiagnostics::Untrack(
		this,
		TEXT("PluginModule"));
}

void FVerseVisualEditorModule::HandleEnginePreExit()
{
	VerseVisualEditorLifetimeDiagnostics::Dump(
		TEXT("FCoreDelegates::OnEnginePreExit"));
}

void FVerseVisualEditorModule::RegisterMenus()
{
	FToolMenuOwnerScoped OwnerScoped(this);
	UToolMenus* ToolMenus = UToolMenus::Get();
	ToolMenus->RegisterMenu(
		VerseVisualEditorModule::MainMenuName,
		TEXT("MainFrame.MainMenu"),
		EMultiBoxType::MenuBar,
		false);

	UToolMenu* FileMenu = ToolMenus->RegisterMenu(
		VerseVisualEditorModule::FileMenuName,
		TEXT("MainFrame.MainMenu.File"),
		EMultiBoxType::Menu,
		false);

	FToolMenuSection& Section = FileMenu->AddSection(
		TEXT("VerseVisualEditorFile"),
		LOCTEXT("VerseVisualEditorFileSection", "Verse Visual Editor"),
		FToolMenuInsert(TEXT("FileLoadAndSave"), EToolMenuInsertType::After));
	const FVerseVisualEditorCommands& Commands = FVerseVisualEditorCommands::Get();
	Section.AddMenuEntry(Commands.Reload);
	Section.AddMenuEntry(Commands.Close);
}

TSharedRef<SDockTab> FVerseVisualEditorModule::SpawnVerseVisualEditorTab(const FSpawnTabArgs& Args)
{
	const TSharedRef<SVerseVisualEditor> Editor = SNew(SVerseVisualEditor);
	const TSharedRef<SDockTab> DockTab = SNew(SDockTab)
		.TabRole(ETabRole::MajorTab)
		[
			Editor
		];

	const TSharedRef<FUICommandList> CommandList = MakeShared<FUICommandList>();
	const FVerseVisualEditorCommands& Commands = FVerseVisualEditorCommands::Get();
	const TSharedPtr<FUICommandInfo> AssetSave =
		FInputBindingManager::Get().FindCommandInContext(TEXT("AssetEditor"), TEXT("SaveAsset"));
	const TSharedPtr<FUICommandInfo> AssetSaveAs =
		FInputBindingManager::Get().FindCommandInContext(TEXT("AssetEditor"), TEXT("SaveAssetAs"));
	if (ensure(AssetSave.IsValid() && AssetSaveAs.IsValid()))
	{
		if (UToolMenu* FileMenu = UToolMenus::Get()->FindMenu(VerseVisualEditorModule::FileMenuName))
		{
			// Extend Unreal's inherited Save section, matching the standalone asset-editor
			// host. This must not create a second section labelled "Save".
			FToolMenuSection& SaveSection = FileMenu->FindOrAddSection(TEXT("FileLoadAndSave"));
			if (!SaveSection.FindEntry(AssetSave->GetCommandName()))
			{
				SaveSection.AddMenuEntry(
					AssetSave,
					TAttribute<FText>(),
					TAttribute<FText>(),
					FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("AssetEditor.SaveAsset")));
			}
			if (!SaveSection.FindEntry(AssetSaveAs->GetCommandName()))
			{
				SaveSection.AddMenuEntry(
					AssetSaveAs,
					TAttribute<FText>(),
					TAttribute<FText>(),
					FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("AssetEditor.SaveAssetAs")));
			}
		}

		CommandList->MapAction(
			AssetSave,
			FExecuteAction::CreateSP(Editor, &SVerseVisualEditor::SaveActiveDocumentFromMenu),
			FCanExecuteAction::CreateSP(Editor, &SVerseVisualEditor::CanSaveActiveDocument));
		CommandList->MapAction(
			AssetSaveAs,
			FExecuteAction::CreateSP(Editor, &SVerseVisualEditor::SaveActiveDocumentAs),
			FCanExecuteAction::CreateSP(Editor, &SVerseVisualEditor::HasActiveDocument));
	}
	CommandList->MapAction(
		Commands.Reload,
		FExecuteAction::CreateSP(Editor, &SVerseVisualEditor::RevertActiveDocument),
		FCanExecuteAction::CreateSP(Editor, &SVerseVisualEditor::HasActiveDocument));
	const TSharedPtr<FUICommandInfo> MainFrameSaveAll =
		FInputBindingManager::Get().FindCommandInContext(TEXT("MainFrame"), TEXT("SaveAll"));
	if (ensure(MainFrameSaveAll.IsValid()))
	{
		CommandList->MapAction(
			MainFrameSaveAll,
			FExecuteAction::CreateSP(Editor, &SVerseVisualEditor::SaveAllFromMainFrame),
			FCanExecuteAction::CreateSP(Editor, &SVerseVisualEditor::CanSaveAllFromMainFrame));
	}
	CommandList->MapAction(
		Commands.Close,
		FExecuteAction::CreateSP(Editor, &SVerseVisualEditor::CloseActiveDocument),
		FCanExecuteAction::CreateSP(Editor, &SVerseVisualEditor::HasActiveDocument));
	CommandList->MapAction(
		FGenericCommands::Get().Undo,
		FExecuteAction::CreateSP(Editor, &SVerseVisualEditor::UndoActiveDocument),
		FCanExecuteAction::CreateSP(Editor, &SVerseVisualEditor::CanUndoActiveDocument));
	CommandList->MapAction(
		FGenericCommands::Get().Redo,
		FExecuteAction::CreateSP(Editor, &SVerseVisualEditor::RedoActiveDocument),
		FCanExecuteAction::CreateSP(Editor, &SVerseVisualEditor::CanRedoActiveDocument));

	VerseEditorTabManager = FGlobalTabmanager::Get()->NewTabManager(DockTab);
	VerseEditorTabManager->SetAllowWindowMenuBar(true);
	FToolMenuContext MenuContext(CommandList);
	IMainFrameModule& MainFrameModule = FModuleManager::LoadModuleChecked<IMainFrameModule>(TEXT("MainFrame"));
	MainFrameModule.MakeMainMenu(
		VerseEditorTabManager,
		VerseVisualEditorModule::MainMenuName,
		MenuContext);

	return DockTab;
}

IMPLEMENT_MODULE(FVerseVisualEditorModule, VerseVisualEditor)

#undef LOCTEXT_NAMESPACE
