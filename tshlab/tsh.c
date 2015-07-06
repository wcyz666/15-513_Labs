/* 
 * tsh - A tiny shell program with job control
 * 
 * Name: Wang Cheng
 * CMU ID: chengw1
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <errno.h>
#include <stdarg.h>

/* Misc manifest constants */
#define MAXLINE    1024   /* max line size */
#define MAXARGS     128   /* max args on a command line */
#define MAXJOBS      16   /* max jobs at any point in time */
#define MAXJID    1<<16   /* max job ID */

/* Job states */
#define UNDEF         0   /* undefined */
#define FG            1   /* running in foreground */
#define BG            2   /* running in background */
#define ST            3   /* stopped */

/* 
 * Jobs states: FG (foreground), BG (background), ST (stopped)
 * Job state transitions and enabling actions:
 *     FG -> ST  : ctrl-z
 *     ST -> FG  : fg command
 *     ST -> BG  : bg command
 *     BG -> FG  : fg command
 * At most 1 job can be in the FG state.
 */

/* Parsing states */
#define ST_NORMAL   0x0   /* next token is an argument */
#define ST_INFILE   0x1   /* next token is the input file */
#define ST_OUTFILE  0x2   /* next token is the output file */


/* Global variables */
extern char **environ;      /* defined in libc */
char prompt[] = "tsh> ";    /* command line prompt (DO NOT CHANGE) */
int verbose = 0;            /* if true, print additional output */
int nextjid = 1;            /* next job ID to allocate */
char sbuf[MAXLINE];         /* for composing sprintf messages */

struct job_t {              /* The job struct */
    pid_t pid;              /* job PID */
    int jid;                /* job ID [1, 2, ...] */
    int state;              /* UNDEF, BG, FG, or ST */
    char cmdline[MAXLINE];  /* command line */
};
struct job_t job_list[MAXJOBS]; /* The job list */

struct cmdline_tokens {
    int argc;               /* Number of arguments */
    char *argv[MAXARGS];    /* The arguments list */
    char *infile;           /* The input file */
    char *outfile;          /* The output file */
    enum builtins_t {       /* Indicates if argv[0] is a builtin command */
        BUILTIN_NONE,
        BUILTIN_QUIT,
        BUILTIN_JOBS,
        BUILTIN_BG,
        BUILTIN_FG} builtins;
};

/* End global variables */


/* Function prototypes */
void eval(char *cmdline);

void sigchld_handler(int sig);
void sigtstp_handler(int sig);
void sigint_handler(int sig);

void putJobForeground(struct job_t* targetJob, sigset_t maskSig);
void putJobBackground(struct job_t* targetJob);

/* Here are helper routines that we've provided for you */
int parseline(const char *cmdline, struct cmdline_tokens *tok); 
void sigquit_handler(int sig);

void clearjob(struct job_t *job);
void initjobs(struct job_t *job_list);
int maxjid(struct job_t *job_list); 
int addjob(struct job_t *job_list, pid_t pid, int state, char *cmdline);
int deletejob(struct job_t *job_list, pid_t pid); 
pid_t fgpid(struct job_t *job_list);
struct job_t *getjobpid(struct job_t *job_list, pid_t pid);
struct job_t *getjobjid(struct job_t *job_list, int jid); 
int pid2jid(pid_t pid); 
void listjobs(struct job_t *job_list, int output_fd);

void usage(void);
void unix_error(char *msg);
void app_error(char *msg);
typedef void handler_t(int);
handler_t *Signal(int signum, handler_t *handler);
/* Process control wrappers */

pid_t Fork(void);
void Execve(const char *filename, char *const argv[], char *const envp[]);
pid_t Wait(int *status);
pid_t Waitpid(pid_t pid, int *iptr, int options);
void Kill(pid_t pid, int signum);
unsigned int Sleep(unsigned int secs);
void Pause(void);
unsigned int Alarm(unsigned int seconds);
void Setpgid(pid_t pid, pid_t pgid);
pid_t Getpgrp();

