
/* Filename: smallsh.c
 * Author: Howard Chen
 * Date Created: 8-1-2017
 * Description: Implements a basic shell according to the Program 3 specifications
 * Class: CS_344
 *
 * citations:
 * 	https://stackoverflow.com/questions/18433585/kill-all-child-processes-of-a-parent-
 * 		but-leave-the-parent-alive    provided the idea for using different
 * 		signals for parent versus children processes to gain different behavior
 * 	lecture notes -- for a lot of things, including but not limited to how to use
 * 		getline, how to work with processes, how to work with signals,
 * 		and how to work with the kill command.
 */


#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <dirent.h>
#include <signal.h>


/**********          Program constants         ************* */
#define EXIT 30       /*Return value that indicates shell should exit */
#define MAX_CHAR 4000  /*max chars that a single command can take */
#define MAX_ARG 700 /*Max args that can be in a single command */



/*******        Global variables          ************/
/*Ideally I wouldn't use global variables, but they were really convenient to use in my program to allow
 * signals to communicate with the main program. I did my best to ensure that the global variables
 * were only written to in one or two places, and only read from one or two places, and ONLY
 * in the parent process, so that no data races would occur */

int foreground_status = 0; /*Holds exit value or terminating signal */
int is_exit = 1;  /*Indicates whether foreground_status is an exit value or terminating signal */

char pid[50]; /*Stores the PID of the shell */

sig_atomic_t special; /*Global variable to hold whether special SIGTSP state has been entered 
 sig_atomic_t type was used for reentrancy*/


/************  Function Prototypes   *************/
/* See function implementations at end for function comments */
void kill_everything();
void catchSIGINT(int signo);
void catchSIGTSTP(int signo);
void catchSIGCHLD(int signo);
void catchSIGUSR1(int signo);
void parentSignalSetup();
void foregroundSignalSetup();
void backgroundSignalSetup();

void getInput(char command[]);
void getCommand(char command[]);

int is_builtin(char* params[]);
int cd(char* params[]);
int status();
int exec_builtin(char* params[], int argc);
void redirect_in_out(char* params[], int argc, int foreground);
void clean(char* params[], int argc);
int is_foreground(char* params[], int argc);
int exec_non_builtin(char* params[], int argc);
int execute(char* params[], int argc);

void cleanup();
int parse( char* params[], int max, char* command);


/************   MAIN             *****************************/
int main() {

	int argc; /* number of arguments */
	int ex;   /* exit flag */
	char* params[MAX_ARG];  /* holds arguments */
	char command[MAX_CHAR]; /* Holds user input command */
	
	/*Initially, not in special TSTP state */
	special = 0;

	/*Before everything, set up the signals for the parent process*/
	parentSignalSetup();

	/*Get the process ID for use later*/
	sprintf(pid, "%i", getpid());

	/*get initial command*/
	memset(command, '\0', sizeof(command));
	getCommand(command);
	
	/*parse the initial command to get an argument list */
	memset(params, 0, sizeof(params));
	argc = parse(params, MAX_ARG - 1, command);

	ex = 0; /*initial exit flag is not set*/
	while( 1 ) {
		if (argc == 0) {
			/* if only newlines and white spaces were entered, do nothing */
		}
		else if (params[0][0] == '#') {
			/* if command starts with #, it is a comment and should be ignored.
 * 				So do nothing */

		}
		else if(argc < MAX_ARG - 2) {
			/*Otherwise, if the argument list was not exceeded, execute the command */
			ex = execute(params, argc);

			/*If the exit flag was called, execute returns EXIT. So break out of the loop */
			if (ex == EXIT) {
				break;
			}
		}
		else {
			perror("Too many command line arguments.\n");
		}
		/*Before getting next command, print out any completed background processes*/
		/*Let a returning child process send a signal to set a flag, so cleanup happens here.*/
		cleanup();

		/*Get the next command and parse it into arguments */
		memset(command, '\0', sizeof(command));
		getCommand(command);
		memset(params, 0, sizeof(params));
		argc = parse(params, MAX_ARG,  command);
	} 


	/*Before exiting the shell, kill all background processes */
	kill_everything();

	return 0;
}

