# xfce4-journal-plugin

## Build/test/install: Docker only, never bare meson/ninja on the host

`./dev.sh` is the *only* sanctioned entry point for building, testing, or
installing this project. Every subcommand shells out to `docker`; the
compiling, testing, and file-copying all happen inside an
`archlinux:base-devel`-based container built from the root `Dockerfile`. Do
not run `meson setup`/`meson compile`/`meson test`/`meson install` directly
on the host — the compiled `.so` is dlopen'd directly by the host's live
`xfce4-panel`, so it needs an Arch-based build environment to track the
host's own library ABI closely (this project targets Manjaro/Arch).

The only host-side actions in this project are: supplying bind-mount targets
to `docker run` (inside `dev.sh` itself), and `xfce4-panel -r` / manual GUI
interaction to actually see the plugin (Docker cannot drive the live desktop
session).

## The core model: day files are content, config.json is settings only

A journal's day files are the sole source of truth for *content* — every
bullet a user has ever added lives only in plain markdown on disk, wherever
that journal's `root_dir` points. `config.json` holds only *settings*:
which journals exist, their layout/template, and their category
definitions. There is no
derived run-state to keep in sync with a file's contents — a day file isn't
an event log being replayed, it's prose a user is appending structured
bullets into. So `Config`/`Journal`/`Category` (`config.h`) carry no
transient fields at all: everything in them is persisted, full stop, and
`panel_widget.c` needs no tick timer, no mtime-watch, no "did this change
externally" reconciliation.

Every add is synchronous and user-initiated: open the Add-entry dialog,
type, click Add. `entry_writer_add_entries()` reads a day file's current
on-disk content fresh at that exact moment, splices the new bullets in via
`markdown_editor_insert_entries()`, and writes the whole file back
atomically. There's no polling loop and no background reconciliation to
design around — see "Design decisions" below for why that's a
deliberate, justified simplification rather than a gap.

## Layout

- `src/config.c`/`.h` — hand-rolled JSON persistence
  (`~/.config/xfce4-journal-plugin/config.json`) via jansson: atomic write
  (temp file + chmod 0600 + rename), malformed file → `.bak` + defaults,
  never crashes. Defines `Category` (id, label, marker) and `Journal` (id,
  name, root_dir, layout_template, author, new_file_template, ordered
  categories) — the structs every other module uses. GTK-free, unit-tested.
- `src/template.c`/`.h` — one pure function, `template_render()`:
  single-pass `{TOKEN}` substitution against a `GHashTable`, leaving an
  unknown token's braces untouched. The one place the `{TOKEN}` grammar is
  defined; reused by `layout.c` for both path tokens (`{YYYY}`/`{MM}`/`{DD}`)
  and new-file-content tokens (those plus `{DATE}`/`{AUTHOR}`). GTK-free,
  unit-tested.
- `src/layout.c`/`.h` — renders a `Journal`'s `layout_template` against a
  `GDateTime` into a day file's path (relative and absolute), and renders
  its `new_file_template` into a brand-new file's starting content. Pure
  except for one filesystem-touching convenience wrapper
  (`layout_today_absolute_path`, which just calls
  `g_date_time_new_now_local()`). GTK-free, unit-tested.