/* Signal wrappers */
void Sigprocmask(int how, const sigset_t *set, sigset_t *oldset);
void Sigemptyset(sigset_t *set);
void Sigfillset(sigset_t *set);
void Sigaddset(sigset_t *set, int signum);
void Sigdelset(sigset_t *set, int signum);
int Sigismember(const sigset_t *set, int signum);
int Sigsuspend(const sigset_t *mask);
/* Unix I/O wrappers */
int Open(const char *pathname, int flags);
ssize_t Read(int fd, void *buf, size_t count);
ssize_t Write(int fd, const void *buf, size_t count);
off_t Lseek(int fildes, off_t offset, int whence);
void Close(int fd);
void Dup2(int, int);
void safe_printf(const char* format, ...);
/*
 * main - The shell's main routine 
 */
int 
main(int argc, char **argv) 
{
    char c;
    char cmdline[MAXLINE];    /* cmdline for fgets */
    int emit_prompt = 1; /* emit prompt (default) */

    /* Redirect stderr to stdout (so that driver will get all output
     * on the pipe connected to stdout) */
    dup2(1, 2);

    /* Parse the command line */
    while ((c = getopt(argc, argv, "hvp")) != EOF) {
        switch (c) {
        case 'h':             /* print help message */
            usage();
            break;
        case 'v':             /* emit additional diagnostic info */
            verbose = 1;
            break;
        case 'p':             /* don't print a prompt */
            emit_prompt = 0;  /* handy for automatic testing */
            break;
        default:
            usage();
        }
    }

    /* Install the signal handlers */

    /* These are the ones you will need to implement */
    Signal(SIGINT,  sigint_handler);   /* ctrl-c */
    Signal(SIGTSTP, sigtstp_handler);  /* ctrl-z */
    Signal(SIGCHLD, sigchld_handler);  /* Terminated or stopped child */
    Signal(SIGTTIN, SIG_IGN);
    Signal(SIGTTOU, SIG_IGN);

    /* This one provides a clean way to kill the shell */
    Signal(SIGQUIT, sigquit_handler); 

    /* Initialize the job list */
    initjobs(job_list);


    /* Execute the shell's read/eval loop */
    while (1) {

        if (emit_prompt) {
            printf("%s", prompt);
            fflush(stdout);
        }
        if ((fgets(cmdline, MAXLINE, stdin) == NULL) && ferror(stdin))
            app_error("fgets error");
        if (feof(stdin)) { 
            /* End of file (ctrl-d) */
            printf ("\n");
            fflush(stdout);
            fflush(stderr);
            exit(0);
        }
        
        /* Remove the trailing newline */
        cmdline[strlen(cmdline)-1] = '\0';
        
        /* Evaluate the command line */
        eval(cmdline);
        
        fflush(stdout);
        fflush(stdout);
    } 
    
    exit(0); /* control never reaches here */
}

/* 
 * eval - Evaluate the command line that the user has just typed in
 * 
 * If the user has requested a built-in command (quit, jobs, bg or fg)
 * then execute it immediately. Otherwise, fork a child process and
 * run the job in the context of the child. If the job is running in
 * the foreground, wait for it to terminate and then return.  Note:
 * each child process must have a unique process group ID so that our
 * background children don't receive SIGINT (SIGTSTP) from the kernel
 * when we type ctrl-c (ctrl-z) at the keyboard.  
 */
