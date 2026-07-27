# Verse Visual Editor Development Plan

## Project Direction

Verse Visual Editor is an editor-only Unreal Engine 6 plugin for visually
authoring Verse. It will depend on the Verse plugins included with UE6-main,
use public engine APIs, and avoid engine modifications so the plugin can
eventually be distributed through Fab and GitHub.

The editor will present ordinary Verse source as discoverable visual tiles.
It must preserve the original source text, whitespace, comments, and formatting
except where the user explicitly makes a change.

Initial source-file support is limited to UTF-8. Support for additional
encodings will be added after UTF-8 round-trip preservation is reliable.

## Progress

- Project and plugin scaffolding: complete.
- Lossless document foundation: complete; builds successfully and all
  `VerseVisualEditor.Foundation` automation tests pass.
- Semantic UTF-8 ownership: complete; the editor target builds successfully and
  all `VerseVisualEditor.Foundation` automation tests pass.
- Window implementation: complete; the nomad window, project Verse source
  tree, read-only document tabs, source-control status, and external-change
  monitoring are implemented and covered by `VerseVisualEditor.Window` tests.
- Revision-neutral parse snapshot: complete; the editor target builds
  successfully and all `VerseVisualEditor.Foundation` automation tests pass.
- Top-level recognition: complete; the official Verse compiler VST supplies
  supported definitions, unsupported gaps produce complete ordered source
  coverage, and all `VerseVisualEditor` automation tests pass.
- Global-scope tile presentation: complete; parser-backed definitions,
  comments, and unknown regions render as collapsible source-ordered tiles in
  a scrollable, pannable, bounded-zoom graph.
- Original-source line ranges: complete; every tile displays its one-based
  source line or inclusive line range beneath its expansion arrow.
- Single-tile selection and properties: complete; selected tiles receive a
  bright gold outline and expose filterable read-only properties in a persistent
  panel.
- Editable source and revision pipeline: complete; localized UTF-8 replacements,
  revisioned ranges, cached materialization, reparsing, and rebuilt tiles are
  coordinated by a per-document session.
- Rename and save vertical slice: complete; Details-driven localized renaming,
  content-state dirty tracking, atomic BOM-preserving saves, and dirty external
  change handling are implemented.
- Subsequent visual editing steps: pending.

## Architecture

### Lossless source model

The Verse compiler's syntax and semantic data will be used to understand code,
but compiler structures will not be trusted to reproduce the original file.
Each open document will retain:

- The complete immutable original source as BOM-free `FUtf8String` data.
- The original BOM, encoding, and line-ending information.
- Token, trivia, and source ranges.
- The source range represented by every visual tile.

Before editing is introduced, source models will use `FVerseByteRange`, which
contains a byte offset and length. Once editing exists, current tiles,
selections, diagnostics, and text operations will use `FVerseTextRange`, which
combines a document revision with an `FVerseByteRange`.

Edited source will be represented by ordered spans into the immutable original
text and an append-only UTF-8 added-text buffer. Visual operations will produce
localized text replacements. Visual tiles will be rebuilt from authoritative
edited source and will never be used to serialize the complete file.

Complete current text will be materialized and cached only when a consumer such
as parsing, display, copying, compilation, or saving requires it.

A lightweight, error-tolerant source layer will identify supported constructs
and preserve unsupported regions. It will not attempt to replace the complete
Verse parser or compiler.

### Invalid and unsupported code

Parsing or compilation errors must never cause source text to be discarded or
an edit to be automatically undone. Recognized constructs will become typed
tiles. Invalid, incomplete, unsupported, or unrecognized regions will become
raw tiles that retain their exact source text.

Compiler diagnostics may be attached to raw or typed tiles without preventing
the document from being displayed or saved.

### Tile design

The implementation will separate:

- Immutable document and source models.
- Editable source spans and document-session state.
- Tile models representing Verse constructs.
- Slate widgets responsible for presentation.
- Shared capabilities such as selection, renaming, movement, deletion, and
  child ownership.

