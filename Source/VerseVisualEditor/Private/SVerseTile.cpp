#include "SVerseTile.h"

#include "Brushes/SlateRoundedBoxBrush.h"
#include "GraphEditorSettings.h"
#include "Rendering/DrawElements.h"
#include "Settings/EditorStyleSettings.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "VerseDefinitionIcon.h"
#include "VerseDocument.h"
#include "VerseParseSnapshotBuilder.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SVerseTile"

namespace
{
	float GetVerseGraphMajorGridWidth()
	{
		const float GridSize = static_cast<float>(GetDefault<UEditorStyleSettings>()->GridSnapSize);
		const float RulePeriod = FMath::Max(1.0f, FAppStyle::GetFloat(TEXT("Graph.Panel.GridRulePeriod")));
		return GridSize * RulePeriod;
	}

	FLinearColor GetVerseTilePinColor(const FString& VerseType)
	{
		const UGraphEditorSettings* Settings = GetDefault<UGraphEditorSettings>();
		FString Type = VerseType.TrimStartAndEnd().ToLower();
		while (Type.RemoveFromStart(TEXT("?")) || Type.RemoveFromStart(TEXT("[]")))
		{
		}
		if (Type == TEXT("logic")) return Settings->BooleanPinTypeColor;
		if (Type == TEXT("int")) return Settings->IntPinTypeColor;
		if (Type == TEXT("float")) return Settings->FloatPinTypeColor;
		if (Type == TEXT("string")) return Settings->StringPinTypeColor;
		if (Type == TEXT("message")) return Settings->TextPinTypeColor;
		if (Type == TEXT("char")) return Settings->BytePinTypeColor;
		if (Type == TEXT("type")) return Settings->ClassPinTypeColor;
		return Settings->ObjectPinTypeColor;
	}

	const FSlateBrush* GetVerseTilePinBrush(const FString& VerseType, bool bConnected)
	{
		const bool bArray = VerseType.TrimStartAndEnd().StartsWith(TEXT("[]"));
		return FAppStyle::GetBrush(bArray
			? (bConnected ? "Graph.ArrayPin.Connected" : "Graph.ArrayPin.Disconnected")
			: (bConnected ? "Graph.Pin.Connected" : "Graph.Pin.Disconnected"));
	}

	class SVerseTileExecutionPin final : public SLeafWidget
	{
	public:
		SLATE_BEGIN_ARGS(SVerseTileExecutionPin) {}
			SLATE_ARGUMENT(bool, Input)
			SLATE_ARGUMENT(bool, Connected)
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs)
		{
			bInput = InArgs._Input;
			bConnected = InArgs._Connected;
			SetCanTick(false);
		}

		virtual FVector2D ComputeDesiredSize(float) const override
		{
			return bInput ? FVector2D(48.0f, 32.0f) : FVector2D(24.0f, 48.0f);
		}

		virtual int32 OnPaint(
			const FPaintArgs& Args,
			const FGeometry& AllottedGeometry,
			const FSlateRect& MyCullingRect,
			FSlateWindowElementList& OutDrawElements,
			int32 LayerId,
			const FWidgetStyle& InWidgetStyle,
			bool bParentEnabled) const override
		{
			const FVector2D PinCenter = bInput ? FVector2D(24.0f, 24.0f) : FVector2D(12.0f, 8.0f);
			const FSlateBrush* PinBrush = FAppStyle::GetBrush(
				bConnected ? "Graph.ExecPin.Connected" : "Graph.ExecPin.Disconnected");
			const FVector2D PinSize = PinBrush->ImageSize;
			FSlateDrawElement::MakeRotatedBox(
				OutDrawElements,
				LayerId,
				AllottedGeometry.ToPaintGeometry(
					PinSize,
					FSlateLayoutTransform(PinCenter - PinSize * 0.5f)),
				PinBrush,
				ESlateDrawEffect::None,
				HALF_PI,
				PinSize * 0.5f,
				FSlateDrawElement::RelativeToElement,
				InWidgetStyle.GetColorAndOpacityTint());
			return LayerId;
		}

	private:
		bool bInput = false;
		bool bConnected = false;
	};
}

