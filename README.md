# **Vin**yard

Vin is a mINimal implementation of a subset of vi. Its goal is to mimic the editor experience of [this
Neovim configuration](https://github.com/lukasrhoades/dotfiles/blob/main/nvim/init.lua).

Vin is built on [antirez's kilo editor](https://github.com/antirez/kilo), initially following along with
[Snaptoken instructions](https://viewsourcecode.org/snaptoken/kilo/index.html) by Paige Ruten. This
phase has since been completed, and development has shifted towards implementing vi features.

Currently implemented:
- open and save files
    - leader: space
    - prompt mode
- basic insert mode and normal mode commands
    - hjkl, ctrl-U/D, ctrl-B/F, 0^$
    - i, ESC to toggle modes
- basic status & message bar
- soft indentation
    - tabs insert spaces
    - backspace removes tab-worths of space
- search mode
    - / ? n N to navgiate
- basic syntax highlighting
    - C language

To-do:
- highlight matches
- more insert & normal mode commands
    - a, A
    - gg, G
- cache-based commands
    - undo, redo, ctrl-O
    - yank, paste
- motions
    - numeric repeats
    - w, e, b and c, d
    - r, R
- visual mode
    - v, shift-V
- line numbers
- smart indentation
    - auto indent to start
- read config file
- multi-key sequence support for leader bindings