Tile types will share small base interfaces. Reusable behavior will be
composed from capabilities rather than placed into one deep inheritance tree.

### Editing and history

Editing infrastructure will be introduced only when the first visual
modification is implemented. `FVerseDocument` will remain the immutable source
owner, `FVerseEditBuffer` will represent current source spans, and
`FVerseDocumentSession` will coordinate current revisions, parse snapshots,
saving, dirty state, and history.

Document revisions will increase monotonically after edits, undo, redo, reload,
or replacement. A separate content-state identifier will identify reusable span
snapshots for saving and history. Undo and redo will use linear before/after
span snapshots and will be introduced only after the first editable workflow.

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
- Never mutate or discard source because compilation failed.

### Testing

Lossless source-retention tests are the highest priority. A typical test will:

1. Load a fixture as bytes.
2. Build the document model.
3. Compare reconstructed bytes with the input.
4. Verify source views and metadata against the input.

Fixtures will cover comments, whitespace, blank lines, indentation, braces,
colon syntax, different line endings, incomplete input, invalid input, and
unsupported syntax. Additional encodings will be added in a later phase.

Each implementation substep must build and pass its focused automation tests
before the next begins. Infrastructure will be introduced only in the first
step with a direct consumer.

## Prototype Implementation Steps

### 0. Foundation (complete)

- Create the lossless document model and source-range representation.
- Preserve original source and raw regions.
- Create the fixture-based automation test framework.
- Prove byte-for-byte source retention before implementing visual editing.

### 0.1 Semantic UTF-8 ownership (complete)

- Replace persistent `TArray<uint8>` source storage with BOM-free
  `FUtf8String` storage.
- Replace `FVerseSourceRange` with reusable `FVerseByteRange` range arithmetic.
- Track the original UTF-8 BOM independently and reproduce it exactly when
  reconstructing the file.
- Retain original line-ending and line-start metadata.
- Preserve the current raw-region behavior until parse snapshots replace it in
  Step 2.1.
- Update foundation tests for exact reconstruction, UTF-8 views, multibyte
  text, embedded NULs, BOMs, invalid UTF-8, and byte ranges.

This step delivers the UTF-8 representation required by later parsing and
editing without introducing edit state prematurely.

### 1. Window (complete)

- Create the main Verse Visual Editor window.
- Add a Verse folder tree on the left.
- Add a tabbed editing area on the right.
- Open a file in a tab when selected in the tree.
- Expand and scroll the file tree to the active file when tabs are restored,
  activated outside the tree, or reconciled after a tree refresh.
- Restore open tabs, the active tab, temporary-tab state, and per-tab vertical
  scroll offsets when the editor window is reopened.
- Display version-control state independently from document state.
- Monitor files for external changes.
- Reload externally changed files immediately while documents are read-only.

Edited-file styling, unsaved-state tracking, and dirty-file external-change
prompts are deferred to Step 5.2, when local edits and saving first exist.

#### Further work
- Right click a folder or file to go to that folder
- Single click a file to open it in a "temporary" tab that closes again if another file opens via that temporary tab. Double click or right click to open for real
- Save the tabs that are open and their scroll status (don't care about cursor since that won't be a hting later) and open them when the editor starts again
- open the tree to the file that was just opened whenever the file is opened, including when you open for the first time and files are auto opened

### 2. Global scope view

#### 2.1 Revision-neutral parse snapshot (complete)

- Introduce `FVerseParseSnapshot` and a concrete compiler-backed snapshot
  builder.
- Move typed and raw source regions out of immutable `FVerseDocument` and into
  the parse snapshot.
- Store `FVerseByteRange` on every region.
- Supply raw snapshot construction that represents the complete source as one
  exact raw region when parsing fails.
- Ensure failure, incomplete input, and unsupported input always produce a
  usable raw snapshot.

This step gives every Verse file a uniform, lossless visual-model input without
introducing editing concepts.

#### 2.2 Top-level recognition (complete)

