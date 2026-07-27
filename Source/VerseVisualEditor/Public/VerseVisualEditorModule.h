#pragma once

#include "Modules/ModuleInterface.h"

class FSpawnTabArgs;
class FTabManager;
class SDockTab;

class FVerseVisualEditorModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	void RegisterMenus();
	TSharedRef<SDockTab> SpawnVerseVisualEditorTab(const FSpawnTabArgs& Args);

	TSharedPtr<FTabManager> VerseEditorTabManager;
};
