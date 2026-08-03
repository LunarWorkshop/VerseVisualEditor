#include "Editing/VerseSpecifier.h"

#include "Internationalization/Text.h"
#include "Document/VerseDocumentSession.h"

#define LOCTEXT_NAMESPACE "VerseSpecifier"

FText NormalizeVerseSpecifiers(FStringView ProposedText, FString& OutNormalizedText)
{
	OutNormalizedText.Reset();
	int32 Cursor = 0;
	while (Cursor < ProposedText.Len())
	{
		while (Cursor < ProposedText.Len() && FChar::IsWhitespace(ProposedText[Cursor]))
		{
			++Cursor;
		}
		if (Cursor == ProposedText.Len())
		{
			break;
		}
		if (ProposedText[Cursor] != TEXT('<'))
		{
			return LOCTEXT(
				"MissingSpecifierOpen",
				"Specifiers must use <name> syntax. Source was not changed.");
		}
		const int32 NameBegin = ++Cursor;
		while (Cursor < ProposedText.Len() && ProposedText[Cursor] != TEXT('>'))
		{
			++Cursor;
		}
		if (Cursor == ProposedText.Len())
		{
			return LOCTEXT(
				"MissingSpecifierClose",
				"A specifier is missing its closing >. Source was not changed.");
		}

		const FStringView Name = ProposedText.Mid(NameBegin, Cursor - NameBegin);
		bool bValidName = !Name.IsEmpty()
			&& (Name[0] == TEXT('_') || FChar::IsAlpha(Name[0]));
		for (int32 Index = 1; bValidName && Index < Name.Len(); ++Index)
		{
			bValidName = Name[Index] == TEXT('_') || FChar::IsAlnum(Name[Index]);
		}
		if (!bValidName)
		{
			return LOCTEXT(
				"InvalidSpecifierName",
				"Every specifier must contain one valid Verse identifier. Source was not changed.");
		}
		OutNormalizedText += TEXT('<');
		OutNormalizedText.AppendChars(Name.GetData(), Name.Len());
		OutNormalizedText += TEXT('>');
		++Cursor;
	}
	return FText::GetEmpty();
}

bool TryReplaceWithValidatedVerseSpecifiers(
	FVerseDocumentSession& Session,
	FVerseTextRange Range,
	FStringView ProposedText,
	FText& OutError)
{
	FString Normalized;
	OutError = NormalizeVerseSpecifiers(ProposedText, Normalized);
	if (!OutError.IsEmpty())
	{
		return false;
	}

	const FTCHARToUTF8 Converted(*Normalized);
	return Session.Replace(
		Range,
		FUtf8StringView(
			reinterpret_cast<const UTF8CHAR*>(Converted.Get()),
			Converted.Length()),
		OutError);
}

#undef LOCTEXT_NAMESPACE