- Recognize top-level modules, classes, structs, interfaces, enums, functions,
  variables, constants, and type aliases.
- Emit typed regions for recognized definitions and raw regions for unsupported
  gaps and unimplemented contents.
- Preserve complete, ordered source coverage without overlaps or dropped bytes.
- Add lossless fixtures for supported forms, unsupported syntax, incomplete
  input, and invalid input.

This step delivers a structural representation independently of Slate.

#### 2.3 Global-scope tile presentation (complete)

- Convert the Step 2.2 parse snapshot into visual tiles.
- Arrange global-scope tiles horizontally in source order, with structural
  definitions growing vertically inside tile-shaped containers.
- Display each definition's name and type.
- Represent unimplemented contents as raw `unknown` tiles.
- Add collapse controls; reserve dotted composition guides for nested function
  composition rather than global scope.
- Add graph scrolling, panning, and bounded zooming.

This step delivers the first read-only visual representation of Verse source.

##### Further work
- let's call these things 'tiles' not 'blocks' since blocks are already a thing in verse
- moving the canvas should be RMB not MMB, and scroll should be zoom
- do that thing where the mouse gets replaced and hidden when you scroll past the edge so you can keep dragging the canvas forever like blueprint does

### 3. Line numbers (complete)

- Associate every tile with its parse-snapshot `FVerseByteRange`.
- Derive an inclusive, one-based original source line range from each tile's
  half-open byte range.
- Display `L8` for a single-line tile or `L5-6` for a multi-line tile beneath
  the expansion arrow in a small, regular-weight, subdued font.
- Cover exact single-line, multi-line, and merged-comment ranges with
  automation tests.
- Defer updating line information after localized edits to Step 5.1, where
  current document revisions first exist.

### 4. Selection and properties (complete)

- Add single-tile selection, retained independently for each open document.
- Indicate the selected tile with a bright golden-yellow outline while
  retaining the normal structural-tile outline when it is not selected.
- Select from the tile header and restrict expansion and collapse to the
  disclosure arrow itself.
- Show the selected tile in a Blueprint-style, closable **Details** tab. Keep
  an open tab empty when selection is cleared, and reopen it when a tile is
  selected after the tab was closed.
- Expose tile kind, definition metadata, comment style, and source lines as
  read-only properties with a case-insensitive property filter.
- Test replacement-style single selection, selection clearing, property
  generation, and property filtering.

Do not add edit transactions or selection-history snapshots until Step 5.

### 5. Global-scope modifications

#### 5.1 Editable source and revision pipeline (complete)

- Keep `FVerseDocument` as the immutable original-source owner.
- Add `FVerseEditBuffer` containing spans into the immutable original source,
  an append-only `FUtf8String` added-text buffer, and a current ordered span
  list.
- Implement one localized replacement operation that splits and coalesces
  spans without materializing the complete document.
- Add monotonically increasing `FVerseDocumentRevision` values.
- Add `FVerseTextRange`, containing a revision and an `FVerseByteRange`.
- Add `FVerseDocumentSession` to coordinate the immutable document, edit
  buffer, current parse snapshot, and current revision.
- Reject stale ranges, invalid bounds, and edits that split UTF-8 code points.
- Cache materialized source by revision and invalidate it on every source-state
  transition.
- After a replacement, increment the revision and rebuild the error-tolerant
  parse and tile representations from current source.
- Preserve invalid edits as raw regions rather than rolling them back.
- Update line numbers to use current revisioned ranges.
- Test localized replacement, span splitting and coalescing, append-only added
  storage, cache reuse and invalidation, revision validation, reparsing, and
  preservation of all unaffected bytes.

This step delivers one tested localized source change through source, parsing,
tiles, and line numbers.

#### 5.2 Rename and save vertical slice (complete)

- Implement renaming as the first visual modification.
- Convert a rename into one localized replacement of the identifier's current
  `FVerseTextRange`.
- Validate identifiers for feedback without preventing invalid text from
  remaining or being saved.