/* Description: kills all background processes
 * Arguments: none
 * Pre: Background processes should do the default action upon receiving
 *		SIGTERM
 * Post: All background processes will terminate, and the parent process will clean them up
 * ret: none
 */
void kill_everything() {
	int childPID = 5;
	int childExitMethod = 5;
	int exitStatus;
	int signal;

	/*Send the terminate signal to all background processess 
 * 		of the shell*/
	kill(0, SIGTERM);

	/*Sleep to give background processes time to die */
	sleep(2);

	/*Clean up every terminated background process that is available*/
	childPID = waitpid(-1, &childExitMethod, WNOHANG);
	while(childPID != 0 && childPID != -1) {
		/*Just for kicks, get the exit/terminate number for each child process */	
		/*Technically, this is unnecessary and wastes CPU time */
		if(WIFEXITED(childExitMethod) != 0) {
			exitStatus = WEXITSTATUS(childExitMethod);
		} else if (WIFSIGNALED(childExitMethod) != 0) {
			signal = WTERMSIG(childExitMethod);
		} else {
			perror("Failure to find child exit! \n"); fflush(stderr);
			exit(1);
		}
		
		/*Check for another child process to clean up */
		childPID = waitpid(-1, &childExitMethod, WNOHANG);
	}
	fflush(stdout);

	return;
}

/*Signal handler for SIGTSTP. All this does is print a message based on the special
 * global variable, and then update the special global variable */
void catchSIGTSTP(int signo) {
	char* message;
	if(special == 0) {
		message = "\nEntering foreground-only mode (& is now ignored)\n";
		write(STDOUT_FILENO, message, 50);
	}
	else {
		message = "\nExiting foreground-only mode\n";
		write(STDOUT_FILENO, message, 30);
	}
	
	/*Update the special global variable*/
	special = (special + 1) % 2;
	return;
	
}

/*Signal handler for SIGCHLD. Does nothing. I tried deleting and setting the parent process
 * to ignore SIGCHLD, but that caused bugs for some reason, so I've left this in even though
 * it really should be removed */
void catchSIGCHLD(int signo) {
	/*Update the available gloabal variable */
}

/* Description: sets up signal handling for the shell
 * Args: none
 * pre: none
 * post: Sets up SIGTSTP and SIGCHLD for their own special signal handling.
 * 	Sets up SIGINT and SIGTERM to be ignored by the shell process
 * ret: none
 *
 */
void parentSignalSetup() {
	/*Parent ignores SIGINT */
	/*Parent responds to SIGTSTP */

	struct sigaction IGNORE_action = {{0}};
	struct sigaction SIGTSTP_action = {{0}};
	struct sigaction SIGCHLD_action = {{0}};

	/*Set up SIGTSTP handling */
	SIGTSTP_action.sa_handler = catchSIGTSTP;
	sigfillset(&SIGTSTP_action.sa_mask);
	SIGTSTP_action.sa_flags = 0;
	sigaction(SIGTSTP, &SIGTSTP_action, NULL);

	/*Set up SIGCHLD handling */
	SIGCHLD_action.sa_handler = catchSIGCHLD;
	sigfillset(&SIGCHLD_action.sa_mask);
	SIGCHLD_action.sa_flags = 0;
	sigaction(SIGCHLD, &SIGCHLD_action, NULL);

	/*Set up ignore handling for SIGINT and SIGTERM*/
	IGNORE_action.sa_handler = SIG_IGN;
	sigaction(SIGINT, &IGNORE_action, NULL);
	sigaction(SIGTERM, &IGNORE_action, NULL);
}