void 
eval(char *cmdline) 
{
    int bg;              /* should the job run in bg or fg? */
    struct cmdline_tokens tok;
    int input_fd = 0;  
    int output_fd = 1;
    pid_t chpid;             /* contains the pid of the child */
    struct job_t* targetJob; /* used for bg and fg coomand */ 
    int parsedID;            /* contains the jobid/processid for bg/fg */
    int state;
    sigset_t maskSig, prevSig;

    /* Since many wrapper functions in csapp.c have been imported
     * Here I can omit all error handling routins.
     */

    /* Create signal mask. The following signals will be masked
     * and blocked before Fork() is called:
     *  SIGCHLD
     *  SIGINT
     *  SIGTSTP
     */
    
    Sigemptyset(&maskSig);
    Sigaddset(&maskSig, SIGCHLD);
    Sigaddset(&maskSig, SIGINT);
    Sigaddset(&maskSig, SIGTSTP);
    
    /* Parse command line */
    bg = parseline(cmdline, &tok); 

    if (bg == -1) /* parsing error */
        return;
    if (tok.argv[0] == NULL) /* ignore empty lines */
        return;

   /* Open redirected files if needed. Otherwise, the default value of 
    * input_fd is 0 (STDIN), and output_fd is 1 (STDOUT). */

    if (tok.infile != NULL) {
        input_fd = Open(tok.infile, O_RDONLY);
    }

    if (tok.outfile != NULL) {
        output_fd = Open(tok.outfile, O_WRONLY);
    }
    
    /* execute the command according to BUILTINS */

    /* Maybe I should use switch? */    

    /* Block some signals to avoid race condition */
    Sigprocmask(SIG_BLOCK, &maskSig, &prevSig);

    if (tok.builtins == BUILTIN_QUIT) {
        _exit(0);
    }
    else if (tok.builtins == BUILTIN_JOBS) {
        listjobs(job_list, output_fd);      
    }
    else if (tok.builtins == BUILTIN_BG) {

        /* No arguments -> Invalid input */

        if (tok.argv[1] == NULL) {
             (void) fprintf(stderr,
                       "bg command requires PID or %%jobid argument\n");
        }
        else if (tok.argv[1][0] == '%') {

           /* the second argument is a job ID */

            parsedID = atoi(&tok.argv[1][1]);

            if ((targetJob = getjobjid(job_list, parsedID)) == NULL) {
                (void) fprintf(stderr,
                       "%s: No such job\n", tok.argv[1]);   
            }
            else {
                                putJobBackground(targetJob);
            }
            
        }
        else if (tok.argv[1][0] >= '0' && tok.argv[1][0] <= '9') {

           /* The second argument is a process ID. */

            parsedID = atoi(&tok.argv[1][0]);

            if ((targetJob = getjobpid(job_list, (pid_t) parsedID)) == NULL) {
                (void) fprintf(stderr,
                       "(%d): No such process\n", parsedID);    
            }
            else {
                                putJobBackground(targetJob);
            }
            
        }
        else {

            /* Other invalid input */

             (void) fprintf(stderr,
                       "bg command requires PID or %%jobid argument\n");
        }
        
        
    }
    else if (tok.builtins == BUILTIN_FG) {

        /* No arguments -> Invalid input */

        if (tok.argv[1] == NULL) {
             (void) fprintf(stderr,
                       "fg command requires PID or %%jobid argument\n");
        }
        else if (tok.argv[1][0] == '%') {

           /* the second argument is a job ID */

            parsedID = atoi(&tok.argv[1][1]);

            if ((targetJob = getjobjid(job_list, parsedID)) == NULL) {
                (void) fprintf(stderr,
                       "%s: No such job\n", tok.argv[1]);   
            }
            else {
                putJobForeground(targetJob, prevSig);
            }
            
        }
        else if (tok.argv[1][0] >= '0' && tok.argv[1][0] <= '9') {

           /* The second argument is a process ID. */

            parsedID = atoi(&tok.argv[1][0]);

            if ((targetJob = getjobpid(job_list, (pid_t) parsedID)) == NULL) {
                (void) fprintf(stderr,
                       "(%d): No such process\n", parsedID);    
            }
            else {
                putJobForeground(targetJob, prevSig);
            }
            
        }
        else {

            /* Other invalid input */

             (void) fprintf(stderr,
                       "fg command requires PID or %%jobid argument\n");
        }
        
    }
    else {
        
        /* Not a buildin command, fork-execute needed */    

        if ((chpid = Fork()) == 0) {
            
            /* in Child */ 
       
            /* Step 0: Restore all signal handlers and signal mask to their 
                default value */    
        
            Signal(SIGINT, SIG_DFL);   /* ctrl-c */
            Signal(SIGTSTP, SIG_DFL);  /* ctrl-z */
            Signal(SIGCHLD, SIG_DFL);  /* Terminated or stopped child */
            Signal(SIGTTIN, SIG_DFL);
            Signal(SIGTTOU, SIG_DFL);
            Signal(SIGQUIT, SIG_DFL); 
            
            /* Step 1: Set gid to create a new process group */

            /* Noted that setpgid is called twice, both in shell side
             * and child side. This is to avoid race condition: each
             * child must be put in the process group before executing
             * and shell itself depends on having all child process
             * in the process group before continuing. So call it twice
             * ensures that the right thing happens no matter which 
             * process goes first.
             * Since one of the two calls will failed, here we don't 
             * use wrapper funtions 
             */
            
            chpid = getpid();
            setpgid(chpid, chpid);
    
            Sigprocmask(SIG_SETMASK, &prevSig, NULL);
            
            /* Step 2: Deal with input and output 
             * we copy the newly opened fd to the original fd and close it. 
             */
        
            if (input_fd != 0) {
                Dup2(input_fd, 0);
                Close(input_fd);
            }
        
            if (output_fd != 1) {
                Dup2(output_fd, 1);
                Close(output_fd);
            }   
            
            /* Step 3: Execute the specified command */

            /* FIRE THE MISSILE! */
            Execve(tok.argv[0], tok.argv, environ);
        }
        else {

            /* In parent */
            
            /* Step 0: Discover whether this is a fg job or not. */
            
            state = (bg == 1) ? BG : FG;

            /* Step 1: Double-set pgid to confirm it is set immediately
             *  after a child is born.
             */

            /* Noted that setpgid is called twice, both in shell side
             * and child side. This is to avoid race condition: each
             * child must be put in the process group before executing
             * and shell itself depends on having all child process
             * in the process group before continuing. So call it twice
             * ensures that the right thing happens no matter which 
             * process goes first.
             * Since one of the two calls will failed, here we don't 
             * use wrapper funtions 
             */
            
            setpgid(chpid, chpid);

            /* Step 2: Add this new job to joblist. */

            if (addjob(job_list, chpid, state, cmdline) == 1) {
                
                targetJob = getjobpid(job_list, chpid);
            
                /* Step 3: put the job to foreground if needed 
                   Waiting until the foreground job is stopped or dead
                */
                
                
                if (state == FG) {
                    
                    while (fgpid(job_list) != 0) {
                        Sigsuspend(&prevSig);
                    }

                }
                else {

                    /* Print the info of background job */
                    printf("[%d] (%d) %s\n", targetJob->jid, targetJob->pid, cmdline);
                }
            }

        }

    }

    /* Restore previous signal mask */


   /* Close opened files if needed.
    * Parent side or Child side for failure
    */
    
    Sigprocmask(SIG_SETMASK, &prevSig, NULL);
    
    if (input_fd != 0)
        Close(input_fd);
    if (output_fd != 1)
        Close(output_fd);
    
    return;
}

