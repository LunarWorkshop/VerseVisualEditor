#if WITH_DEV_AUTOMATION_TESTS

#include "VerseEditorFileTree.h"

#include "Framework/Docking/TabManager.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Widgets/Docking/SDockTab.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVerseEditorFileTreeTest,
	"VerseVisualEditor.Window.FileTree",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVerseEditorFileTreeTest::RunTest(const FString& Parameters)
{
	const FString RootDirectory = FPaths::Combine(
		FPaths::ProjectIntermediateDir(),
		TEXT("VerseVisualEditorTests/FileTree"));
	IFileManager::Get().DeleteDirectory(*RootDirectory, false, true);
	IFileManager::Get().MakeDirectory(*FPaths::Combine(RootDirectory, TEXT("Nested")), true);
	IFileManager::Get().MakeDirectory(*FPaths::Combine(RootDirectory, TEXT("Empty")), true);
	FFileHelper::SaveStringToFile(TEXT("Root := module{}\n"), *FPaths::Combine(RootDirectory, TEXT("root.verse")));
	FFileHelper::SaveStringToFile(TEXT("Nested := module{}\n"), *FPaths::Combine(RootDirectory, TEXT("Nested/nested.verse")));
	FFileHelper::SaveStringToFile(TEXT("ignored"), *FPaths::Combine(RootDirectory, TEXT("ignored.txt")));

	const TArray<FVerseSourceRoot> Roots = {{TEXT("TestRoot"), RootDirectory}};
	const TArray<TSharedPtr<FVerseFileTreeItem>> Tree = VerseVisualEditor::BuildVerseFileTree(Roots);
	if (!TestEqual(TEXT("One source root is produced"), Tree.Num(), 1))
	{
		IFileManager::Get().DeleteDirectory(*RootDirectory, false, true);
		return false;
	}

	const TSharedPtr<FVerseFileTreeItem>& Root = Tree[0];
	TestEqual(TEXT("Root label is preserved"), Root->Name, FString(TEXT("TestRoot")));
	TestEqual(TEXT("Only the populated directory and Verse file remain"), Root->Children.Num(), 2);
	TestTrue(
		TEXT("Nested directory is present"),
		Root->Children.ContainsByPredicate([](const TSharedPtr<FVerseFileTreeItem>& Item)
		{
			return Item->bIsDirectory && Item->Name == TEXT("Nested") && Item->Children.Num() == 1;
		}));
	TestTrue(
		TEXT("Root Verse file is present"),
		Root->Children.ContainsByPredicate([](const TSharedPtr<FVerseFileTreeItem>& Item)
		{
			return !Item->bIsDirectory && Item->Name == TEXT("root.verse");
		}));
	TestFalse(
		TEXT("Non-Verse files are excluded"),
		Root->Children.ContainsByPredicate([](const TSharedPtr<FVerseFileTreeItem>& Item)
		{
			return Item->Name == TEXT("ignored.txt");
		}));
	TestFalse(
		TEXT("Empty directories are excluded"),
		Root->Children.ContainsByPredicate([](const TSharedPtr<FVerseFileTreeItem>& Item)
		{
			return Item->Name == TEXT("Empty");
		}));
	TestEqual(
		TEXT("File module path combines the source root and containing directories"),
		FString::Join(
			VerseVisualEditor::BuildVerseModulePath(
				FPaths::Combine(RootDirectory, TEXT("Nested/nested.verse")),
				Roots),
			TEXT("/")),
		FString(TEXT("TestRoot/Nested")));

	IFileManager::Get().DeleteDirectory(*RootDirectory, false, true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVerseEditorTabSpawnerTest,
	"VerseVisualEditor.Window.TabSpawner",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVerseEditorTabSpawnerTest::RunTest(const FString& Parameters)
{
	if (!TestTrue(
		TEXT("Verse Visual Editor nomad tab is registered"),
		FGlobalTabmanager::Get()->HasTabSpawner(TEXT("VerseVisualEditor"))))
	{
		return false;
	}

	const TSharedPtr<SDockTab> Tab = FGlobalTabmanager::Get()->TryInvokeTab(FTabId(TEXT("VerseVisualEditor")));
	TestTrue(TEXT("Verse Visual Editor window opens"), Tab.IsValid());
	if (Tab.IsValid())
	{
		Tab->RequestCloseTab();
	}
	return true;
}

#endif
