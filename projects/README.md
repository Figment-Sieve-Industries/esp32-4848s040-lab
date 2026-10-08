# Projects index

Each project is a self-contained PlatformIO project in its own numbered folder. All projects share the board library in `../common/lib/Board4848S040` via `lib_dir = ../../common/lib`.

| # | Folder | Status | Description |
|---|---|---|---|
| 01 | [01-hello-display-touch](01-hello-display-touch/) | Builds ✅ (not yet run on hardware) | Colour test, finger-paint, touch coordinates on screen and serial |

Status values: `idea` → `wip` → `builds` → `works on hw` → `done`.

## Adding a new project
1. Copy `01-hello-display-touch` to `NN-short-name` (next free number).
2. Edit the header comment in `platformio.ini` and `src/main.cpp`.
3. Write a short `README.md` in the project folder: goal, how to run, status.
4. Add a row to this table **and** to the Projects table in the root `README.md`.
5. Commit.

Non-PlatformIO projects (ESPHome YAML, ESP-IDF) go in their own numbered folders too. Note the toolchain in the README.