/* Description: sets up signal handling for foreground child processes
 * args: none
 * pre: CALL THIS FUNCTION WITHIN THE CHILD PROCESS
 * post: foreground child process will handle SIGINT using the default handler
 * 	SIGTERM, SIGQUIT, SIGTSTP, and SIGCHLD will be ignored
 * ret: none
 */
void foregroundSignalSetup() {
	struct sigaction SIGINT_action = {{ 0 }};
	struct sigaction IGNORE_action = {{ 0 }};


	/*Set up SIGINT handling to do default action of termination*/
	SIGINT_action.sa_handler = SIG_DFL;
	sigfillset(&SIGINT_action.sa_mask);
	SIGINT_action.sa_flags = 0;
	sigaction(SIGINT, &SIGINT_action, NULL);



	/*Ignore all remaining user signals */
	/*Ignore other signals used by the parent process */
	IGNORE_action.sa_handler = SIG_IGN;
	sigaction(SIGTERM, &IGNORE_action, NULL);
	sigaction(SIGQUIT, &IGNORE_action, NULL);
	sigaction(SIGTSTP, &IGNORE_action, NULL);
	sigaction(SIGCHLD, &IGNORE_action, NULL);
}

/* Description: sets up signal handling for background child processes
 * args: none
 * pre: CALL THIS FUNCTION WITHIN THE CHILD PROCESS
 * post: background child process will handle SIGTERM using the default handler
 * 	SIGCHLD, SIGTSTP, SIGINT, and SIGQUIT will be ignored
 * ret: none
 *
 */
void backgroundSignalSetup() {
	struct sigaction SIGTERM_action = {{0}};
	struct sigaction IGNORE_action = {{0}};

	/*Set up background process to respond to SIGTERM */
	SIGTERM_action.sa_handler = SIG_DFL;
	sigfillset(&SIGTERM_action.sa_mask);
	SIGTERM_action.sa_flags = 0;
	sigaction(SIGTERM, &SIGTERM_action, NULL);

	/*Ignore all remaining signals */
	IGNORE_action.sa_handler = SIG_IGN;
	sigaction(SIGCHLD, &IGNORE_action, NULL);
	sigaction(SIGTSTP, &IGNORE_action, NULL);
	sigaction(SIGINT, &IGNORE_action, NULL);
	sigaction(SIGQUIT, &IGNORE_action, NULL);
}


/* Description: gets user input from stdin
 * args: [1] command: a char array
 * pre: command should have at most MAX_CHAR - 3 elements available
 * post: command will be filled with user input
 * ret: none
 */
void getInput(char command[]) {
	int numChars = -5;
	size_t bufferSize = 0;
	char* lineEntered = NULL;


	int charsRead = 0;	
	int current = 0;
	int dollars = 0;
	char c;

	/*As described in the lecture notes, this method uses getline() to get user
 * 		input, but also recovers from any signals that interfere with
 * 		getline */
	while(1) {
		printf(": "); fflush(stdout);
		numChars = getline(&lineEntered, &bufferSize, stdin);
		if(numChars == -1) {
			clearerr(stdin);
		}
		else {
			break; /*Exit the loop - we have input */
		}
	}

	/*remove trailing newline */
	lineEntered[strcspn(lineEntered, "\n")] = '\0';

	memset(command, '\0', sizeof(command));
	

	/*For every character in the user input, copy it into the command array.
 * 	Anytime two dollar signs are encountered, expand the dollar signs into
 * 	the process ID */
	charsRead = 0;
	current = 0;
		/*continue while there are still characters to read, and while
 * 			the buffer has not overflowed */
	while(current < MAX_CHAR - 2 && charsRead < strlen(lineEntered) ) {
		c = lineEntered[charsRead];
		charsRead++;

		/*If two dollar signs in a row, do the replacement*/
		if( c == '$' && dollars == 1) {
			strcpy(command + current - 1, pid);
			dollars = 0;
			current = strlen(command); 
		} else {
		/*If first dollar sign, increment the dollar count*/
		/*If not, set dollar count to 0 */
		/*Either way, store the char*/
			if(c == '$') {
				dollars++;
			} else {
				dollars = 0;
			}
			command[current] = c;
			current++;
		}
	}

	/*Free the heap memory containing user input */
	free(lineEntered);
	
	return;
}


	
/*Simply calls getInput(command). This is an artifact of earlier development where I thought
 * getCommand would have more to do. I don't have time to refactor to remove this,
 * so I've left it in */
