#pragma once

enum class EVerseExternalChangeAction
{
	Ignore,
	Reload,
	PromptReloadOrKeepLocal,
};

inline EVerseExternalChangeAction DetermineVerseExternalChangeAction(
	bool bDiskMatchesLastKnown,
	bool bHasLocalChanges)
{
	if (bDiskMatchesLastKnown)
	{
		return EVerseExternalChangeAction::Ignore;
	}
	return bHasLocalChanges
		? EVerseExternalChangeAction::PromptReloadOrKeepLocal
		: EVerseExternalChangeAction::Reload;
}
