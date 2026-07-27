#include "VerseOutliner.h"

#include "VerseParseSnapshot.h"
#include "VerseParseSnapshotBuilder.h"
#include "VerseVisualTile.h"

namespace
{
	FString Decode(const FVerseParseSnapshot& Snapshot, FVerseByteRange Range)
	{
		return Range.IsSet()
			? Snapshot.GetDocument()->DecodeOriginalRange(Range)
			: FString();
	}

	FString BuildFunctionLabel(
		const FVerseVisualTile& Tile,
		const FVerseParseSnapshot& Snapshot)
	{
		FString Label = Decode(Snapshot, Tile.NameRange) + TEXT("(");
		for (int32 Index = 0; Index < Tile.FunctionParameters.Num(); ++Index)
		{
			if (Index > 0)
			{
				Label += TEXT(", ");
			}
			const FVerseVisualFunctionParameter& Parameter = Tile.FunctionParameters[Index];
			Label += Decode(Snapshot, Parameter.NameRange);
			const FString Type = Decode(Snapshot, Parameter.TypeRange);
			if (!Type.IsEmpty())
			{
				Label += TEXT(" : ") + Type;
			}
		}
		Label += TEXT(")");
		const FString ReturnType = Decode(Snapshot, Tile.TypeRange);
		if (!ReturnType.IsEmpty())
		{
			Label += TEXT(" : ") + ReturnType;
		}
		return Label;
	}

	FString BuildLabel(const FVerseVisualTile& Tile, const FVerseParseSnapshot& Snapshot)
	{
		const FString Name = Decode(Snapshot, Tile.NameRange);
		if (Tile.DefinitionKind == VerseSyntaxKind::Function)
		{
			return BuildFunctionLabel(Tile, Snapshot);
		}
		if (Tile.DefinitionKind == VerseSyntaxKind::Constant)
		{
			const FString Type = Decode(Snapshot, Tile.TypeRange);
			return Type.IsEmpty() ? Name + TEXT(" :=") : Name + TEXT(" : ") + Type;
		}
		if (Tile.DefinitionKind == VerseSyntaxKind::TypeAlias)
		{
			return Name + TEXT(" := type");
		}
		return Name + TEXT(" := ") + Tile.DefinitionKind.ToString().ToLower();
	}

	bool IsOutlinerDefinition(FName Kind)
	{
		return Kind == VerseSyntaxKind::Module
			|| Kind == VerseSyntaxKind::Class
			|| Kind == VerseSyntaxKind::Struct
			|| Kind == VerseSyntaxKind::Interface
			|| Kind == VerseSyntaxKind::Enum
			|| Kind == VerseSyntaxKind::Function
			|| Kind == VerseSyntaxKind::Constant
			|| Kind == VerseSyntaxKind::TypeAlias;
	}

	TArray<TSharedPtr<FVerseOutlinerItem>> BuildItems(
		TConstArrayView<FVerseVisualTile> Tiles,
		const FVerseParseSnapshot& Snapshot)
	{
		TArray<TSharedPtr<FVerseOutlinerItem>> Items;
		for (const FVerseVisualTile& Tile : Tiles)
		{
			if (Tile.Kind != EVerseVisualTileKind::Definition
				|| !IsOutlinerDefinition(Tile.DefinitionKind)
				|| !Tile.NameRange.IsSet())
			{
				continue;
			}

			TSharedPtr<FVerseOutlinerItem> Item = MakeShared<FVerseOutlinerItem>();
			Item->Name = Snapshot.GetDocument()->DecodeOriginalRange(Tile.NameRange);
			Item->Label = BuildLabel(Tile, Snapshot);
			Item->DefinitionKind = Tile.DefinitionKind;
			Item->TileRange = Tile.Range;
			Item->Children = BuildItems(Tile.Children, Snapshot);
			Items.Add(MoveTemp(Item));
		}
		return Items;
	}
}

TArray<TSharedPtr<FVerseOutlinerItem>> FVerseOutlinerBuilder::Build(
	TConstArrayView<FVerseVisualTile> Tiles,
	const FVerseParseSnapshot& Snapshot)
{
	return BuildItems(Tiles, Snapshot);
}
