#include "VerseIdentifier.h"

#include "Internationalization/Text.h"

#define LOCTEXT_NAMESPACE "VerseIdentifier"

FText ValidateVerseIdentifier(FStringView Identifier)
{
	if (Identifier.IsEmpty())
	{
		return LOCTEXT("EmptyIdentifier", "A Verse identifier cannot be empty. The text was kept as source.");
	}
	if (Identifier[0] != TEXT('_') && !FChar::IsAlpha(Identifier[0]))
	{
		return LOCTEXT("InvalidIdentifierStart", "A Verse identifier must begin with a letter or underscore. The text was kept as source.");
	}
	for (int32 Index = 1; Index < Identifier.Len(); ++Index)
	{
		if (Identifier[Index] != TEXT('_') && !FChar::IsAlnum(Identifier[Index]))
		{
			return LOCTEXT("InvalidIdentifierCharacter", "A Verse identifier may contain only letters, numbers, and underscores. The text was kept as source.");
		}
	}

	static const TSet<FString> ReservedIdentifiers = {
		TEXT("class"), TEXT("enum"), TEXT("false"), TEXT("for"), TEXT("if"),
		TEXT("interface"), TEXT("module"), TEXT("return"), TEXT("struct"),
		TEXT("true"), TEXT("type"), TEXT("var")};
	if (ReservedIdentifiers.Contains(FString(Identifier)))
	{
		return LOCTEXT("ReservedIdentifier", "That name is reserved by Verse. The text was kept as source.");
	}
	return FText::GetEmpty();
}

#undef LOCTEXT_NAMESPACE
