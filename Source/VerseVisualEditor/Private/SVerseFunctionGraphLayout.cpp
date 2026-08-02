#include "SVerseFunctionGraphLayout.h"

#include "SVerseTile.h"

#include "Layout/ArrangedChildren.h"

namespace
{
	void AddArrangedWidget(
		const FGeometry& AllottedGeometry,
		FArrangedChildren& ArrangedChildren,
		const TSharedRef<SWidget>& Widget,
		FVector2D Position,
		FVector2D Size)
	{
		if (ArrangedChildren.Accepts(Widget->GetVisibility()))
		{
			ArrangedChildren.AddWidget(
				Widget->GetVisibility(),
				AllottedGeometry.MakeChild(Widget, Position, Size));
		}
	}

	float GetVerticalExecutionSpineX(const SVerseTile&)
	{
		// Vertical execution home plates are deliberately left-docked. Both the
		// incoming pin and output zero (the completed path for controls) share
		// this center, so this is the spine that must be kept straight.
		return 16.0f;
	}

	float GetHorizontalExecutionSpineY(const SVerseTile& Tile)
	{
		return FMath::Min(16.0f, Tile.GetDesiredSize().Y * 0.5f);
	}
}

SVerseExpressionLayoutPanel::SVerseExpressionLayoutPanel()
	: Children(this)
{
}

void SVerseExpressionLayoutPanel::Construct(const FArguments& InArgs)
{
	HorizontalGap = InArgs._HorizontalGap;
	VerticalGap = InArgs._VerticalGap;
}

void SVerseExpressionLayoutPanel::AddChild(const TSharedRef<SWidget>& Widget)
{
	FSlot::FSlotArguments Arguments(MakeUnique<FSlot>());
	Arguments.AttachWidget(Widget);
	Children.AddSlot(MoveTemp(Arguments));
}

void SVerseExpressionLayoutPanel::SetRoot(const TSharedRef<SVerseTile>& InRoot)
{
	check(!Root.IsValid());
	Root = InRoot;
	AddChild(InRoot);
}

void SVerseExpressionLayoutPanel::AddOperand(
	const TSharedRef<SWidget>& Widget,
	const TSharedRef<SVerseTile>& OperandRoot,
	TFunction<FVector2D()> OperandRootPosition,
	int32 InputSocketIndex)
{
	check(Root.IsValid());
	FOperand& Operand = Operands.AddDefaulted_GetRef();
	Operand.Root = OperandRoot;
	Operand.RootPosition = MoveTemp(OperandRootPosition);
	Operand.InputSocketIndex = InputSocketIndex;
	AddChild(Widget);
}

SVerseExpressionLayoutPanel::FComputedLayout
SVerseExpressionLayoutPanel::ComputeLayout() const
{
	FComputedLayout Result;
	Result.Positions.SetNum(Children.Num());
	if (!Root.IsValid() || Children.Num() == 0)
	{
		return Result;
	}

	Root->SlatePrepass();
	const FVector2D RootSize = Root->GetDesiredSize();
	float OperandWidth = 0.0f;
	float OperandStackHeight = 0.0f;
	for (int32 Index = 0; Index < Operands.Num(); ++Index)
	{
		const TSharedRef<SWidget> Widget = Children[Index + 1].GetWidget();
		Widget->SlatePrepass();
		const FVector2D Size = Widget->GetDesiredSize();
		OperandWidth = FMath::Max(OperandWidth, Size.X);
		OperandStackHeight += Size.Y;
		if (Index > 0)
		{
			OperandStackHeight += VerticalGap;
		}
	}

	float RootY = FMath::Max(0.0f, (OperandStackHeight - RootSize.Y) * 0.5f);
	float OperandY = FMath::Max(0.0f, (RootSize.Y - OperandStackHeight) * 0.5f);
	if (Operands.Num() == 1 && Operands[0].Root.IsValid())
	{
		Operands[0].Root->SlatePrepass();
		const FVector2D OperandRootPosition = Operands[0].RootPosition
			? Operands[0].RootPosition() : FVector2D::ZeroVector;
		const float OperandOutputY = OperandRootPosition.Y
			+ Operands[0].Root->GetValueSocketCenterY(0, true);
		const float RootInputY = Root->GetValueSocketCenterY(
			Operands[0].InputSocketIndex, false);
		RootY = FMath::Max(0.0f, OperandOutputY - RootInputY);
		OperandY = FMath::Max(0.0f, RootInputY - OperandOutputY);
	}

	Result.RootPosition = FVector2D(
		Operands.IsEmpty() ? 0.0f : OperandWidth + HorizontalGap,
		RootY);
	Result.Positions[0] = Result.RootPosition;
	float CursorY = OperandY;
	for (int32 Index = 0; Index < Operands.Num(); ++Index)
	{
		const TSharedRef<SWidget> Widget = Children[Index + 1].GetWidget();
		const FVector2D Size = Widget->GetDesiredSize();
		Result.Positions[Index + 1] = FVector2D(OperandWidth - Size.X, CursorY);
		CursorY += Size.Y + VerticalGap;
	}

	Result.DesiredSize = FVector2D(
		Result.RootPosition.X + RootSize.X,
		FMath::Max(RootY + RootSize.Y,
			Operands.IsEmpty() ? 0.0f : CursorY - VerticalGap));
	return Result;
}

FVector2D SVerseExpressionLayoutPanel::GetRootPosition() const
{
	return ComputeLayout().RootPosition;
}

