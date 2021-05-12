/*** includes ***/
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

/*** data ***/
struct termios orig_termios;

/*** terminal ***/
void die(const char *s) {
	perror(s);
	exit(1);
}

void disableRawMode() {
	// restore settings when this program exits
	// discard unread input before applying changes
	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios) == -1)
		die("tcsetattr");
}

void enableRawMode() {
	if (tcgetattr(STDIN_FILENO, &orig_termios) == -1)  // read terminal attributes in the termios
		die("tcgetattr");
	atexit(disableRawMode);  // call the func automatically when returning from main or exit()
	
	struct termios raw = orig_termios; 
	raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
	raw.c_oflag &= ~(OPOST); // turn-off ouput preprocessing (no auto adding '\r' after)
	raw.c_cflag |= (CS8);
	raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG); // turn off echoing | read word by word | turn-off CTRL-V | turn-off SIGINT & SIGTSTP signal
	raw.c_cc[VMIN] = 0;  // min wait time
	raw.c_cc[VTIME] = 1;  // max wait time
	
	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)  // apply attributes to the terminal
		die("tcsetattr");
}

/*** init ***/
int main() {
	enableRawMode();
	
    while (1) {
    	char c = '\0';
    	if (read(STDIN_FILENO, &c, 1) == -1 && errno != EAGAIN)
    		die("read");
    	if (iscntrl(c)) { // check if it's a control char
    		printf("%d\r\n", c);
    	} else {
    		printf("%d ('%c')\r\n", c, c);
    	}
    	if (c == 'q') break;
    }  
    return 0;
}