- Add `FVerseContentStateId` to identify span states independently of document
  revisions.
- Track dirty state against the last successfully saved content state.
- Save by materializing current UTF-8 once, restoring the original BOM, writing
  a same-directory temporary file, and replacing the target after a successful
  write.
- Preserve existing LF, CRLF, CR, and mixed line-ending bytes without
  normalizing the complete file.
- Mark the current content state saved only after replacement succeeds.
- Keep failed or cancelled saves dirty and retain the previous saved
  checkpoint.
- Restore edited-file tab styling, independent dirty and version-control state,
  and reload-or-keep-local-changes prompts for externally changed dirty files.
- Test renaming, invalid identifiers, atomic save failure, BOM and line-ending
  preservation, dirty transitions, and external-change behavior.

This step delivers the first complete edit-and-save workflow.

##### Further work
- if there are local edits show a modal confirmation before closing yes/no/cancel

### 6. Compile errors

- Add continuous, compile-on-save, and manual compilation modes.
- Compile materialized source for a specified document revision.
- Run asynchronously and debounce continuous compilation.
- Map structured diagnostics to revisioned `FVerseTextRange` values and current
  tiles.
- Highlight affected tiles in red.
- Display messages in the margin.
- Ignore compilation results for superseded document revisions.
- Never mutate, undo, or discard source because compilation failed.
- Test stale-result rejection and diagnostic mapping.

### 7. Nested-body range transition

- `FVerseSourceRegion::BodyRange` is a durable requirement, but the current
  `FindBodyRange()` and `TrimClauseDelimiters()` implementation is transitional.
  It uses the official VST to select a body clause, then performs a small
  source-range trim for its enclosing braces or colon. Replace that specific
  trimming path with the richer VST-derived clause model in these steps as soon
  as nested contents are represented.
- Retain both the complete definition range and the body/interior range. They
  serve different operations: whole-definition selection, copying, deletion,
  and movement versus child layout and insertion inside the definition.
- Replace `FindBodyRange()`/`TrimClauseDelimiters()` with a VST-derived clause
  descriptor containing the interior range, opening punctuation range, closing
  punctuation range, punctuation style (braces or colon/indentation), and an
  empty-body insertion anchor.
- Recursively traverse the official Verse VST clause children to build nested
  visual regions. Never run a second parser over `BodyRange`.
- Partition every body into complete ordered coverage: recognized child
  regions plus exact raw gaps for whitespace, comments not yet modeled at that
  step, invalid syntax, and unsupported constructs.
- Preserve leading and trailing interior trivia even when no VST child owns it.
  Child loci alone are not a replacement for the parent interior range.
- Add lossless brace-style, colon/indentation-style, empty-body, comment/trivia,
  invalid-body, and nested-definition fixtures before removing the transitional
  delimiter trimming.

### 8. Functions

- Represent the function body using its VST clause descriptor, keeping the
  signature outside the body and its enclosing punctuation outside child
  regions.
- Display parameters, their names, types, and Blueprint-style type colors.
- Display the return value and type.
- Distinguish used and unused parameters.
- Show reference locations when hovering usage indicators.
- Display and edit effects and access specifiers in properties.
- Keep function bodies as raw `unknown` tiles initially.
- Build that initial raw body from the descriptor's exact interior range; later
  expression steps must replace portions with VST-derived children while
  preserving the remaining raw gaps.

### 9. Modules

- Use the VST-derived module clause descriptor and recursively convert its VST
  children into nested definitions and exact raw gaps.
- Display module names and effects.
- Allow modules to contain all definition types already implemented.
- Reuse single-tile selection, insertion, deletion, renaming, and reordering
  behavior.
- Allow definitions to move between valid module and global scopes.
- Support nested modules.
- Preserve each nested module's independent complete, header, punctuation, and
  interior ranges so movement and insertion target the correct scope.

### 10. Statements and identifiers

