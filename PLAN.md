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
- Nested-body range transition: complete; VST-derived clause descriptors retain
  complete definitions, exact interiors and punctuation, insertion anchors, and
  recursively lossless child coverage.
- Dynamic expression candidates: complete; expression search now traverses the
  compiler-owned active scope, signatures, generic overloads, imported APIs,
  and intrinsic operators, with structural fallbacks when the current private
  semantic overlay fails.
- Generic expression actions: complete; identifier references, calls, and
  prefix, infix, and postfix operators use source-form descriptors, preserve
  bound inputs, and require atomic prospective syntax and semantic validation.
- Modules: complete; module tiles render VST-derived nested definitions and raw
  gaps recursively, display append specifiers, and retain independent source
  ranges at every nesting level.
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

Structured edits made through tiles or Details controls must validate the
complete proposed syntax before changing the source. If they don't then the
code becomes syntactically invalid and parsing is not possible, and creating
all the tiles depends on parsing, and so suddenly the user is looking at one
big unknown block.
Invalid input remains only
in the UI as a visible validation state; it must not create an edit-buffer
replacement, increment the revision, dirty the document, or trigger a reparse.
The error-tolerant parser remains responsible for invalid text that already
exists on disk or enters through a future raw-source editing workflow.

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
- Validate identifiers before replacement. Keep rejected text in the Details
  control with visible feedback, but do not change source, revision, dirty
  state, parsing, or tiles until the proposed identifier is valid.
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
- Test renaming, rejection of invalid identifiers without source mutation,
  atomic save failure, BOM and line-ending preservation, dirty transitions,
  and external-change behavior.

This step delivers the first complete edit-and-save workflow.

### 6. Compile errors (complete)

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

### 7. Nested-body range transition (complete)

- `FVerseSourceRegion::BodyRange` remains a durable requirement. The former
  `FindBodyRange()` and `TrimClauseDelimiters()` transition has been replaced by
  a richer VST-derived clause model now that nested contents are represented.
- Retain both the complete definition range and the body/interior range. They
  serve different operations: whole-definition selection, copying, deletion,
  and movement versus child layout and insertion inside the definition.
- Use a VST-derived clause
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
- Cover brace-style, colon/indentation-style, empty-body, comment/trivia,
  invalid-body, and nested-definition cases with lossless fixtures.

### 8. Functions (complete)

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

### 9. Modules (complete)

- Use the VST-derived module clause descriptor and recursively convert its VST
  children into nested definitions and exact raw gaps.
- Display module names and effects.
- Allow modules to contain all definition types already implemented. (and allowing for containing future ones)
- Reuse the currently available single-tile selection and renaming behavior.
- Defer nested insertion and deletion to Additional Step 4 and module/global
  movement and reordering to Additional Step 5.
- Support nested modules.
- Preserve each nested module's independent complete, header, punctuation, and
  interior ranges so movement and insertion target the correct scope.

### 10. Statements and identifiers (complete)

- Treat a statement as the contextual occurrence of a root expression in an
  executable clause, not as a separate VST node or visible wrapper tile.
- Represent file definitions, function entry and return nodes, and expression
  nodes with the same visual-tile model and shared Slate tile chrome. Canvases
  arrange those tiles differently but do not introduce canvas-specific tile
  types, allowing later expression tiles to be nested in file-scope tiles.
- Retain a nonvisual clause-item descriptor for each root expression containing
  its revision-specific expression, trivia, and type ranges; separator form;
  blank-line count; and final-value position.
- Limit the first supported expression to a line containing one identifier.
  Preserve every other root expression as a read-only unsupported expression
  tile until its later expression step is implemented.
- Arrange root expression tiles vertically and connect them with solid,
  Blueprint-style execution pins and lines.
- Represent extra blank lines as additional execution-line length with small
  horizontal markers, without losing or regenerating their source bytes.
