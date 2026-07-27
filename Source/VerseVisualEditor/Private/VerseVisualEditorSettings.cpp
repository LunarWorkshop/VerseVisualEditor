#include "VerseVisualEditorSettings.h"

#define LOCTEXT_NAMESPACE "VerseVisualEditorSettings"

FText UVerseVisualEditorSettings::GetSectionText() const
{
	return LOCTEXT("SectionText", "Verse Visual Editor");
}

FText UVerseVisualEditorSettings::GetSectionDescription() const
{
	return LOCTEXT("SectionDescription", "Configure per-user behavior for the Verse Visual Editor.");
}

#undef LOCTEXT_NAMESPACE