void SVerseTile::Construct(const FArguments& InArgs)
{
	constexpr float BlueprintCornerRadius = 6.0f;
	constexpr float InnerCornerRadius = BlueprintCornerRadius - 1.0f;
	OuterBrush = MakeUnique<FSlateRoundedBoxBrush>(FLinearColor::White, BlueprintCornerRadius);
	ExpandedHeaderBrush = MakeUnique<FSlateRoundedBoxBrush>(
		FLinearColor::White,
		FVector4(InnerCornerRadius, InnerCornerRadius, 0.0f, 0.0f));
	CollapsedHeaderBrush = MakeUnique<FSlateRoundedBoxBrush>(FLinearColor::White, InnerCornerRadius);
	BodyBrush = MakeUnique<FSlateRoundedBoxBrush>(
		FLinearColor::White,
		FVector4(0.0f, 0.0f, InnerCornerRadius, InnerCornerRadius));
	Tile = InArgs._Tile;
	Document = InArgs._Document;
	IsSelected = InArgs._IsSelected;
	OnSelected = InArgs._OnSelected;
	OnOpened = InArgs._OnOpened;
	OnSocketDragStarted = InArgs._OnSocketDragStarted;
	OnInlineLiteralCommitted = InArgs._OnInlineLiteralCommitted;
	UnselectedOutlineColor = InArgs._UnselectedOutlineColor;
	bShowBody = InArgs._ShowBody;
	bCollapsible = bShowBody && !(
		Tile.Kind == EVerseVisualTileKind::Expression
		&& Tile.OperatorRange.IsSet());

	TSharedRef<SVerticalBox> TileWithExecution = SNew(SVerticalBox);
	if (Tile.bHasExecutionInput)
	{
		TileWithExecution->AddSlot()
		.AutoHeight()
		.HAlign(HAlign_Left)
		[
			SAssignNew(ExecutionInputAnchor, SVerseTileExecutionPin)
			.Input(true)
			.Connected(Tile.bExecutionInputConnected)
		];
	}
	const bool bOperatorTile = Tile.Kind == EVerseVisualTileKind::Expression
		&& Tile.OperatorRange.IsSet();
	const FText OperatorLines = bOperatorTile ? GetLineText() : FText::GetEmpty();

	TSharedRef<SBorder> TileSurface =
		SNew(SBorder)
		.OnMouseButtonDown(this, &SVerseTile::HandleTileMouseButtonDown)
		.BorderImage(OuterBrush.Get())
		.BorderBackgroundColor(this, &SVerseTile::GetOutlineColor)
		.Padding(1.0f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SBorder)
				.BorderImage(this, &SVerseTile::GetHeaderBrush)
				.BorderBackgroundColor(InArgs._TileColor)
				.Padding(0.0f)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SAssignNew(OperatorLineWidget, STextBlock)
						.Visibility(bOperatorTile && !OperatorLines.IsEmpty()
							? EVisibility::Visible
							: EVisibility::Collapsed)
						.Text(OperatorLines)
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
						.ColorAndOpacity(FLinearColor(0.52f, 0.58f, 0.64f, 1.0f))
						.Margin(FMargin(5.0f, 2.0f, 0.0f, 0.0f))
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
					SAssignNew(HeaderSocketRow, SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						BuildSocketColumn(Tile.ValueInputs, false)
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Top)
					.Padding(InArgs._ArrowPadding)
					[
						SNew(SButton)
						.Visibility(bCollapsible ? EVisibility::Visible : EVisibility::Collapsed)
						.ButtonStyle(FCoreStyle::Get(), "NoBorder")
						.ContentPadding(0.0f)
						.OnClicked(this, &SVerseTile::ToggleExpanded)
						[
							SNew(SImage).Image(this, &SVerseTile::GetExpansionImage)
						]
					]
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					[
						SNew(SBorder)
						.OnMouseButtonDown(this, &SVerseTile::HandleHeaderMouseButtonDown)
						.BorderImage(FCoreStyle::Get().GetBrush("NoBorder"))
						.Padding(InArgs._HeaderPadding)
						[
							BuildHeader(InArgs._Compact, InArgs._DiagnosticText)
						]
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						BuildSocketColumn(Tile.ValueOutputs, true)
					]
					]
				]
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SBorder)
				.Visibility(this, &SVerseTile::GetBodyVisibility)
				.BorderImage(BodyBrush.Get())
				.BorderBackgroundColor(FLinearColor(0.025f, 0.025f, 0.035f, 1.0f))
				.Padding(0.0f)
				[
					InArgs._BodyContent.Widget
				]
			]
		]
	;

	TSharedRef<SOverlay> TileAndOutput = SNew(SOverlay);
	TileAndOutput->AddSlot()
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			TileSurface
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SBox)
			.HeightOverride(Tile.bHasExecutionOutput ? 41.0f : 0.0f)
		]
	];

	if (Tile.bHasExecutionOutput)
	{
		TileAndOutput->AddSlot()
		.HAlign(HAlign_Left)
		.VAlign(VAlign_Bottom)
		.Padding(12.0f, 0.0f, 0.0f, 0.0f)
		[
			SAssignNew(ExecutionOutputAnchor, SVerseTileExecutionPin)
			.Input(false)
			.Connected(Tile.bExecutionOutputConnected)
		];
	}

	TileWithExecution->AddSlot()
	.AutoHeight()
	.Padding(0.0f, Tile.bHasExecutionInput ? -8.0f : 0.0f, 0.0f, 0.0f)
	[
		TileAndOutput
	];
	ChildSlot[TileWithExecution];
}