- Give identifier expression tiles left and right Blueprint-style typed sockets.
  Resolve the initial socket type from an in-scope function parameter when
  available and use the generic Blueprint object color otherwise.
- Reserve the left socket for consuming an r-value reference and the right
  socket for the identifier's assignable/l-value role. Step 11 will make a drag
  from an identifier into an empty expression create an identifier reference;
  Step 13 will implement assignment-producing drags. Step 10 renders the typed
  endpoints but does not create unsupported syntax early.
- Display the expression's exact source byte range as its read-only shorthand
  label. The source range remains authoritative for future localized editing;
  tiles do not keep serialized source copies.

### 11. Expression search and compiler-driven semantics

#### 11.1 Revision-specific semantic workspace (complete)

- Treat the official Verse semantic program as the sole authority for callable
  definitions, overloads, parameter and result types, effects, generics,
  constraints, access, availability, and scope visibility.
- Read the compiled-project baseline from the editor IDE owned by
  `ISolarisLoadCompilerModule`; do not construct a second independent baseline
  or modify the engine-owned semantic program.
- Create a private Solaris development environment only for unsaved visual
  editor revisions. Create an independently owned project source through
  `ISolarisModule::CreateProjectSource`, then overlay open buffers with
  `CSourceFileSnippet::SetModifiedText` so analysis uses the current UTF-8
  without changing the main Compile Verse state. Do not use
  `ISolIdeSourceProject::MakeShallowCopy` for these overlays: its packages and
  snippets are shared with the source project, so modified text would leak into
  the main Compile Verse inputs.
- When an open file has no snippet in the engine-owned source project, create an
  editor-only `FVerseProjectContainer` package and pass it through
  `CreateProjectSource`'s `AdditionalPackages` input. Populate that in-memory
  package with the current UTF-8 buffers, inherit the active user package's
  scope, language version, and feature settings, and depend on every compatible
  package already supplied by Solaris. This keeps unregistered plugin-private
  files semantic and searchable without adding a shipped `VersePath`, changing
  the main compiler project, or requiring the file to appear in Verse Explorer.
- Run the private environment's `CProgramBuildManager` directly with
  `_bSemanticAnalysisOnly`; skip linking, digest generation, and code
  generation. Do not use `ISolarisIde::BuildAll` for this path because it is the
  full editor-build wrapper and carries output/fingerprint assumptions that do
  not apply to isolated live analysis.
- Analyze after a successful project compilation and after a debounced valid
  source edit. Publish an immutable semantic snapshot containing the project
  VST, `CSemanticProgram`, diagnostics, and the exact document revisions it
  describes.
- Reject completed analysis for superseded revisions. The lightweight parser
  remains responsible for immediate lossless source presentation while
  semantic analysis is pending.
- Give every expression action an explicit validation requirement. Structural
  actions may mutate only after current-revision range checks and prospective
  syntax/VST validation succeed. Actions bound to visibility, overloads, types,
  access, or other semantic claims additionally require a semantic snapshot for
  the exact current revision. A last-successful snapshot may provide discovery
  information, but it must never authorize a semantic claim against newer
  source.
- If analysis fails, retain the current source and syntax-derived tiles and show
  the diagnostics in a closable Local Compile Errors panel beneath only the
  central editing space. Keep the panel closed by default and open it when a new
  private analysis fails; never put these transient diagnostics in expression
  search or Unreal's project-level Verse message log. Candidate discovery falls
  back to the last successful local semantic snapshot together with the current
  Solaris baseline, so compiled project APIs, native APIs, intrinsics, and
  editor-supported syntax forms remain searchable. Never treat that fallback as
  exact-revision proof for a source mutation.
- Do not attempt in this step to make other Unreal tools consume unsaved visual
  editor buffers. The private environment exists only for live analysis inside
  this plugin.

#### 11.2 Dynamic expression candidates (complete)

- Add a prominent creation control to empty expression positions and retain the
  automatically focused, filterable expression menu and frozen preview wire.