/* 
 * parseline - Parse the command line and build the argv array.
 * 
 * Parameters:
 *   cmdline:  The command line, in the form:
 *
 *                command [arguments...] [< infile] [> oufile] [&]
 *
 *   tok:      Pointer to a cmdline_tokens structure. The elements of this
 *             structure will be populated with the parsed tokens. Characters 
 *             enclosed in single or double quotes are treated as a single
 *             argument. 
 * Returns:
 *   1:        if the user has requested a BG job
 *   0:        if the user has requested a FG job  
 *  -1:        if cmdline is incorrectly formatted
 * 
 * Note:       The string elements of tok (e.g., argv[], infile, outfile) 
 *             are statically allocated inside parseline() and will be 
 *             overwritten the next time this function is invoked.
 */
int 
parseline(const char *cmdline, struct cmdline_tokens *tok) 
{

    static char array[MAXLINE];          /* holds local copy of command line */
    const char delims[10] = " \t\r\n";   /* argument delimiters (white-space) */
    char *buf = array;                   /* ptr that traverses command line */
    char *next;                          /* ptr to the end of the current arg */
    char *endbuf;                        /* ptr to end of cmdline string */
    int is_bg;                           /* background job? */

    int parsing_state;                   /* indicates if the next token is the
                                            input or output file */

    if (cmdline == NULL) {
        (void) fprintf(stderr, "Error: command line is NULL\n");
        return -1;
    }

    (void) strncpy(buf, cmdline, MAXLINE);
    endbuf = buf + strlen(buf);

    tok->infile = NULL;
    tok->outfile = NULL;

    /* Build the argv list */
    parsing_state = ST_NORMAL;
    tok->argc = 0;

    while (buf < endbuf) {
        /* Skip the white-spaces */
        buf += strspn (buf, delims);
        if (buf >= endbuf) break;

        /* Check for I/O redirection specifiers */
        if (*buf == '<') {
            if (tok->infile) {
                (void) fprintf(stderr, "Error: Ambiguous I/O redirection\n");
                return -1;
            }
            parsing_state |= ST_INFILE;
            buf++;
            continue;
        }
        if (*buf == '>') {
            if (tok->outfile) {
                (void) fprintf(stderr, "Error: Ambiguous I/O redirection\n");
                return -1;
            }
            parsing_state |= ST_OUTFILE;
            buf ++;
            continue;
        }

        if (*buf == '\'' || *buf == '\"') {
            /* Detect quoted tokens */
            buf++;
            next = strchr (buf, *(buf-1));
        } else {
            /* Find next delimiter */
            next = buf + strcspn (buf, delims);
        }
        
        if (next == NULL) {
            /* Returned by strchr(); this means that the closing
               quote was not found. */
            (void) fprintf (stderr, "Error: unmatched %c.\n", *(buf-1));
            return -1;
        }

        /* Terminate the token */
        *next = '\0';

        /* Record the token as either the next argument or the i/o file */
        switch (parsing_state) {
        case ST_NORMAL:
            tok->argv[tok->argc++] = buf;
            break;
        case ST_INFILE:
            tok->infile = buf;
            break;
        case ST_OUTFILE:
            tok->outfile = buf;
            break;
        default:
            (void) fprintf(stderr, "Error: Ambiguous I/O redirection\n");
            return -1;
        }
        parsing_state = ST_NORMAL;

        /* Check if argv is full */
        if (tok->argc >= MAXARGS-1) break;

        buf = next + 1;
    }

    if (parsing_state != ST_NORMAL) {
        (void) fprintf(stderr,
                       "Error: must provide file name for redirection\n");
        return -1;
    }

    /* The argument list must end with a NULL pointer */
    tok->argv[tok->argc] = NULL;

    if (tok->argc == 0)  /* ignore blank line */
        return 1;

    if (!strcmp(tok->argv[0], "quit")) {                 /* quit command */
        tok->builtins = BUILTIN_QUIT;
    } else if (!strcmp(tok->argv[0], "jobs")) {          /* jobs command */
        tok->builtins = BUILTIN_JOBS;
    } else if (!strcmp(tok->argv[0], "bg")) {            /* bg command */
        tok->builtins = BUILTIN_BG;
    } else if (!strcmp(tok->argv[0], "fg")) {            /* fg command */
        tok->builtins = BUILTIN_FG;
    } else {
        tok->builtins = BUILTIN_NONE;
    }

    /* Should the job run in the background? */
    if ((is_bg = (*tok->argv[tok->argc-1] == '&')) != 0)
        tok->argv[--tok->argc] = NULL;

    return is_bg;
}


