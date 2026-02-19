/*** includes ***/

#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

/*** defines ***/

#define VERSION "0.0.1"
#define TAB_STOP 2
#define QUIT_TIMES 2

#define CTRL_KEY(k) ((k) & 0x1f)
#define LDR 0x20

enum editorKey {
  BACKSPACE = 127,
  QUIT = 1000,
  TAB,
  UP, DOWN, LEFT, RIGHT,
  FULL_LEFT, START_LINE, END_LINE,
  MV_UP, MV_DOWN,
  PG_UP, PG_DOWN,
  WRITE,
  ENTER,
  RETURN_CLI, CANCEL_CLI, BS_CLI,
  FWD_SEARCH, BWD_SEARCH, NXT_SEARCH, PRV_SEARCH,
  CLR_MATCHES
};

enum modes {
  NORMAL = 2000,
  INSERT,
  VISUAL,
  CLI,
  REPLACE
};

#define BREAK 3000

#define CURR_ROW \
  (E.cy >= E.numrows) ? NULL : &E.row[E.cy]

/*** data ***/

typedef struct erow {
  int size;
  char *chars;
} erow;

typedef struct match {
  int cx;
  int cy;
  int rowoff;
} match;

struct editorConfig {
  int cx, cy;
  int rowoff;
  int coloff;
  int screenrows;
  int screencols;
  int numrows;
  erow *row;
  int dirty;
  char *filename;
  char statusmsg[80];
  time_t statusmsg_time;
  int mode;
  int dirsearch;
  match *match_cache;
  int num_matches;
  int match_index;
  struct termios orig_termios;
};

struct editorConfig E;

/*** prototypes ***/

void editorSetStatusMessage(const char* fmt, ...);
void editorRefreshScreen(void);
char *editorPrompt(char *prompt, void (*callback)(char *, int));

/*** terminal ***/

void die(const char *s) {
  write(STDOUT_FILENO, "\x1b[2J", 4);
  write(STDOUT_FILENO, "\x1b[H", 3);

  perror(s);
  exit(1);
}

void disableRawMode(void) {
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1)
    die("tcsetattr");
}

void enableRawMode(void) {
  if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1)
    die("tcgetattr");
  atexit(disableRawMode);

  struct termios raw = E.orig_termios;
  raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
  raw.c_oflag &= ~(OPOST);
  raw.c_cflag |= (CS8);
  raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
  raw.c_cc[VMIN] = 0;
  raw.c_cc[VTIME] = 1;

  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
    die("tcsetattr");
}

int editorReadKey(void) {
  int nread;
  char c;

  while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
    if (nread == -1)
      die ("read");
  }
  
  if (c == '\x1b') {
    char seq[3];

    if (read(STDIN_FILENO, &seq[0], 1) != 1) {
      if (E.mode == INSERT) {
        E.mode = NORMAL;
        return BREAK;
      }
    }

    if (E.mode == CLI)
      return CANCEL_CLI;

    return '\x1b';
  } else if (E.mode == NORMAL && c == LDR) {
    char seq[2];

    if (read(STDIN_FILENO, &seq[0], 1) != 1)
      return LDR;

    switch (seq[0]) {
      case 'q':
        return QUIT;
      case 'w':
        return WRITE;
      case 'c':
        return CLR_MATCHES;
    }

    return LDR;
  } else {
    switch (c) {
      case 'i':
        if (E.mode == NORMAL) {
          E.mode = INSERT;
          return BREAK;
        }
        break;

      case BACKSPACE:
        if (E.mode == INSERT)
          break;
        if (E.mode == CLI) {
          return BS_CLI;
        }
      case 'h':
        if (E.mode == NORMAL)
          return LEFT;
        break;
      case 'j':
        if (E.mode == NORMAL)
          return DOWN;
        break;
      case 'k':
        if (E.mode == NORMAL)
          return UP;
        break;
      case 'l':
        if (E.mode == NORMAL)
          return RIGHT;
        break;

      case '\r':
        if (E.mode == INSERT)
          return ENTER;
        if (E.mode == CLI) {
          E.mode = NORMAL;
          return RETURN_CLI;
        }
        E.cy++;
      case '^':
        return START_LINE;
      case '0':
        if (E.mode == NORMAL)
          return FULL_LEFT;
        break;
      case '$':
        if (E.mode == NORMAL)
          return END_LINE;
        break;

      case CTRL_KEY('U'):
        if (E.mode == NORMAL)
          return MV_UP;
        break;
      case CTRL_KEY('D'):
        if (E.mode == NORMAL)
          return MV_DOWN;
        break;

      case CTRL_KEY('B'):
        if (E.mode == NORMAL)
          return PG_UP;
        break;
      case CTRL_KEY('F'):
        if (E.mode == NORMAL)
          return PG_DOWN;
        break;

      case '/':
        if (E.mode == NORMAL) {
          E.mode = CLI;
          return FWD_SEARCH;
        }
        break;
      case '?':
        if (E.mode == NORMAL) {
          E.mode = CLI;
          return BWD_SEARCH;
        }
        break;
      case 'n':
        if (E.mode == NORMAL && E.match_cache)
          return NXT_SEARCH;
        break;
      case 'N':
        if (E.mode == NORMAL && E.match_cache)
          return PRV_SEARCH;
        break;
    }

    return c;
  }
}

