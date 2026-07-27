#include "VerseVisualEditorModule.h"

#include "Framework/Commands/Commands.h"
#include "Framework/Commands/UICommandList.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "Interfaces/IMainFrameModule.h"
#include "Modules/ModuleManager.h"
#include "SVerseVisualEditor.h"
#include "Styling/AppStyle.h"
#include "Textures/SlateIcon.h"
#include "ToolMenus.h"
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
		UI_COMMAND(Save, "Save", "Save the active Verse file.", EUserInterfaceActionType::Button,
			FInputChord(EModifierKey::Control, EKeys::S));
		UI_COMMAND(SaveAll, "Save All", "Save every modified Verse file.", EUserInterfaceActionType::Button,
			FInputChord(EModifierKey::Control | EModifierKey::Shift, EKeys::S));
		UI_COMMAND(Revert, "Revert", "Reload the active Verse file from disk and discard local changes.",
			EUserInterfaceActionType::Button, FInputChord());
		UI_COMMAND(Close, "Close", "Close the active Verse file.", EUserInterfaceActionType::Button,
			FInputChord(EModifierKey::Control, EKeys::W));
	}

	TSharedPtr<FUICommandInfo> Save;
	TSharedPtr<FUICommandInfo> SaveAll;
	TSharedPtr<FUICommandInfo> Revert;
	TSharedPtr<FUICommandInfo> Close;
};

void FVerseVisualEditorModule::StartupModule()
{
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
	UToolMenus::UnRegisterStartupCallback(this);
	UToolMenus::UnregisterOwner(this);
	FVerseVisualEditorCommands::Unregister();
	VerseEditorTabManager.Reset();

	if (FSlateApplication::IsInitialized())
	{
		FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(VerseVisualEditorModule::MainTabId);
	}
}

void FVerseVisualEditorModule::RegisterMenus()
{
	FToolMenuOwnerScoped OwnerScoped(this);
	UToolMenus* ToolMenus = UToolMenus::Get();
	ToolMenus->RegisterMenu(
		VerseVisualEditorModule::MainMenuName,
		TEXT("MainFrame.NomadMainMenu"),
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
		FToolMenuInsert(NAME_None, EToolMenuInsertType::First));
	const FVerseVisualEditorCommands& Commands = FVerseVisualEditorCommands::Get();
	Section.AddMenuEntry(Commands.Save);
	Section.AddMenuEntry(Commands.SaveAll);
	Section.AddSeparator(TEXT("VerseVisualEditorSaveSeparator"));
	Section.AddMenuEntry(Commands.Revert);
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
	CommandList->MapAction(
		Commands.Save,
		FExecuteAction::CreateSP(Editor, &SVerseVisualEditor::SaveActiveDocumentFromMenu),
		FCanExecuteAction::CreateSP(Editor, &SVerseVisualEditor::CanSaveActiveDocument));
	CommandList->MapAction(
		Commands.SaveAll,
		FExecuteAction::CreateSP(Editor, &SVerseVisualEditor::SaveAllDocuments),
		FCanExecuteAction::CreateSP(Editor, &SVerseVisualEditor::CanSaveAnyDocument));
	CommandList->MapAction(
		Commands.Revert,
		FExecuteAction::CreateSP(Editor, &SVerseVisualEditor::RevertActiveDocument),
		FCanExecuteAction::CreateSP(Editor, &SVerseVisualEditor::HasActiveDocument));
	CommandList->MapAction(
		Commands.Close,
		FExecuteAction::CreateSP(Editor, &SVerseVisualEditor::CloseActiveDocument),
		FCanExecuteAction::CreateSP(Editor, &SVerseVisualEditor::HasActiveDocument));

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