- Replace the hand-built candidate registry with a semantic candidate provider
  rooted at the active expression's VST node and current `CLogicalScope`.
- Discover visible data and `CFunction` definitions through the compiler's
  scope traversal or VerseAssist. Respect imports, access, shadowing,
  availability versions, overloads, and the current package automatically.
- Read parameter names and types, return types, effects, generic variables, and
  constraints from `SSignature`. Use Verse type instantiation, constraint, and
  subtype operations for socket compatibility; string spellings are for display
  only and must not authorize a connection or source edit.
- Discover intrinsic and user-defined operators from semantic function
  definitions such as `operator'+'`. Do not register individual operators or
  their overloads in the plugin.
- For a drag from an output socket, create one action for every callable
  parameter position that accepts the dragged value. Distinguish the bound
  parameter when asymmetric or overloaded callables expose multiple valid
  placements.
- For an input-side search, include candidates whose resolved result satisfies
  the requested socket type.
- Automatically include newly added project functions, imported APIs, native
  intrinsics, and new overloads without adding editor enum values or registry
  entries.

#### 11.3 Generic expression actions (complete)

- Describe actions by editor-owned source forms such as identifier reference,
  ordinary call, infix operator, and prefix operator. Store the selected
  semantic definition, overload, bound parameter index, source spelling, and
  syntax form; do not use an Add-specific action kind.
- Preserve the dragged expression as the selected input. Fill other required
  inputs only through the editor's type-directed default-literal factory. Do
  not offer a candidate when every remaining required input cannot be populated
  with source-safe syntax.
- Scratch-parse and semantically analyze the complete prospective replacement
  before changing the document. Rejection must not change source, revision,
  dirty state, parsing, selection, or tiles.
- Commit an accepted action through one localized source replacement, rebuild
  syntax-derived representations, and schedule semantic analysis for the new
  revision.
- Allow compatible identifier drags and later expression implementations to
  use this same candidate and action pipeline rather than registering parallel
  search systems.

### 13. Basic expressions (complete)

- Replace operation-specific expression classification with a finite set of
  visual syntax shapes: unsupported, identifier, literal, call, binary
  operator, unary operator, and later genuinely distinct layouts. These shapes
  select editor presentation; they are not a catalog of Verse operations.
- Retain the official VST node type and tags, complete source range,
  operator-token range, and ordered operand ranges on each recursive,
  revision-specific expression descriptor. Keep trivia, separators,
  blank-line count, and final-value status on the enclosing clause item.
- Represent an operator occurrence by its generic syntax shape, source token,
  and resolved semantic `CFunction`. Remove `EVerseExpressionKind::Addition`;
  adding a new operator or overload must not require a new expression enum
  value.
- Keep every expression and operand range source-exact and VST-derived. A tile
  that displays expression source must decode only its own byte range.
- Preserve unsupported tiles for VST forms without a visual renderer. The
  official VST and semantic program may understand more expressions than the
  visual editor currently presents.
- Retain Add as the first binary-operator layout: place its operands above-left
  and below-left, expose semantic parameter sockets on the left and its result
  socket on the right, and draw all operand and return connections through the
  shared graph connection layer.
- Resolve Add overloads exclusively from the current semantic snapshot. Do not
  hardcode int, float, array, string, or other Add signatures. Types newly
  supported by the active Verse compiler must work automatically when the
  generic binary layout can present them.
- If Add is the function's final value, connect its result to the Return tile
  through the existing implicit-return behavior. Keep stable socket identities,
  filled connected sockets, execution flow, pan, zoom, clipping, selection, and
  source-range behavior.
- Generalize the same binary layout to arithmetic, comparison, Boolean,
  assignment, and user-defined operators without assuming `(T, T) -> T`.
  Semantic signatures must support asymmetric parameters, different result
  types, reference destinations, unary operators, overloads, and generics.