int getCursorPosition(int *rows, int *cols) {
  char buf[32];
  unsigned int i = 0;

  if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4)
    return -1;

  while (i < sizeof(buf) - 1) {
    if (read(STDIN_FILENO, &buf[i], 1) != 1)
      break;
    if (buf[i] == 'R')
      break;
    i++;
  }
  buf[i] = '\0';

  if (buf[0] != '\x1b' || buf[1] != '[')
    return -1;
  if (sscanf(&buf[2], "%d;%d", rows, cols) != 2)
    return -1;

  return 0;
}

int getWindowSize(int *rows, int *cols) {
  struct winsize ws;

  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
    if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12)
      return -1;
    return getCursorPosition(rows, cols);
  } else {
    *cols = ws.ws_col;
    *rows = ws.ws_row;
    return 0;
  }
}

/*** row operations ***/

int editorUpdateRow(erow *row) {
  int tabs = 0, j = 0;
  while (j < row->size)
    if (row->chars[j++] == '\t')
      tabs++;

  char *new = malloc(row->size + tabs*(TAB_STOP-1) + 1);
  
  int i = 0, inc = 1;
  for (int k=0; k<row->size; k++) {
    if (row->chars[k] == '\t') {
      new[i++] = ' ';
      while (i % TAB_STOP != 0) {
        inc++;
        new[i++] = ' ';
      }
    } else
      new[i++] = row->chars[k];
  }
  new[i] = '\0';
  row->chars = realloc(row->chars, i+1);
  memcpy(row->chars, new, i+1);
  row->size = i;
  free(new);

  return inc;
}

void editorInsertRow(int at, char *s, size_t len) {
  if (at < 0 || at > E.numrows)
    return;
  E.row = realloc(E.row, sizeof(erow) * (E.numrows+1));
  memmove(&E.row[at+1], &E.row[at], sizeof(erow) * (E.numrows-at));

  E.row[at].size = len;
  E.row[at].chars = malloc(len+1);
  memcpy(E.row[at].chars, s, len);
  E.row[at].chars[len] = '\0';
  editorUpdateRow(&E.row[at]);

  E.numrows++;
  E.dirty++;
}

int editorGetFirstCharIdx(erow *row) {
  int i = 0;

  if (E.cy == E.numrows)
    return 0;

  while (i < row->size) {
    if (row->chars[i] != ' ')
      return i;
    i++;
  }

  return 0;
}

void editorFreeRow(erow *row) {
  free(row->chars);
}

void editorDelRow(int at) {
  if (at < 0 || at >= E.numrows)
    return;
  editorFreeRow(&E.row[at]);
  memmove(&E.row[at], &E.row[at+1], sizeof(erow) * (E.numrows-at-1));
  E.numrows--;
  E.dirty++;
}

