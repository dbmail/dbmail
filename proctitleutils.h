/*
 * proctitleutils.h
 *
 * headerfile declaring prototypes for functions to adjust their title 
 * in the process list
 * 
 */

#ifndef PROC_TITLE_UTILS_H
#define PROC_TITLE_UTILS_H

void set_proc_title(char *fmt,...);
void init_set_proc_title(int argc, char *argv[], char *envp[], const char *name);

#endif
