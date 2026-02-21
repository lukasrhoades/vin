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
    - hjkl, ctrl-u/d, ctrl-b/f, 0^$, a/A, gg/G, x
    - i, ESC to toggle modes
- basic status & message bar
- soft indentation
    - tabs insert spaces
    - backspace removes tab-worths of space
- search mode
    - / ? n N to navgiate
    - highlighted matches, ldr-nh to clear
- basic syntax highlighting
    - C language

To-do:
- more insert & normal mode commands
    - r, R
- cache-based commands
    - undo, redo, ctrl-o/i
    - yank, paste
- motions
    - numeric repeats
    - w, e, b and c, d
- visual mode
    - v, shift-V
- line numbers
- smart indentation
    - auto indent to start
- read config file