void getCommand(char command[]) {
	getInput(command);

	return;
}

/*Examines the first parameter and checks to see if it is in the list
 * of builtin commands. Returns 1 if it is, 0 otherwise */
int is_builtin(char* params[]) {
	char* builtin = " cd status exit ";

	/*if the token is not in the list of commands, return 0*/
	if( strstr(builtin, params[0]) == NULL) {
		return 0;
	} 

	/*else return 1*/
	return 1;
	
}

/* Changes the directory
 * args: params, an array of char* that are parameters
 * pre: params[0] is "cd", params[1] is eitehr NULL or a filepath
 * post: if a filepath is given in params[1], an attempt to change to that filepath will be made.
 * 	otherwise, if params[1] is NULL, change to the filepath specified by the HOME
 * 	environmental variable
 * ret: status, which is the result of calling chdir
 *
 */
int cd(char* params[]) {
	/*First, determine whether or not there is an additional argument*/
	char* filepath = params[1];

	int status = 0;
	
	/*If no filepath, use HOME */
	if(filepath == NULL) {
		/*execute cd by checking the home directory*/
		filepath = getenv("HOME");
		status = chdir(filepath);
	} else {
		/*Otherwise, change directory to the specified filepath */
		status = chdir(filepath);
	}
	return status;
}

/*Simply prints out the value of the foreground_status global variable, based on whether
 * or not the is_exit flag is set. The return value is meaningless */
int status() {
	if(is_exit == 1) {
		fprintf(stdout, "exit value %i\n", foreground_status); fflush(stdout);
	}
	else {
		fprintf(stdout, "terminated by signal %i\n", foreground_status); fflush(stdout);
	}
	return 0;
}


/* Description: Executes the specified builtin command
 * args: [1] params: array of char* parameters
 * 	[2] argc: number of parameters
 * pre: params[0] must be a builtin command: either "cd", "status", or "exit"
 * post: the specified builtin command is executed
 * ret: the integer EXIT if the command was "exit"
 *	otherwies returns 0
 */
int exec_builtin(char* params[], int argc) {
	int s = 0;
	char* name = params[0];

	int foreground = 1; /*Always execute builtin in foreground */

	/*Do redirection and clean the arguments */
	redirect_in_out(params, argc, foreground);
	clean(params, argc);
	if(strcmp(name, "cd") == 0) {
		s = cd(params);

	} else if (strcmp(name, "status") == 0) {
		s = status();	

	} else {
		/*otherwise exit*/
		return EXIT;
	}
		
	return 0;
}

/* Description: redirects input and output based on the parameters
 * args: [1] params: array of char*
 * 	[2] argc: number of params
 * 	[3] foreground: either 1, or 0 -- whether or not process is a foreground
 * 			or background process
 * pre: at most one instance of ">" and "<" in the params array
 * post: redirection specified by ">" and "<" will be done. If it fails,
 * 	an error message will be printed to stderr
 * ret: none
 *
 *
 */
