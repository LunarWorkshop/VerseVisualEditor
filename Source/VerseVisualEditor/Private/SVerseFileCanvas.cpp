#include "SVerseFileCanvas.h"
#include "SVerseTile.h"

#include "VerseDocumentSession.h"
#include "VerseOrderedTilePacking.h"
#include "Styling/CoreStyle.h"
#include "VerseParseSnapshotBuilder.h"
#include "VerseVisualTile.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SPanel.h"
#include "Widgets/Text/SMultiLineEditableText.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SVerseFileCanvas"

namespace
{
	class SOrderedSquareTilePanel final : public SPanel
	{
	public:
		SLATE_BEGIN_ARGS(SOrderedSquareTilePanel) {}
		SLATE_END_ARGS()

		SOrderedSquareTilePanel()
			: Children(this)
		{
		}

		void Construct(const FArguments& InArgs)
		{
		}

		void AddTile(TSharedRef<SWidget> Tile)
		{
			FSlot::FSlotArguments SlotArguments(MakeUnique<FSlot>());
			SlotArguments.AttachWidget(Tile);
			Children.AddSlot(MoveTemp(SlotArguments));
		}

		virtual void OnArrangeChildren(
			const FGeometry& AllottedGeometry,
			FArrangedChildren& ArrangedChildren) const override
		{
			const FVerseOrderedTilePackingResult Layout = ComputeLayout();
			for (int32 Index = 0; Index < Children.Num(); ++Index)
			{
				const FSlot& Slot = Children[Index];
				const TSharedRef<SWidget> Widget = Slot.GetWidget();
				const EVisibility Visibility = Widget->GetVisibility();
				if (ArrangedChildren.Accepts(Visibility))
				{
					ArrangedChildren.AddWidget(
						Visibility,
						AllottedGeometry.MakeChild(
							Widget,
							Widget->GetDesiredSize(),
							FSlateLayoutTransform(Layout.Positions[Index])));
				}
			}
		}

		virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override
		{
			return ComputeLayout().Size;
		}

		virtual FChildren* GetChildren() override
		{
			return &Children;
		}

	private:
		struct FSlot : TSlotBase<FSlot>
		{
		};

		FVerseOrderedTilePackingResult ComputeLayout() const
		{
			TArray<FVector2D, TInlineAllocator<16>> Sizes;
			Sizes.Reserve(Children.Num());
			for (int32 Index = 0; Index < Children.Num(); ++Index)
			{
				Sizes.Add(Children[Index].GetWidget()->GetDesiredSize());
			}
			return PackVerseTilesApproximatelySquare(Sizes, 16.0f, 8.0f);
		}

		TPanelChildren<FSlot> Children;
	};

	bool BelongsInCompactStack(const FVerseVisualTile& Tile)
	{
		return Tile.Kind == EVerseVisualTileKind::Comment
			|| (Tile.Kind == EVerseVisualTileKind::Definition
				&& (Tile.DefinitionKind == VerseSyntaxKind::Constant
					|| Tile.DefinitionKind == VerseSyntaxKind::TypeAlias
					|| Tile.DefinitionKind == VerseSyntaxKind::Function));
	}

	FLinearColor GetVerseTypeColor(const FString& TypeName)
	{
		const FString Type = TypeName.TrimStartAndEnd().ToLower();
		if (Type == TEXT("logic"))
		{
			return FLinearColor(0.78f, 0.08f, 0.12f, 1.0f);
		}
		if (Type == TEXT("int"))
		{
			return FLinearColor(0.10f, 0.72f, 0.62f, 1.0f);
		}
		if (Type == TEXT("float"))
		{
			return FLinearColor(0.32f, 0.82f, 0.18f, 1.0f);
		}
		if (Type == TEXT("string") || Type == TEXT("message"))
		{
			return FLinearColor(0.92f, 0.18f, 0.62f, 1.0f);
		}
		if (Type == TEXT("char"))
		{
			return FLinearColor(0.42f, 0.85f, 0.35f, 1.0f);
		}
		if (Type == TEXT("void"))
		{
			return FLinearColor(0.72f, 0.72f, 0.72f, 1.0f);
		}
		return FLinearColor(0.24f, 0.58f, 1.0f, 1.0f);
	}

}

