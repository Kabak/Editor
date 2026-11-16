# Roadmap

Planned improvements for the editor. Items are not in a strict order.

## Done

- [x] Automatic encoding detection on open (BOM, UTF-8/16/32, Cyrillic and
      other legacy code pages) and per-tab encoding selection on save
      (menu *Encoding*), line-ending switching, extensible encoding registry.
- [x] Find overview ruler next to the scroll bar (marks of all matches, visible-area box,
      click to scroll), with matching done in short time-limited steps so the editor stays responsive.

## Planned

### Find overview ruler: ideas for later
The ruler (marks of Find matches next to the scroll bar) is done and scans in short steps in the
UI thread. Possible follow-ups, only if measurements on large files show they are needed:
- scan in a worker thread (the matcher is Qt-independent; the text would have to be handed over in
  chunks because `QTextDocument` is not thread-safe), and split very large files across cores;
- split a single very long line into several steps (a step is currently one whole line);
- update the marks incrementally after edits instead of rescanning the whole document;
- optionally show the cursor and selection on the ruler, like VS Code.

### Watching open files for changes on disk

Today the editor reads a file into memory once and never looks at the disk
again; the only protection is the "File changed on disk" prompt shown when
saving. Planned behavior:

- Check every open file when the window regains focus, and periodically
  (about once a second) while the editor is active. Use `QFileSystemWatcher`
  where it is reliable and fall back to comparing size, modification time and
  the existing SHA-256 digest (network drives, some Linux file systems).
- **Tab without unsaved edits:** reload the file silently, keeping the cursor
  position, scroll position and the tab's current encoding and line endings.
- **Tab with unsaved edits:** show a non-blocking banner in the tab:
  *File changed on disk* with **Reload** (discard my edits) and **Keep my
  version** (the next save will overwrite the file).
- **File deleted, moved or inaccessible:** mark the tab (for example
  "file missing"), keep the text in the editor, never close or clear it. Saving
  recreates the file after confirmation.
- Do not react to the editor's own saves.
- Setting in the preferences to turn watching on/off, with a choice of
  "reload silently" or "always ask".
- Tests: external modification, deletion and recreation, modification while the
  tab has unsaved edits, editor's own save, large files.

### Review the built-in syntax highlighting (probably remove it)

Two languages are hard-wired into the program and do not use `.prf` color profiles:

- **C/C++**: files with the extensions `c`, `h`, `cpp`, `hpp`, `cc`, `hh`, `cxx`, `hxx`
  (tree-sitter-cpp grammar; chosen in `Editor::detectLanguage`).
- **JSON**: files with the extension `json` (tree-sitter-json grammar).

Everything else is "Plain text" and is colored only by color profiles. The built-in
highlighting overlaps with the new profiles (`C.prf`, `Cpp.prf`, `H.prf`, `JSON.prf` in
`ColorProfiles/`), its colors cannot be edited by the user, and it adds the tree-sitter
dependency (three downloaded source archives at build time).

Probably to be removed, so that all coloring comes from color profiles:

- remove tree-sitter and its grammars from `CMakeLists.txt` (and the offline-build notes in
  the README), the syntax code in `highlighter.*` and `Editor::detectLanguage`;
- decide what the status bar shows instead of "C/C++", "JSON" and "Plain text"
  (for example the name of the bound color profile);
- before removing, compare the profiles with the built-in coloring on real files, because
  profile rules are plain substring matches and cannot color strings and comments;
  consider a "whole word" option for phrase rules first.
