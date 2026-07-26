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
- Subsequent visual editing steps: pending.

## Architecture

### Lossless source model

The Verse compiler's syntax and semantic data will be used to understand code,
but compiler structures will not be trusted to reproduce the original file.
Each open document will retain:

- The complete immutable original source as BOM-free `FUtf8String` data.
- The original BOM, encoding, and line-ending information.
- Token, trivia, and source ranges.
- The source range represented by every visual block.

Before editing is introduced, source models will use `FVerseByteRange`, which
contains a byte offset and length. Once editing exists, current blocks,
selections, diagnostics, and text operations will use `FVerseTextRange`, which
combines a document revision with an `FVerseByteRange`.

Edited source will be represented by ordered spans into the immutable original
text and an append-only UTF-8 added-text buffer. Visual operations will produce
localized text replacements. Visual blocks will be rebuilt from authoritative
edited source and will never be used to serialize the complete file.

Complete current text will be materialized and cached only when a consumer such
as parsing, display, copying, compilation, or saving requires it.

A lightweight, error-tolerant source layer will identify supported constructs
and preserve unsupported regions. It will not attempt to replace the complete
Verse parser or compiler.

### Invalid and unsupported code

Parsing or compilation errors must never cause source text to be discarded or
an edit to be automatically undone. Recognized constructs will become typed
blocks. Invalid, incomplete, unsupported, or unrecognized regions will become
raw blocks that retain their exact source text.

Compiler diagnostics may be attached to raw or typed blocks without preventing
the document from being displayed or saved.

### Block design

The implementation will separate:

- Immutable document and source models.
- Editable source spans and document-session state.
- Block models representing Verse constructs.
- Slate widgets responsible for presentation.
- Shared capabilities such as selection, renaming, movement, deletion, and
  child ownership.

Block types will share small base interfaces. Reusable behavior will be
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

## Implementation Steps

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

#### 2.3 Global-scope block presentation

- Convert the Step 2.2 parse snapshot into visual blocks.
- Stack blocks vertically in source order.
- Display each definition's name and type.
- Represent unimplemented contents as raw `unknown` blocks.
- Add collapse controls and dotted composition guides.
- Add graph scrolling, panning, and bounded zooming.

This step delivers the first read-only visual representation of Verse source.

### 3. Line numbers

- Associate every block with its parse-snapshot `FVerseByteRange`.
- Display the corresponding original source line numbers in a left margin.
- Defer updating line information after localized edits to Step 5.1, where
  current document revisions first exist.

### 4. Selection, copying, and properties

- Add single-block selection.
- Add Shift-click selection toggling.
- Select a block and its descendants on double-click.
- Remove descendants automatically when their parent leaves the selection.
- Copy immutable source represented by selected blocks' `FVerseByteRange`
  values.
- Add equivalent context-menu commands.
- Add the properties panel and property filter.
- Show only common properties with matching names and types for multiple
  selection.

Do not add edit transactions or selection-history snapshots until Step 5.

### 5. Global-scope modifications

#### 5.1 Editable source and revision pipeline

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
  parse and block representations from current source.
- Preserve invalid edits as raw regions rather than rolling them back.
- Update line numbers and copying to use current revisioned ranges.
- Test localized replacement, span splitting and coalescing, append-only added
  storage, cache reuse and invalidation, revision validation, reparsing, and
  preservation of all unaffected bytes.

This step delivers one tested localized source change through source, parsing,
blocks, line numbers, and copying.

#### 5.2 Rename and save vertical slice

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

#### 5.3 Undo and redo

- Add linear command history using lightweight before and after span snapshots
  that share the original and added UTF-8 buffers.
- Store revisioned selected ranges plus caret and anchor offsets with each
  command.
- On undo or redo, restore the corresponding spans, allocate a new monotonically
  increasing document revision, invalidate materialized source, reparse, rebuild
  blocks, and restore selection state.
- Discard the redo tail when a new edit follows undo.
- Keep history after saving.
- Use `FVerseContentStateId` so undoing away from the saved state becomes dirty
  and redoing back to it becomes clean.
- Test source, block, revision, selection, and saved-state restoration.

This step makes the existing rename workflow safely reversible.

#### 5.4 Atomic multi-edit transactions

- Generalize the single replacement operation into
  `FVerseEditTransaction`.
- Give each transaction a description, one or more localized edits, and before
  and after selection state.
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

#### 5.5 Insertion and deletion

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

#### 5.6 Reordering

- Allow valid contiguous selections to be reordered by dragging.
- Move the selected current-source range through one atomic delete-and-insert
  transaction without regenerating its contents.
- Preserve the selection's formatting and internal raw regions exactly.
- Reject destinations inside the moved range or outside the valid scope.
- Restore selection to the moved definitions after reparsing.
- Treat the complete move as one undo step.
- Test byte preservation and invalid destination rejection.

This step delivers lossless definition movement using the transaction support
introduced immediately beforehand.

### 6. Compile errors

- Add continuous, compile-on-save, and manual compilation modes.
- Compile materialized source for a specified document revision.
- Run asynchronously and debounce continuous compilation.
- Map structured diagnostics to revisioned `FVerseTextRange` values and current
  blocks.
- Highlight affected blocks in red.
- Display messages in the margin.
- Ignore compilation results for superseded document revisions.
- Never mutate, undo, or discard source because compilation failed.
- Test stale-result rejection and diagnostic mapping.

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

All editing features in Steps 7 through 19 must reuse localized revisioned
replacements, atomic transactions, span-snapshot undo and redo, saved
content-state tracking, and error-tolerant reparsing. They must not introduce a
second serialization, editing, or history path.

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
- Show source lines modified relative to version control.
- Allow complex expression connections to refactor code automatically by
  introducing identifiers and splitting statements when necessary.
- Open Verse files in VVE via the "Verse Explorer"
- Move/copy/paste/duplicate/delete files and folders via the tree