/*****************
 * Signal handlers
 *****************/

/* 
 * sigchld_handler - The kernel sends a SIGCHLD to the shell whenever
 *     a child job terminates (becomes a zombie), or stops because it
 *     received a SIGSTOP, SIGTSTP, SIGTTIN or SIGTTOU signal. The 
 *     handler reaps all available zombie children, but doesn't wait 
 *     for any other currently running children to terminate.  
 */
void 
sigchld_handler(int sig) 
{
    int status;
    pid_t chpid;
    struct job_t* targetJob;
    sigset_t mask, prev_mask;

    /* Fill all signals for further use. */
    Sigfillset(&mask);

    while ((chpid = Waitpid(-1, &status, WNOHANG | WUNTRACED)) > 0) {
        
        if ((targetJob = getjobpid(job_list, chpid)) != NULL) { 

            /* Step 0: Block all signals before we change the
             * shared data structures. 
             *
             * Since getjobpid and fgpid only read the shared date structures,
             * We don't have to block signals before them.
             */
            Sigprocmask(SIG_BLOCK, &mask, &prev_mask);
        
            /* Case 1: the process is stopped by any signal.
             * In this case we change the status of the process and 
             * print corresponding message
             */
            if (WIFSTOPPED(status)) {
                safe_printf("Job [%d] (%d) stopped by signal %d\n",
                    targetJob->jid, chpid, WSTOPSIG(status));
                
                targetJob->state = ST;
            }
            else {

            /* Case 2: the process is terminated by any signal;
             * In this case we delete the job and print corres-
             * -ponding message
             * 
             * Case 3: the process is terminated normally;
             * In this case we just delete the job.
             */ 
                if (WIFSIGNALED(status)) {
                    safe_printf("Job [%d] (%d) terminated by signal %d\n",
                        targetJob->jid, chpid, WTERMSIG(status));
                }
                
                deletejob(job_list, chpid);
            }
            
            Sigprocmask(SIG_SETMASK, &prev_mask, NULL);
        }
    }

    return;
}

