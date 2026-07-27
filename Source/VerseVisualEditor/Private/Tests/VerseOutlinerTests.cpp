#if WITH_DEV_AUTOMATION_TESTS

#include "VerseOutliner.h"

#include "Interfaces/IPluginManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "VerseDocument.h"
#include "VerseDefinitionIcon.h"
#include "VerseParseSnapshotBuilder.h"
#include "VerseVisualTile.h"

namespace VerseOutlinerTests
{
	const FVerseOutlinerItem* FindItem(
		TConstArrayView<TSharedPtr<FVerseOutlinerItem>> Items,
		FStringView Name)
	{
		for (const TSharedPtr<FVerseOutlinerItem>& Item : Items)
		{
			if (Item->Name == Name)
			{
				return Item.Get();
			}
			if (const FVerseOutlinerItem* Nested = FindItem(Item->Children, Name))
			{
				return Nested;
			}
		}
		return nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVerseOutlinerHierarchyTest,
	"VerseVisualEditor.Prototype.Outliner.ActiveFileHierarchy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVerseOutlinerHierarchyTest::RunTest(const FString& Parameters)
{
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("VerseVisualEditor"));
	if (!TestTrue(TEXT("VerseVisualEditor plugin is discoverable"), Plugin.IsValid()))
	{
		return false;
	}

	FText Error;
	TSharedPtr<FVerseDocument> Document = FVerseDocument::LoadFromFile(
		FPaths::Combine(Plugin->GetBaseDir(), TEXT("Tests/Fixtures/nested_modules.verse")),
		Error);
	if (!TestTrue(TEXT("Nested module fixture loads"), Document.IsValid()))
	{
		return false;
	}

	const FVerseParseSnapshot Snapshot = FVerseParseSnapshotBuilder::Build(Document.ToSharedRef());
	const TArray<FVerseVisualTile> Tiles = FVerseVisualTileBuilder::Build(
		Snapshot,
		FVerseDocumentRevision{31});
	const TArray<TSharedPtr<FVerseOutlinerItem>> Items = FVerseOutlinerBuilder::Build(Tiles, Snapshot);
	const FVerseOutlinerItem* Root = VerseOutlinerTests::FindItem(Items, TEXT("RootModule"));
	const FVerseOutlinerItem* NestedModule = VerseOutlinerTests::FindItem(Items, TEXT("NestedModule"));
	const FVerseOutlinerItem* NestedFunction = VerseOutlinerTests::FindItem(Items, TEXT("ChildFunction"));
	const FVerseOutlinerItem* Constant = VerseOutlinerTests::FindItem(Items, TEXT("ChildConstant"));
	const FVerseOutlinerItem* TypeAlias = VerseOutlinerTests::FindItem(Items, TEXT("ChildTypeAlias"));

	TestNotNull(TEXT("Top-level module is listed"), Root);
	TestNotNull(TEXT("Nested module is listed"), NestedModule);
	TestNotNull(TEXT("Function inside a module is listed"), NestedFunction);
	TestNull(TEXT("Variables are intentionally omitted"),
		VerseOutlinerTests::FindItem(Items, TEXT("ChildVariable")));
	TestTrue(TEXT("Nested definitions retain hierarchy"),
		Root != nullptr
		&& VerseOutlinerTests::FindItem(Root->Children, TEXT("NestedModule")) != nullptr);
	TestTrue(TEXT("Outliner items retain selectable tile ranges"),
		NestedFunction != nullptr && NestedFunction->TileRange.IsSet());
	TestTrue(TEXT("Module label mirrors its Verse declaration"),
		Root != nullptr && Root->Label == TEXT("RootModule := module"));
	TestTrue(TEXT("Function label shows parameters and return type but no effects"),
		NestedFunction != nullptr
		&& NestedFunction->Label == TEXT("ChildFunction(Input : int) : int")
		&& !NestedFunction->Label.Contains(TEXT("computes")));
	TestTrue(TEXT("Constant label retains its declared type without its value"),
		Constant != nullptr && Constant->Label == TEXT("ChildConstant : string"));
	TestTrue(TEXT("Type alias label omits its defining constraint"),
		TypeAlias != nullptr && TypeAlias->Label == TEXT("ChildTypeAlias := type"));
	TestEqual(TEXT("Functions use Blueprint's function icon"),
		GetVerseDefinitionIconName(VerseSyntaxKind::Function),
		FName(TEXT("GraphEditor.Function_16x")));
	TestEqual(TEXT("Classes use Blueprint's event graph icon"),
		GetVerseDefinitionIconName(VerseSyntaxKind::Class),
		FName(TEXT("GraphEditor.EventGraph_16x")));
	TestEqual(TEXT("Structs use the native struct hierarchy glyph"),
		GetVerseDefinitionIconName(VerseSyntaxKind::Struct),
		FName(TEXT("GraphEditor.StructGlyph")));
	TestEqual(TEXT("Interfaces use the native Blueprint interface glyph"),
		GetVerseDefinitionIconName(VerseSyntaxKind::Interface),
		FName(TEXT("ClassIcon.BlueprintInterface")));
	TestEqual(TEXT("Enums use the native enum list glyph"),
		GetVerseDefinitionIconName(VerseSyntaxKind::Enum),
		FName(TEXT("GraphEditor.Enum_16x")));
	TestEqual(TEXT("Modules, enums, constants, and type aliases have distinct icons"),
		TSet<FName>({
			GetVerseDefinitionIconName(VerseSyntaxKind::Module),
			GetVerseDefinitionIconName(VerseSyntaxKind::Enum),
			GetVerseDefinitionIconName(VerseSyntaxKind::Constant),
			GetVerseDefinitionIconName(VerseSyntaxKind::TypeAlias)}).Num(),
		4);
	return true;
}

#endif