- Preserve evaluation order when later combining homogeneous operator chains.
  Partition mixed left-associative chains into maximal homogeneous visual runs
  without merging unlike operations.
- Remove `EVerseOperatorKind`, `FVerseOperatorTyping`, the hardcoded Add
  candidate, manual overload tables, string-based compatibility authorization,
  and the manually declared `string + string` case unless the active compiler
  actually exposes it.
- Test exact VST and source ranges, official syntax identity, semantic
  definition binding, recursive operand order, byte preservation, indexed
  sockets, child wires, execution flow, and implicit returns. Verify compiler-
  supplied int, float, and generic-array Add overloads; imported operators such
  as vector or color Add; asymmetric and generic signatures; and absence of
  overloads not provided by the active compiler.
- Add fixture-defined functions and operators and verify they become searchable
  without changing plugin enums, operator tables, or candidate registries.
- Test unsaved semantic overlays, exact-revision publication, stale-result
  rejection, failed-analysis behavior, and prospective syntax and semantic
  validation. Preserve the existing localized-edit, UTF-8, save, and dirty-state
  coverage.

### 14. Function calls (complete)

- Represent project functions, imported functions, native functions, and
  intrinsics with one generic call descriptor bound to a semantic `CFunction`.
- Reuse Step 11's scope-aware semantic discovery; do not create a second
  function registry or special intrinsic path.
- Display and edit arguments from the selected `SSignature`, including named,
  asymmetric, overloaded, and generic parameters.
- Render parameter and result sockets from compiler types and enforce
  compatibility through the shared semantic candidate pipeline.
- Generate and prospectively validate ordinary call source through the generic
  expression action pipeline before committing a localized replacement.

### 15. Control expressions (complete)

- Add branching and looping constructs such as `if`, `for`, and `loop`
  (`while` is not a Verse syntax form).
- Represent each body with its own automatically created tile region.
- Display main and nested execution paths distinctly.
- Preserve and expose supported formatting variations.
- Add whitespace properties for choosing among supported body styles.

### 16. Failable `if` predicates

Implement the reusable failure-context foundation through `if` first. Each
substep must leave a visible, usable result and must be completed in order.

#### 16.1. Failure-aware sockets and wires (complete)

- Derive failure from the active UE6 compiler's exact semantic
  `CanFail()` result and derive the carried value type from the compiler result
  type. Do not maintain an editor catalog of failable functions or operators.
- Model the outcome of an expression independently from its value type:
  ordinary value, failable typed value, or failure-only. Failure is a semantic
  decoration, not a new Verse type.
- Continue using the ordinary Blueprint color for the carried type. Draw a
  failable typed socket as that type's colored diamond and annotate its wire
  with repeated bright-gold diamonds. Draw failure-only sockets and wires with
  gold diamonds.
- Extend the shared graph-surface connection renderer rather than adding
  owner-specific wire widgets. Resolve all geometry in graph-local paint space
  so the decorations remain aligned after moving, redocking, panning, or
  zooming the editor.
- When an exact semantic snapshot is unavailable, reserve stable decoration
  geometry but do not invent a type, effect, or failure capability.
- Add a fixed `Tests/Fixtures/failure_contexts.verse` fixture inside the plugin.
  Never use the mutable development corpus for this coverage. Test failable and
  non-failable calls, operators, casts, carried types, failure-only outcomes,
  zoom, pan, and window movement.

This slice visibly distinguishes values whose failure is still propagating
without changing any control tile yet.

#### 16.2. Reusable failable-block tile (complete)

- Add a selectable failable-block representation using the shared
  `SVerseTile` behavior, line numbers, golden selection outline, Details panel,
  and graph-surface painting.
- Paint four bright `#FFDC4A` diamonds at the tile's corners.
- Fill the entire interior with an opaque, dark diamond pattern derived from
  the current Blueprint-like graph background and tinted subtly gold. Do not
  use a translucent panel that obscures or washes out its child tiles.
