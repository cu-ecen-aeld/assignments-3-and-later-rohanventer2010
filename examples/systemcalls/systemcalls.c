#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/wait.h>
#include "systemcalls.h"


/**
 * @param cmd the command to execute with system()
 * @return true if the command in @param cmd was executed
 *   successfully using the system() *call, false if an error occurred,
 *   either in invocation of the system() call, or if a non-zero return
 *   value was returned by the command issued in @param cmd.
*/
bool do_system(const char *cmd)
{

/*
 *  Call the system() function with the command set in the cmd
 *   and return a boolean true if the system() call completed with success
 *   or false() if it returned a failure
*/
    int ret;
    ret = system(cmd);
    return (ret == 0) ? true : false;
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
    va_list args;
    va_start(args, count);
    char * command[count+1];
    int i;
    for(i=0; i<count; i++)
    {
        command[i] = va_arg(args, char *);
    }

    /* command list must be NULL terminated */
    command[count] = NULL;
    va_end(args);
    /*
    *   Execute a system command by calling fork, execv(),
    *   and wait instead of system (see LSP page 161).
    *   Use the command[0] as the full path to the command to execute
    *   (first argument to execv), and use the remaining arguments
    *   as second argument to the execv() command.
    *
    */

    int status;
    pid_t pid;
    pid = fork();

    if (pid == -1)
        return false;
    else if (pid == 0) 
    {
        execv (command[0], command); /* will only return on error */
        exit(EXIT_FAILURE); /* exit child process with failure */
    }

    pid_t ret_pid;
    ret_pid = waitpid (pid, &status, 0); /* pid_t waitpid(pid_t pid, int *stat_loc, int options) */
    if (ret_pid == -1)
        return false;
    
    /* pid == ret_pid ? 
    * check exit status code
    */
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

/**
* @param outputfile - The full path to the file to write with command output.
*   This file will be closed at completion of the function call.
* All other parameters, see do_exec above
*/
bool do_exec_redirect(const char *outputfile, int count, ...)
{
    va_list args;
    va_start(args, count);
    char * command[count+1];
    int i;
    for(i=0; i<count; i++)
    {
        command[i] = va_arg(args, char *);
    }
    /* command list must be NULL terminated */
    command[count] = NULL;
    va_end(args);

    /*
    *   Call execv, but first using https://stackoverflow.com/a/13784315/1446624 as a reference,
    *   redirect standard out to a file specified by outputfile.
    *   The rest of the behaviour is same as do_exec()
    */

    int fd;
    /* should we add the O_CLOEXEC option, as the comment in the stackoverflow post suggested?*/
    fd = open(outputfile, O_WRONLY|O_TRUNC|O_CREAT, 0644);

    if (fd < 0) 
        return false;

    int status;
    pid_t pid;
    pid = fork();

    if (pid == -1)
        return false;
    else if (pid == 0) 
    {
        if (dup2(fd, 1) < 0)
            exit(EXIT_FAILURE); /* exit child process with failure */
        close(fd);
        execv (command[0], command); /* will only return on error */
        exit(EXIT_FAILURE); /* exit child process with failure */
    }

    pid_t ret_pid;
    ret_pid = waitpid (pid, &status, 0); /* pid_t waitpid(pid_t pid, int *stat_loc, int options) */
    if (ret_pid == -1)
        return false;
    
    /* pid == ret_pid ? 
    * check exit status code
    */
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;

}
