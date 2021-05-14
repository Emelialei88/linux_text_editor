/*** includes ***/

#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>

/*** defines ***/

#define KILO_VERSION "0.0.1"

#define CTRL_KEY(k) ((k) & 0x1f)

enum editorKey {
	ARROW_LEFT = 1000,
	ARROW_RIGHT,
	ARROW_UP,
	ARROW_DOWN,
	DEL_KEY,
	HOME_KEY,
	END_KEY,
	PAGE_UP,
	PAGE_DOWN
};

/*** data ***/

typedef struct erow {  // store a row
	int size;
	char *chars;
} erow;

struct editorConfig {
	int cx, cy;
	int rowoff;
	int coloff;
	int screenrows;
	int screencols;
	int numrows;
	erow *row;	
	struct termios orig_termios;
};

struct editorConfig E;

/*** terminal ***/
void die(const char *s) {
	perror(s);
	exit(1);
}

void disableRawMode() {
	// restore settings when this program exits
	// discard unread input before applying changes
	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1)
		die("tcsetattr");
}

void enableRawMode() {
	if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1)  // read terminal attributes in the termios
		die("tcgetattr");
	atexit(disableRawMode);  // call the func automatically when returning from main or exit()
	
	struct termios raw = E.orig_termios; 
	raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
	raw.c_oflag &= ~(OPOST); // turn-off ouput preprocessing (no auto adding '\r' after)
	raw.c_cflag |= (CS8);
	raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG); // turn off echoing | read word by word | turn-off CTRL-V | turn-off SIGINT & SIGTSTP signal
	raw.c_cc[VMIN] = 0;  // min wait time
	raw.c_cc[VTIME] = 3;  // max wait time
	
	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)  // apply attributes to the terminal
		die("tcsetattr");
}

int editorReadKey() {	// wait for one key press and return
	int nread;
	char c;
	while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
		if (nread == -1 && errno != EAGAIN) die("read");
	}
	
	if (c == '\x1b') {  // read arrows
		char seq[3];
		
		if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
		if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';
		
		if (seq[0] == '[') {
			if (seq[1] >= '0' && seq[1] <= '9') {
				if (read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b';
				if (seq[2] == '~') {
					switch (seq[1]) {
						case 1: return HOME_KEY;
						case 3: return DEL_KEY;
						case 4: return END_KEY;
						case 5: return PAGE_UP;
						case 6: return PAGE_DOWN;
						case 7: return HOME_KEY;
						case 8: return END_KEY;
					}
				}
			} else {	
				switch (seq[1]) {
					case 'A': return ARROW_UP;
					case 'B': return ARROW_DOWN;
					case 'C': return ARROW_RIGHT;
					case 'D': return ARROW_LEFT;
					case 'H': return HOME_KEY;
					case 'F': return END_KEY;
				}
			}
		} else if (seq[0] == '0') {
			switch (seq[1]) {
				case 'H': return HOME_KEY;
        		case 'F': return END_KEY;
			}
		}
		return '\x1b';
	} else {	
		return c;
	}
}

int getCursorPosition(int *rows, int *cols) {
	char buf[32];
	unsigned int i = 0;
	
	if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) return -1; // ask for cursor position 
	
	while (i < sizeof(buf) - 1) {
		if (read(STDIN_FILENO, &buf[i], 1) != 1) break;  // read reply from stdin
		if (buf[i] == 'R') break;
		i++;
	}
	buf[i] = '\0';
	
	if (buf[0] != '\x1b' || buf[1] != '[') return -1; // respond with an escape sequence
	if (sscanf(&buf[2], "%d;%d", rows, cols) != 2) return -1;

	return 0;
} 

int getWindowSize(int *rows, int *cols) {  // get the size of terminal
	struct winsize ws;
	
	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
		if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) return -1; // move cursor to the bottom-right corner
		return getCursorPosition(rows, cols);
	} else {
		*cols = ws.ws_col;
		*rows = ws.ws_row;
		return 0;
	}
}

/*** row operations ***/
void editorAppendRow(char *s, size_t len) {
	E.row = realloc(E.row, sizeof(erow) * (E.numrows + 1));

	int at = E.numrows;
	E.row[at].size = len;
	E.row[at].chars = malloc(len + 1);
	memcpy(E.row[at].chars, s, len);
	E.row[at].chars[len] = '\0';
	E.numrows++;
}

/*** file i/o ***/

void editorOpen(char *filename) {
	FILE *fp = fopen(filename, "r");
	if (!fp) die("fopen");
	
	char *line = NULL;
	size_t linecap = 0;
	ssize_t linelen;
	
	while ((linelen = getline(&line, &linecap, fp)) != -1) {
		while (linelen > 0 && (line[linelen - 1] == '\n' ||
							   line[linelen - 1] == '\r'))
			linelen--;
		editorAppendRow(line, linelen);
	}
	free(line);
	fclose(fp);	
}

/*** append buffer ***/

struct abuf {
	char *b;
	int len;
};  // remember to add a ';'

#define ABUF_INIT {NULL, 0}

