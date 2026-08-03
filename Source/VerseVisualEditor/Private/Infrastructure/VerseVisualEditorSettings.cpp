#include "VerseVisualEditorSettings.h"

#define LOCTEXT_NAMESPACE "VerseVisualEditorSettings"

FText UVerseVisualEditorProjectSettings::GetSectionText() const
{
	return LOCTEXT("ProjectSectionText", "Verse Visual Editor");
}

FText UVerseVisualEditorProjectSettings::GetSectionDescription() const
{
	return LOCTEXT(
		"ProjectSectionDescription",
		"Formatting follows the most local existing source convention first: the destination clause and its neighbors, then the dominant style of the file. These project values are fallback defaults used only when the source does not establish a convention.");
}

FText UVerseVisualEditorSettings::GetSectionText() const
{
	return LOCTEXT("SectionText", "Verse Visual Editor");
}

FText UVerseVisualEditorSettings::GetSectionDescription() const
{
	return LOCTEXT("SectionDescription", "Configure per-user behavior for the Verse Visual Editor.");
}

#undef LOCTEXT_NAMESPACE