int editorRowInsertChar(erow *row, int at, int c) {
  if (at < 0 || at > row->size)
    at = row->size;
  row->chars = realloc(row->chars, row->size+2);
  memmove(&row->chars[at+1], &row->chars[at], row->size-at+1);
  row->size++;
  row->chars[at] = c;
  int inc = editorUpdateRow(row);
  E.dirty++;

  return inc;
}

void editorRowAppendString(erow *row, char *s, size_t len) {
  row->chars = realloc(row->chars, row->size+len+1);
  memcpy(&row->chars[row->size], s, len);
  row->size += len;
  row->chars[row->size] = '\0';
  editorUpdateRow(row);
  E.dirty++;
}

int editorRowDelChar(erow *row, int at) {
  if (at < 0 || at >= row->size)
    return 0;
  
  int tabCheck(char *ptr, int len);

  if ((at+1) % TAB_STOP == 0) {
    int len;
    if ((len = tabCheck(&row->chars[at], TAB_STOP)) > 1) {
      memmove(&row->chars[at+1-len], &row->chars[at+1], row->size-at);
      row->size -= len;
      editorUpdateRow(row);
      E.dirty++;
      return len;
    }
  }
  memmove(&row->chars[at], &row->chars[at+1], row->size-at);
  row->size--;
  editorUpdateRow(row);
  E.dirty++;
  
  return 1;
}

/*** helpers ***/

int tabCheck(char *ptr, int len) {
  int i = 0, count = 0;

  while (len-- > 0) {
    if (ptr[i--] != ' ')
      return count;
    else
      count++;
  }

  return count;
}

/*** editor operations***/

void editorInsertChar(int c) {
  if (E.cy == E.numrows)
    editorInsertRow(E.numrows, "", 0);
  int inc = editorRowInsertChar(&E.row[E.cy], E.cx, c);
  E.cx += inc;
}

void editorInsertNewline(void) {
  if (E.cx == 0)
    editorInsertRow(E.cy, "", 0);
  else {
    erow *row = &E.row[E.cy];
    editorInsertRow(E.cy+1, &row->chars[E.cx], row->size - E.cx);
    row = &E.row[E.cy];
    row->size = E.cx;
    row->chars[row->size] = '\0';
    editorUpdateRow(row);
  }
  E.cy++;
  E.cx = 0;
}

void editorDelChar(void) {
  if (E.cy == E.numrows)
    return;
  if (E.cx == 0 && E.cy == 0)
    return;

  erow *row = &E.row[E.cy];
  if (E.cx > 0) {
    int dec = editorRowDelChar(row, E.cx-1);
    E.cx -= dec;
  } else {
    E.cx = E.row[E.cy-1].size;
    editorRowAppendString(&E.row[E.cy-1], row->chars, row->size);
    editorDelRow(E.cy);
    E.cy--;
  }
}

/*** file i/o ***/

char *editorRowsToString(int *buflen) {
  int totlen = 0;
  
  for (int j=0; j<E.numrows; j++)
    totlen += E.row[j].size + 1;
  *buflen = totlen;

  char *buf = malloc(totlen);
  char *p = buf;
  
  for (int j=0; j<E.numrows; j++) {
    memcpy(p, E.row[j].chars, E.row[j].size);
    p += E.row[j].size;
    *p = '\n';
    p++;
  }
  
  return buf;
}

void editorOpen(char *filename) {
  free(E.filename);
  E.filename = strdup(filename);

  FILE *fp = fopen(filename, "r");
  if (!fp)
    die("fopen");

  char *line = NULL;
  size_t linecap = 0;
  ssize_t linelen;

  while ((linelen = getline(&line, &linecap , fp)) != -1) {
    while (linelen > 0 && (line[linelen-1] == '\n' ||
          line[linelen-1] == '\r'))
      linelen--;
    editorInsertRow(E.numrows, line, linelen);
  }
  free(line);
  fclose(fp);
  E.dirty = 0;
}