/* 
 * sigint_handler - The kernel sends a SIGINT to the shell whenver the
 *    user types ctrl-c at the keyboard.  Catch it and send it along
 *    to the foreground job.  
 */
void 
sigint_handler(int sig) 
{
    /* The logic is very easy. Shell is only a forwarder.
     * When we receive a SIGINT signal, we just forward it 
     * to the foreground process. If there is no foreground
     * process, we do nothing
     */

    pid_t foregroundPid;

    if ((foregroundPid = fgpid(job_list)) != 0) {
    
        /* Forward it to foreground process group */

        Kill(-foregroundPid, SIGINT);
    }
         
    return;
}

/*
 * sigtstp_handler - The kernel sends a SIGTSTP to the shell whenever
 *     the user types ctrl-z at the keyboard. Catch it and suspend the
 *     foreground job by sending it a SIGTSTP.  
 */
void 
sigtstp_handler(int sig) 
{
    /* The logic is very easy. Shell is only a forwarder.
     * When we receive a SIGTSTP signal, we just forward it 
     * to the foreground process. If there is no foreground
     * process, we do nothing.
     */

    pid_t foregroundPid;

    if ((foregroundPid = fgpid(job_list)) != 0) {
    
        /* Forward it to foreground process group */

        Kill(-foregroundPid, SIGTSTP);
    }

    return;
}

/*********************
 * End signal handlers
 *********************/

/***********************************************
 * Helper routines that manipulate the job list
 **********************************************/

/* clearjob - Clear the entries in a job struct */
void 
clearjob(struct job_t *job) {
    job->pid = 0;
    job->jid = 0;
    job->state = UNDEF;
    job->cmdline[0] = '\0';
}

/* initjobs - Initialize the job list */
void 
initjobs(struct job_t *job_list) {
    int i;

    for (i = 0; i < MAXJOBS; i++)
        clearjob(&job_list[i]);
}

/* maxjid - Returns largest allocated job ID */
int 
maxjid(struct job_t *job_list) 
{
    int i, max=0;

    for (i = 0; i < MAXJOBS; i++)
        if (job_list[i].jid > max)
            max = job_list[i].jid;
    return max;
}

/* addjob - Add a job to the job list */
int 
addjob(struct job_t *job_list, pid_t pid, int state, char *cmdline) 
{
    int i;

    if (pid < 1)
        return 0;

    for (i = 0; i < MAXJOBS; i++) {
        if (job_list[i].pid == 0) {
            job_list[i].pid = pid;
            job_list[i].state = state;
            job_list[i].jid = nextjid++;
            if (nextjid > MAXJOBS)
                nextjid = 1;
            strcpy(job_list[i].cmdline, cmdline);
            if(verbose){
                printf("Added job [%d] %d %s\n",
                       job_list[i].jid,
                       job_list[i].pid,
                       job_list[i].cmdline);
            }
            return 1;
        }
    }
    printf("Tried to create too many jobs\n");
    return 0;
}

/* deletejob - Delete a job whose PID=pid from the job list */
int 
deletejob(struct job_t *job_list, pid_t pid) 
{
    int i;

    if (pid < 1)
        return 0;

    for (i = 0; i < MAXJOBS; i++) {
        if (job_list[i].pid == pid) {
            clearjob(&job_list[i]);
            nextjid = maxjid(job_list)+1;
            return 1;
        }
    }
    return 0;
}

/* fgpid - Return PID of current foreground job, 0 if no such job */
pid_t 
fgpid(struct job_t *job_list) {
    int i;

    for (i = 0; i < MAXJOBS; i++)
        if (job_list[i].state == FG)
            return job_list[i].pid;
    return 0;
}

/* getjobpid  - Find a job (by PID) on the job list */
struct job_t 
*getjobpid(struct job_t *job_list, pid_t pid) {
    int i;

    if (pid < 1)
        return NULL;
    for (i = 0; i < MAXJOBS; i++)
        if (job_list[i].pid == pid)
            return &job_list[i];
    return NULL;
}

/* getjobjid  - Find a job (by JID) on the job list */
struct job_t *getjobjid(struct job_t *job_list, int jid) 
{
    int i;

