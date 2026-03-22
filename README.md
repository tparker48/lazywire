# lazywire

A terminal UI file manager for managing files across local and remote machines over SSH/SCP.

![screenshot](images/sc.png)

## Usage

**Launch with a remote connection:**
```
lazywire user@host
```
You'll be prompted for the password (no echo), just like `ssh`. Launches directly into a session with that remote.

**Launch without args:**
```
lazywire
```
Opens the TUI where you can add remote connections manually.

## Navigation

- `hjkl` or arrow keys to move
- `Space` to expand/collapse folders
- `Tab` to switch between panes

## Jump Mode

Type `/` followed by a path to jump directly to any location. As you type, lazywire performs a real-time fuzzy search across the filesystem — works on both local and remote panes.

## File Operations

| Key | Action |
|-----|--------|
| `c` | Copy |
| `x` | Cut |
| `p` | Paste (into selected folder, or same level if a file is selected) |
| `d` | Delete |
| `r` | Rename |

Operations work across panes — copy from local, paste to remote, and vice versa.

## Building

```
cmake -B build && cmake --build build
```

Requires `libssh`.
