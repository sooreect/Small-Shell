/**************************************************************************************************
 * Author: Tida Sooreechine
 * Date: 2/25/2017
 * Program: CS344 Program 3 - Small Shell
 * Description: A mini shell with three built-in commands.
**************************************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>


//source: https://brennan.io/2015/01/16/write-a-shell-in-c/	
//source: CS344 Block 3 Lectures

//global variables
int fgOnly = 0;
int childExitMethod = -9;
int bgProcessCount = 0;
int bgDoneCount = 0;
int bgProcessList[999];	
int bgDoneList[999];


//FUNCTION PROTOTYPES
char* getUserLine(void);
char** extractArguments(char* line);
void changeDirectory(char** arguments);
void checkBgProcesses();
void catchSIGTSTP(int signo);


//MAIN
int main()
{
	char *line;
	char* chptr;
	char **args;
	char smallshPidStr[10];
	char argString[100];
	int i, background, inFileIndex, outFileIndex, infd, outfd;
	int termination = 0;
	int fgExitStatus = 0;
	int fgTermSignal = -9;
	int running = 1;
	pid_t smallshPid, spawnPid, childPid;

	//set up for SIGINT to be ignored while running the smallsh
	//ignore action will be lifted after forking into child foreground process
	struct sigaction ignore_action = {0};
	ignore_action.sa_handler = SIG_IGN;	

	//set up SIGINT to do its default action instead of being ignored 
	//this will be in effect in child foreground process
	struct sigaction SIGINT_action = {0};
	SIGINT_action.sa_handler = SIG_DFL; 
	sigfillset(&SIGINT_action.sa_mask);
	SIGINT_action.sa_flags = 0;

	//set up SIGTSTP to switch between foreground-only and background-allowed modes
	//allow the interrupted call to restart after signal handler returns
	//source: https://piazza.com/class/ixhzh3rn2la6vk?cid=391
	struct sigaction SIGTSTP_action = {0};
	SIGTSTP_action.sa_handler = catchSIGTSTP;
	sigfillset(&SIGTSTP_action.sa_mask);
	SIGTSTP_action.sa_flags = SA_RESTART;
	sigaction(SIGTSTP, &SIGTSTP_action, NULL);

	do {
		//SIGINT ignore handler is in action in shell
		sigaction(SIGINT, &ignore_action, NULL);	

		//get smallsh shell PID
		smallshPid = getpid();

		//print PIDs of completed bg processes, if there are any
		checkBgProcesses();	

		//print prompt
		printf(": ");
		fflush(stdout);
	
		//get user's line of input and break it up into an array of arguments
		line = getUserLine();
		args = extractArguments(line);

			//$$ variable expansion
			//first convert smallsh shell pid from pid_t into string format
			//for each argument in the array, search for an instance of $$
			//if $$ exists, find the location of substring and substitute it with string pid 
			memset(smallshPidStr, '\0', 10);
			snprintf(smallshPidStr, 10, "%d", smallshPid);		//convert pid from pid_t into string
			i = 0;
			while (args[i] != NULL) {
				memset(argString, '\0', 100);					//clear argString array of chars
				strncpy(argString, args[i], strlen(args[i]));	//copy argument from array to argString
				chptr = strstr(argString, "$$");				//find '$$' substring in argString
				if (chptr != NULL) {							//if '$$' exists, paste over it with pid string
					strncpy(chptr, smallshPidStr, strlen(smallshPidStr));
					args[i] = argString;
				}
				i++;		
			}

		//execute commands from array of arguments
		if (args[0] == NULL)						//restart if user input is blank
			continue;								
		else if (strncmp(args[0], "#", 1) == 0) 	//restart if user input starts with # sign
			continue;			
		else if (strcmp(args[0], "exit") == 0) {	//built-in command 1: exit
			running = 0;
			exit(0);
		} 
		else if (strcmp(args[0], "cd") == 0) {		//built-in command 2: cd
			changeDirectory(args);							
		}
		else if (strcmp(args[0], "status") == 0) {	//built-in command 3: status
			if (termination == 1) {
				fgExitStatus = WEXITSTATUS(childExitMethod);
				printf("Exit value %d\n", fgExitStatus);
				fflush(stdout);
			}
			else if (termination == -1) {
				fgTermSignal = WTERMSIG(childExitMethod);
				printf("Terminated by signal %d\n", fgTermSignal);
				fflush(stdout);
			}
		}
		else {	//non-built-in commands
			//determine if command is a background process and if it needs input/output redirection
			//search arguments array for &, <, and > characters
			i = 0;
			background = 0;
			inFileIndex = -9;
			outFileIndex = -9;
			while (args[i] != NULL) {
				if (strcmp(args[i], "<") == 0) 
					inFileIndex = i+1; 
				else if (strcmp(args[i], ">") == 0) 
					outFileIndex = i+1;
				else if (strcmp(args[i], "&") == 0) { 
					background = 1;
					args[i] = NULL;
				}
				i++;
			}
			

			spawnPid = fork();
			if (spawnPid < 0) {
				//unsuccessful forking
				printf("Hull Breach!\n"); 
				fflush(stdout);
				exit(1);
			}
			else if (spawnPid == 0) {
				//successful forking - this is a child process

				//allow SIGINT signal to interrupt child foreground process				
				if (!background)
					sigaction(SIGINT, &SIGINT_action, NULL);
				
				//input redirection
				if (inFileIndex > 0) {
					//create file descriptor and exit if an error occurs
					//if this is a background process, redirect stdin from /dev/null 
					//else redirect stdin from file specified by user	
					if (background) {
						infd = open("/dev/null", O_RDONLY);
						if (infd == -1) {
							printf("Input redirection error\n");
							fflush(stdout);
							exit(1);
						}
					}
					else { 
						infd = open(args[inFileIndex], O_RDONLY);
						if (infd == -1) { 
							printf("Cannot open %s for input\n", args[inFileIndex]);
							fflush(stdout);
							exit(1);
						}
					}

					//redirect input so that stdin now points to the file instead of terminal
					if(dup2(infd, 0) == -1) {
						printf("Input redirection error\n");
						fflush(stdout);
						exit(1);
					}
					close(infd);
				}
				
				//output redirection
				if (outFileIndex > 0) {
					//create file descriptor and exit if an error occurs
					//if this is a background process, redirect stdout to /dev/null (do not print to screen)
					//else redirect stdout to file specified by user
					if (background) {
						outfd = open("/dev/null", O_WRONLY);
						if (outfd == -1) {
							printf("Output redirection error\n");
							fflush(stdout);
							exit(1);
						}
					}
					else {
						outfd = open(args[outFileIndex], O_WRONLY | O_CREAT | O_TRUNC, 0644);
						if (outfd == -1) {
							//if runs properly, this should never be printed
							printf("Cannot open %s for output\n", args[outFileIndex]);
							fflush(stdout);
							exit(1); 
						}
					}

					//redirect output so that stdout now points to the file instead of terminal
					if(dup2(outfd, 1) == -1) {
						printf("Output redirection error\n");
						fflush(stdout);
						exit(1);
					}
					close(outfd);
				}

				//remove extra arguments after input and output redirections
				//>, <, and & operators are not features of the kernel but of the shell
				//source: http://stackoverflow.com/questions/19548652/i-o-redirection-on-linux-shell
				//source: http://stackoverflow.com/questions/13784269/redirection-inside-call-to-execvp-not-working
				i = 0;
				while(args[i] != NULL) {
					if (strcmp(args[i], "<") == 0) 
						args[i] = NULL;
					else if (strcmp(args[i], ">") == 0) 
						args[i] = NULL;
					else if (strcmp(args[i], "&") == 0)
						args[i] == NULL;
					i++;	
				}
		
				if (execvp(args[0], args) < 0) { 
					printf("%s: no such file or directory\n", args[0]);
					fflush(stdout);
					exit(1);
				}

				exit(1);
			}
			else {
				//successful forking - this is the parent process
				if ((!background) || (fgOnly)) {	//foreground process 
					childPid = waitpid(spawnPid, &childExitMethod, 0);
					if (WIFEXITED(childExitMethod) != 0) {				//if normal termination
						termination = 1; 
					}
					if (WIFSIGNALED(childExitMethod) != 0) {			//if terminated with signal
						termination = -1;
						fgTermSignal = WTERMSIG(childExitMethod);
						printf("Terminate by signal %d\n", fgTermSignal);
					}
					//printf("Foreground process completed.\n");
				}
				else {	//background process
					//childPid = waitpid(-1, &childExitMethod, WNOHANG);
					printf("Background PID is %d\n", spawnPid);
					bgProcessList[bgProcessCount] = spawnPid;
					bgProcessCount++;					
				}
			}
		}						

		free(line);
		free(args);

	} while (running);
	printf("\n");
	return 0; 
}


//FUNCTION DEFINITIONS

//read a line containing command line arguments from stdin (user)
//return the entire line, null-terminated, in a variable
char* getUserLine(void) {
	int maxChars = 2048;	//max number of characters for command line allowed
	char* input = NULL;
	ssize_t bufferSize = 0;

	getline(&input, &bufferSize, stdin);	//buffer is automatically null-terminated
	if ((strlen(input) - 1) > maxChars)		//if input has more than 2048 characters (not including null)
		input[maxChars] = '\0';				//null terminate it at 2049th character

	return input;
}

//break up the user input line into a list of arguments
//return arguments as a null-terminated array of pointers
char** extractArguments(char* line) {
	int maxArgs = 512;		//max number of arguments for command line allowed
	int index = 0;
	char* token;			//pointer to char
	char** tokens;			//array of pointers to char

	tokens = malloc(sizeof(char*) * maxArgs);
	
	//get the first token, using space and new-line characters as delimiters	
	token = strtok(line, " \n");		

	//fill in the array with other pointers to char tokens
	while ((token != NULL) && (index < maxArgs)) {
		tokens[index] = token; 
		index++;

		token = strtok(NULL, " \n");
	}
	
	//close off the array list with NULL
	tokens[index] = NULL;		

	return tokens;	
}

//change working directory
//if no arguments were specified for cd, then redirect user to home
//else redirect user to specified path, relative or absolute
void changeDirectory(char** arguments) {
	if (arguments[1] == NULL) {			//no second argument was given by user, just cd
		chdir(getenv("HOME"));			//change directory to HOME
	}
	else {								//if there is a path after cd
		if (chdir(arguments[1]) != 0) {
			printf("%s: No such file or directory\n", arguments[1]);
			fflush(stdout);
		}	
	}
}

//check background processes and report to shell which processes have recently been completed
void checkBgProcesses() {
	int i, j;
	int printed;
	
	//check bg process array and flag it if it has already been reaped and reported 
	//the reaped/reported bg processes are store in the bgDoneList array
	for (i = 0; i < bgProcessCount; i++) {
		printed = 0;
		if (waitpid(bgProcessList[i], &childExitMethod, WNOHANG)) {
			for (j = 0; j < bgDoneCount; j++) {
				if (bgProcessList[i] == bgDoneList[j]) 
					printed = 1; 
			}			

			//if the bg process has not been reported, print details and clean up resources
			//add the process to the list of reaped/reported
			if (!printed) {
				if (WIFEXITED(childExitMethod)) {
					printf("Background PID %d is done: exited value %d\n", bgProcessList[i], WEXITSTATUS(childExitMethod));
					fflush(stdout);	
				}	
				if (WIFSIGNALED(childExitMethod)) {
					printf("Background PID %d is done: terminated by signal %d\n", bgProcessList[i], WTERMSIG(childExitMethod));
					fflush(stdout);
				}
				bgDoneList[bgDoneCount] = bgProcessList[i];
				bgDoneCount++;		
			}			
		}				
	}
}

//SIGTSTP signal handler function
void catchSIGTSTP(int signo) {
	char* message1 = "\nEntering foreground-only mode (& is now ignored)\n: ";
	char* message2 = "\nExiting foreground-only mode\n: ";

	if (!fgOnly) { 
		write(STDOUT_FILENO, message1, 52);
		fgOnly = 1;
	}
	else {
		write(STDOUT_FILENO, message2, 32); 
		fgOnly = 0;
	}
}