    if (jid < 1)
        return NULL;
    for (i = 0; i < MAXJOBS; i++)
        if (job_list[i].jid == jid)
            return &job_list[i];
    return NULL;
}

/* pid2jid - Map process ID to job ID */
int 
pid2jid(pid_t pid) 
{
    int i;

    if (pid < 1)
        return 0;
    for (i = 0; i < MAXJOBS; i++)
        if (job_list[i].pid == pid) {
            return job_list[i].jid;
        }
    return 0;
}

/* listjobs - Print the job list */
void 
listjobs(struct job_t *job_list, int output_fd) 
{
    int i;
    char buf[MAXLINE];

    for (i = 0; i < MAXJOBS; i++) {
        memset(buf, '\0', MAXLINE);
        if (job_list[i].pid != 0) {
            sprintf(buf, "[%d] (%d) ", job_list[i].jid, job_list[i].pid);
            if(write(output_fd, buf, strlen(buf)) < 0) {
                fprintf(stderr, "Error writing io output file\n");
                exit(1);
            }
            memset(buf, '\0', MAXLINE);
            switch (job_list[i].state) {
            case BG:
                sprintf(buf, "Running    ");
                break;
            case FG:
                sprintf(buf, "Foreground ");
                break;
            case ST:
                sprintf(buf, "Stopped    ");
                break;
            default:
                sprintf(buf, "listjobs: Internal error: job[%d].state=%d ",
                        i, job_list[i].state);
            }
            if(write(output_fd, buf, strlen(buf)) < 0) {
                fprintf(stderr, "Error writing to output file\n");
                exit(1);
            }
            memset(buf, '\0', MAXLINE);
            sprintf(buf, "%s\n", job_list[i].cmdline);
            if(write(output_fd, buf, strlen(buf)) < 0) {
                fprintf(stderr, "Error writing to output file\n");
                exit(1);
            }
        }
    }
}
/******************************
 * end job list helper routines
 ******************************/


/***********************
 * Other helper routines
 ***********************/

/*
 * usage - print a help message
 */
void 
usage(void) 
{
    printf("Usage: shell [-hvp]\n");
    printf("   -h   print this message\n");
    printf("   -v   print additional diagnostic information\n");
    printf("   -p   do not emit a command prompt\n");
    exit(1);
}

/*
 * unix_error - unix-style error routine
 */
void 
unix_error(char *msg)
{
    fprintf(stdout, "%s: %s\n", msg, strerror(errno));
    exit(1);
}

/*
 * app_error - application-style error routine
 */
void 
app_error(char *msg)
{
    fprintf(stdout, "%s\n", msg);
    exit(1);
}

/*
 * Signal - wrapper for the sigaction function
 */
handler_t 
*Signal(int signum, handler_t *handler) 
{
    struct sigaction action, old_action;

    action.sa_handler = handler;  
    sigemptyset(&action.sa_mask); /* block sigs of type being handled */
    action.sa_flags = SA_RESTART; /* restart syscalls if possible */

    if (sigaction(signum, &action, &old_action) < 0)
        unix_error("Signal error");
    return (old_action.sa_handler);
}

/*
 * sigquit_handler - The driver program can gracefully terminate the
 *    child shell by sending it a SIGQUIT signal.
 */
void 
sigquit_handler(int sig) 
{
    printf("Terminating after receipt of SIGQUIT signal\n");
    exit(1);
}



/*********************************************
 * Wrappers for Unix process control functions
 ********************************************/

/* $begin forkwrapper */
pid_t Fork(void) 
{
    pid_t pid;

    if ((pid = fork()) < 0)
    unix_error("Fork error");
    return pid;
}
/* $end forkwrapper */

void Execve(const char *filename, char *const argv[], char *const envp[]) 
{
    if (execve(filename, argv, envp) < 0)
    unix_error("Execve error");
}

/* $begin wait */
pid_t Wait(int *status) 
{
    pid_t pid;

    if ((pid  = wait(status)) < 0)
    unix_error("Wait error");
    return pid;
}
/* $end wait */

pid_t Waitpid(pid_t pid, int *iptr, int options) 
{
    pid_t retpid;

    retpid  = waitpid(pid, iptr, options);
    
    return(retpid);
}

/* $begin kill */
void Kill(pid_t pid, int signum) 
{
    int rc;

    if ((rc = kill(pid, signum)) < 0)
    unix_error("Kill error");
}
/* $end kill */

