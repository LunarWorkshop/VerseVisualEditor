# Verse Visual Editor Development Plan

## Project Direction

Verse Visual Editor is an editor-only Unreal Engine 6 plugin for visually
authoring Verse. It will depend on the Verse plugins included with UE6-main,
use public engine APIs, and avoid engine modifications so the plugin can
eventually be distributed through Fab and GitHub.

The editor will present ordinary Verse source as discoverable visual blocks.
It must preserve the original source text, whitespace, comments, and formatting
except where the user explicitly makes a change.

Initial source-file support is limited to UTF-8. Support for additional
encodings will be added after UTF-8 round-trip preservation is reliable.

## Progress

- Project and plugin scaffolding: complete.
- Lossless document foundation: complete; builds successfully and all
  `VerseVisualEditor.Foundation` automation tests pass.
- Window implementation: pending in the current workspace.
- Subsequent visual editing steps: pending.

## Architecture

### Lossless source model

The Verse compiler's syntax and semantic data will be used to understand code,
but compiler structures will not be trusted to reproduce the original file.
Each open document will retain:

- The complete original source.
- Encoding and line-ending information.
- Token, trivia, and source ranges.
- The source range represented by every visual block.

Editing, saving, and undo/redo will be designed when the first visual
modification workflow is implemented, with Unreal Editor integration available
to define their actual requirements.

A lightweight, error-tolerant source layer will identify supported constructs
and preserve unsupported regions. It will not attempt to replace the complete
Verse parser or compiler.

### Invalid and unsupported code

Parsing or compilation errors must never cause source text to be discarded.
Recognized constructs will become typed blocks. Invalid, incomplete,
unsupported, or unrecognized regions will become raw blocks that retain their
exact source text.

Compiler diagnostics may be attached to raw or typed blocks without preventing
the document from being displayed.

### Block design

The implementation will separate:

- Lossless document and source models.
- Block models representing Verse constructs.
- Slate widgets responsible for presentation.
- Shared capabilities such as selection, renaming, movement, deletion, and
  child ownership.

Block types will share small base interfaces. Reusable behavior will be
composed from capabilities rather than placed into one deep inheritance tree.

### Compilation

The plugin will use the available Verse compilation APIs and structured
diagnostics rather than reading compiler text from stdout.

Compilation requests will:

- Run asynchronously.
- Be debounced when continuous compilation is enabled.
- Carry the document revision that requested the compilation.
- Discard results for obsolete document revisions.
- Return results to the editor thread before updating Slate state.
- Populate semantic types, references, and diagnostic locations.

### Testing

Lossless source-retention tests are the highest priority. A typical test will:

1. Load a fixture as bytes.
2. Build the document model.
3. Compare the retained bytes with the input.
4. Verify source views and metadata against the input.

Fixtures will cover comments, whitespace, blank lines, indentation, braces,
colon syntax, different line endings, incomplete input, invalid input, and
unsupported syntax. Additional encodings will be added in a later phase.

## Implementation Steps

Each step should be completed and verified before proceeding to the next.

### 0. Foundation

- Create the lossless document model and source-range representation.
- Preserve original source and raw regions.
- Create the fixture-based automation test framework.
- Prove byte-for-byte source retention before implementing visual editing.

### 1. Window

- Create the main Verse Visual Editor window.
- Add a Verse folder tree on the left.
- Add a tabbed editing area on the right.
- Open a file in a tab when selected in the tree.
- Show edited files using italic yellow names.
- Track unsaved state and version-control state independently.
- Monitor files for external changes.
- Reload externally changed files immediately when no local edits exist.
- Show a reload-or-keep-local-changes prompt when local edits exist.

### 2. Global scope view

- Recognize top-level modules, classes, structs, interfaces, enums, functions,
  variables, constants, and type aliases.
- Represent each recognized definition with a visual block.
- Stack blocks vertically in source order.
- Add collapse controls and dotted composition guides.
- Display the definition name and type.
- Represent the unimplemented contents of each definition as a raw `unknown`
  block that preserves its exact source.
- Add graph scrolling, panning, and bounded zooming.
- Add round-trip fixtures for all recognized top-level forms.

### 3. Line numbers

- Associate every block with its original source range.
- Display the corresponding source line numbers in a left margin.
- Keep line information accurate as localized edits change the document.

### 4. Selection, copying, and properties

- Add single-block selection.
- Add Shift-click selection toggling.
- Select a block and its descendants on double-click.
- Remove descendants automatically when their parent leaves the selection.
- Copy the original source represented by selected blocks.
- Add equivalent context-menu commands.
- Add the properties panel and property filter.
- Show only common properties with matching names and types for multiple
  selection.