float SVerseTile::GetValueSocketCenterY(int32 SocketIndex, bool bOutput) const
{
	// Execution input consumes 32 Slate units, while the tile surface overlaps
	// it by 8. The outer one-unit outline precedes the header contents. Both
	// value-pin columns are vertically centered in HeaderSocketRow.
	const float ExecutionOffset = Tile.bHasExecutionInput ? 24.0f : 0.0f;
	const float OperatorLineHeight = OperatorLineWidget.IsValid()
		? OperatorLineWidget->GetDesiredSize().Y
		: 0.0f;
	const float HeaderRowHeight = HeaderSocketRow.IsValid()
		? HeaderSocketRow->GetDesiredSize().Y
		: 0.0f;
	const TSharedPtr<SWidget>& Column = bOutput ? ValueOutputColumn : ValueInputColumn;
	const TArray<TSharedPtr<SWidget>>& Rows = bOutput ? ValueOutputRows : ValueInputRows;
	if (!Column.IsValid() || !Rows.IsValidIndex(SocketIndex))
	{
		return ExecutionOffset + 1.0f + OperatorLineHeight + HeaderRowHeight * 0.5f;
	}

	float RowCenter = 0.0f;
	for (int32 Index = 0; Index < SocketIndex; ++Index)
	{
		RowCenter += Rows[Index]->GetDesiredSize().Y + 2.0f;
	}
	RowCenter += 1.0f + Rows[SocketIndex]->GetDesiredSize().Y * 0.5f;
	const float ColumnTop = (HeaderRowHeight - Column->GetDesiredSize().Y) * 0.5f;
	return ExecutionOffset + 1.0f + OperatorLineHeight + ColumnTop + RowCenter;
}

