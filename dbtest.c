#include "db.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "debug.h"
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define PNAME "dbmail/insert-test"

char blk[READ_BLOCK_SIZE];
char header[] = "Test: test\n\n";
char uniqueid[70];

int main()
{
  u64_t msgid;
  int i,j;
  time_t start,stop,cost;

  db_connect();


  db_disconnect();
  return 0;
}

