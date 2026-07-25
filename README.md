# Visual Verse Editor

Visual Verse Editor brings a block-based Verse authoring experience into Unreal
Editor. It's designed to be as approachable as Blueprint while simultaneously
exposing the entire language.

> Visual Verse Editor is in early development and is not yet ready for
> production use.

## About

Visual Verse Editor presents Verse code as a visual workspace inspired by
Blueprint and block-based programming tools. Verse definitions and expressions
appear as readable, discoverable blocks with familiar colors, connections, and
editing controls.

The editor is intended to prevent invalid edits where possible, surface useful
compiler feedback close to the affected code, and help users discover the
values and operations available in the current context. Existing files remain
ordinary Verse text, and the editor aims to preserve their original formatting
except where the user makes a change.

## Getting Started

Visual Verse Editor requires an Unreal Engine 6 main-branch source checkout
with Verse support and a game project that uses that checkout.

Clone the repository into the `Plugins` directory of your game project:

```text
cd <UE6-main>/<YourGame>/Plugins
git clone <repository-url> VisualVerseEditor
```

The resulting layout should be:

```text
<UE6-main>/
└── <YourGame>/
    └── Plugins/
        └── VisualVerseEditor/
```

Regenerate your game project files, build your game’s Editor target, and open
the project in Unreal Editor. Enable **Verse** and **Visual Verse Editor** for
the project if they are not already enabled, then restart the editor when
prompted.

## Contributing

Bug reports, documentation improvements, feature proposals, tests, and code
contributions are welcome. Please read [CONTRIBUTING.md](CONTRIBUTING.md) before
opening an issue or pull request.

## License

Visual Verse Editor is provided under the Fab Standard License. See
[LICENSE.md](LICENSE.md).