TSharedRef<SWidget> SVerseTile::BuildHeader(bool bCompact, const FText& DiagnosticText) const
{
	const FText Kind = GetKindText();
	const FText Name = GetNameText();
	const FText Type = GetTypeText();
	const FText Lines = GetLineText();
	TSharedRef<SVerticalBox> Header = SNew(SVerticalBox);
	if (Tile.Kind == EVerseVisualTileKind::Expression && Tile.OperatorRange.IsSet())
	{
		Header->AddSlot()
		.AutoHeight()
		[
			SNew(SBox)
			.MinDesiredWidth(72.0f)
			[
				SNew(STextBlock)
				.Text(Decode(Tile.OperatorRange))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 22))
				.Justification(ETextJustify::Center)
				.Margin(FMargin(0.0f, 2.0f))
			]
		];
		if (!DiagnosticText.IsEmpty())
		{
			Header->AddSlot().AutoHeight().Padding(0.0f, 4.0f, 0.0f, 0.0f)
			[
				SNew(STextBlock)
				.Text(DiagnosticText)
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
				.ColorAndOpacity(FLinearColor(1.0f, 0.20f, 0.12f, 1.0f))
				.AutoWrapText(true)
			];
		}
		return Header;
	}
	Header->AddSlot()
	.AutoHeight()
	[
		SNew(SHorizontalBox)
		.Visibility(Kind.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 5.0f, 0.0f)
		[
			SNew(SImage)
			.Visibility(GetIcon() ? EVisibility::Visible : EVisibility::Collapsed)
			.Image(GetIcon())
			.DesiredSizeOverride(FVector2D(16.0f, 16.0f))
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text(Kind)
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
			.ColorAndOpacity(FLinearColor(0.65f, 0.80f, 1.0f, 1.0f))
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(bCompact ? 10.0f : 0.0f, 0.0f)
		[
			SNew(STextBlock)
			.Visibility(bCompact && !Name.IsEmpty() ? EVisibility::Visible : EVisibility::Collapsed)
			.Text(Name)
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 11))
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(bCompact ? 8.0f : 0.0f, 0.0f)
		[
			SNew(STextBlock)
			.Visibility(bCompact && !Type.IsEmpty() ? EVisibility::Visible : EVisibility::Collapsed)
			.Text(Type.IsEmpty() ? FText::GetEmpty() : FText::Format(LOCTEXT("CompactType", ": {0}"), Type))
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
		]
	];
	if (!bCompact && !Name.IsEmpty())
	{
		Header->AddSlot().AutoHeight().Padding(0.0f, 2.0f, 0.0f, 0.0f)
		[
			SNew(STextBlock).Text(Name).Font(FCoreStyle::GetDefaultFontStyle("Bold", 12))
		];
	}
	if (!Lines.IsEmpty())
	{
		const float LineLeftPadding = bShowBody ? -19.0f : 0.0f;
		Header->AddSlot().AutoHeight().Padding(LineLeftPadding, 6.0f, 0.0f, 0.0f)
		[
			SNew(STextBlock)
			.Text(Lines)
			.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
			.ColorAndOpacity(FLinearColor(0.52f, 0.58f, 0.64f, 1.0f))
		];
	}
	if (!bCompact && !Type.IsEmpty() && Tile.Kind == EVerseVisualTileKind::Definition)
	{
		Header->AddSlot().AutoHeight()
		[
			SNew(STextBlock)
			.Text(FText::Format(LOCTEXT("DefinitionType", "Type: {0}"), Type))
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
		];
	}
	if (!DiagnosticText.IsEmpty())
	{
		Header->AddSlot().AutoHeight().Padding(-19.0f, 4.0f, 0.0f, 0.0f)
		[
			SNew(STextBlock)
			.Text(DiagnosticText)
			.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
			.ColorAndOpacity(FLinearColor(1.0f, 0.20f, 0.12f, 1.0f))
			.AutoWrapText(true)
		];
	}
	return Header;
}

