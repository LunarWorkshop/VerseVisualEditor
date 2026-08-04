#include "Slate/SVerseFunctionCanvas.h"

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
		.EndpointRegistry(InArgs._EndpointRegistry)
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
	TSharedPtr<SWidget> InInitialAnchor,
	TSharedPtr<FVerseGraphEndpointRegistry> InEndpointRegistry)
{
	if (Surface.IsValid())
	{
		Surface->SetContentAndAnchor(InContent, MoveTemp(InInitialAnchor));
		Surface->SetConnections(MoveTemp(InConnections));
		Surface->SetEndpointRegistry(MoveTemp(InEndpointRegistry));
	}
}

FReply SVerseFunctionCanvas::BeginConnectionDrag(
	const FVerseSocketDragStart& DragStart,
	TMap<FVerseVisualSocketEndpoint, EVerseSocketDragVisualState> DragStates)
{
	return Surface.IsValid()
		? Surface->BeginConnectionDrag(DragStart, MoveTemp(DragStates))
		: FReply::Unhandled();
}

void SVerseFunctionCanvas::EndConnectionPreview()
{
	if (Surface.IsValid())
	{
		Surface->EndConnectionPreview();
	}
}
