# Installation

## Prerequisites

- Docker (all building/testing/installing happens inside a container — see
  [../CLAUDE.md](../CLAUDE.md)).
- XFCE4 panel (`xfce4-panel`) on the host.

## Install

```sh
./dev.sh install
xfce4-panel -r
```

This builds the plugin inside Docker, then copies the compiled `.so`,
`.desktop` file, and icon into:

- `~/.local/lib/xfce4/panel/plugins/libxfce4-journal-plugin.so`
- `~/.local/share/xfce4/panel/plugins/xfce4-journal-plugin.desktop`
- `~/.local/share/icons/hicolor/scalable/apps/xfce4-journal-plugin.svg`

`xfce4-panel -r` restarts the panel so it picks up the new plugin.

## Add it to a panel

Right-click a panel → **Panel** → **Add New Items…** → find **Journal** →
**Add**.

## First-run configuration

Right-click the new panel item → **Properties…** (or use the plugin's own
"Preferences…" menu item) to add your first journal: a name, a root
directory, a day-file layout, and its categories. See
[CONFIGURATION.md](CONFIGURATION.md) for the field reference and the exact
marker-matching rules.