void editorSave(void) {
  if (E.filename == NULL) {
    E.filename = editorPrompt("Save as: %s", NULL);
    if (E.filename == NULL) {
      editorSetStatusMessage("Save aborted");
      return;
    }
  }

  int len;
  char *buf = editorRowsToString(&len);

  int fd = open(E.filename, O_RDWR | O_CREAT, 0644);
  if (fd != -1) {
    if (ftruncate(fd, len) != -1) {
      if (write(fd, buf, len) == len) {
        close(fd);
        free(buf);
        E.dirty = 0;
        editorSetStatusMessage("\"%s\" %dL, %dB written", E.filename, E.numrows, len);
        return;
      }
    }
    close(fd);
  }

  free(buf);
  editorSetStatusMessage("Can't save! I/O error: %s", strerror(errno));
}

/*** match operations ***/

void insertMatch(int cx, int cy, int rowoff) {
  E.match_cache = realloc(E.match_cache, sizeof(match) * (E.num_matches+1));

  int at = E.num_matches;
  E.match_cache[at].cx = cx;
  E.match_cache[at].cy = cy;
  E.match_cache[at].rowoff = rowoff;

  E.num_matches++;
}

/*** find ***/

void editorGoToCurrMatch(void) {
  match curr_match = E.match_cache[E.match_index];

  E.cx = curr_match.cx;
  E.cy = curr_match.cy;
  E.rowoff = curr_match.rowoff;
}

void editorGoToNextMatch(void) {
  E.dirsearch ? E.match_index++ : E.match_index--;
  if (E.match_index == E.num_matches)
    E.match_index = 0;
  if (E.match_index == -1)
    E.match_index = E.num_matches-1;
  editorGoToCurrMatch();
}

void editorGoToPrevMatch(void) {
  E.dirsearch ? E.match_index-- : E.match_index++;
  if (E.match_index == E.num_matches)
    E.match_index = 0;
  if (E.match_index == -1)
    E.match_index = E.num_matches-1;
  editorGoToCurrMatch();
}

void editorFindCallback(char *query, int key) {
  if (key == RETURN_CLI)
    return;

  free(E.match_cache);
  E.match_cache = NULL;
  E.num_matches = 0;
  E.match_index = 0;

  if (key == CANCEL_CLI)
    return;

  for (int i=0; i<E.numrows; i++) {
    erow *row = &E.row[i];
    char *match = strstr(row->chars, query);
    if (match) {
      if (i<=E.cy)
        E.match_index = E.num_matches;
      insertMatch(match-row->chars, i, E.numrows);
    }
  }

  if (E.num_matches > 0)
    editorGoToCurrMatch();
}

void editorFind(int fwd) {
  int saved_cx = E.cx;
  int saved_cy = E.cy;
  int saved_coloff = E.coloff;
  int saved_rowoff = E.rowoff;

  E.dirsearch = fwd;
  char *query = editorPrompt(E.dirsearch ? "/%s" : "?%s", editorFindCallback);

  if (query)
    free(query);
  else {
    E.cx = saved_cx;
    E.cy = saved_cy;
    E.coloff = saved_coloff;
    E.rowoff = saved_rowoff;
  }
}

/*** append buffer ***/

struct abuf {
  char *b;
  int len;
};

#define ABUF_INIT {NULL, 0}

void abAppend(struct abuf *ab, const char *s, int len) {
  char *new = realloc(ab->b, ab->len + len);

  if (new == NULL)
    return;
  memcpy(&new[ab->len], s, len);
  ab->b = new;
  ab->len += len;
}

void abFree(struct abuf *ab) {
  free(ab->b);
}

/*** input ***/

char *editorPrompt(char *prompt, void (*callback)(char *, int)) {
  size_t bufsize = 128;
  char *buf = malloc(bufsize);

  size_t buflen = 0;
  buf[0] = '\0';

  E.mode = CLI;

  while (1) {
    editorSetStatusMessage(prompt, buf);
    editorRefreshScreen();

    int c = editorReadKey();
    if (c == BS_CLI) {
      if (buflen != 0)
        buf[--buflen] = '\0';
    }
    if (c == CANCEL_CLI) {
      editorSetStatusMessage("");
      if (callback)
        callback(buf, c);
      free(buf);
      E.mode = NORMAL;
      return NULL;
    } else if (c == RETURN_CLI) {
      if (buflen != 0) {
        editorSetStatusMessage("");
        if (callback)
          callback(buf, c);
        E.mode = NORMAL;
        return buf;
      }
    } else if (!iscntrl(c) && c < 128) {
      if (buflen == bufsize - 1) {
        bufsize *= 2;
        buf = realloc(buf, bufsize);
      }
      buf[buflen++] = c;
      buf[buflen] = '\0';
    }

    if (callback)
      callback(buf, c);
  }
}

