#pragma once

#include "Containers/UnrealString.h"

class FText;

/** Returns empty text when the proposed common-form Verse identifier looks valid. */
FText ValidateVerseIdentifier(FStringView Identifier);
