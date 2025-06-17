#include "systemcalls.h"

/**
 * @param cmd the command to execute with system()
 * @return true if the command in @param cmd was executed
 *   successfully using the system() call, false if an error occurred,
 *   either in invocation of the system() call, or if a non-zero return
 *   value was returned by the command issued in @param cmd.
*/
bool do_system(const char *cmd)
{
    if (cmd == NULL) {
        return false;
    }

    int ret = system(cmd);

    // system() returns -1 if it fails to execute
    // otherwise, we check if the command itself exited with 0 (success)
    return (ret != -1 && WIFEXITED(ret) && WEXITSTATUS(ret) == 0);
}

/**
* @param count -The numbers of variables passed to the function. The variables are command to execute.
*   followed by arguments to pass to the command
*   Since exec() does not perform path expansion, the command to execute needs
*   to be an absolute path.
* @param ... - A list of 1 or more arguments after the @param count argument.
*   The first is always the full path to the command to execute with execv()
*   The remaining arguments are a list of arguments to pass to the command in execv()
* @return true if the command @param ... with arguments @param arguments were executed successfully
*   using the execv() call, false if an error occurred, either in invocation of the
*   fork, waitpid, or execv() command, or if a non-zero return value was returned
*   by the command issued in @param arguments with the specified arguments.
*/

bool do_exec(int count, ...)
{
    if (count < 1) {
        return false;
    }

    va_list args;
    va_start(args, count);

    // Create argument array for execv
    char *command[count + 1];
    for (int i = 0; i < count; i++) {
        command[i] = va_arg(args, char *);
    }
    command[count] = NULL;

    va_end(args);

    pid_t pid = fork();
    if (pid == -1) {
        // Fork failed
        return false;
    } else if (pid == 0) {
        // Child process
        execv(command[0], command);
        // If execv returns, there was an error
        perror("execv failed");
        exit(EXIT_FAILURE); // terminate child with failure
    } else {
        // Parent process: wait for child
        int status;
        if (waitpid(pid, &status, 0) == -1) {
            return false;
        }

        // Check if child exited normally and with exit code 0
        if ((status & 0x7F) == 0 && ((status >> 8) & 0xFF) == 0) {
            return true;
        } else {
            return false;
        }
    }
}

/**
* @param outputfile - The full path to the file to write with command output.
*   This file will be closed at completion of the function call.
* All other parameters, see do_exec above
*/
bool do_exec_redirect(const char *outputfile, int count, ...)
{
    if (count < 1 || outputfile == NULL) {
        return false;
    }

    va_list args;
    va_start(args, count);

    char *command[count + 1];
    for (int i = 0; i < count; i++) {
        command[i] = va_arg(args, char *);
    }
    command[count] = NULL;

    va_end(args);

    pid_t pid = fork();
    if (pid == -1) {
        // fork failed
        return false;
    }

    if (pid == 0) {
        // Child process

        // Open output file for writing (create if it doesn't exist, truncate if it does)
        int fd = open(outputfile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) {
            perror("open");
            exit(EXIT_FAILURE);
        }

        // Redirect stdout (file descriptor 1) to the file
        if (dup2(fd, STDOUT_FILENO) < 0) {
            perror("dup2");
            close(fd);
            exit(EXIT_FAILURE);
        }

        // Optional: redirect stderr too if you want
        // dup2(fd, STDERR_FILENO);

        close(fd); // original fd no longer needed

        execv(command[0], command);

        // If execv returns, an error occurred
        perror("execv");
        exit(EXIT_FAILURE);
    } else {
        // Parent process
        int status;
        if (waitpid(pid, &status, 0) == -1) {
            return false;
        }

        return ((status & 0x7F) == 0) && (((status >> 8) & 0xFF) == 0);
    }
}
