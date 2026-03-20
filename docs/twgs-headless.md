# Running TWGS Headless

## macOS

Wine uses the native Mac display driver. No virtual framebuffer needed. The GUI window exists but the server works without interacting with it.

```bash
# In a tmux/screen session (detachable)
tmux new-session -d -s twgs \
  'DYLD_LIBRARY_PATH=/opt/wine/lib WINEPREFIX=~/.wine /opt/wine/bin/wine ~/.wine/drive_c/TWGS/twgs.exe'

# Reattach
tmux attach -t twgs

# Detach: Ctrl+B then D
```

## Linux

Wine needs a display. Use Xvfb (virtual framebuffer) for headless:

```bash
# Install
sudo apt install xvfb wine

# Start virtual framebuffer
Xvfb :99 -screen 0 1024x768x24 &

# Run TWGS
DISPLAY=:99 wine ~/.wine/drive_c/TWGS/twgs.exe
```

Or combined in one tmux session:

```bash
tmux new-session -d -s twgs \
  'Xvfb :99 -screen 0 1024x768x24 & sleep 1; DISPLAY=:99 wine ~/.wine/drive_c/TWGS/twgs.exe'
```

## Ports

- **2002** — Telnet (player connections)
- **2003** — Admin console

## Suppressing Wine Debug Noise

```bash
WINEDEBUG=-all wine ~/.wine/drive_c/TWGS/twgs.exe
```

Or suppress specific noisy channels:

```bash
WINEDEBUG=-fixme+edit,-fixme+font wine ~/.wine/drive_c/TWGS/twgs.exe
```