void editorMoveCursor(int key) {
  erow *row = CURR_ROW;

  switch (key) {
    case LEFT:
      if (E.cx != 0)
        E.cx--;
      break;
    case RIGHT:
      if (row && E.cx < row->size)
        E.cx++;
      break;
    case UP:
      if (E.cy != 0)
        E.cy--;
      break;
    case DOWN:
      if (E.cy < E.numrows)
        E.cy++;
      break;
  }

  row = CURR_ROW;
  int rowlen = row ? row->size : 0;
  if (E.cx > rowlen)
    E.cx = rowlen;
}

void editorGoToFirstChar(void) {
  erow *row = &E.row[E.cy];

  E.cx = editorGetFirstCharIdx(row);
}


void editorProcessKeypress(void) {
  static int quit_times = QUIT_TIMES;

  int c = editorReadKey();

  switch (c) {
    case BREAK:
      break;

    case QUIT:
      if (E.dirty && quit_times > 0) {
        editorSetStatusMessage("Warning, unsaved changes. Quit %d more times to exit.", quit_times--);
        return;
      }
      write(STDOUT_FILENO, "\x1b[2J", 4);
      write(STDOUT_FILENO, "\x1b[H", 3);
      exit(0);
      break;

    case WRITE:
      editorSave();
      break;

    case ENTER:
      editorInsertNewline();
      break;

    case BACKSPACE:
      editorDelChar();
      break;

    case LEFT: case DOWN: case UP: case RIGHT:
      editorMoveCursor(c);
      break;

    case START_LINE:
      editorGoToFirstChar();
      break;

    case FULL_LEFT:
      E.cx = 0;
      break;
    case END_LINE:
      { 
        erow *row = CURR_ROW;
        int rowlen = row ? row->size : 0;
        E.cx = rowlen;
      }
      break;

    case MV_UP: case MV_DOWN:
      {
        int times = E.screenrows/2;
        while (times--)
          editorMoveCursor(c == MV_UP ? UP : DOWN);
      }
      break;

    case PG_UP: case PG_DOWN:
      {
        if (c == PG_UP)
          E.cy = E.rowoff;
        else if (c == PG_DOWN) {
          E.cy = E.rowoff + E.screenrows-1;
          if (E.cy > E.numrows)
            E.cy = E.numrows;
        }

        int times = E.screenrows;
        while (times--)
          editorMoveCursor(c == PG_UP ? UP : DOWN);
      }
      break;

    case CTRL_KEY('l'):
    case '\x1b':
      break;

    case FWD_SEARCH:
      editorFind(1);
      break;
    case BWD_SEARCH:
      editorFind(0);
      break;

    case NXT_SEARCH:
      editorGoToNextMatch();
      break;
    case PRV_SEARCH:
      editorGoToPrevMatch();
      break;

    case CLR_MATCHES:
      free(E.match_cache);
      E.match_cache = NULL;
      E.num_matches = 0;
      E.match_index = 0;
      break;

    default:
      if (E.mode == INSERT)
        editorInsertChar(c);
      break;
  }

  quit_times = QUIT_TIMES;
}

/*** output***/

void editorScroll(void) {
  if (E.cy < E.rowoff)
    E.rowoff = E.cy;
  if (E.cy >= E.rowoff + E.screenrows)
    E.rowoff = E.cy - E.screenrows + 1;
  if (E.cx < E.coloff)
    E.coloff = E.cx;
  if (E.cx >= E.coloff + E.screencols)
    E.coloff = E.cx - E.screencols + 1;
}

