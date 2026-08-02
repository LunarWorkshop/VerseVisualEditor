#pragma once

#include "CoreMinimal.h"

class ISlateStyle;
struct FVerseVisualTile;

/** Plugin-owned graph chrome. Never mutates the editor's global AppStyle. */
namespace VerseVisualEditorStyle
{
	void Initialize();
	void Shutdown();
	bool IsInitialized();
	const ISlateStyle& Get();

	FLinearColor GetTypeColor(const FString& VerseType);
	FLinearColor GetTileTitleColor(const FVerseVisualTile& Tile);
	FLinearColor GetPrimaryTextColor();
	FLinearColor GetSecondaryTextColor();
	FLinearColor GetMetadataTextColor();
}