- Present one ordered vertical expression chain. Do not create parallel lanes.
- Put one downward execution home plate at the top inside list-capable
  failable blocks. Arrange their expressions below it through the normal solid
  white execution chain.
- When a standalone or result-producing failable block preserves its final
  expression value, expose that value from a right-edge, base-type-colored
  diamond socket. Owner policies in later steps may deliberately suppress that
  socket, transform it into a normal handled-result socket, or move ownership
  of the result to an enclosing boundary.
- Treat expression wires as confined to their lexical block. Execution wires
  may not enter or leave a failable block except through the owner's defined
  structural connections. Explicit typed binding ports may cross the boundary
  where a later substep permits them.
- Add layout and paint tests for empty and populated blocks, corner
  decorations, child clipping, selection, and graph-coordinate alignment.

This slice produces the common visual container that `if`, `for`, and later
failure-context owners will reuse.

#### 16.3. Place the failable predicate beside `if` (complete)

- Derive an `if` failure-context descriptor from the VST. It must retain the
  owner's identity, exact source range, ordered predicate expression ranges,
  punctuation and separator ranges, insertion points, and current revision.
- Replace the hardcoded Boolean condition input with a reusable external
  failable block automatically placed to the left of the `if` tile.
- Feed the failable predicate's structural success into the existing `if`
  branches. The `if` owner discards the predicate's value, so do not display a
  right-side value socket for this block even when its final expression has a
  value.
- Keep the `if` tile's incoming, Completed, True, and False execution paths and
  body layout. The new predicate container must not become part of the outer
  statement execution chain.
- Rebuild the descriptor and tile tree from edited source after every accepted
  change; never serialize the visual tree.
- Test brace and indentation forms, empty and multiple predicates, nested
  failure contexts, and byte-accurate ranges.

This slice makes the new representation visible in every parsed `if`.

#### 16.4. Scoped predicate bindings (complete)

- Record each binding introduced by an `if` predicate using compiler identity,
  displayed name, compiler type, declaration range, and legal consumer scopes.
- Expose each binding as a typed socket on the right edge of the external
  failable-context header. Put these sockets below the gold failure socket,
  vertically center the complete socket group, and grow the header to contain
  it. Connect each defining expression inside the failable block to its
  boundary port.
- Make an `if` binding available only in the corresponding `then` body. It must
  never appear in `else` or after the `if`.
- Use the same scope-aware expression search and semantic compatibility checks
  as other identifiers. Do not synthesize bindings from source spelling alone
  when exact semantic identity is unavailable.
- Test shadowing, duplicate displayed names, nested predicates, legal `then`
  consumers, and rejection in `else` and outer scopes.

This slice makes identifiers created by successful predicates visibly usable
inside the True body.

#### 16.5. Edit ordered expressions inside failable blocks (complete)

- Generalize executable-clause editing so function bodies and list-capable
  failable blocks use the same insertion, deletion, and layout mechanisms.
- Dragging from the block's top home plate inserts before its first expression.
  Dragging from an expression's execution output inserts after that expression
  and opens the normal expression search.
- Reuse semantic expression actions for identifiers, calls, and operators. Add
  structural source actions for the supported public failure-context forms;
  do not add per-function expression kinds.
- Add the generic delete command for a selected expression.
- Allow vertical drag reordering only within the same clause. Reject moves
  across failure contexts or other lexical boundaries.
- Apply insertion, deletion, and reorder as localized UTF-8 replacements.
  Preserve punctuation, separators, indentation, blank lines, line endings,
  and comments not owned by the edited expression.
- Determine comment ownership from the VST. If ownership is ambiguous, keep the
  comment fixed rather than guessing.
- Use one atomic validated multi-range operation for a reorder. Do not add
  undo/redo history in this prototype step.
- Prospectively parse and rebuild the VST before accepting source. Reject
  syntactically invalid replacements without mutation, but allow semantic
  errors. After acceptance, reparse, rebuild the graph, and schedule semantic
  analysis.