- Represent each statement-level expression with a tile.
- Display statement order with Blueprint-style execution lines.
- Represent extra blank lines visually without losing their source text.
- Add typed expression sockets where assignment or reference is valid.
- Add identifier expressions and scope-aware references.
- Display the corresponding Verse line as a read-only tile label.

### 11. Expression search

- Add a prominent creation control to empty expression positions.
- Open a filterable, automatically focused expression menu.
- Filter entries by expected type and current scope.
- Initially populate the menu with valid identifiers.
- Replace the empty expression with the selected expression type.
- Allow later expression implementations to register their own entries.

### 12. Literal expressions

- Add literal entries for each supported primitive type.
- Provide type-appropriate inline and property-panel editors.
- Add explicit controls for supported floating-point special values.
- Restrict the first implementation to literals that require no casts.

### 13. Basic expressions

- Add arithmetic, comparison, Boolean, and other basic operators.
- Create correctly typed child expression positions automatically.
- Support unary and binary layouts.
- Restrict expression choices according to operand and result types.

### 14. Function calls

- Add scope-aware function discovery to expression search.
- Display and edit function arguments.
- Enforce parameter and return types visually.
- Add support for intrinsic functions.

### 15. Control expressions

- Add branching and looping constructs such as `if`, `for`, and `while`.
- Represent each body with its own automatically created tile region.
- Display main and nested execution paths distinctly.
- Preserve and expose supported formatting variations.
- Add whitespace properties for choosing among supported body styles.

## Additional Implementation Steps

### 1. Multi-selection and copying

- Add Shift-click selection toggling.
- Select a tile and its descendants on double-click.
- Remove descendants automatically when their parent leaves the selection.
- Copy current source represented by selected tiles' current-revision
  `FVerseTextRange` values, materializing only the selected ranges.
- Add equivalent context-menu commands.
- Show only common properties with matching names and types for multiple
  selection.
- When undo/redo and edit transactions are available, upgrade their optional
  single-selected-tile range to an ordered collection of revisioned selected
  ranges and restore that collection after history operations.

This step owns the transition from single-selection to multi-selection state.

##### Further work
- UI options to compile the open file or all files immediately

### 2. Undo and redo

- Add linear command history using lightweight before and after span snapshots
  that share the original and added UTF-8 buffers.
- Store the optional current-revision range of the single selected tile with
  each command.
- On undo or redo, restore the corresponding spans, allocate a new monotonically
  increasing document revision, invalidate materialized source, reparse, rebuild
  tiles, and restore the single selected tile when it still has a corresponding
  rebuilt range.
- Discard the redo tail when a new edit follows undo.
- Keep history after saving.
- Use `FVerseContentStateId` so undoing away from the saved state becomes dirty
  and redoing back to it becomes clean.
- Test source, tile, revision, single-selection, and saved-state restoration.

This step makes the existing rename workflow safely reversible.

### 3. Atomic multi-edit transactions

- Generalize the single replacement operation into
  `FVerseEditTransaction`.
- Give each transaction a description, one or more localized edits, and
  optional before and after single-selected-tile ranges.
- Require every edit to target the same current revision.
- Validate the complete transaction before changing source state.
- Reject overlapping edits and invalid UTF-8 boundaries without partial
  application.
- Apply edits in descending byte order or with an equivalent one-pass span
  rewrite.
- Record the complete transaction as one undo step.
- Test multiple edits, stale revisions, overlaps, failed atomic validation, and
  one-command/one-undo behavior.

This step supplies compound editing only when later visual operations need it.

### 4. Insertion and deletion

- Add insertion controls between global definitions.
- Offer every supported global definition type.
- Create definitions with an automatically focused name field.
- Generate the smallest required Verse fragment using the local line-ending and
  indentation context.
- Express insertion as a zero-length revisioned range.
- Delete a definition's exact current range plus only the separator trivia
  assigned by the command's documented policy.
- Preserve every unaffected byte and reparse after each transaction.
- Treat each insertion or deletion as one undo step.
- Add lossless insertion and deletion fixtures.

