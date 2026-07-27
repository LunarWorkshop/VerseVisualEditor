#include "VerseDefinitionIcon.h"

#include "VerseParseSnapshotBuilder.h"

FName GetVerseDefinitionIconName(FName DefinitionKind)
{
	if (DefinitionKind == VerseSyntaxKind::Function)
	{
		return TEXT("GraphEditor.Function_16x");
	}
	if (DefinitionKind == VerseSyntaxKind::Class)
	{
		return TEXT("GraphEditor.EventGraph_16x");
	}
	if (DefinitionKind == VerseSyntaxKind::Struct)
	{
		return TEXT("GraphEditor.StructGlyph");
	}
	if (DefinitionKind == VerseSyntaxKind::Interface)
	{
		return TEXT("ClassIcon.BlueprintInterface");
	}
	if (DefinitionKind == VerseSyntaxKind::Module)
	{
		return TEXT("ContentBrowser.AssetTreeFolderClosed");
	}
	if (DefinitionKind == VerseSyntaxKind::Enum)
	{
		return TEXT("GraphEditor.Enum_16x");
	}
	if (DefinitionKind == VerseSyntaxKind::Constant)
	{
		return TEXT("Kismet.AllClasses.VariableIcon");
	}
	if (DefinitionKind == VerseSyntaxKind::TypeAlias)
	{
		return TEXT("Icons.Convert");
	}
	return TEXT("Icons.Documentation");
}
