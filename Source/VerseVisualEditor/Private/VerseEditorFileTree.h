#pragma once

#include "Containers/Array.h"
#include "Containers/UnrealString.h"
#include "Templates/SharedPointer.h"

struct FVerseSourceRoot
{
	FString Label;
	FString Directory;
};

struct FVerseFileTreeItem
{
	FString Name;
	FString FullPath;
	bool bIsDirectory = false;
	TArray<TSharedPtr<FVerseFileTreeItem>> Children;
};

namespace VerseVisualEditor
{
	void DiscoverProjectVerseRoots(TArray<FVerseSourceRoot>& OutRoots);
	TArray<TSharedPtr<FVerseFileTreeItem>> BuildVerseFileTree(TConstArrayView<FVerseSourceRoot> Roots);
	TArray<FString> BuildVerseModulePath(
		const FString& FilePath,
		TConstArrayView<FVerseSourceRoot> Roots);
}