- `src/markdown_editor.c`/`.h` — the core insertion algorithm, and the
  module most worth reading before changing anything here: pure
  string-in/string-out functions, no I/O. CRLF-tolerant line splitting/
  joining, a style-agnostic "does this line look like a header" check (ATX
  `#`-`######` or a whole line wrapped in matching `*…*`/`**…**`), literal
  marker-line matching (trimmed, `\r`-tolerant), and
  `markdown_editor_insert_entries()` — finds the `marker_occurrence`-th
  (1-based, default 1) line matching a category's marker, inserts new
  bullets immediately before the next header-like line following it (or at
  EOF), and falls back to appending the marker plus the new bullets at
  end-of-file if that occurrence isn't present. A marker that repeats in a
  day file (e.g. `*Reviews*` under both `*Development*` and `*Operational*`)
  is disambiguated by giving two categories the same `marker` with
  `marker_occurrence` 1 and 2 — see `docs/CONFIGURATION.md`. GTK-free,
  heavily unit-tested against the exact sample file this plugin was
  designed against (see `tests/test_journal.c`'s `SAMPLE_FILE_LF`).
- `src/entry_writer.c`/`.h` — the only module that touches a day file on
  disk: `entry_writer_ensure_file_exists()` (creates parent dirs + writes
  the rendered new-file content, never overwrites an existing file) and
  `entry_writer_add_entries()` (ensure-exists, re-read fresh, splice via
  `markdown_editor_insert_entries()`, atomic write — temp file + rename,
  deliberately *without* config.json's chmod 0600; see below). GTK-free but
  real I/O.
- `src/panel_widget.c`/`.h` — the button (icon-only, no live-updating
  label or tick timer — there's no run-state to display), the left-click
  menu (right-click intentionally left unhandled so it falls through to the
  standard XFCE panel context menu — Move/Properties/Remove), and the multi-line Add-entry
  dialog. The menu is entirely flat, using a section-header idiom
  (`make_menu_section_header`) rather than nested
  GtkMenus: with more than one journal configured, a `GtkCheckMenuItem` per
  journal (no group — exactly one is ever active in
  `PanelWidget.active_journal_id`, session-only state, so nothing more than
  that single source of truth is needed to keep them mutually exclusive
  across rebuilds) lets the user pick which one is "active" (see
  `get_active_journal()`), and that journal's "Journal actions" header
  (Open today's entry/Open journal folder) plus its "Add entry" header
  (its categories) always follow directly below, never nested.
- `src/preferences.c`/`.h` — 100% hand-built GTK preferences dialog (no
  GtkBuilder/.ui files anywhere), a `GtkTreeView` journal list with Add/Edit/Remove/Move-up/
  Move-down, and a per-journal editor dialog that itself contains a nested
  `GtkTreeView` category list with its own Add/Edit/Remove/Move-up/
  Move-down, mutated purely in-memory until the outer journal editor's own
  OK.
- `src/plugin.c` — the `XFCE_PANEL_PLUGIN_REGISTER` entry point. No
  notification init/shutdown — see below for why this plugin has no
  libnotify dependency at all.

## Why no libnotify

Desktop notifications suit *asynchronous, background-detected* state
changes — events the user isn't necessarily looking at when they happen.
This plugin has no background state at all: every write is synchronously triggered by the user
clicking "Add" in a modal dialog that is itself the confirmation, and any
failure already surfaces via a blocking `GtkMessageDialog`. There is
nothing left for a desktop notification to usefully report, so
`notifications.c` and the `libnotify` dependency are simply absent.

## Design decisions

- **Read-modify-write, not append-only.** A bare append (open in `"a"`
  mode, write one line) can't work here: inserting a bullet *mid-file*,
  immediately before some later header line, requires reading the whole file, computing "original content with N new lines
  spliced in at some index" via `markdown_editor_insert_entries()`, and
  writing the entire result back — forcing the same atomic-write recipe
  `config.c` already uses for `config.json` (temp file + rename) rather
  than a bare append.
- **No chmod 0600 on day files** (unlike `config.json`). A day file is
  unambiguously the user's own content, frequently living inside a
  git-tracked personal repo the user edits with other tools and expects
  normal `0644`-ish permissions on — so `entry_writer_add_entries()`
  deliberately skips the chmod step and lets the temp file inherit the
  process's normal umask.
- **No locking despite the read-modify-write race window.** In theory an
  external editor could resave a day file between opening the Add-entry
  dialog and clicking "Add." Two things make this a non-issue without
  adding any locking: the dialog never displays or caches the file's
  existing content (it's just a blank compose box, so there's no "the text
  I'm editing went stale" problem), and
  `entry_writer_add_entries()` performs its read → splice → write as one
  uninterrupted sequence at the moment "Add" is clicked, always re-reading
  fresh rather than trusting anything read earlier.
- **No derived run-state / no tick timer.** See "The core model" above.