FVector2D SVerseExpressionLayoutPanel::ComputeDesiredSize(float) const
{
	return ComputeLayout().DesiredSize;
}

void SVerseExpressionLayoutPanel::OnArrangeChildren(
	const FGeometry& AllottedGeometry,
	FArrangedChildren& ArrangedChildren) const
{
	const FComputedLayout Layout = ComputeLayout();
	for (int32 Index = 0; Index < Children.Num(); ++Index)
	{
		const TSharedRef<SWidget> Widget = Children[Index].GetWidget();
		AddArrangedWidget(
			AllottedGeometry, ArrangedChildren, Widget,
			Layout.Positions[Index], Widget->GetDesiredSize());
	}
}

SVerseStatementLayoutPanel::SVerseStatementLayoutPanel()
	: Children(this)
{
}

void SVerseStatementLayoutPanel::Construct(const FArguments& InArgs)
{
	Presentation = InArgs._Presentation;
	StatementGap = InArgs._StatementGap;
}

void SVerseStatementLayoutPanel::AddChild(const TSharedRef<SWidget>& Widget)
{
	FSlot::FSlotArguments Arguments(MakeUnique<FSlot>());
	Arguments.AttachWidget(Widget);
	Children.AddSlot(MoveTemp(Arguments));
}

void SVerseStatementLayoutPanel::AddStatement(
	const TSharedRef<SWidget>& Widget,
	const TSharedRef<SVerseTile>& Root,
	TFunction<FVector2D()> RootPosition,
	float LeadingSpace)
{
	FStatement& Statement = Statements.AddDefaulted_GetRef();
	Statement.Root = Root;
	Statement.RootPosition = MoveTemp(RootPosition);
	Statement.LeadingSpace = LeadingSpace;
	AddChild(Widget);
}

FVector2D SVerseStatementLayoutPanel::GetStatementPosition(int32 StatementIndex) const
{
	const FComputedLayout Layout = ComputeLayout();
	return Layout.Positions.IsValidIndex(StatementIndex)
		? Layout.Positions[StatementIndex]
		: FVector2D::ZeroVector;
}

SVerseStatementLayoutPanel::FComputedLayout
SVerseStatementLayoutPanel::ComputeLayout() const
{
	FComputedLayout Result;
	Result.Positions.SetNum(Children.Num());
	if (Children.Num() == 0)
	{
		return Result;
	}

	const bool bVertical = Presentation
		== EVerseFunctionGraphPresentation::VerticalExecution;
	float SharedSpine = 0.0f;
	for (int32 Index = 0; Index < Statements.Num(); ++Index)
	{
		const TSharedRef<SWidget> Widget = Children[Index].GetWidget();
		Widget->SlatePrepass();
		if (!Statements[Index].Root.IsValid())
		{
			continue;
		}
		Statements[Index].Root->SlatePrepass();
		const FVector2D RootPosition = Statements[Index].RootPosition
			? Statements[Index].RootPosition() : FVector2D::ZeroVector;
		const float LocalSpine = bVertical
			? RootPosition.X + GetVerticalExecutionSpineX(*Statements[Index].Root)
			: RootPosition.Y + GetHorizontalExecutionSpineY(*Statements[Index].Root);
		SharedSpine = FMath::Max(SharedSpine, LocalSpine);
	}

	float Cursor = 0.0f;
	for (int32 Index = 0; Index < Statements.Num(); ++Index)
	{
		const TSharedRef<SWidget> Widget = Children[Index].GetWidget();
		const FVector2D Size = Widget->GetDesiredSize();
		const FVector2D RootPosition = Statements[Index].RootPosition
			? Statements[Index].RootPosition() : FVector2D::ZeroVector;
		const float LocalSpine = !Statements[Index].Root.IsValid() ? 0.0f
			: bVertical
				? RootPosition.X + GetVerticalExecutionSpineX(*Statements[Index].Root)
				: RootPosition.Y + GetHorizontalExecutionSpineY(*Statements[Index].Root);
		Cursor += Statements[Index].LeadingSpace;
		if (bVertical)
		{
			Result.Positions[Index] = FVector2D(SharedSpine - LocalSpine, Cursor);
			Result.DesiredSize.X = FMath::Max(
				Result.DesiredSize.X, Result.Positions[Index].X + Size.X);
			Cursor += Size.Y + StatementGap;
			Result.DesiredSize.Y = Cursor - StatementGap;
		}
		else
		{
			Result.Positions[Index] = FVector2D(Cursor, SharedSpine - LocalSpine);
			Result.DesiredSize.Y = FMath::Max(
				Result.DesiredSize.Y, Result.Positions[Index].Y + Size.Y);
			Cursor += Size.X + StatementGap;
			Result.DesiredSize.X = Cursor - StatementGap;
		}
	}
	return Result;
}

FVector2D SVerseStatementLayoutPanel::ComputeDesiredSize(float) const
{
	return ComputeLayout().DesiredSize;
}

void SVerseStatementLayoutPanel::OnArrangeChildren(
	const FGeometry& AllottedGeometry,
	FArrangedChildren& ArrangedChildren) const
{
	const FComputedLayout Layout = ComputeLayout();
	for (int32 Index = 0; Index < Children.Num(); ++Index)
	{
		const TSharedRef<SWidget> Widget = Children[Index].GetWidget();
		AddArrangedWidget(
			AllottedGeometry, ArrangedChildren, Widget,
			Layout.Positions[Index], Widget->GetDesiredSize());
	}
}
