#pragma once

#include "Modules/ModuleInterface.h"

class FVisualVerseEditorModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
