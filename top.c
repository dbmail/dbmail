/*
 Copyright (C) 2006 Aaron Stone aaron@serendipity.cx

 This program is free software; you can redistribute it and/or 
 modify it under the terms of the GNU General Public License 
 as published by the Free Software Foundation; either 
 version 2 of the License, or (at your option) any later 
 version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

/* $Id: top.c 2224 2006-08-14 17:46:47Z aaron $
 * 
 * main file for dbmail-top */

#define trace ncurses_trace
#include <curses.h>
#undef trace // Our trace macro conflicts with ncurses.

#include "dbmail.h"
#define THIS_MODULE "top"
#define PNAME "dbmail/top"

static int parse_scoreboard(char *scoreBuf, Scoreboard_t *scoreboard);
static int read_file(FILE * f, char **m_buf);
static char **get_state(char *service);
static int count_services(char **services);

ServerConfig_t config;

/* loudness and assumptions */
static int verbose = 0;

int do_showhelp(void) {
	printf("*** dbmail-top ***\n");

	printf("Use this program to watch the status of your DBMail daemons.\n");
	printf("See the man page for more info.\n\n");

	printf("\nCommon options for all DBMail utilities:\n");
	printf("     -p file   specify an alternative pid file\n");
	printf("     -s file   specify an alternative state file\n");
	printf("     -f file   specify an alternative config file\n");
	printf("     -q        quietly skip interactive prompts\n"
	       "               use twice to suppress error messages\n");
	printf("     -n        show the intended action but do not perform it, no to all\n");
	printf("     -y        perform all proposed actions, as though yes to all\n");
	printf("     -v        verbose details\n");
	printf("     -V        show the version\n");
	printf("     -h        show this help message\n");

	return 0;
}

int main(int argc, char **argv)
{
	int q;
	char *configFile = DEFAULT_CONFIG_FILE;
	int c, c_prev = 0, usage_error = 0;
	
	openlog(PNAME, LOG_PID, LOG_MAIL);

	/* Check for commandline options.
	 * The initial '-' means that arguments which are not associated
	 * with an immediately preceding option are return with option 
	 * value '1'. We will use this to allow for multiple values to
	 * follow after each of the supported options. */
	while ((c = getopt(argc, argv, "-p:s:f:qnyvVh")) != EOF) {
		/* Received an n-th value following the last option,
		 * so recall the last known option to be used in the switch. */
		if (c == 1)
			c = c_prev;
		c_prev = c;
		/* Do something with this option. */
		switch (c) {

		/* Common command line options. */
		case 'f':
			if (optarg && strlen(optarg) > 0)
				configFile = optarg;
			else {
				fprintf(stderr, "dbmail-top: -f requires a filename\n\n" );
				return 1;
			}
			break;

		case 'v':
			verbose = 1;
			break;

		case 'V':
			PRINTF_THIS_IS_DBMAIL;
			return 1;

		default:
			usage_error = 1;
			break;
		}

		/* At the end of each round of options, check
		 * to see if there were any errors worth stopping for. */
		if (usage_error) {
			do_showhelp();
			exit(1);
		}
	}


	// Get the log files, the pid files, the state files.
	config_read(configFile);
	config_get_logfiles(&config);
	SetTraceLevel("top");

	WINDOW *mainwin;

	// The ncurses manpage says to do this:
	mainwin = initscr();
	cbreak();
	noecho();

	// And then do this:
	nonl();
	intrflush(stdscr, FALSE);
	keypad(stdscr, TRUE);
	timeout(0); // getch returns immediately.

	atexit((void *)endwin); // Call endwin() at exit.

	int mx, my;
	getmaxyx(stdscr, mx, my);

	// Now more stuff...
	attron(A_BOLD);
	mvprintw(0, 0, "DBMail top utility -- Update interval 3 sec -- Press Q to quit");
	mvprintw(1, 0, "Daemons found or (not found):"); // Ends at col 30
	attroff(A_BOLD);

	WINDOW *services_win[5];

	services_win[0] = subwin(mainwin, mx - 2, my, 2, 0);

//	int mvderwin(WINDOW *win, int par_y, int par_x);

	// Hit them with SIGUSR1
	// Read state files
	// Update display
	while ((q = getch()) != 'q' && q != 'Q') {
		int s, i, found;
		char **statelines;
		char *services[] = { "lmtpd", "imapd", "pop3d", "timsieved", NULL };
		char *services_not[] = { "(lmtpd)", "(imapd)", "(pop3d)", "(timsieved)", NULL };
		int services_pos[] = { 0, 0, 0, 0, 0 };

		// Total number of services we have to display
		found = count_services(services);

		for (s = 0; services[s] != NULL; s++) {
			statelines = get_state(services[s]);

			attron(A_BOLD);
			if (!statelines) {
				int sf;
				for (sf = s; services[sf+1]; sf++) {
					services_pos[sf+1] += strlen(services_not[sf]) + 1;
				}

				move(1, 30 + services_pos[s]);
				printw("%s    ", services_not[s]); // Clear trailing space
				continue;
			} else {
				int sf;
				for (sf = s; services[sf+1]; sf++) {
					services_pos[sf+1] += strlen(services[sf]) + 1;
				}

				move(1, 30 + services_pos[s]);
				printw("%s    ", services[s]);

				mvwprintw(services_win[s], 1, 1, "State for %s", services[s]);
			}
			attroff(A_BOLD);

			// First five lines only
			// TODO: Top 5 active lines
			// TODO: Offset to first relevant line
			for (i = 0; i < 5; i++) {
				mvwprintw(services_win[s], i + 2, 1, "%s\n", statelines[i+1]);
			}
                        
			box(services_win[s], 0, 0);
			wrefresh(services_win[s]);

			g_free(statelines[0]);
			g_free(statelines);
		}

		refresh();
		sleep(3);
	}

	return 0;
}

