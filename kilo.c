/*** includes ***/
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

/*** defines ***/
#define CTRL_KEY(k) ((k) & 0x1f)

/*** data ***/
struct editorConfig {
	int screenrows;
	int screencols;	
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
	raw.c_cc[VTIME] = 1;  // max wait time
	
	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)  // apply attributes to the terminal
		die("tcsetattr");
}

char editorReadKey() {	// wait for one key press and return
	int nread;
	char c;
	while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
		if (nread == -1 && errno != EAGAIN) die("read");
	}
	return c;
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

/*** append buffer ***/

struct abuf {
	char *b;
	int len;
}

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
void editorDrawRows() {  // Draw tildes
	int y;
	for (y = 0; y < E.screenrows; y++) {
		// no '/r' for the last line (otherwise row down 1 more line)
		write(STDOUT_FILENO, "~", 1);
		
		if (y < E.screenrows - 1) {
			write(STDOUT_FILENO, "\r\n", 2);
		}
	}
}

void editorRefreshScreen() {
	write(STDOUT_FILENO, "\x1b[2J", 4);  // clear the screen
	write(STDOUT_FILENO, "\x1b[H", 3);  // put cursor to top-left
	
	editorDrawRows();
	write(STDOUT_FILENO, "\x1b[H", 3);
}

/*** input ***/
void editorProcessKeypress() {  // mapping keys to editor functions
	char c = editorReadKey();
	
	switch (c) {
		case CTRL_KEY('q'):
			exit(0);
			break;
	}
}

/*** init ***/
void initEditor() {
	if (getWindowSize(&E.screenrows, &E.screencols) == -1) die("getWindowSize");
}
int main() {
	enableRawMode();
	initEditor();  // initialize all the fields in the E struct
	
    while (1) {
    	editorRefreshScreen();
    	editorProcessKeypress();
    }  
    return 0;
}
