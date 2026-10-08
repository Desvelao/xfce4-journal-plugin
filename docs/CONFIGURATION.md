# Configuration

All settings live in `~/.config/xfce4-journal-plugin/config.json`, written and read by
`src/config.c`. This file holds only *settings* — journal definitions, category
definitions — never any journal content. Every day file stays plain markdown on disk,
wherever you pointed its journal's root directory.

## Schema

```json
{
  "config_schema_version": 1,
  "journals": [
    {
      "id": "journal-3f2a9c10",
      "name": "Work",
      "root_dir": "/home/you/journal",
      "layout_template": "{YYYY}/{MM}/{YYYY}-{MM}-{DD}.md",
      "author": "Jane Doe",
      "new_file_template": "# Report\n*{AUTHOR} {DATE}*\n*Development*\n*Triage*\n*Newly opened*\n*Worked on*\n\n*Reviews*\n\n\n*Operational*\n*Triage*\n*Newly opened*\n*Worked on*\n*Reviews*\n\n*Meetings*\n\n\n*Miscellaneous*\n\n\n# My things\n\n## TODO\n\n### On hold\n\n## Issue\n\n## Community\n\n## Meeting\n\n## Daily\n\n## Miscellaneous\n",
      "categories": [
        { "id": "cat-1a", "label": "TODO", "marker": "## TODO", "marker_occurrence": 1, "entry_type": "todo" },
        { "id": "cat-1b", "label": "On hold", "marker": "### On hold", "marker_occurrence": 1,
          "entry_type": "custom", "entry_template": "- [ ] $ENTRY #hold" },
        { "id": "cat-1c", "label": "Issue", "marker": "## Issue", "marker_occurrence": 1 },
        { "id": "cat-1d", "label": "Community", "marker": "## Community", "marker_occurrence": 1 },
        { "id": "cat-1e", "label": "Meeting", "marker": "## Meeting", "marker_occurrence": 1 },
        { "id": "cat-1f", "label": "Daily", "marker": "## Daily", "marker_occurrence": 1 },
        { "id": "cat-1g", "label": "Miscellaneous", "marker": "## Miscellaneous", "marker_occurrence": 1 },
        { "id": "cat-1h", "label": "Reviews (Development)", "marker": "*Reviews*", "marker_occurrence": 1 },
        { "id": "cat-1i", "label": "Reviews (Operational)", "marker": "*Reviews*", "marker_occurrence": 2 }
      ]
    }
  ]
}
```

All of this is normally edited through Preferences, never by hand — but the file is
plain JSON if you need to script something.

## Journal fields

- `name` — shown in the panel menu (only when more than one journal is configured).
- `root_dir` — absolute path each day file is resolved under.
- `layout_template` — a day file's path relative to `root_dir`, with tokens `{YYYY}`
  `{MM}` `{DD}`. Default: `{YYYY}/{MM}/{YYYY}-{MM}-{DD}.md`. A flat layout with no
  subfolders works too, e.g. `{YYYY}-{MM}-{DD}.md`.
- `author` — optional; substituted into a new file's `{AUTHOR}` token.
- `new_file_template` — raw text (tokens `{DATE}` `{AUTHOR}` `{YYYY}` `{MM}` `{DD}`)
  written verbatim the first time a given day's file is needed. Leave blank for a
  genuinely empty new file.
- `categories` — ordered list of `{ label, marker, marker_occurrence, entry_type,
  entry_template }`, in the order they appear in the Add-entry menu. See
  "Entry types" below.

## Entry types

Each category chooses how its entries are written (`entry_type`):

- `bullet` (default, also used when the key is absent) — `- text`
- `todo` — `- [ ] text`
- `custom` — `entry_template` with every `$ENTRY` replaced by the text, e.g.
  `"* [ ] $ENTRY #work"` gives `* [ ] fix login #work`. The template must
  contain `$ENTRY`; a custom category without it is loaded as `bullet`.

Each non-empty line typed in the Add-entry dialog becomes its own formatted line.

## How a category's marker is matched

Each category stores the *exact* header line as it appears in your day files,
whatever its style — an ATX heading (`"## TODO"`, `"### On hold"`) or a bold-wrapped
pseudo-heading (`"*Reviews*"`, `"**Reviews**"`). Adding an entry:

1. Finds the `marker_occurrence`-th line in today's file that equals the marker (after
   trimming whitespace and a possible trailing `\r`) — `marker_occurrence` defaults to
   `1`, the first match, which is what you want unless the marker text repeats.
2. Inserts the new bullet(s) immediately before the next line that "looks like a
   header" — either an ATX heading (`#` through `######` followed by whitespace) or a
   whole line wrapped in matching `*…*`/`**…**` — or at end-of-file if no such line
   follows.
3. If that occurrence isn't found — including when the marker text appears fewer
   times than `marker_occurrence` requests — the marker line and the new bullet(s)
   are appended at the very end of the file instead, as a visible fallback (never
   silently dropped, never refused).

**A repeated marker is handled with `marker_occurrence`.** The bold Report-section
labels like `*Reviews*` or `*Worked on*` appear twice per day file — once under
`*Development*`, once under `*Operational*` — so two categories can share the same
`marker` and target each occurrence separately, as `cat-1h`/`cat-1i` do above
(`marker_occurrence: 1` for Development, `2` for Operational). If you leave
`marker_occurrence` at its default `1` for a repeated marker, only the first
occurrence is ever reachable — the second one just sits there unmatched.

## `new_file_template` and `categories` are independent

The template is raw text you write; categories don't derive from it and it doesn't
derive from them. If a category's marker isn't present in `new_file_template`, the
very first add on a brand-new file just hits the fallback above — that's expected,
not a bug. To have a marker land in its "natural" spot on a fresh file, include that
exact line in `new_file_template` yourself, as done for every header above — note
that a repeated marker like `*Reviews*` only needs to appear in the template as many
times as the file's own structure calls for; `marker_occurrence` on the category side
is what tells the plugin which of those already-present lines to target.

## External tools

Day files are just markdown. Anything else — a text editor, a script, `git` — can
edit them freely; the plugin always reads a file's current contents fresh at the
moment you click Add, so there's nothing to keep in sync.
