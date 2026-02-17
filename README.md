# **Vin**yard

Vin is a mINimal implementation of a subset of vi. Its goal is to mimic the editor experience of [this
Neovim configuration](https://github.com/lukasrhoades/dotfiles/blob/main/nvim/init.lua).

Vin is built on [antirez's kilo editor](https://github.com/antirez/kilo), following along with
[Snaptoken instructions](https://viewsourcecode.org/snaptoken/kilo/index.html) by Paige Ruten.

Currently implemented:
- open and save existing files
    - leader: space
- basic insert mode and normal mode commands
    - hjkl, ctrl-U/D, ctrl-B/F, 0^$
    - i, ESC to toggle modes
- basic status & message bar

To-do:
- write new files (Snaptoken)
- search (Snaptoken)
- syntax highlighting (Snaptoken)
- cache-based commands
    - undo, redo, ctrl-O
    - yank, paste
- motions
    - numeric repeats
    - w, e, b and c, d
    - r, R
- visual mode
    - v, shift-V
- tab to insert spaces
- line numbers
- smart indentation
    - auto indent to start
- read config file
