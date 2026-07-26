#pragma once

#include "Modules/ModuleInterface.h"

class FSpawnTabArgs;
class SDockTab;

class FVerseVisualEditorModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	TSharedRef<SDockTab> SpawnVerseVisualEditorTab(const FSpawnTabArgs& Args);
};
