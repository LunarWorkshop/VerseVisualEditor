#include "Slate/SVerseVisualEditor.h"

#include "Slate/SVerseLiteralEditor.h"

#include "Slate/SVerseFunctionCanvas.h"
#include "Slate/SVerseFunctionGraphLayout.h"
#include "Slate/SVerseFileCanvas.h"
#include "Slate/SVerseTile.h"

#include "Algo/AllOf.h"
#include "Async/Async.h"
#include "DirectoryWatcherModule.h"
#include "DesktopPlatformModule.h"
#include "EdGraph/EdGraphSchema.h"
#include "Editor/UnrealEdEngine.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "FileHelpers.h"
#include "GraphEditorSettings.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "IDirectoryWatcher.h"
#include "IDesktopPlatform.h"
#include "ISourceControlModule.h"
#include "ISourceControlProvider.h"
#include "ISourceControlState.h"
#include "ISolarisEditorModule.h"
#include "SolarisLoadCompilerModule.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/MessageDialog.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "SGraphActionMenu.h"
#include "SGraphPalette.h"
#include "UnrealEdGlobals.h"
#include "VerseDocument.h"
#include "VerseParseSnapshotBuilder.h"
#include "Editing/VerseClauseEditing.h"
#include "Editing/VerseIntrinsicPresentation.h"
#include "Document/VerseDocumentSession.h"
#include "Slate/VerseDefinitionIcon.h"
#include "Editing/VerseExpressionActions.h"
#include "Document/VerseExternalChange.h"
#include "VisualModel/VerseFunctionNavigation.h"
#include "Editing/VerseIdentifier.h"
#include "Editing/VerseProvisionalState.h"
#include "Semantics/VerseSemanticCandidates.h"
#include "Semantics/VerseSemanticWorkspace.h"
#include "Editing/VerseSpecifier.h"
#include "VisualModel/VerseTileProperties.h"
#include "VisualModel/VerseVisualTile.h"
#include "VerseVisualEditorSettings.h"
#include "Slate/VerseVisualEditorStyle.h"
#include "Infrastructure/VerseVisualEditorLifetimeDiagnostics.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Input/SSearchableComboBox.h"
#include "Widgets/Input/SSegmentedControl.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Views/STableRow.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Text/SMultiLineEditableText.h"
#include "Widgets/Text/STextBlock.h"

#include "Slate/SVerseVisualEditorInternal.h"

#define LOCTEXT_NAMESPACE "SVerseVisualEditor"

namespace VerseVisualEditorPrivate
{
	FLinearColor GetBlueprintPinColor(const FString& VerseType)
	{
		return VerseVisualEditorStyle::GetTypeColor(VerseType);
	}

	FString GetVisualTypeName(
		const FVerseTextRange& TypeRange,
		FName IntrinsicTypeName,
		const FVerseDocument& Document,
		FStringView SemanticTypeName)
	{
		return !SemanticTypeName.IsEmpty()
			? FString(SemanticTypeName)
			: TypeRange.IsSet()
			? Document.DecodeOriginalRange(TypeRange).TrimStartAndEnd()
			: IntrinsicTypeName.ToString();
	}

	const FVerseVisualTile* FindTileByRange(
		TConstArrayView<FVerseVisualTile> Tiles,
		FVerseTextRange Range)
	{
		for (const FVerseVisualTile& Tile : Tiles)
		{
			if (Tile.Range == Range)
			{
				return &Tile;
			}
			if (const FVerseVisualTile* Nested = FindTileByRange(Tile.Children, Range))
			{
				return Nested;
			}
		}
		return nullptr;
	}

	void ApplyProvisionalState(
		TArray<FVerseVisualTile>& Tiles,
		const FVerseProvisionalState& ProvisionalTiles)
	{
		for (FVerseVisualTile& Tile : Tiles)
		{
			Tile.bIsProvisional = ProvisionalTiles.Contains(Tile.Range);
			ApplyProvisionalState(Tile.Children, ProvisionalTiles);
		}
	}