void editorDrawRows(struct abuf *ab) {
  int y;
  for (y = 0; y < E.screenrows; y++) {
    int filerow = y + E.rowoff;

    if (filerow >= E.numrows) {
      if (E.numrows == 0 && y == E.screenrows / 3) {
        char welcome[80];

        int welcomelen = snprintf(welcome, sizeof(welcome),
            "\x1b[1;4mVin\x1b[myard editor v%s", VERSION);
        if (welcomelen > E.screencols)
          welcomelen = E.screencols;

        int padding = (E.screencols - welcomelen) / 2;
        if (padding) {
          abAppend(ab, "~", 1);
          padding--;
        }
        while (padding--)
          abAppend(ab, " ", 1);

        abAppend(ab, welcome, welcomelen);
      } else {
        abAppend(ab, "~", 1);
      }
    } else {
      int len = E.row[filerow].size - E.coloff;
      if (len < 0)
        len = 0;
      if (len > E.screencols)
        len = E.screencols;
      abAppend(ab, &E.row[filerow].chars[E.coloff], len);
    }

    abAppend(ab, "\x1b[K", 3);
    abAppend(ab, "\r\n", 2);
  }
}

void editorDrawStatusBar(struct abuf *ab) {
  char status[80], rstatus[80];
  int len = snprintf(status, sizeof(status), "%.20s %s",
      E.filename ? E.filename : "[No Name]",
      E.dirty ? "[+]" : "");
  int rlen = snprintf(rstatus, sizeof(rstatus), "%d,%d %10.0f%%",
      E.cy+1, E.cx+1, 100 * (E.cy+1)/(float)E.numrows);

  if (len > E.screencols)
    len = E.screencols;
  abAppend(ab, status, len);

  while (len < E.screencols) {
    if (E.screencols - len == rlen) {
      abAppend(ab, rstatus, rlen);
      break;
    }
    else {
      abAppend(ab, " ", 1);
      len++;
    }
  }
  abAppend(ab, "\r\n", 2);
}

void editorDrawMessageBar(struct abuf *ab) {
  abAppend(ab, "\x1b[K", 3);
  int msglen = strlen(E.statusmsg);
  if (msglen > E.screencols)
    msglen = E.screencols;
  if (msglen && time(NULL) - E.statusmsg_time < 5)
    abAppend(ab, E.statusmsg, msglen);
}

void editorRefreshScreen(void) {
  editorScroll();

  struct abuf ab = ABUF_INIT;

  abAppend(&ab, "\x1b[?25l", 6);
  abAppend(&ab, "\x1b[H", 3);

  editorDrawRows(&ab);
  editorDrawStatusBar(&ab);
  editorDrawMessageBar(&ab);

  char buf[32];
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (E.cy-E.rowoff)+1, (E.cx-E.coloff)+1);
  abAppend(&ab, buf, strlen(buf));

  abAppend(&ab, "\x1b[?25h", 6);

  write(STDOUT_FILENO, ab.b, ab.len);
  abFree(&ab);
}

void editorSetStatusMessage(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(E.statusmsg, sizeof(E.statusmsg), fmt, ap);
  va_end(ap);
  E.statusmsg_time = time(NULL);
}

/*** init ***/

void initEditor(void) {
  E.cx = 0;
  E.cy = 0;
  E.rowoff = 0;
  E.coloff = 0;
  E.numrows = 0;
  E.row = NULL;
  E.dirty = 0;
  E.filename = NULL;
  E.statusmsg[0] = '\0';
  E.statusmsg_time = 0;
  E.mode = NORMAL;
  E.dirsearch = 0;
  E.match_cache = NULL;
  E.num_matches = 0;
  E.match_index = 0;

  if (getWindowSize(&E.screenrows, &E.screencols) == -1)
    die ("getWindowSize");
  E.screenrows -= 2;
}

int main(int argc, char *argv[]) {
  enableRawMode();
  initEditor();
  if (argc >= 2)
    editorOpen(argv[1]);

  editorSetStatusMessage("HELP: Leader(Space)-Q = quit");

  while (1) {
    editorRefreshScreen();
    editorProcessKeypress();
  }

  return 0;
}