void SVerseFileCanvas::Construct(
	const FArguments& InArgs,
	TSharedRef<const FVerseDocumentSession> InSession,
	FVerseCanvasViewState InitialViewState,
	TOptional<FVerseTextRange> InitialSelectedRange,
	FOnVerseTileSelected InOnTileSelected,
	FSimpleDelegate InOnSelectionCleared)
{
	Snapshot.Emplace(InSession->GetParseSnapshot());
	Tiles = InSession->GetTiles();
	TileWidgets.Reset();
	Diagnostics = InArgs._Diagnostics;
	OnTileSelected = MoveTemp(InOnTileSelected);
	OnFunctionOpened = InArgs._OnFunctionOpened;
	OnSelectionCleared = MoveTemp(InOnSelectionCleared);
	if (InitialSelectedRange.IsSet())
	{
		Selection.Select(InitialSelectedRange.GetValue());
	}
	ChildSlot
	[
		SAssignNew(GraphSurface, SVerseGraphSurface, InitialViewState, false)
		.UseEdgePanPadding(true)
		.OnBackgroundClicked(FSimpleDelegate::CreateSP(this, &SVerseFileCanvas::ClearTileSelection))
		[
			BuildTileRow()
		]
	];
}

FVerseCanvasViewState SVerseFileCanvas::GetViewState() const
{
	return GraphSurface.IsValid() ? GraphSurface->GetViewState() : FVerseCanvasViewState{};
}

void SVerseFileCanvas::RefreshContent(
	TSharedRef<const FVerseDocumentSession> InSession,
	TOptional<FVerseTextRange> SelectedRange,
	TArray<FVerseCompilationDiagnostic> InDiagnostics)
{
	Snapshot.Emplace(InSession->GetParseSnapshot());
	Tiles = InSession->GetTiles();
	TileWidgets.Reset();
	Diagnostics = MoveTemp(InDiagnostics);
	Selection.Clear();
	if (SelectedRange.IsSet())
	{
		Selection.Select(SelectedRange.GetValue());
	}
	if (GraphSurface.IsValid())
	{
		GraphSurface->SetContent(BuildTileRow());
	}
}

bool SVerseFileCanvas::FocusTile(const FVerseVisualTile& Tile)
{
	TSharedPtr<SWidget> WidgetToFocus;
	int32 SmallestContainingRange = MAX_int32;
	for (const FTileWidgetEntry& Entry : TileWidgets)
	{
		const bool bExact = Entry.Range == Tile.Range;
		const bool bContains = Entry.Range.IsSet()
			&& Tile.Range.IsSet()
			&& Tile.Range.BeginByte >= Entry.Range.BeginByte
			&& Tile.Range.EndByte() <= Entry.Range.EndByte();
		if ((bExact || bContains) && Entry.Range.NumBytes < SmallestContainingRange)
		{
			if (const TSharedPtr<SWidget> Candidate = Entry.Widget.Pin())
			{
				WidgetToFocus = Candidate;
				SmallestContainingRange = Entry.Range.NumBytes;
				if (bExact)
				{
					break;
				}
			}
		}
	}
	if (!WidgetToFocus.IsValid())
	{
		return false;
	}

	Selection.Select(Tile.Range);
	OnTileSelected.ExecuteIfBound(Tile);
	GraphSurface->FocusWidget(WidgetToFocus, 20.0f);
	Invalidate(EInvalidateWidgetReason::Paint);
	return true;
}

TSharedRef<SWidget> SVerseFileCanvas::BuildTileRow()
{
	return BuildTileSequence(Tiles, INDEX_NONE, true);
}