void Pause() 
{
    (void)pause();
    return;
}

unsigned int Sleep(unsigned int secs) 
{
    unsigned int rc;

    if ((rc = sleep(secs)) < 0)
    unix_error("Sleep error");
    return rc;
}

unsigned int Alarm(unsigned int seconds) {
    return alarm(seconds);
}
 
void Setpgid(pid_t pid, pid_t pgid) {
    int rc;

    if ((rc = setpgid(pid, pgid)) < 0)
    unix_error("Setpgid error");
    return;
}

pid_t Getpgrp(void) {
    return getpgrp();
}


void Sigprocmask(int how, const sigset_t *set, sigset_t *oldset)
{
    if (sigprocmask(how, set, oldset) < 0)
    unix_error("Sigprocmask error");
    return;
}

void Sigemptyset(sigset_t *set)
{
    if (sigemptyset(set) < 0)
    unix_error("Sigemptyset error");
    return;
}

void Sigfillset(sigset_t *set)
{ 
    if (sigfillset(set) < 0)
    unix_error("Sigfillset error");
    return;
}

void Sigaddset(sigset_t *set, int signum)
{
    if (sigaddset(set, signum) < 0)
    unix_error("Sigaddset error");
    return;
}

void Sigdelset(sigset_t *set, int signum)
{
    if (sigdelset(set, signum) < 0)
    unix_error("Sigdelset error");
    return;
}

int Sigismember(const sigset_t *set, int signum)
{
    int rc;
    if ((rc = sigismember(set, signum)) < 0)
    unix_error("Sigismember error");
    return rc;
}


/********************************
 * Wrappers for Unix I/O routines
 ********************************/

int Open(const char *pathname, int flags) 
{
    int rc;

    if ((rc = open(pathname, flags))  < 0)
        unix_error("Open error");
    return rc;
}

ssize_t Read(int fd, void *buf, size_t count) 
{
    ssize_t rc;

    if ((rc = read(fd, buf, count)) < 0) 
    unix_error("Read error");
    return rc;
}

ssize_t Write(int fd, const void *buf, size_t count) 
{
    ssize_t rc;

    if ((rc = write(fd, buf, count)) < 0)
    unix_error("Write error");
    return rc;
}

off_t Lseek(int fildes, off_t offset, int whence) 
{
    off_t rc;

    if ((rc = lseek(fildes, offset, whence)) < 0)
    unix_error("Lseek error");
    return rc;
}

void Close(int fd) 
{
    int rc;

    if ((rc = close(fd)) < 0)
        unix_error("Close error");
}

void Dup2(int newFd, int oldFd) {
    int rc;

    if ((rc = dup2(newFd, oldFd)) < 0)
        unix_error("Dup error");
}
int Sigsuspend(const sigset_t *mask) {
    return sigsuspend(mask);
}
void putJobForeground(struct job_t* targetJob, sigset_t maskSig) {

    /* Update the state info of the specified job
     * And send SIGCONT signal to it.
     */
    if (targetJob->state == ST) {
        Kill(-targetJob->pid, SIGCONT);
    }    
    targetJob->state = FG;
                
    /* Suspend the shell and waiting for the blocked signals:
     *  SIGINT
     *  SIGTSTP
     *  SIGCHLD
     */
    while (fgpid(job_list) != 0) {
        Sigsuspend(&maskSig);
    }
}
void safe_printf(const char* format, ...) {
    /* Safe printf can be used in signal handlers without
     * worrying about race condition.
     */
    
    char buf [1024];
    va_list args;
    sigset_t mask, prev_mask;

    Sigfillset(&mask);
    Sigprocmask(SIG_BLOCK, &mask, &prev_mask);
    va_start(args, format);
    vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);
    Write(1, buf, strlen(buf));
    Sigprocmask(SIG_SETMASK, &prev_mask, NULL);
}

void putJobBackground(struct job_t* targetJob) {

    /* Update the state info of the specified job
     * And send SIGCONT signal to it.
     */

        printf("[%d] (%d) %s\n", 
                targetJob->jid, targetJob->pid, targetJob->cmdline);
    if (targetJob->state == ST) {
        Kill(-targetJob->pid, SIGCONT);
            targetJob->state = BG;
    }
}    
