# Changelog

## [0.1.0] - Unreleased

- Initial release: define any number of markdown journals, each with its
  own root directory, day-file layout, new-file template, and ordered
  categories.
- Categories are matched by literal marker line (ATX heading or bold
  pseudo-heading), style-agnostic — a new entry is spliced in immediately
  before the next header-like line, or appended (marker included) at the
  end of the file as a visible fallback if the marker isn't present yet.
- Left-click panel menu: journal submenus (when more than one is
  configured) containing per-category items, each opening a multi-line
  Add-entry dialog where every non-empty line becomes its own bullet.
- Per-category entry type: bullet (default), to-do (`- [ ]`), or custom
  template using `$ENTRY` as the text placeholder.
- "Open today's entry" and "Open journal folder" actions per journal.
- Preferences dialog: add/edit/remove/reorder journals, each with a nested
  add/edit/remove/reorder editor for its categories.