TSharedRef<SWidget> SVerseFileCanvas::BuildTileSequence(
	TConstArrayView<FVerseVisualTile> TilesToBuild,
	int32 SharedDiagnosticTileIndex,
	bool bShowEmptyDocumentMessage)
{
	TSharedRef<SHorizontalBox> TileRow = SNew(SHorizontalBox);
	for (int32 TileIndex = 0; TileIndex < TilesToBuild.Num();)
	{
		const int32 DiagnosticTileIndex = SharedDiagnosticTileIndex == INDEX_NONE
			? TileIndex
			: SharedDiagnosticTileIndex;
		TSharedRef<SWidget> Presentation = SNullWidget::NullWidget;
		if (BelongsInCompactStack(TilesToBuild[TileIndex]))
		{
			TSharedRef<SOrderedSquareTilePanel> CompactStack =
				SNew(SOrderedSquareTilePanel);
			do
			{
				CompactStack->AddTile(
					BuildTile(
						TilesToBuild[TileIndex],
						SharedDiagnosticTileIndex == INDEX_NONE
							? TileIndex
							: SharedDiagnosticTileIndex));
				++TileIndex;
			}
			while (TileIndex < TilesToBuild.Num()
				&& BelongsInCompactStack(TilesToBuild[TileIndex]));
			Presentation = CompactStack;
		}
		else
		{
			Presentation = BuildTile(TilesToBuild[TileIndex], DiagnosticTileIndex);
			++TileIndex;
		}

		TileRow->AddSlot()
		.AutoWidth()
		.VAlign(VAlign_Top)
		.Padding(8.0f, 5.0f)
		[
			Presentation
		];
	}

	if (TilesToBuild.IsEmpty() && bShowEmptyDocumentMessage)
	{
		TileRow->AddSlot()
		.AutoWidth()
		.VAlign(VAlign_Top)
		.Padding(12.0f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("EmptyDocument", "This Verse file is empty."))
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
		];
	}
	return TileRow;
}

TSharedRef<SWidget> SVerseFileCanvas::BuildTile(const FVerseVisualTile& Tile, int32 TileIndex)
{
	const bool bCompactDefinition = Tile.Kind == EVerseVisualTileKind::Definition
		&& (Tile.DefinitionKind == VerseSyntaxKind::Constant
			|| Tile.DefinitionKind == VerseSyntaxKind::TypeAlias);
	TSharedRef<SWidget> Widget = bCompactDefinition
		? BuildCompactTile(Tile, TileIndex)
		: BuildStructuralTile(Tile, TileIndex);
	TileWidgets.Add({Tile.Range, Widget});
	return Widget;
}

TSharedRef<SWidget> SVerseFileCanvas::BuildStructuralTile(const FVerseVisualTile& Tile, int32 TileIndex)
{
	const bool bDefinition = Tile.Kind == EVerseVisualTileKind::Definition;
	const bool bComment = Tile.Kind == EVerseVisualTileKind::Comment;
	const FLinearColor TileColor = bDefinition
		? Tile.DefinitionKind == VerseSyntaxKind::Function
			? FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("2f4655")))
			: FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("182a36")))
		: bComment
			? FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("10251a")))
			: FLinearColor(0.35f, 0.20f, 0.08f, 1.0f);
	const bool bHasDiagnostic = HasDiagnosticForTile(TileIndex);
	const FVerseByteRange ContentRange = Tile.Kind == EVerseVisualTileKind::Unknown
		? Tile.Range
		: Tile.BodyRange;

	TSharedRef<SWidget> BodyContent = SNew(SMultiLineEditableText)
		.Text(Decode(ContentRange))
		.IsReadOnly(true)
		.AutoWrapText(true);
	if (bDefinition && Tile.DefinitionKind == VerseSyntaxKind::Module && !Tile.Children.IsEmpty())
	{
		BodyContent = BuildTileSequence(Tile.Children, TileIndex, false);
	}
	else if (bDefinition && Tile.DefinitionKind == VerseSyntaxKind::Function)
	{
		TSharedRef<SVerticalBox> FunctionBody = SNew(SVerticalBox);
		FunctionBody->AddSlot()
		.AutoHeight()
		[
			BuildFunctionSignature(Tile)
		];
		BodyContent = FunctionBody;
	}

	return SNew(SBox)
		.MaxDesiredWidth(bDefinition && Tile.DefinitionKind == VerseSyntaxKind::Module ? 2400.0f : 720.0f)
		[
			SNew(SVerseTile)
			.Tile(Tile)
			.Document(Snapshot->GetDocument())
			.DiagnosticText(bHasDiagnostic ? FormatDiagnosticMessages(TileIndex) : FText::GetEmpty())
			.TileColor(TileColor)
			.UnselectedOutlineColor(bHasDiagnostic
				? FLinearColor(1.0f, 0.08f, 0.04f, 1.0f)
				: FLinearColor::Black)
			.HeaderPadding(FMargin(0.0f, 6.0f, 8.0f, 6.0f))
			.ArrowPadding(FMargin(8.0f, 14.0f, 3.0f, 0.0f))
			.IsSelected_Lambda([this, Range = Tile.Range]()
			{
				return IsTileSelected(Range);
			})
			.OnSelected(FOnClicked::CreateSP(this, &SVerseFileCanvas::SelectTileFromClick, Tile))
			.OnOpened(Tile.DefinitionKind == VerseSyntaxKind::Function
				? FOnClicked::CreateSP(this, &SVerseFileCanvas::OpenFunctionTile, Tile)
				: FOnClicked())
			.BodyContent()
			[
				SNew(SBorder)
				.BorderImage(nullptr)
				.Padding(10.0f)
				[
					BodyContent
				]
			]
		];
}

