# xfce4-journal-plugin

An XFCE panel plugin for adding dated entries to one or more markdown journals.
Each journal is a tree of day files (`<root>/YYYY/MM/YYYY-MM-DD.md` by default,
fully configurable), and each defines its own new-file template and ordered list
of categories. Add an entry from the panel and it's spliced straight into the
right section of today's file.

## Features

- **Multiple journals.** Each has its own root directory, day-file layout
  (`{YYYY}`/`{MM}`/`{DD}` tokens — nested by default, or a flat
  `{YYYY}-{MM}-{DD}.md` layout works too), optional author, and a raw
  new-file template (`{DATE}`/`{AUTHOR}`/`{YYYY}`/`{MM}`/`{DD}` tokens)
  written verbatim the first time a day's file is needed.
- **Categories, matched by literal marker line.** Each category stores the
  exact header line as it appears in your files, whatever its markdown
  style — an ATX heading (`"## TODO"`) or a bold pseudo-heading
  (`"*Reviews*"`). A new entry is spliced in right before the next
  header-like line, wherever that falls, leaving everything else untouched.
- **Flat left-click menu**, right-click left unhandled so XFCE's own panel
  context menu shows through as usual. With more than one journal, a check
  item per journal lets you pick which one the rest of the menu acts on (no
  submenus); its "Journal actions" (Open today's entry/Open journal folder)
  and "Add entry" (its categories) sections always follow directly below.
- **Multi-line Add-entry dialog** — each non-empty line you type or paste
  becomes its own bullet, all inserted together in the order typed.
- **Open today's entry** / **Open journal folder** actions per journal.
- A day file that doesn't yet have a given category's marker still gets the
  entry: it's appended (marker included) at the end of the file as a
  visible fallback, never silently dropped.

See [docs/CONFIGURATION.md](docs/CONFIGURATION.md) for the exact config
schema and marker-matching rules.

## Install

See [docs/INSTALLATION.md](docs/INSTALLATION.md) — in short:

```sh
./dev.sh install
xfce4-panel -r
```

Then add "Journal" from the panel's Add New Items dialog.

## Development

Everything builds/tests/installs through Docker via `./dev.sh` — never run
`meson`/`ninja` directly on the host. See [CLAUDE.md](CLAUDE.md) for why.

```sh
./dev.sh build     # compile
./dev.sh test      # run the unit tests
./dev.sh install   # install into ~/.local
./dev.sh uninstall # remove what install placed
```

## Uninstall

See [docs/UNINSTALL.md](docs/UNINSTALL.md).

## License

MIT — see [LICENSE](LICENSE).
