#include "VerseEditorFileTree.h"

#include "HAL/FileManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"

namespace
{
	void NormalizeDirectory(FString& Directory)
	{
		Directory = FPaths::ConvertRelativePathToFull(Directory);
		FPaths::NormalizeDirectoryName(Directory);
	}

	void AddRoot(TArray<FVerseSourceRoot>& Roots, FString Label, FString Directory)
	{
		NormalizeDirectory(Directory);
		if (!IFileManager::Get().DirectoryExists(*Directory))
		{
			return;
		}

		if (Roots.ContainsByPredicate([&Directory](const FVerseSourceRoot& Existing)
		{
			return Existing.Directory.Equals(Directory, ESearchCase::IgnoreCase);
		}))
		{
			return;
		}

		Roots.Add({MoveTemp(Label), MoveTemp(Directory)});
	}

	void AddModuleVerseRoots(
		TArray<FVerseSourceRoot>& Roots,
		const FString& OwnerLabel,
		const FString& SourceDirectory)
	{
		TArray<FString> ModuleDirectories;
		IFileManager::Get().FindFiles(ModuleDirectories, *FPaths::Combine(SourceDirectory, TEXT("*")), false, true);
		ModuleDirectories.Sort();
		for (const FString& ModuleName : ModuleDirectories)
		{
			AddRoot(
				Roots,
				FString::Printf(TEXT("%s/%s"), *OwnerLabel, *ModuleName),
				FPaths::Combine(SourceDirectory, ModuleName, TEXT("Verse")));
		}
	}

	TSharedPtr<FVerseFileTreeItem> BuildDirectory(const FString& Directory, const FString& DisplayName, bool bKeepEmpty)
	{
		TSharedPtr<FVerseFileTreeItem> Item = MakeShared<FVerseFileTreeItem>();
		Item->Name = DisplayName;
		Item->FullPath = Directory;
		Item->bIsDirectory = true;

		TArray<FString> ChildDirectories;
		IFileManager::Get().FindFiles(ChildDirectories, *FPaths::Combine(Directory, TEXT("*")), false, true);
		ChildDirectories.Sort();
		for (const FString& ChildName : ChildDirectories)
		{
			const FString ChildPath = FPaths::Combine(Directory, ChildName);
			if (TSharedPtr<FVerseFileTreeItem> Child = BuildDirectory(ChildPath, ChildName, false))
			{
				Item->Children.Add(MoveTemp(Child));
			}
		}

		TArray<FString> VerseFiles;
		IFileManager::Get().FindFiles(VerseFiles, *FPaths::Combine(Directory, TEXT("*.verse")), true, false);
		VerseFiles.Sort();
		for (const FString& FileName : VerseFiles)
		{
			TSharedPtr<FVerseFileTreeItem> FileItem = MakeShared<FVerseFileTreeItem>();
			FileItem->Name = FileName;
			FileItem->FullPath = FPaths::Combine(Directory, FileName);
			FPaths::NormalizeFilename(FileItem->FullPath);
			Item->Children.Add(MoveTemp(FileItem));
		}

		return bKeepEmpty || Item->Children.Num() > 0 ? Item : nullptr;
	}
}

void VerseVisualEditor::DiscoverProjectVerseRoots(TArray<FVerseSourceRoot>& OutRoots)
{
	OutRoots.Reset();
	AddRoot(OutRoots, TEXT("Project"), FPaths::Combine(FPaths::ProjectDir(), TEXT("Verse")));
	AddModuleVerseRoots(OutRoots, FApp::GetProjectName(), FPaths::Combine(FPaths::ProjectDir(), TEXT("Source")));

	for (const TSharedRef<IPlugin>& Plugin : IPluginManager::Get().GetEnabledPlugins())
	{
		if (Plugin->GetLoadedFrom() != EPluginLoadedFrom::Project || !Plugin->CanContainVerse())
		{
			continue;
		}

		AddRoot(
			OutRoots,
			FString::Printf(TEXT("%s/Content"), *Plugin->GetName()),
			FPaths::Combine(Plugin->GetBaseDir(), TEXT("Content")));
		AddModuleVerseRoots(
			OutRoots,
			Plugin->GetName(),
			FPaths::Combine(Plugin->GetBaseDir(), TEXT("Source")));
	}

	OutRoots.Sort([](const FVerseSourceRoot& Left, const FVerseSourceRoot& Right)
	{
		return Left.Label < Right.Label;
	});
}

TArray<TSharedPtr<FVerseFileTreeItem>> VerseVisualEditor::BuildVerseFileTree(TConstArrayView<FVerseSourceRoot> Roots)
{
	TArray<TSharedPtr<FVerseFileTreeItem>> Result;
	for (const FVerseSourceRoot& Root : Roots)
	{
		if (TSharedPtr<FVerseFileTreeItem> RootItem = BuildDirectory(Root.Directory, Root.Label, true))
		{
			Result.Add(MoveTemp(RootItem));
		}
	}
	return Result;
}

TArray<FString> VerseVisualEditor::BuildVerseModulePath(
	const FString& FilePath,
	TConstArrayView<FVerseSourceRoot> Roots)
{
	FString NormalizedFile = FPaths::ConvertRelativePathToFull(FilePath);
	FPaths::NormalizeFilename(NormalizedFile);
	for (const FVerseSourceRoot& Root : Roots)
	{
		FString NormalizedRoot = FPaths::ConvertRelativePathToFull(Root.Directory);
		FPaths::NormalizeDirectoryName(NormalizedRoot);
		const FString RootPrefix = NormalizedRoot + TEXT("/");
		if (!NormalizedFile.StartsWith(RootPrefix, ESearchCase::IgnoreCase))
		{
			continue;
		}

		TArray<FString> Result;
		Root.Label.ParseIntoArray(Result, TEXT("/"), true);
		const FString RelativeDirectory = FPaths::GetPath(
			NormalizedFile.RightChop(RootPrefix.Len()));
		TArray<FString> DirectoryParts;
		RelativeDirectory.ParseIntoArray(DirectoryParts, TEXT("/"), true);
		Result.Append(MoveTemp(DirectoryParts));
		return Result;
	}
	return {};
}
