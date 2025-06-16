#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>

int main(int argc, char *argv[]) {
    // Open syslog with LOG_USER facility
    openlog("writer", LOG_PID, LOG_USER);

    // Check arguments
    if (argc != 3) {
        syslog(LOG_ERR, "Invalid number of arguments. Usage: %s <file> <string>", argv[0]);
        fprintf(stderr, "Error: Invalid number of arguments.\nUsage: %s <file> <string>\n", argv[0]);
        closelog();
        return 1;
    }

    const char *writefile = argv[1];
    const char *writestr = argv[2];

    // Open the file for writing (overwrite if exists)
    FILE *fp = fopen(writefile, "w");
    if (fp == NULL) {
        syslog(LOG_ERR, "Failed to open file: %s", writefile);
        perror("Error opening file");
        closelog();
        return 1;
    }

    // Write the string to the file
    if (fputs(writestr, fp) == EOF) {
        syslog(LOG_ERR, "Failed to write to file: %s", writefile);
        perror("Error writing to file");
        fclose(fp);
        closelog();
        return 1;
    }

    // Close the file
    fclose(fp);

    // Log the successful write
    syslog(LOG_DEBUG, "Writing %s to %s", writestr, writefile);

    // Close syslog
    closelog();

    return 0;
}
