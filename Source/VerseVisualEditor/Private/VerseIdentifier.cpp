#include "VerseIdentifier.h"

#include "Internationalization/Text.h"
#include "VerseDocumentSession.h"
#include "uLang/Parser/ReservedSymbols.h"

#define LOCTEXT_NAMESPACE "VerseIdentifier"

FText ValidateVerseIdentifier(FStringView Identifier)
{
	if (Identifier.IsEmpty())
	{
		return LOCTEXT("EmptyIdentifier", "A Verse identifier cannot be empty. Source was not changed.");
	}
	if (Identifier[0] != TEXT('_') && !FChar::IsAlpha(Identifier[0]))
	{
		return LOCTEXT("InvalidIdentifierStart", "A Verse identifier must begin with a letter or underscore. Source was not changed.");
	}
	for (int32 Index = 1; Index < Identifier.Len(); ++Index)
	{
		if (Identifier[Index] != TEXT('_') && !FChar::IsAlnum(Identifier[Index]))
		{
			return LOCTEXT("InvalidIdentifierCharacter", "A Verse identifier may contain only letters, numbers, and underscores. Source was not changed.");
		}
	}

	const FTCHARToUTF8 Utf8Identifier(Identifier.GetData(), Identifier.Len());
	const uLang::CUTF8String Candidate(uLang::CUTF8StringView(
		Utf8Identifier.Get(),
		Utf8Identifier.Length()));
	static const uLang::TSet<uLang::CUTF8String> ReservedIdentifiers =
		uLang::GetReservedSymbols(
			Verse::Version::LatestUnstable,
			VerseFN::UploadedAtFNVersion::Latest);
	if (ReservedIdentifiers.Contains(Candidate))
	{
		return LOCTEXT("ReservedIdentifier", "That name is reserved by Verse. Source was not changed.");
	}
	return FText::GetEmpty();
}

bool TryReplaceWithValidatedVerseIdentifier(
	FVerseDocumentSession& Session,
	FVerseTextRange Range,
	FStringView Identifier,
	FText& OutError)
{
	OutError = ValidateVerseIdentifier(Identifier);
	if (!OutError.IsEmpty())
	{
		return false;
	}

	const FTCHARToUTF8 Converted(Identifier.GetData(), Identifier.Len());
	return Session.Replace(
		Range,
		FUtf8StringView(
			reinterpret_cast<const UTF8CHAR*>(Converted.Get()),
			Converted.Length()),
		OutError);
}

#undef LOCTEXT_NAMESPACE
