#include "VerseFunctionNavigation.h"

#include "VerseParseSnapshot.h"
#include "VerseParseSnapshotBuilder.h"
#include "VerseVisualTile.h"

namespace
{
	bool IntroducesScope(FName DefinitionKind)
	{
		return DefinitionKind == VerseSyntaxKind::Module
			|| DefinitionKind == VerseSyntaxKind::Class
			|| DefinitionKind == VerseSyntaxKind::Struct
			|| DefinitionKind == VerseSyntaxKind::Interface;
	}

	void BuildItems(
		TConstArrayView<FVerseVisualTile> Tiles,
		const FVerseParseSnapshot& Snapshot,
		const TArray<FString>& ParentPath,
		TArray<FVerseFunctionNavigationItem>& OutItems)
	{
		for (const FVerseVisualTile& Tile : Tiles)
		{
			TArray<FString> ChildPath = ParentPath;
			FString Name;
			if (Tile.Kind == EVerseVisualTileKind::Definition && Tile.NameRange.IsSet())
			{
				Name = Snapshot.GetDocument()->DecodeOriginalRange(Tile.NameRange);
				if (IntroducesScope(Tile.DefinitionKind))
				{
					ChildPath.Add(Name);
				}
				else if (Tile.DefinitionKind == VerseSyntaxKind::Function)
				{
					FVerseFunctionNavigationItem& Item = OutItems.AddDefaulted_GetRef();
					Item.Name = Name;
					Item.ScopePath = ParentPath;
					Item.ScopePath.Add(Name);
					Item.FunctionRange = Tile.Range;
					Item.DeclarationRange = Tile.HeaderRange;
					Item.BodyRange = Tile.BodyRange;
					Item.BodyItems = Tile.BodyClause.Items;
					Item.ReturnTypeRange = Tile.TypeRange;
					for (const FVerseVisualFunctionParameter& Parameter : Tile.FunctionParameters)
					{
						FVerseFunctionNavigationParameter& NavigationParameter =
							Item.Parameters.AddDefaulted_GetRef();
						NavigationParameter.NameRange = Parameter.NameRange;
						NavigationParameter.TypeRange = Parameter.TypeRange;
					}
					if (Tile.HeaderRange.IsSet())
					{
						Item.FirstDeclarationLine = Snapshot.GetDocument()->GetOriginalLineNumber(
							Tile.HeaderRange.BeginByte);
						Item.LastDeclarationLine = Snapshot.GetDocument()->GetOriginalLineNumber(
							FMath::Max(Tile.HeaderRange.BeginByte, Tile.HeaderRange.EndByte() - 1));
					}
				}
			}

			BuildItems(Tile.Children, Snapshot, ChildPath, OutItems);
		}
	}

	bool FindPath(
		TConstArrayView<FVerseVisualTile> Tiles,
		const FVerseParseSnapshot& Snapshot,
		FVerseTextRange DefinitionRange,
		const TArray<FString>& ParentPath,
		TArray<FString>& OutPath)
	{
		for (const FVerseVisualTile& Tile : Tiles)
		{
			TArray<FString> TilePath = ParentPath;
			if (Tile.Kind == EVerseVisualTileKind::Definition && Tile.NameRange.IsSet())
			{
				const FString Name = Snapshot.GetDocument()->DecodeOriginalRange(Tile.NameRange);
				if (IntroducesScope(Tile.DefinitionKind)
					|| Tile.DefinitionKind == VerseSyntaxKind::Function)
				{
					TilePath.Add(Name);
				}
			}
			if (Tile.Range == DefinitionRange)
			{
				OutPath = MoveTemp(TilePath);
				return true;
			}
			if (FindPath(Tile.Children, Snapshot, DefinitionRange, TilePath, OutPath))
			{
				return true;
			}
		}
		return false;
	}
}

TArray<FVerseFunctionNavigationItem> FVerseFunctionNavigationBuilder::Build(
	TConstArrayView<FVerseVisualTile> Tiles,
	const FVerseParseSnapshot& Snapshot)
{
	TArray<FVerseFunctionNavigationItem> Items;
	BuildItems(Tiles, Snapshot, {}, Items);
	return Items;
}

bool FVerseFunctionNavigationBuilder::FindDefinitionPath(
	TConstArrayView<FVerseVisualTile> Tiles,
	const FVerseParseSnapshot& Snapshot,
	FVerseTextRange DefinitionRange,
	TArray<FString>& OutPath)
{
	OutPath.Reset();
	return FindPath(Tiles, Snapshot, DefinitionRange, {}, OutPath);
}
