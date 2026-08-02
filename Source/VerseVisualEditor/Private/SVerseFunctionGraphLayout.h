#pragma once

#include "VerseVisualEditorSettings.h"
#include "Widgets/SPanel.h"

class SVerseTile;

/**
 * A recursively measured expression subtree. Operand subtrees live to the
 * left of their consumer and are fanned around it without overlapping.
 */
class SVerseExpressionLayoutPanel final : public SPanel
{
public:
	SLATE_BEGIN_ARGS(SVerseExpressionLayoutPanel) {}
		SLATE_ARGUMENT(float, HorizontalGap)
		SLATE_ARGUMENT(float, VerticalGap)
	SLATE_END_ARGS()

	SVerseExpressionLayoutPanel();
	void Construct(const FArguments& InArgs);

	void SetRoot(const TSharedRef<SVerseTile>& InRoot);
	void AddOperand(
		const TSharedRef<SWidget>& Widget,
		const TSharedRef<SVerseTile>& OperandRoot,
		TFunction<FVector2D()> OperandRootPosition,
		int32 InputSocketIndex);

	FVector2D GetRootPosition() const;

	virtual void OnArrangeChildren(
		const FGeometry& AllottedGeometry,
		FArrangedChildren& ArrangedChildren) const override;
	virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override;
	virtual FChildren* GetChildren() override { return &Children; }

private:
	struct FSlot : TSlotBase<FSlot>
	{
	};

	struct FOperand
	{
		TSharedPtr<SVerseTile> Root;
		TFunction<FVector2D()> RootPosition;
		int32 InputSocketIndex = INDEX_NONE;
	};

	struct FComputedLayout
	{
		TArray<FVector2D> Positions;
		FVector2D DesiredSize = FVector2D::ZeroVector;
		FVector2D RootPosition = FVector2D::ZeroVector;
	};

	FComputedLayout ComputeLayout() const;
	void AddChild(const TSharedRef<SWidget>& Widget);

	TPanelChildren<FSlot> Children;
	TArray<FOperand> Operands;
	TSharedPtr<SVerseTile> Root;
	float HorizontalGap = 72.0f;
	float VerticalGap = 18.0f;
};

/**
 * Places complete statement subtrees on a shared execution spine. Each item
 * reserves its complete measured bounds, including pure operands and control
 * branches, before the following statement is placed.
 */
class SVerseStatementLayoutPanel final : public SPanel
{
public:
	SLATE_BEGIN_ARGS(SVerseStatementLayoutPanel) {}
		SLATE_ARGUMENT(EVerseFunctionGraphPresentation, Presentation)
		SLATE_ARGUMENT(float, StatementGap)
	SLATE_END_ARGS()

	SVerseStatementLayoutPanel();
	void Construct(const FArguments& InArgs);

	void AddStatement(
		const TSharedRef<SWidget>& Widget,
		const TSharedRef<SVerseTile>& Root,
		TFunction<FVector2D()> RootPosition,
		float LeadingSpace = 0.0f);
	FVector2D GetStatementPosition(int32 StatementIndex) const;

	virtual void OnArrangeChildren(
		const FGeometry& AllottedGeometry,
		FArrangedChildren& ArrangedChildren) const override;
	virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override;
	virtual FChildren* GetChildren() override { return &Children; }

private:
	struct FSlot : TSlotBase<FSlot>
	{
	};

	struct FStatement
	{
		TSharedPtr<SVerseTile> Root;
		TFunction<FVector2D()> RootPosition;
		float LeadingSpace = 0.0f;
	};

	struct FComputedLayout
	{
		TArray<FVector2D> Positions;
		FVector2D DesiredSize = FVector2D::ZeroVector;
	};

	FComputedLayout ComputeLayout() const;
	void AddChild(const TSharedRef<SWidget>& Widget);

	TPanelChildren<FSlot> Children;
	TArray<FStatement> Statements;
	EVerseFunctionGraphPresentation Presentation =
		EVerseFunctionGraphPresentation::VerticalExecution;
	float StatementGap = 12.0f;
};
