#pragma once

#include "CoreTypes.h"

/**
 * Temporary shutdown diagnostics for the Verse Visual Editor.
 *
 * The tracker deliberately owns no engine or compiler objects. It records raw
 * addresses and raw program counters so its final dump remains safe after
 * config, logging, Slate, and compiler subsystems have begun shutting down.
 */
namespace VerseVisualEditorLifetimeDiagnostics
{
	void Track(const void* Instance, const TCHAR* Kind, const TCHAR* Label = TEXT(""));
	void Update(const void* Instance, const TCHAR* Kind, const TCHAR* Label);
	void Untrack(const void* Instance, const TCHAR* Kind);
	void Event(const TCHAR* Name, const void* Owner = nullptr, const void* Related = nullptr);
	void Dump(const TCHAR* Phase);
}