This step delivers creation and removal without introducing source
regeneration.

### 5. Reordering

- Allow one selected definition to be reordered by dragging.
- Move that definition's current-source range through one atomic
  delete-and-insert
  transaction without regenerating its contents.
- Preserve the definition's formatting and internal raw regions exactly.
- Reject destinations inside the moved range or outside the valid scope.
- Restore the single selection to the moved definition after reparsing.
- Treat the complete move as one undo step.
- Test byte preservation and invalid destination rejection.

This step delivers lossless definition movement using the transaction support
introduced immediately beforehand.

### 6. Comments

- Support for adding/removing/changing/etc comments. Needs a plan, which may be complicated because of inline comments.
- Preserve VST comment attachment and comment type while partitioning nested
  bodies. Inline, prefix, and postfix comments must become children or raw gaps
  of the appropriate clause without scanning body text for comment syntax.

### 7. Enum contents

- Make enum bodies the first consumer of the VST-derived clause descriptor and
  retire the transitional delimiter trim for enum definitions.
- Represent each enum label with an indented tile.
- Add renaming and identifier validation.
- Add drag handles for reordering.
- Add removal buttons.
- Add insertion controls before, between, and after labels.
- Add the open/closed property.
- Add lossless fixtures for supported enum formatting styles.

### 8. Global constants

- Model the initializer as its own VST-derived expression range rather than
  treating it as a delimited definition body or using
  `TrimClauseDelimiters()`.
- Display and edit the declared type.
- Provide an appropriate editor for simple typed literal values.
- Provide selection controls for enums, types, and other discoverable values.
- Add Blueprint-style type indicators and usage indicators.
- Preserve complex initializer expressions as raw tiles initially.

### 9. Structs

- Replace transitional struct body trimming with the shared VST clause
  descriptor before presenting fields as child tiles.
- Add shared effect support suitable for structs, classes, and interfaces.
- Enforce and explain effect availability and inheritance rules.
- Extend definitions to represent mutable `var` fields.
- Add constant and optional properties.
- Support field initializers and the `converges` requirement.
- Reuse existing single-definition selection and movement behavior.
- Partition struct interiors into VST-derived fields and lossless raw gaps,
  including empty brace and empty indentation forms.

### 10. Classes

- Replace transitional class body trimming with the shared VST clause
  descriptor before presenting members as child tiles.
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
- Recursively derive fields, methods, nested types, initializer blocks, and
  unsupported gaps from the class VST body without reparsing its source text.

### 11. Interfaces

- Replace transitional interface body trimming with the shared VST clause
  descriptor before presenting members as child tiles.
- Add interface tiles and interface-specific effects.
- Add inherited-interface lists.
- Support interface fields.
- Add getter and setter selection for fields with accessors.
- Add support for external definitions.
- Recursively derive interface fields, accessors, nested definitions, and raw
  gaps from the interface VST body while retaining the parent interior range
  for empty-body insertion and trivia preservation.

All editing features for comments, definitions, statements, and expressions
must reuse localized revisioned replacements, atomic transactions,
span-snapshot undo and redo, saved content-state tracking, and error-tolerant
reparsing. They must not introduce a second serialization, editing, or history
path.

## Deferred Optimizations

The initial editing implementation will prioritize correctness and complete
lossless tests. The following are deferred until profiling or product
requirements justify them:

- Balanced piece trees or ropes.
- Added-text buffer compaction.
- Undo-history memory limits and pruning.
- Branching undo history.

## Future Extensions

These items are intentionally not planned in detail yet:

- Detect required function effects while authoring and offer to add them.
- User option to show or hide line numbers at all
- Show source lines modified relative to version control.
- Allow complex expression connections to refactor code automatically by
  introducing identifiers and splitting statements when necessary.
- Open Verse files in VVE via the "Verse Explorer"
- Move/copy/paste/duplicate/delete files and folders via the tree
- A panel that shows all global scope items in a file for easy access, in case there are a hojillion of them and you want fast access.