- Extend the fixed fixture with insert, delete, reorder, nested-context,
  comment, mixed-line-ending, and byte-preservation cases. Build the editor
  target and run the focused tests from the command line.

This slice makes the `if` predicate block directly editable with the same
workflow used elsewhere in the function graph.

### 17. Failable `for` filters and generators

Implement `for` as the next prototype owner after the `if` foundation.

#### 17.1. Render the ordered `for` failure context

- Derive the `for` filter list, exact ranges, separators, punctuation,
  insertion points, and body range directly from the VST.
- Place one reusable failable block inside the `for` tile and render all
  filters and generators as a single ordered vertical chain. Failure stops the
  remaining chain; do not present independent lanes.
- A `for` owner discards the filter chain's value, so omit the block's
  right-side value socket.
- Preserve the existing outer execution and loop-body presentation while
  preventing internal execution wires from crossing the filter-block boundary.
- Test empty, single, multiple, mixed generator/filter, brace, indentation, and
  nested `for` forms.

This slice gives existing `for` expressions their final predicate layout.

#### 17.2. Create and edit generators

- Offer generator creation through the ordinary context menu and tile workflow;
  do not introduce a dedicated generator dialog.
- Insert a source-safe, collision-free initial generator such as
  `Item : array{}`.
- Expose the generated name in Details and rename it through the validated
  identifier-editing path.
- Show the iterable as a normal typed input socket. Wiring a compatible
  iterable replaces the `array{}` placeholder through a localized source edit.
- Support the optional key binding when the compiler identifies a map
  generator.
- Reuse Step 16.5 for insertion, deletion, reordering, syntax validation,
  byte preservation, and graph rebuilding.
- Test collision-free names, iterable replacement, maps, malformed prospective
  syntax, and semantic-error acceptance.

This slice lets a user visibly create a valid generator without typing Verse.

#### 17.3. Expose generator and filter bindings

- Record generator and predicate bindings with compiler identity, type,
  declaration range, and precise validity interval.
- Expose them as typed sockets on the right edge of the `for` tile and connect
  their internal definitions to those ports.
- Make each binding available to later filters in the same ordered chain and
  to the loop body, but never before its declaration or outside the `for`.
- Filter expression search and connection targets by these exact scopes.
- Test dependent filters, key/value map bindings, shadowing, nested loops, body
  consumers, and every rejected out-of-scope use.

This slice completes the useful data flow from a `for` predicate into its body.

### 18. Failable `<decides>` function bodies

Implement deciding functions as the final prototype failure-context owner.

#### 18.1. Mark the entire deciding function graph

- Detect `<decides>` from exact compiler semantics and associate the function
  body's VST clause with the failure context.
- Do not create a selectable nested failable-block tile. Instead, paint the
  entire function graph background with the same opaque dark, subtly gold
  diamond pattern used by failable blocks, including the function entry, body,
  and return area.
- Keep tile placement, RMB panning, cursor-relative zooming, clipping,
  selection, and graph persistence unchanged. Paint the pattern in graph-local
  coordinates through `SVerseGraphSurface`.
- Add visual and coordinate tests for moving, redocking, zooming, and panning a
  deciding function beside an ordinary function.

This slice makes a deciding function's failure context immediately recognizable
without adding another visual nesting level.

#### 18.2. Use the function boundary as the result owner

- Treat the final expression in a deciding function as the function result
  according to the compiler-selected return type.
- Do not add a separate right-side result socket to the background failure
  context. The existing function boundary and return presentation own the
  result.
- Show failure-aware sockets and wires on calls to deciding functions whenever
  failure remains unhandled at the call site.
- Reuse the generalized executable-clause editor for the deciding body and
  preserve all source bytes outside each localized edit.
- Test void and value-returning deciding functions, implicit final values,
  nested failure contexts, deciding calls, exact `CanFail()` propagation, and
  transformed output source.