void redirect_in_out(char* params[], int argc, int foreground) {
	int current;
	int outputFD = -1;
	int inputFD = -1;
	int inResult;
	int outResult;

	/*Start by redirecting to /dev/null if command is to run in background */
	if(foreground == 0) {
		inputFD = open("/dev/null", O_RDONLY);
		dup2(inputFD, 0);

		outputFD = open("/dev/null", O_WRONLY); 
		dup2(outputFD, 1);
	}

	/*Regardless of foreground or background, do any specified redirection
 * 		by examining nearly every parameter in the array*/
	for(current = argc - 1; current > 0; current--) {
		/*Check if current param is a redirection operator. */
		/*If so, open the file and do a redirection */

			/*Error message if any files fail to open */
		if(strcmp(params[current], "<") == 0) {
			close(inputFD);
			inputFD = open(params[current + 1], O_RDONLY);
			if(inputFD < 0) {
				fprintf(stderr, "cannot open %s for input\n", params[current + 1]); 
				fflush(stderr);
				exit(1);
			}		
			/*If successful, redirect stdin*/
			inResult = dup2(inputFD, 0);

		} else if (strcmp(params[current], ">") == 0) {
			close(outputFD);
			outputFD = open(params[current + 1], O_WRONLY | O_CREAT | O_TRUNC, 0600);
			if(outputFD < 1) {
				fprintf(stderr, "cannot open %s for output\n", params[current + 1]);
				fflush(stderr);
				exit(1);
			}

			/*If successful, redirect stdout */
			outResult = dup2(outputFD, 1);
		}
	}
}

/*Removes any instances of ">", "<" from the entire parameter array, and replaces it with NULL
 * Removes "&" if it is the last parameter in the array, and replaces it with NULL */
void clean(char* params[], int argc) {
	int current;

	/*Remove "&" if it is there */
	if(strcmp(params[argc - 1], "&") == 0) {
		params[argc - 1] = NULL;
	} 

	/*Remove all ">" and "<" */
	for(current = argc - 2; current > 0; current--) {
		if(strcmp(params[current], "<") == 0 || strcmp(params[current], ">") == 0) {
			params[current] = NULL;
			params[current + 1] = NULL;
		}
	}
}

/*Checks whether or not the parameter list specifies a foreground or background process 
 * Return 1 if foreground, 0 otherwise*/
int is_foreground(char* params[], int argc) {
	assert(argc > 0); /* At least one argument */
	assert(params[argc - 1] != NULL);

	/*If the last param is "&", it is a background process */
	if(strcmp(params[argc - 1], "&") == 0 ) {
		return 0;
	}
	return 1;
}

/* Description: Executes non-builtin functions as child processes
 * args: [1] params: array of char* parameters
 * 	[2] argc: number of parameters
 * pre: argc > 1
 * post: appropriate signal handling will be set up for the child process,
 * 	whether foreground or background. If foreground, shell will wait
 * 	for child process and then update foreground_status and is_exit
 * 	global variables based on how the process termined 
 * 	print messages based on termination values
 *
 * 	if background, do nothing in this function.
 * ret: 0
 *
 *
 *
 */
int exec_non_builtin(char* params[], int argc) {
	/* This code models the code given in the processes lecture */
	pid_t spawnpid = -5;
	int state = -5;
	int childExitMethod = -5;
	int exitStatus = 0;
	int signal = 0;

	int foreground;

	/*Foreground? Or background? */
	foreground = is_foreground(params, argc);

	/*If the special state is set, run in foreground always, regardless */
	if (special == 1) {
		foreground = 1;
	}
	
	spawnpid = fork();
	switch (spawnpid) {

		case -1:
			perror("Failure to spawn a process!\n"); fflush(stderr);
			exit(1);
			break;
		case 0:
			/*In child process: */
			/*Set up signals based on foreground or background */
			if(foreground == 1) {
				foregroundSignalSetup();
			}
			else {
				backgroundSignalSetup();
			}

			/*Handle redirection and clean the arguments */
			redirect_in_out(params, argc, foreground);
			clean(params, argc);
			
			state = execvp(params[0], params);
			printf("%s: no such file or directory\n", params[0]); fflush(stdout);	
			/*If it returned, an error occurred, so terminate process */
			exit(1);
			
			break;
		default:
			/*Keep processing as the parent. Wait if it's a foreground */
			/*Don't wait if it's a background */	

			if(foreground == 1) {
				while( waitpid(spawnpid, &childExitMethod, 0) != spawnpid) {
					/*Keep waiting until it's over */
				}
				if(WIFEXITED(childExitMethod) != 0) {
					/*Child did not exit by signal */
					exitStatus = WEXITSTATUS(childExitMethod);
					/*Update global status */
					foreground_status = exitStatus;	
					is_exit = 1;
				} else if (WIFSIGNALED(childExitMethod) != 0) {
					signal = WTERMSIG(childExitMethod);
					printf("terminated by signal %i\n", signal); fflush(stdout);
					/*Update global status */
					foreground_status = signal;
					is_exit = 0;
				} else {
					perror("Failure to find child exit! \n"); fflush(stderr);
					exit(1);
				}

			} else {
				/*If it's a background process, do nothing. */
				/*Let signal handling and the cleanup function fix this */
				printf("background pid is %i\n", spawnpid); fflush(stdout);

			}
			break;
	}
	

	return 0;
}

