#include <stdio.h>
#include <stdlib.h>
#include <syslog.h>

int main(int argc, char *argv[])
{
    FILE* writefile;
    int res;

    // check number of parameters
    if (argc < 3)
    {
        printf("Usage: %s <writefile> <writestr>\n", argv[0]);
        return 1;
    }

    openlog("writer", LOG_PID, LOG_USER);

    // open file for writing
    writefile = fopen(argv[1], "w");
    if (writefile == NULL) {
        syslog(LOG_ERR, "File could not be created");
        printf("File could not be created\n");
        return 1;
    }

    // write to file
    syslog(LOG_DEBUG, "Writing %s to %s", argv[2], argv[1]);
    res = fprintf(writefile, "%s", argv[2]);
    if (res < 0) {
        syslog(LOG_ERR, "File could not be written");
        printf("File could not be written\n");
        return 1;
    }

    fclose(writefile);
    closelog();

    return 0;
}
