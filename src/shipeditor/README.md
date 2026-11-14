# xpilot-tng-shipeditor

Graphical editor for XPilot ship shapes (Dear ImGui + SDL3), built separately from
the main CMake project.

## Build

Part of the normal CMake build (`-DBUILD_SHIPEDITOR=OFF` skips it); needs SDL3 like the
launcher and map editor.

    cmake -B build && cmake --build build --target xpilot-tng-shipeditor
    ./xpilot-tng-shipeditor [shipshapes-file | "(SH: ...)..."]

The launcher's profile editor starts it with the "Edit shipshape" button. `ship_shape`,
`ship_files` and `ship_widgets` (parser, validation, ship list/picker) are shared with the
launcher; the editor uses the launcher's `xpilotrc.cpp`.

## Features

- **Library**: stock ships (`lib/shipshapes.cfg`, read-only), your own collection
  (`~/.config/xpilot-tng/shipshapes.cfg`), any opened file (or drag & drop), and the
  ship currently set in the xpilotrc. Double-click loads a ship; right-click for more
  (copy to collection, use as my ship, reorder, delete).
- **Editing**: tools for hull, engine, main gun, left/right (rear) guns, lights and
  missile racks (keys 1-9, 0). Click to add (on an edge: insert), drag to move,
  right-click to delete, arrow keys nudge. "Mirror L/R" places left/right parts in
  pairs. Flip, rotate, make symmetric, undo/redo, numeric point lists, raw text.
- **Validation**: exact port of `shape2wire()` from `src/common/shipshape.c`
  (size zones, minimum size and width, special points inside the ship). Unset
  special points are shown at the position the game picks.
- **Hitbox**:
  - Walls: the hull edges, shown rotated to any of the 128 directions with the
    server's 1/64 px rounding.
  - Shots, items, balls, mines, missiles: a circle of radius 16 (`SHIP_SZ`) plus the
    object's radius - independent of the shape.
  - Ship vs. ship: centres closer than 26 px.
  - Shield as drawn by the client.
- **Saving**: "Save to collection" writes your collection file (one
  `xpilot.shipShape:` line per ship, usable as `shipShapeFile`). "Use as my ship"
  writes the full definition to `xpilot.shipShape` in the xpilotrc (backup:
  `xpilotrc.bak`).

Note: the launcher picks ships by name from the stock list, so give your own ships
names that do not exist there (the editor warns about clashes).