/*Check if params specifies a builtin command or not. If so, execute it.
 * Otherwise execute a non-builtin command.
 *
 * Return the result of executing the builtin command, but just return 0 if
 * the non-builtin command is executed */
int execute(char* params[], int argc) {
	if ( is_builtin(params) == 1) {
		return exec_builtin(params, argc);
	} else {
		exec_non_builtin(params, argc);
	}
	return 0;
}

/* Description: checks for background processes that have terminated, and cleans up any
 * 		that are zombies that are discovred
 * args: none
 * pre: none
 * post: any zombie child processes will be cleaned up, and their pid and method of
 * 	termination will be printed to stdout
 * ret: none
 */
void cleanup() {
	int childPID = 5;
	int childExitMethod = 5;
	int exitStatus;
	int signal;

	/*Clean up every terminated background process that is currently available*/
	childPID = waitpid(-1, &childExitMethod, WNOHANG);
	while(childPID != 0 && childPID != -1) {
		/*Get exit code or signal code, and print to console */	
		if(WIFEXITED(childExitMethod) != 0) {
			exitStatus = WEXITSTATUS(childExitMethod);
			printf("background pid %i is done: exit value %i\n", childPID, exitStatus);
			fflush(stdout);
		} else if (WIFSIGNALED(childExitMethod) != 0) {
			signal = WTERMSIG(childExitMethod);
			printf("background pid %i is done: terminated by signal %i\n", childPID, signal);
			fflush(stdout);
		} else {
			perror("Failure to find child exit! \n"); fflush(stderr);
			exit(1);
		}
		
		/*Check if any more background processes can be cleaned up */
		childPID = waitpid(-1, &childExitMethod, WNOHANG);
	}
	fflush(stdout);
	return;
}

/* Description: parses a command string into space-delimited parameters
 * args: [1] params: array of char* that will point to space-delimited parameters
 * 	[2] max: maximum number of arguments that can be parsed without overflow
 * 	[3] command: string representing the command
 * pre: command must be a space-delimited string of parameters
 * 	the params array should be initialized to all NULL pointers
 * 	max > 0 and max < sizeof(params) 
 * post: the command string's whitespaces are all now '\0' characters
 * 	param contains pointers to each parameter in the command string
 * 	max is unchanged
 * ret: count: the number of arguments successfully parsed from the command string
 */
int parse( char* params[], int max, char* command) {
	int count = 0;	
	char* word = NULL;
	char* rest = command;

	/*While there are more space-delimited parameters and the params array
 * 		has not overflowed, keep getting and storing parameters */
	word = strtok_r(rest, " \t", &rest);
	while(word != NULL && count < max) {
		params[count] = word;
		count++;
		
		/*get next parameter */
		word = strtok_r(rest, " \t", &rest);
	}

	/*Place a null pointer to terminate the parameter list
 * 		if params is full, just put the null pointer at 
 * 		the last element of the params array */
	if(count == max) {
		params[count - 1] = NULL;	
		count--; /*Since we replaced the last argument */
	}
	else {
		params[count] = NULL;
	}

	/*Return the number of arguments */
	return count;
}