TSharedRef<SWidget> SVerseFileCanvas::BuildFunctionSignature(const FVerseVisualTile& Tile) const
{
	TSharedRef<SVerticalBox> Signature = SNew(SVerticalBox);
	for (const FVerseVisualFunctionParameter& Parameter : Tile.FunctionParameters)
	{
		const FText ParameterName = Decode(Parameter.NameRange);
		const FText ParameterType = Decode(Parameter.TypeRange);
		FText UsageTooltip = LOCTEXT("UnusedParameterTooltip", "Unused parameter");
		if (Parameter.IsUsed())
		{
			FString Locations;
			for (const FVerseTextRange& Reference : Parameter.ReferenceRanges)
			{
				if (!Locations.IsEmpty())
				{
					Locations += TEXT("\n");
				}
				Locations += FString::Printf(
					TEXT("L%d"),
					Snapshot->GetDocument()->GetOriginalLineNumber(Reference.BeginByte));
			}
			UsageTooltip = FText::Format(
				LOCTEXT("UsedParameterTooltip", "Used at:\n{0}"),
				FText::FromString(Locations));
		}

		Signature->AddSlot()
		.AutoHeight()
		.Padding(0.0f, 1.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0.0f, 0.0f, 6.0f, 0.0f)
			[
				SNew(STextBlock)
				.Text(Parameter.IsUsed() ? FText::FromString(TEXT("●")) : FText::FromString(TEXT("○")))
				.ToolTipText(UsageTooltip)
				.ColorAndOpacity(Parameter.IsUsed()
					? FSlateColor(FLinearColor(0.25f, 0.85f, 0.35f, 1.0f))
					: FSlateColor::UseSubduedForeground())
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(ParameterName)
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(6.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(STextBlock)
				.Text(ParameterType.IsEmpty()
					? LOCTEXT("UntypedFunctionParameter", "untyped")
					: ParameterType)
				.ColorAndOpacity(GetVerseTypeColor(ParameterType.ToString()))
			]
		];
	}

	const FText ReturnType = Decode(Tile.TypeRange);
	Signature->AddSlot()
	.AutoHeight()
	.Padding(0.0f, Tile.FunctionParameters.IsEmpty() ? 0.0f : 5.0f, 0.0f, 0.0f)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		[
			SNew(STextBlock)
			.Text(LOCTEXT("FunctionReturnValue", "Return Value"))
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(6.0f, 0.0f, 0.0f, 0.0f)
		[
			SNew(STextBlock)
			.Text(ReturnType.IsEmpty() ? LOCTEXT("InferredReturnType", "inferred") : ReturnType)
			.ColorAndOpacity(GetVerseTypeColor(ReturnType.ToString()))
		]
	];
	return Signature;
}