This slice completes the prototype path from authoring a deciding function to
using its failable result.

## Additional Implementation Steps

### Further work
- if there are local edits show a modal confirmation before closing yes/no/cancel
- Dragging tabs left and right
- Save the open function tabs
- Zoom into the mouse pointer position
- Inform the user that their open file is not registered with Solaris
- Context menu hover tooltips for function descriptions
- Hover sockets

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
- Add the same insertion and deletion behavior inside module bodies, using the
  module clause descriptor's interior and empty-body insertion anchor.
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
- Allow definitions to move within a module and between valid module and global
  scopes.
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

- Consume the existing VST-derived clause descriptor for enum bodies.
- Represent each enum label with an indented tile.
- Add renaming and identifier validation.
- Add drag handles for reordering.
- Add removal buttons.
- Add insertion controls before, between, and after labels.
- Add the open/closed property.
- Add lossless fixtures for supported enum formatting styles.

### 8. Global constants

- Model the initializer as its own VST-derived expression range rather than
  treating it as a delimited definition body.
- Display and edit the declared type.
- Provide an appropriate editor for simple typed literal values.
- Provide selection controls for enums, types, and other discoverable values.
- Add Blueprint-style type indicators and usage indicators.
- Preserve complex initializer expressions as raw tiles initially.

### 9. Structs

- Use the shared VST clause descriptor when presenting struct fields as child
  tiles.
- Add shared effect support suitable for structs, classes, and interfaces.
- Enforce and explain effect availability and inheritance rules.
- Extend definitions to represent mutable `var` fields.
- Add constant and optional properties.
- Support field initializers and the `converges` requirement.
- Reuse existing single-definition selection and movement behavior.
- Partition struct interiors into VST-derived fields and lossless raw gaps,
  including empty brace and empty indentation forms.

### 10. Classes

- Use the shared VST clause descriptor when presenting class members as child
  tiles.
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

- Use the shared VST clause descriptor when presenting interface members as
  child tiles.
- Add interface tiles and interface-specific effects.
- Add inherited-interface lists.
- Support interface fields.
- Add getter and setter selection for fields with accessors.
- Add support for external definitions.
- Recursively derive interface fields, accessors, nested definitions, and raw
  gaps from the interface VST body while retaining the parent interior range
  for empty-body insertion and trivia preservation.

### 12. Literal expressions (completed)

- Add literal entries for each supported primitive type.
- Provide type-appropriate inline and property-panel editors.
- Do not offer pseudo-literals for floating-point special values; Verse source
  must remain limited to syntax accepted directly by the compiler.
- Restrict the first implementation to literals that require no casts.

### 13. `first` failure contexts

#### 13.1. Render `first` filters and body

- Reuse the `for` failure-context descriptor, single ordered filter chain, and
  generator presentation for `first`.
- Discard filter values while preserving the selected body's value.
- Display the result as a base-type-colored failable diamond socket because the
  `first` expression itself can fail.
- Keep execution confined to the owner and reuse the shared graph connection
  renderer.

This slice makes existing `first` expressions visually distinct from `for`.

#### 13.2. Edit and scope `first`

- Reuse generator creation, iterable replacement, map bindings, insertion,
  deletion, and within-clause reordering.
- Make bindings available to later filters and the selected body, but nowhere
  outside the `first`.
- Test empty inputs, successful and failing selection, body result types,
  nested forms, generators, maps, scope boundaries, and byte preservation in
  the fixed failure-context fixture.

This slice makes the `first` representation fully authorable.

### 14. `option{}` failure contexts

#### 14.1. Render handled optional construction

- Place the ordered failable block inside the `option{}` owner tile.
- Because `option{}` handles failure, display its output as a normal Blueprint
  socket of type `?T`, not as a failable diamond socket.
- Preserve the final successful expression value as `T`; a failure produces
  the empty option.
- Test value and failure paths, nested contexts, empty and multi-expression
  blocks, and compiler-derived optional result types.

