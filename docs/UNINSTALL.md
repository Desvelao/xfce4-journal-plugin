# Uninstall

```sh
./dev.sh uninstall
xfce4-panel -r
```

This removes exactly the files `./dev.sh install` placed:

- `~/.local/lib/xfce4/panel/plugins/libxfce4-journal-plugin.so`
- `~/.local/share/xfce4/panel/plugins/xfce4-journal-plugin.desktop`
- `~/.local/share/icons/hicolor/scalable/apps/xfce4-journal-plugin.svg`

It never touches:

- `~/.config/xfce4-journal-plugin/config.json` — your journal and category
  definitions.
- Any journal's own day files, wherever you pointed its root directory (e.g.
  a personal journal repo outside this plugin's own directories).

Remove those yourself if you want a completely clean slate; they're left in
place by default so reinstalling later picks up where you left off.
