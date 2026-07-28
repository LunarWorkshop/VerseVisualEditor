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
		.OnConnectionDropped(InArgs._OnConnectionDropped)
		.OnConnectionCancelled(InArgs._OnConnectionCancelled)
		[
			InArgs._Content.Widget
		]
	];
}

FVerseCanvasViewState SVerseFunctionCanvas::GetViewState() const
{
	return Surface.IsValid() ? Surface->GetViewState() : FVerseCanvasViewState{};
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