TSharedRef<SWidget> SVerseTile::BuildSocketColumn(
	TConstArrayView<FVerseVisualSocket> Sockets,
	bool bOutput)
{
	TSharedRef<SVerticalBox> Column = SNew(SVerticalBox);
	TArray<TSharedPtr<SWidget>>& Rows = bOutput ? ValueOutputRows : ValueInputRows;
	Rows.Reset();
	for (int32 SocketIndex = 0; SocketIndex < Sockets.Num(); ++SocketIndex)
	{
		const FVerseVisualSocket& Socket = Sockets[SocketIndex];
		const FString Type = !Socket.SemanticTypeName.IsEmpty()
			? Socket.SemanticTypeName
			: Socket.TypeRange.IsSet()
			? Decode(Socket.TypeRange).ToString()
			: Socket.IntrinsicTypeName.ToString();
		const FText Name = !Socket.SemanticName.IsEmpty()
			? FText::FromString(Socket.SemanticName)
			: Decode(Socket.NameRange);
		TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);
		Rows.Add(Row);
		auto AddPin = [&]()
		{
			TSharedPtr<SImage> PinImage;
			SAssignNew(PinImage, SImage)
				.Image(GetVerseTilePinBrush(Type, Socket.bConnected))
				.ColorAndOpacity(GetVerseTilePinColor(Type))
				.Visibility(EVisibility::HitTestInvisible)
				.DesiredSizeOverride(FVector2D(11.0f, 11.0f));
			if (bOutput)
			{
				ValueOutputAnchors.Add(PinImage);
			}
			else
			{
				ValueInputAnchors.Add(PinImage);
			}
			Row->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(bOutput ? 0.0f : -5.0f, 0.0f, bOutput ? -5.0f : 5.0f, 0.0f)
			[
				SNew(SBorder)
				.BorderImage(nullptr)
				.Padding(0.0f)
				.OnMouseButtonDown(this, &SVerseTile::HandleSocketMouseButtonDown,
					TSharedPtr<SWidget>(PinImage), Socket, bOutput, SocketIndex)
				[
					PinImage.ToSharedRef()
				]
			];
		};
		auto AddName = [&]()
		{
			Row->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(5.0f, 0.0f)
			[
				SNew(STextBlock)
				.Visibility(Name.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible)
				.Text(Name)
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
			];
		};
		auto AddInlineLiteral = [&]()
		{
			if (bOutput || !Socket.InlineLiteralRange.IsSet())
			{
				return;
			}
			const FString SourceText = Decode(Socket.InlineLiteralRange).ToString();
			TSharedRef<SWidget> Editor = SNullWidget::NullWidget;
			float EditorMinWidth = 30.0f;
			switch (Socket.InlineLiteralKind)
			{
			case EVerseLiteralKind::Integer:
			{
				int64 Value = 0;
				LexTryParseString(Value, *SourceText);
				Editor = SNew(SSpinBox<int64>)
					.MinDesiredWidth(0.0f)
					.Value(Value)
					.OnValueCommitted_Lambda(
						[this, Range = Socket.InlineLiteralRange](int64 NewValue, ETextCommit::Type)
						{
							OnInlineLiteralCommitted.ExecuteIfBound(
								Range,
								FText::FromString(LexToString(NewValue)));
						});
				break;
			}
			case EVerseLiteralKind::Float:
			{
				double Value = 0.0;
				LexTryParseString(Value, *SourceText);
				EditorMinWidth = 32.0f;
				Editor = SNew(SSpinBox<double>)
					.MinDesiredWidth(0.0f)
					.Value(Value)
					.OnValueCommitted_Lambda(
						[this, Range = Socket.InlineLiteralRange](double NewValue, ETextCommit::Type)
						{
							OnInlineLiteralCommitted.ExecuteIfBound(
								Range,
								FText::FromString(FString::SanitizeFloat(NewValue)));
						});
				break;
			}
			case EVerseLiteralKind::String:
			case EVerseLiteralKind::Character:
				EditorMinWidth = 38.0f;
				Editor = SNew(SEditableTextBox)
					.MinDesiredWidth(0.0f)
					.Text(FText::FromString(SourceText))
					.OnTextCommitted_Lambda(
						[this, Range = Socket.InlineLiteralRange](const FText& NewText, ETextCommit::Type)
						{
							OnInlineLiteralCommitted.ExecuteIfBound(Range, NewText);
						});
				break;
			case EVerseLiteralKind::Logic:
				EditorMinWidth = 18.0f;
				Editor = SNew(SCheckBox)
					.IsChecked(SourceText.Equals(TEXT("true"), ESearchCase::IgnoreCase)
						? ECheckBoxState::Checked
						: ECheckBoxState::Unchecked)
					.OnCheckStateChanged_Lambda(
						[this, Range = Socket.InlineLiteralRange](ECheckBoxState NewState)
						{
							OnInlineLiteralCommitted.ExecuteIfBound(
								Range,
								NewState == ECheckBoxState::Checked
									? FText::FromString(TEXT("true"))
									: FText::FromString(TEXT("false")));
						});
				break;
			case EVerseLiteralKind::None:
			default:
				return;
			}
			Row->AddSlot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(5.0f, 1.0f, 5.0f, 1.0f)
			[
				SNew(SBox)
				.MinDesiredWidth(EditorMinWidth)
				.MaxDesiredWidth(GetVerseGraphMajorGridWidth())
				[
					Editor
				]
			];
		};
		if (bOutput) { AddName(); AddPin(); }
		else { AddPin(); AddInlineLiteral(); AddName(); }
		Column->AddSlot().AutoHeight().HAlign(bOutput ? HAlign_Right : HAlign_Left).Padding(0.0f, 1.0f)[Row];
	}
	if (bOutput)
	{
		ValueOutputColumn = Column;
	}
	else
	{
		ValueInputColumn = Column;
	}
	return Column;
}