void abAppend(struct abuf *ab, const char *s, int len) {
	char *new = realloc(ab->b, ab->len + len);
	
	if (new == NULL) return;
	memcpy(&new[ab->len], s, len);
	ab->b = new;
	ab->len += len; 
}

void abFree(struct abuf *ab) {
	free(ab->b);
}

/*** output ***/

void editorScroll() {
	if (E.cy < E.rowoff) {  // check if the cursor is above the visible window
		E.rowoff = E.cy;  // scroll up
	}
	if (E.cy >= E.rowoff + E.screenrows) {  // past the bottom
		E.rowoff = E.cy - E.screenrows + 1;
	}
	if (E.cx < E.coloff) {
		E.coloff = E.cx;
	}
	if (E.cx > E.coloff + E.screencols) {
		E.coloff = E.cx - E.screencols + 1;
	}
}

void editorDrawRows(struct abuf *ab) {  // Draw tildes
	int y;
	for (y = 0; y < E.screenrows; y++) {
		int filerow = y + E.rowoff;
		if (filerow >= E.numrows) {
		// no '/r' for the last line (otherwise row down 1 more line)
			if (E.numrows == 0 && y == E.screenrows / 3) {  // print welcome msg 
				char welcome[80];
				int welcomelen = snprintf(welcome, sizeof(welcome),
					"Kilo editor -- version %s", KILO_VERSION);
				if (welcomelen > E.screencols) welcomelen = E.screencols;
				int padding = (E.screencols - welcomelen) / 2;
				if (padding) {  // center the welcome msg
					abAppend(ab, "~", 1);
					padding--;
				}
				while (padding--) abAppend(ab, " ", 1);
				abAppend(ab, welcome, welcomelen);
			} else {
				abAppend(ab, "~", 1);
			}
		} else {
			int len = E.row[filerow].size - E.coloff;
			if (len < 0) len = 0;
			if (len > E.screencols) len = E.screencols;
			abAppend(ab, &E.row[filerow].chars[E.coloff], len);
		}
		
		abAppend(ab, "\x1b[K", 3);  // erases the part of the line to the right of the cursor (default mode)
		if (y < E.screenrows - 1) {
			abAppend(ab, "\r\n", 2);
		}
	}
}

void editorRefreshScreen() {
	editorScroll();
	
	struct abuf ab = ABUF_INIT;
	
	abAppend(&ab, "\x1b[?25l", 6);  // hide the cursor when repainting
	abAppend(&ab, "\x1b[H", 3);  // put cursor to top-left
	
	editorDrawRows(&ab);
	
	char buf[32];
	snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (E.cy - E.rowoff) + 1, (E.cx - E.coloff) + 1);  // change the old H cmd into one with args, add 1 so 0->1; E.cy refers to the pos in the file, but we have to get the pos on the screen
	abAppend(&ab, buf, strlen(buf));	
	
	abAppend(&ab, "\x1b[?25h", 6);  // show the cursor
	
	write(STDOUT_FILENO, ab.b, ab.len);
	abFree(&ab);
}

/*** input ***/

void editorMoveCursor(int key) {
	switch (key) {
		case ARROW_LEFT:
			if (E.cx != 0) {  // prevent moving cursor off screen
				E.cx--;
			}
			break;
		case ARROW_RIGHT:
				E.cx++;
			break;
		case ARROW_UP:
			if (E.cy != 0) {
				E.cy--;
			}
			break;
		case ARROW_DOWN:
			if (E.cy < E.numrows) {  // cursor can advance past the btm of the screen, but not the
				E.cy++;
			}
			break;
	}
}


void editorProcessKeypress() {  // mapping keys to editor functions
	int c = editorReadKey();
	
	switch (c) {
		case CTRL_KEY('q'):
			write(STDOUT_FILENO, "\x1b[2J", 4);
	      	write(STDOUT_FILENO, "\x1b[H", 3);  // clear the screen by scrolling down
			exit(0);
			break;
		
		// move the cursor to the left/right of the screen	
		case HOME_KEY:
			E.cx = 0;
			break;
		case END_KEY:
			E.cx = E.screencols - 1;
			break;
			
		// move the cursor to the top/btm of the screen	
		case PAGE_UP:
  		case PAGE_DOWN:
  			{
  				int times = E.screenrows;
  				while (times--)
  					editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
  			}
  			break;
		
		// move cursor with wsad
		case ARROW_UP:
		case ARROW_DOWN:
		case ARROW_LEFT:
		case ARROW_RIGHT:
			editorMoveCursor(c);
			break;
	}
}

/*** init ***/

void initEditor() {
	E.cx = 0;
	E.cy = 0;
	E.rowoff = 0;
	E.coloff = 0;
	E.numrows = 0;
	E.row = NULL;

	if (getWindowSize(&E.screenrows, &E.screencols) == -1) die("getWindowSize");
}
int main(int argc, char *argv[]) {
	enableRawMode();
	initEditor();  // initialize all the fields in the E struct
	if (argc >= 2) {
		editorOpen(argv[1]);
	}
	
    while (1) {
    	editorRefreshScreen();
    	editorProcessKeypress();
    }  
    return 0;
}