TSharedRef<SWidget> SVerseFileCanvas::BuildCompactTile(const FVerseVisualTile& Tile, int32 TileIndex)
{
	const bool bHasDiagnostic = HasDiagnosticForTile(TileIndex);

	return SNew(SBox)
		.MinDesiredWidth(420.0f)
		[
		SNew(SVerseTile)
		.Tile(Tile)
		.Document(Snapshot->GetDocument())
		.Compact(true)
		.DiagnosticText(bHasDiagnostic ? FormatDiagnosticMessages(TileIndex) : FText::GetEmpty())
		.TileColor(FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("182a36"))))
		.UnselectedOutlineColor(bHasDiagnostic
			? FLinearColor(1.0f, 0.08f, 0.04f, 1.0f)
			: FLinearColor::Black)
		.HeaderPadding(FMargin(0.0f, 2.0f, 4.0f, 2.0f))
		.ArrowPadding(FMargin(4.0f, 3.0f, 3.0f, 0.0f))
		.IsSelected_Lambda([this, Range = Tile.Range]()
		{
			return IsTileSelected(Range);
		})
		.OnSelected(FOnClicked::CreateSP(this, &SVerseFileCanvas::SelectTileFromClick, Tile))
		.OnOpened(FOnClicked())
		.BodyContent()
		[
			SNew(SBorder)
			.BorderImage(nullptr)
			.Padding(8.0f)
			[
				SNew(SMultiLineEditableText)
				.Text(Decode(Tile.Range))
				.IsReadOnly(true)
			]
		]
		];
}

FText SVerseFileCanvas::Decode(FVerseByteRange Range) const
{
	return Range.IsSet()
		? FText::FromString(Snapshot->GetDocument()->DecodeOriginalRange(Range))
		: FText::GetEmpty();
}

FText SVerseFileCanvas::FormatDiagnosticMessages(int32 TileIndex) const
{
	FString Messages;
	for (const FVerseCompilationDiagnostic& Diagnostic : Diagnostics)
	{
		if (!Diagnostic.AffectedTileIndices.Contains(TileIndex))
		{
			continue;
		}
		if (!Messages.IsEmpty())
		{
			Messages += TEXT("\n");
		}
		Messages += FString::Printf(
			TEXT("V%u: %s"),
			Diagnostic.ReferenceCode,
			*Diagnostic.Message);
	}
	return FText::FromString(MoveTemp(Messages));
}

bool SVerseFileCanvas::HasDiagnosticForTile(int32 TileIndex) const
{
	return Diagnostics.ContainsByPredicate([TileIndex](const FVerseCompilationDiagnostic& Diagnostic)
	{
		return Diagnostic.AffectedTileIndices.Contains(TileIndex);
	});
}

bool SVerseFileCanvas::IsTileSelected(FVerseTextRange TileRange) const
{
	const TOptional<FVerseTextRange>& Selected = Selection.GetSelectedRange();
	if (!Selected.IsSet())
	{
		return false;
	}
	const FVerseTextRange& SelectedRange = Selected.GetValue();
	return SelectedRange.IsSet()
		&& TileRange.IsSet()
		&& SelectedRange.Revision == TileRange.Revision
		&& SelectedRange.BeginByte == TileRange.BeginByte
		&& SelectedRange.NumBytes == TileRange.NumBytes;
}

void SVerseFileCanvas::SelectTile(const FVerseVisualTile& Tile)
{
	Selection.Select(Tile.Range);
	OnTileSelected.ExecuteIfBound(Tile);
	Invalidate(EInvalidateWidgetReason::Paint);
	return;
}

void SVerseFileCanvas::ClearTileSelection()
{
	Selection.Clear();
	OnSelectionCleared.ExecuteIfBound();
	Invalidate(EInvalidateWidgetReason::Paint);
}

FReply SVerseFileCanvas::SelectTileFromClick(FVerseVisualTile Tile)
{
	SelectTile(Tile);
	return FReply::Handled();
}

FReply SVerseFileCanvas::OpenFunctionTile(FVerseVisualTile Tile)
{
	SelectTile(Tile);
	OnFunctionOpened.ExecuteIfBound(Tile);
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