FReply SVerseTile::HandleSocketMouseButtonDown(
	const FGeometry& Geometry,
	const FPointerEvent& MouseEvent,
	TSharedPtr<SWidget> Anchor,
	FVerseVisualSocket Socket,
	bool bOutput,
	int32 SocketIndex)
{
	const bool bDraggableStatementOutput = bOutput
		&& Tile.Kind == EVerseVisualTileKind::Expression
		&& Tile.bHasExecutionInput;
	if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton
		|| !bDraggableStatementOutput
		|| !OnSocketDragStarted.IsBound())
	{
		return FReply::Unhandled();
	}
	FVerseSocketDragStart DragStart;
	DragStart.Anchor = MoveTemp(Anchor);
	DragStart.Tile = Tile;
	DragStart.Socket = Socket;
	DragStart.DesktopPosition = FVerseDesktopPoint(MouseEvent.GetScreenSpacePosition());
	DragStart.WireColor = GetVerseTilePinColor(!Socket.SemanticTypeName.IsEmpty()
		? Socket.SemanticTypeName
		: Socket.TypeRange.IsSet()
		? Decode(Socket.TypeRange).ToString()
		: Socket.IntrinsicTypeName.ToString());
	DragStart.bOutput = bOutput;
	DragStart.SocketIndex = SocketIndex;
	return OnSocketDragStarted.Execute(DragStart);
}

FText SVerseTile::Decode(FVerseByteRange Range) const
{
	return Document.IsValid() && Range.IsSet()
		? FText::FromString(Document->DecodeOriginalRange(Range))
		: FText::GetEmpty();
}

FText SVerseTile::GetKindText() const
{
	switch (Tile.Kind)
	{
	case EVerseVisualTileKind::Definition: return FText::FromName(Tile.DefinitionKind);
	case EVerseVisualTileKind::Comment: return LOCTEXT("CommentKind", "Comment");
	case EVerseVisualTileKind::Expression:
		if (Tile.ExpressionKind == EVerseExpressionKind::Identifier)
		{
			return FText::GetEmpty();
		}
		if (Tile.ExpressionKind == EVerseExpressionKind::Call)
		{
			return LOCTEXT("CallKind", "Function");
		}
		if (Tile.ExpressionKind == EVerseExpressionKind::Control)
		{
			switch (Tile.ControlKind)
			{
			case EVerseControlKind::If: return LOCTEXT("IfKind", "If");
			case EVerseControlKind::For: return LOCTEXT("ForKind", "For");
			case EVerseControlKind::Loop: return LOCTEXT("LoopKind", "Loop");
			default: return LOCTEXT("ControlKind", "Control");
			}
		}
		return IsVerseOperatorExpression(Tile.ExpressionKind)
			? LOCTEXT("OperatorKind", "Operator")
			: LOCTEXT("ExpressionKind", "Expression");
	case EVerseVisualTileKind::FunctionEntry: return LOCTEXT("FunctionEntryKind", "Function");
	case EVerseVisualTileKind::FunctionReturn: return LOCTEXT("FunctionReturnKind", "Implicit Return");
	default: return LOCTEXT("UnknownKind", "unknown");
	}
}

FText SVerseTile::GetNameText() const
{
	if (Tile.Kind == EVerseVisualTileKind::FunctionReturn)
	{
		return FText::GetEmpty();
	}
	FText Name = Decode(Tile.NameRange);
	const FText Specifiers = GetSpecifierText();
	return Specifiers.IsEmpty() ? Name : FText::Format(LOCTEXT("NameWithSpecifiers", "{0}{1}"), Name, Specifiers);
}

