#pragma once

#include "Delegates/Delegate.h"
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
	void HandleEnginePreExit();
	TSharedRef<SDockTab> SpawnVerseVisualEditorTab(const FSpawnTabArgs& Args);

	TSharedPtr<FTabManager> VerseEditorTabManager;
	FDelegateHandle EnginePreExitHandle;
};