int count_services(char **services)
{
	int s = 0, found = 0;
	for (; services[s]; s++) {
		char *longservice, *pidName;
		
		longservice = g_strconcat("dbmail-", services[s], NULL);
		pidName = config_get_pidfile(&config, longservice);

		if (access(pidName, F_OK))
			found++;

		g_free(longservice);
		g_free(pidName);
	}

	return found;
}

/* Read the buffer and parse it into a Scoreboard_t. */
int parse_scoreboard(char *scoreBuf, Scoreboard_t *scoreboard)
{
	return DM_SUCCESS;
}

/* Slurp up the entire file. */
int read_file(FILE * f, char **m_buf)
{
	size_t f_len = 1024;
	size_t f_pos = 0;
	char *f_buf = NULL;

	if (!f) {
		TRACE(TRACE_ERROR, "Received NULL file handle\n");
		return -1;
	}

	/* Allocate the initial input buffer. */
	f_buf = g_new(char, f_len);
	if (f_buf == NULL)
		return -2;

	while (!feof(f)) {
		if (f_pos + 1 >= f_len) {
			f_buf = g_renew(char, f_buf, (f_len *= 2));
			if (f_buf == NULL)
				return -2;
		}
		f_buf[f_pos] = fgetc(f);
		f_pos++;
	}

	if (f_pos)
		f_buf[f_pos] = '\0';

	*m_buf = f_buf;
	return 0;
}

/* return[0] - return[n] is each line.
 * to free,
 *     g_free(return[0]);
 *     g_free(return);
 * because return[0] is the beginning of 
 * the whole shebang of memory.
 */
char **get_state(char *service)
{
		char *pidStr, *state, **statelines;
		int pidInt;
		int res;

		FILE *pidFile, *stateFile;
		char *pidName, *stateName;

		char *longservice = g_strconcat("dbmail-", service, NULL);

		pidName = config_get_pidfile(&config, longservice);
		stateName = config_get_statefile(&config, longservice);

		g_free(longservice);

		pidFile = fopen(pidName, "r");
		if (!pidFile) {
			TRACE(TRACE_ERROR, "Could not open pid file [%s].", pidName);
			g_free(pidName);
			g_free(stateName);
			return NULL;
		}
		stateFile = fopen(stateName, "r");
		if (!stateFile) {
			TRACE(TRACE_ERROR, "Could not open state file [%s].", stateName);
			g_free(pidName);
			g_free(stateName);
			return NULL;
		}
		
		g_free(pidName);
		g_free(stateName);

		// Read pidfiles
		// // TODO: Stat the file to see if it changed.
		res = read_file(pidFile, &pidStr);
		if (res != 0) {
			TRACE(TRACE_ERROR, "Could not read pid file [%s].", pidName);
			return NULL;
		}
		pidInt = atoi(pidStr);
		if (pidInt < 1) {
			TRACE(TRACE_ERROR, "Invalid pid found [%s].", pidStr);
			return NULL;
		}
		kill(pidInt, SIGUSR1); // Request a state file.
		usleep(100); // Allow time for the state file to be written.

		// Read statefiles
		res = read_file(stateFile, &state);
		if (res != 0) {
			TRACE(TRACE_ERROR, "Could not read state file [%s].", stateName);
			return NULL;
		}

		fclose(pidFile);
		fclose(stateFile);

		// Count the newlines
		size_t i, j = 0;
		for (i = 0; state[i]; i++) {
			if (state[i] == '\n')
				j++;
		}

		statelines = g_new0(char *, j + 1);

		// Break by newlines
		statelines[0] = state;
		for (i = 1, j = 0; state[j]; j++) {
			if (state[j] == '\n' && state[j+1]) {
				state[j] = '\0';
				statelines[i++] = &state[j+1];
			}
		}
		statelines[i] = NULL;

		return statelines;
}