FText SVerseTile::GetTypeText() const
{
	return !Tile.SemanticTypeName.IsEmpty()
		? FText::FromString(Tile.SemanticTypeName)
		: Tile.TypeRange.IsSet()
		? Decode(Tile.TypeRange)
		: FText::FromName(Tile.IntrinsicTypeName);
}

FText SVerseTile::GetSpecifierText() const
{
	FString Result;
	for (const FVerseTextRange& Range : Tile.SpecifierRanges)
	{
		Result += TEXT("<");
		Result += Decode(Range).ToString();
		Result += TEXT(">");
	}
	return FText::FromString(Result);
}

FText SVerseTile::GetLineText() const
{
	if (Tile.FirstSourceLine == INDEX_NONE || Tile.LastSourceLine == INDEX_NONE)
	{
		return FText::GetEmpty();
	}
	return FText::FromString(Tile.FirstSourceLine == Tile.LastSourceLine
		? FString::Printf(TEXT("L%d"), Tile.FirstSourceLine)
		: FString::Printf(TEXT("L%d-%d"), Tile.FirstSourceLine, Tile.LastSourceLine));
}

const FSlateBrush* SVerseTile::GetIcon() const
{
	if (Tile.Kind == EVerseVisualTileKind::Definition)
	{
		return FAppStyle::GetBrush(GetVerseDefinitionIconName(Tile.DefinitionKind));
	}
	if (Tile.Kind == EVerseVisualTileKind::FunctionEntry)
	{
		return FAppStyle::GetBrush("GraphEditor.Function_16x");
	}
	if (Tile.Kind == EVerseVisualTileKind::Expression
		&& Tile.ExpressionKind == EVerseExpressionKind::Call)
	{
		return FAppStyle::GetBrush("GraphEditor.Function_16x");
	}
	if (Tile.Kind == EVerseVisualTileKind::Expression
		&& Tile.ExpressionKind == EVerseExpressionKind::Control)
	{
		return FAppStyle::GetBrush("GraphEditor.StateMachine_16x");
	}
	return nullptr;
}

FReply SVerseTile::OnMouseButtonDoubleClick(
	const FGeometry& MyGeometry,
	const FPointerEvent& MouseEvent)
{
	return MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton && OnOpened.IsBound()
		? OnOpened.Execute()
		: FReply::Unhandled();
}

FReply SVerseTile::HandleTileMouseButtonDown(
	const FGeometry& MyGeometry,
	const FPointerEvent& MouseEvent)
{
	return MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton
		? FReply::Handled()
		: FReply::Unhandled();
}

FReply SVerseTile::HandleHeaderMouseButtonDown(
	const FGeometry& MyGeometry,
	const FPointerEvent& MouseEvent)
{
	return MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton && OnSelected.IsBound()
		? OnSelected.Execute()
		: FReply::Unhandled();
}

FReply SVerseTile::ToggleExpanded()
{
	if (!bCollapsible)
	{
		return FReply::Handled();
	}
	bExpanded = !bExpanded;
	Invalidate(EInvalidateWidgetReason::Layout | EInvalidateWidgetReason::Paint);
	return FReply::Handled();
}

const FSlateBrush* SVerseTile::GetExpansionImage() const
{
	return FCoreStyle::Get().GetBrush(bExpanded ? "TreeArrow_Expanded" : "TreeArrow_Collapsed");
}

const FSlateBrush* SVerseTile::GetHeaderBrush() const
{
	return bShowBody && (!bCollapsible || bExpanded)
		? ExpandedHeaderBrush.Get()
		: CollapsedHeaderBrush.Get();
}

EVisibility SVerseTile::GetBodyVisibility() const
{
	return bShowBody && (!bCollapsible || bExpanded)
		? EVisibility::Visible
		: EVisibility::Collapsed;
}

FSlateColor SVerseTile::GetOutlineColor() const
{
	return IsSelected.Get(false)
		? FLinearColor(1.0f, 0.82f, 0.05f, 1.0f)
		: UnselectedOutlineColor;
}

#undef LOCTEXT_NAMESPACE