This slice visibly demonstrates failure being handled rather than propagated.

#### 14.2. Edit `option{}` contents

- Reuse top-home-plate insertion, expression insertion, deletion, and
  within-clause reordering.
- Preserve the owner's delimiters and all unrelated UTF-8 bytes.
- Reject syntactically invalid changes without mutation and allow semantic
  errors for later compilation reporting.

This slice makes optional construction editable through the shared workflow.

### 15. `logic{}` failure contexts

#### 15.1. Render handled logic construction

- Place the ordered failable block inside the `logic{}` owner tile.
- Discard successful expression values and convert success or failure into a
  normal `logic` output socket. Do not display a failable diamond after the
  owner because failure has been handled.
- Test success, failure, nested contexts, and exact compiler-derived output.

This slice makes the failure-to-logic boundary visible.

#### 15.2. Edit `logic{}` contents

- Reuse the common list editor and source-preservation rules.
- Test insert, delete, reorder, comments, punctuation, and mixed line endings
  against the fixed fixture.

This slice completes authoring for `logic{}`.

### 16. `not` failure contexts

#### 16.1. Render failure inversion

- Represent `not` as an owner containing its failable operand.
- Discard the operand's value and invert success and failure.
- Show a gold failure-only diamond when the result continues to propagate; do
  not invent a carried value type.
- Keep `not` to one operand unless its VST operand is itself a list-capable
  block. Do not manufacture list syntax.

This slice makes failure inversion readable in the graph.

#### 16.2. Edit the `not` operand

- Reuse normal expression replacement for a single operand and the shared list
  editor only when the VST exposes a real list-capable operand.
- Test single and list-capable forms, nested failures, syntax rejection, and
  byte preservation.

This slice completes authoring without broadening Verse syntax.

### 17. Left-hand `or` failure contexts

#### 17.1. Render fallback data flow

- Represent only the left operand of `or` as the failure context that selects
  whether the fallback right operand is evaluated.
- Preserve the left value on success and join it with the fallback result.
- Derive the joined compiler result type and display a normal typed socket when
  failure is fully handled. Display a base-type-colored failable diamond only
  when the right operand can still fail.
- Keep the two operands and their evaluation order explicit; do not merge the
  operator into a parallel-lane presentation.

This slice shows both fallback control flow and the surviving value.

#### 17.2. Edit and validate `or`

- Reuse ordinary operand editing and source actions; do not expose the
  left-hand context as an independently reorderable list unless its VST operand
  is list-capable.
- Test left success, left failure with fallback, failable fallback, joined
  types, nesting, syntax rejection, semantic-error acceptance, and exact source
  preservation.

This slice completes the public fallback owner.

### 18. Failure-context completeness

- Recognize the complete public UE6 failure-context owner set implemented by
  the preceding steps: `if`, `for`, `first`, `<decides>` function bodies,
  `option{}`, `logic{}`, `not`, and the left operand of `or`.
- Treat `and` expressions and ordinary block expressions as inheriting their
  surrounding failure context rather than creating a new owner tile. Continue
  to decorate their result when exact semantics say failure propagates.
- Explicitly exclude Epic-internal `await`, `batch`, `upon`, and `when` forms
  from customer-facing creation and presentation until they become public
  language features.
- Verify all owner result policies, exact `CanFail()` behavior, value types,
  scoped bindings, socket shapes, wire decorations, nesting combinations, and
  transformed output with the fixed fixture.
- Verify top-home-plate and after-expression insertion, deletion, reordering,
  generators, maps, cross-context rejection, ambiguous-comment preservation,
  mixed line endings, graph alignment, and non-mutating syntax rejection across
  every list-capable owner.
- Build the editor target and run the focused failure-context, expression,
  semantic-workspace, source-preservation, and graph-coordinate automation
  tests from the command line.

This step confirms the editor covers every currently public UE6 failure context
without relying on web documentation or private compiler constructs.

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
