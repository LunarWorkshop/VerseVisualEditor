#include "VerseVisualEditorModule.h"

#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "Modules/ModuleManager.h"
#include "SVerseVisualEditor.h"
#include "Styling/AppStyle.h"
#include "Textures/SlateIcon.h"
#include "Widgets/Docking/SDockTab.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"

#define LOCTEXT_NAMESPACE "VerseVisualEditorModule"

namespace VerseVisualEditorModule
{
	const FName MainTabId(TEXT("VerseVisualEditor"));
}

void FVerseVisualEditorModule::StartupModule()
{
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
	if (FSlateApplication::IsInitialized())
	{
		FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(VerseVisualEditorModule::MainTabId);
	}
}

TSharedRef<SDockTab> FVerseVisualEditorModule::SpawnVerseVisualEditorTab(const FSpawnTabArgs& Args)
{
	return SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		[
			SNew(SVerseVisualEditor)
		];
}

IMPLEMENT_MODULE(FVerseVisualEditorModule, VerseVisualEditor)

#undef LOCTEXT_NAMESPACE
