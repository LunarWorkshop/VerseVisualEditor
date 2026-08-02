#include "SVerseFunctionCanvas.h"

void SVerseFunctionCanvas::Construct(
	const FArguments& InArgs,
	FVerseCanvasViewState InitialViewState,
	bool bCenterInitially)
{
	ChildSlot
	[
		SAssignNew(Surface, SVerseGraphSurface, InitialViewState, bCenterInitially)
		.UseEdgePanPadding(true)
		.InitialAnchor(InArgs._InitialAnchor)
		.Connections(InArgs._Connections)
		.MotionController(InArgs._MotionController)
		.OnConnectionDropped(InArgs._OnConnectionDropped)
		.OnConnectionCancelled(InArgs._OnConnectionCancelled)
		.OnBackgroundClicked(InArgs._OnBackgroundClicked)
		[
			InArgs._Content.Widget
		]
	];
}

FVerseCanvasViewState SVerseFunctionCanvas::GetViewState() const
{
	return Surface.IsValid() ? Surface->GetViewState() : FVerseCanvasViewState{};
}

void SVerseFunctionCanvas::RefreshContent(
	TSharedRef<SWidget> InContent,
	TArray<FVerseGraphConnection> InConnections,
	TSharedPtr<SWidget> InInitialAnchor)
{
	if (Surface.IsValid())
	{
		Surface->SetContent(InContent);
		Surface->SetInitialAnchor(MoveTemp(InInitialAnchor));
		Surface->SetConnections(MoveTemp(InConnections));
	}
}

FReply SVerseFunctionCanvas::BeginConnectionDrag(const FVerseSocketDragStart& DragStart)
{
	return Surface.IsValid() ? Surface->BeginConnectionDrag(DragStart) : FReply::Unhandled();
}

void SVerseFunctionCanvas::EndConnectionPreview()
{
	if (Surface.IsValid())
	{
		Surface->EndConnectionPreview();
	}
}