	const FVerseFunctionNavigationItem* FindFunctionNavigationItem(
		TConstArrayView<FVerseFunctionNavigationItem> Items,
		const FOpenVerseFunctionTab& Tab)
	{
		if (const FVerseFunctionNavigationItem* ByPath = Items.FindByPredicate(
			[&Tab](const FVerseFunctionNavigationItem& Item)
			{
				return Item.ScopePath == Tab.ScopePath;
			}))
		{
			return ByPath;
		}
		return Items.FindByPredicate([&Tab](const FVerseFunctionNavigationItem& Item)
		{
			return Item.FunctionRange.BeginByte == Tab.FunctionRange.BeginByte;
		});
	}

	TSharedPtr<const FVerseSemanticSnapshot> FindExactSemanticSnapshot(
		const FVerseSemanticWorkspace* Workspace,
		const FOpenVerseDocument& Document)
	{
		if (Workspace == nullptr || !Document.Session.IsValid())
		{
			return nullptr;
		}
		for (const TSharedPtr<const FVerseSemanticSnapshot>& Snapshot :
			Workspace->GetCandidateSnapshots())
		{
			if (Snapshot.IsValid()
				&& Snapshot->Describes(Document.FilePath, Document.Session->GetRevision()))
			{
				return Snapshot;
			}
		}
		return nullptr;
	}

	void BindGraphTiles(
		FOpenVerseDocument& Document,
		TArray<FVerseVisualTile>& GraphTiles,
		const TSharedPtr<const FVerseSemanticSnapshot>& Snapshot)
	{
		if (Document.Session.IsValid() && Snapshot.IsValid())
		{
			FVerseSemanticCandidateProvider::BindFunctionGraph(
				GraphTiles,
				Snapshot,
				Document.FilePath,
				*Document.Session->GetParseSnapshot().GetDocument());
		}
	}

	void ReconcileFunctionTabs(
		FOpenVerseDocument& Document,
		const TSharedPtr<const FVerseSemanticSnapshot>& SemanticSnapshot)
	{
		if (!Document.Session.IsValid())
		{
			Document.FunctionTabs.Reset();
			Document.ActiveFunctionTabIndex = INDEX_NONE;
			return;
		}
		Document.ProvisionalTiles.Rebase(
			Document.Session->GetParseSnapshot().GetDocument()->GetOriginalUtf8View(),
			Document.Session->GetRevision());

		const TArray<FVerseFunctionNavigationItem> Items = FVerseFunctionNavigationBuilder::Build(
			Document.Session->GetTiles(),
			Document.Session->GetParseSnapshot());
		for (int32 Index = Document.FunctionTabs.Num() - 1; Index >= 0; --Index)
		{
			FOpenVerseFunctionTab& Tab = Document.FunctionTabs[Index];
			const FVerseFunctionNavigationItem* Item = FindFunctionNavigationItem(Items, Tab);
			if (!Item)
			{
				Document.FunctionTabs.RemoveAt(Index);
				if (Document.ActiveFunctionTabIndex == Index)
				{
					Document.ActiveFunctionTabIndex = INDEX_NONE;
				}
				else if (Document.ActiveFunctionTabIndex > Index)
				{
					--Document.ActiveFunctionTabIndex;
				}
				continue;
			}
			Tab.Name = Item->Name;
			Tab.ScopePath = Item->ScopePath;
			Tab.FunctionRange = Item->FunctionRange;
			Tab.DeclarationRange = Item->DeclarationRange;
			Tab.BodyRange = Item->BodyRange;
			Tab.ReturnTypeRange = Item->ReturnTypeRange;
			Tab.Parameters = Item->Parameters;
			Tab.GraphTiles = Item->GraphTiles;
			BindGraphTiles(Document, Tab.GraphTiles, SemanticSnapshot);
			Tab.GraphRevision = Document.Session->GetRevision();
			Tab.bGraphUsesExactSemanticSnapshot = SemanticSnapshot.IsValid()
				&& SemanticSnapshot->Describes(
					Document.FilePath, Document.Session->GetRevision());
			// Provisional is transient editor state, so reapply it after every
			// parse/semantic graph reconstruction rather than deriving it from source.
			ApplyProvisionalState(Tab.GraphTiles, Document.ProvisionalTiles);
			Tab.FirstDeclarationLine = Item->FirstDeclarationLine;
			Tab.LastDeclarationLine = Item->LastDeclarationLine;
		}
	}
}

#undef LOCTEXT_NAMESPACE