### 5. Global-scope modifications

- Add insertion controls between global definitions.
- Offer every supported global definition type.
- Create definitions with an automatically focused name field.
- Validate identifiers without preventing the file from being saved.
- Support renaming by double-click, context menu, F2, and properties.
- Add deletion.
- Allow valid contiguous selections to be reordered by dragging.
- Preserve all unaffected source text during every modification.

#### 5.1 Undo/Redo

- Support undo and redo (will need a new plan)

### 6. Compile errors

- Add continuous, compile-on-save, and manual compilation modes.
- Debounce continuous compilation.
- Map structured diagnostics to source ranges and blocks.
- Highlight affected blocks in red.
- Display messages in the margin.
- Ignore compilation results for superseded document revisions.

### 7. Enum contents

- Represent each enum label with an indented block.
- Add renaming and identifier validation.
- Add drag handles for reordering.
- Add removal buttons.
- Add insertion controls before, between, and after labels.
- Add the open/closed property.
- Add lossless fixtures for supported enum formatting styles.

### 8. Functions

- Display parameters, their names, types, and Blueprint-style type colors.
- Display the return value and type.
- Distinguish used and unused parameters.
- Show reference locations when hovering usage indicators.
- Display and edit effects and access specifiers in properties.
- Keep function bodies as raw `unknown` blocks initially.

### 9. Global constants

- Display and edit the declared type.
- Provide an appropriate editor for simple typed literal values.
- Provide selection controls for enums, types, and other discoverable values.
- Add Blueprint-style type indicators and usage indicators.
- Preserve complex initializer expressions as raw blocks initially.

### 10. Modules

- Display module names and effects.
- Allow modules to contain all definition types already implemented.
- Reuse selection, insertion, deletion, renaming, and reordering behavior.
- Allow definitions to move between valid module and global scopes.
- Support nested modules.

### 11. Structs

- Add shared effect support suitable for structs, classes, and interfaces.
- Enforce and explain effect availability and inheritance rules.
- Extend definitions to represent mutable `var` fields.
- Add constant and optional properties.
- Support field initializers and the `converges` requirement.
- Reuse existing definition selection and movement behavior.

### 12. Classes

- Add class fields and methods.
- Add class-specific effects and constraints.
- Add initializer and `let` blocks.
- Add member access specifiers.
- Add parent-class selection filtered to valid choices.
- Add constructor validation tied to the class and function names.
- Add function qualifiers and qualifier discovery.
- Add parametric classes and editable type parameters.
- Enforce valid effects for parametric classes.
- Add implemented-interface lists.

### 13. Interfaces

- Add interface blocks and interface-specific effects.
- Add inherited-interface lists.
- Support interface fields.
- Add getter and setter selection for fields with accessors.
- Add support for external definitions.

### 14. Statements and identifiers

- Represent each statement-level expression with a block.
- Display statement order with Blueprint-style execution lines.
- Represent extra blank lines visually without losing their source text.
- Add typed expression sockets where assignment or reference is valid.
- Add identifier expressions and scope-aware references.
- Display the corresponding Verse line as a read-only block label.

### 15. Expression search

- Add a prominent creation control to empty expression positions.
- Open a filterable, automatically focused expression menu.
- Filter entries by expected type and current scope.
- Initially populate the menu with valid identifiers.
- Replace the empty expression with the selected expression type.
- Allow later expression implementations to register their own entries.

### 16. Literal expressions

- Add literal entries for each supported primitive type.
- Provide type-appropriate inline and property-panel editors.
- Add explicit controls for supported floating-point special values.
- Restrict the first implementation to literals that require no casts.

### 17. Basic expressions

- Add arithmetic, comparison, Boolean, and other basic operators.
- Create correctly typed child expression positions automatically.
- Support unary and binary layouts.
- Restrict expression choices according to operand and result types.

### 18. Function calls

- Add scope-aware function discovery to expression search.
- Display and edit function arguments.
- Enforce parameter and return types visually.
- Add support for intrinsic functions.

### 19. Control expressions

- Add branching and looping constructs such as `if`, `for`, and `while`.
- Represent each body with its own automatically created block region.
- Display main and nested execution paths distinctly.
- Preserve and expose supported formatting variations.
- Add whitespace properties for choosing among supported block styles.

## Future Extensions

These items are intentionally not planned in detail yet:

- Detect required function effects while authoring and offer to add them.
- Show source lines modified relative to version control.
- Allow complex expression connections to refactor code automatically by
  introducing identifiers and splitting statements when necessary.
