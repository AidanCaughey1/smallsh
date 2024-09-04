//Project 3
//Author: Aidan Caughey
//Date: May 2024

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <signal.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/wait.h>

//Global Constants
#define MAXLENGTH 2048
#define MAXARG 512

//Action Handlers
struct sigaction sig_int;
struct sigaction sig_tstp;

//foreground variable
bool foreground = true;

//Prompts user for input and tokenizes an array of words
void getInput(char* argc[], int* background, int pid, char inputFile[], char outputFile[]) {
    char input[MAXLENGTH];
    bool found = false;
    //Space delimiter
    const char blank[2] = " ";

    //Prompt for each command line
    printf(": ");
    //Clear output buffer and read input from the user
    fflush(stdout);
    fgets(input, MAXLENGTH, stdin);

    //Iterates through input to check for newline character
    for (int i = 0; i < MAXLENGTH && !found; i++) {
        if (input[i] == '\n') {
            //Replaces newline with null terminator
            input[i] = '\0';
            found = true;
        }
    }

    //If blank input
    if (!strcmp(input, "")) {
        argc[0] = strdup("");
        return;
    }

    char *token = strtok(input, blank);
    //Iterate through all tokens
    for (int i = 0; token; i++) {
        //Background process
        if (!strcmp(token, "&")) {
            *background = 1;
        }
        //Input file
        else if (!strcmp(token, "<")) {
            token = strtok(NULL, blank);
            //Store token as input filename
            strcpy(inputFile, token);
        }
        //Output file
        else if (!strcmp(token, ">")) {
            token = strtok(NULL, blank);
            //Store token as output filename
            strcpy(outputFile, token);
        }
        //Command
        else {
            argc[i] = strdup(token);
            //Loop through characters in the token
            for (int j = 0; argc[i][j]; j++) {
                //Check for $$ and replace with pid
                if (argc[i][j] == '$' && argc[i][j + 1] == '$') {
                    argc[i][j] = '\0';
                    snprintf(argc[i], 256, "%s%d", argc[i], pid);
                }
            }
        }
        //Get next token
        token = strtok(NULL, blank);
    }
}

//Catches SIGTSTP and toggles foreground-only mode
void handleSIGTSTP(int signo) {
    char* statusMessage;
    if (foreground) {
        //Prints message to screen and switches to foreground mode
        statusMessage = "Entering foreground-only mode (& is now ignored)\n";
        write(STDOUT_FILENO, statusMessage, 49);
        foreground = false;
    } else {
        statusMessage = "Exiting foreground-only mode\n";
        write(STDOUT_FILENO, statusMessage, 29);
        foreground = true;
    }
    fflush(stdout);
}

void cd(char* input[]) {
    //Go to the specified directory
    if (input[1]) {
        if (chdir(input[1]) == -1) {
            //ERROR
            printf("Directory not found.\n");
            fflush(stdout);
        }
    } else {
        //home
        chdir(getenv("HOME"));
    }
}

//Prints out the exit status or the terminating signal
void status(int processStatus) {
    //Checks the status of the last process
    if (WIFEXITED(processStatus)) {
        //Normally Terminated
        printf("exit value %d\n", WEXITSTATUS(processStatus));
    } else {
        //Abnormally Terminated
        printf("terminated by signal %d\n", WTERMSIG(processStatus));
    }
    fflush(stdout);
}

void childFork(char* argc[], int input, int output, int goal, char inputFile[], char outputFile[], struct sigaction sig_int) {
    //Enables CTRL C handler
    sig_int.sa_handler = SIG_DFL;
    sigaction(SIGINT, &sig_int, NULL);

    //Input
    if (strcmp(inputFile, "")) {
        //Open file
        input = open(inputFile, O_RDONLY);
        //If no file
        if (input == -1) {
            perror("No input file detected\n");
            exit(1);
        }
        goal = dup2(input, 0);
        //Checks for error again
        if (goal == -1) {
            perror("Unable to assign input file\n");
            exit(1);
        }
        fcntl(input, F_SETFD, FD_CLOEXEC);
    }

    //Output
    if (strcmp(outputFile, "")) {
        //Opens the file for writing
        output = open(outputFile, O_WRONLY | O_CREAT | O_TRUNC, 0666);
        //Checks for error
        if (output == -1) {
            perror("No output file detected\n");
            exit(1);
        }
        goal = dup2(output, 1);
        if (goal == -1) {
            perror("Unable to assign output file\n");
            exit(1);
        }
        fcntl(output, F_SETFD, FD_CLOEXEC);
    }

    //Execute
    if (execvp(argc[0], (char* const*)argc)) {
        printf("%s: no such file or directory\n", argc[0]);
        fflush(stdout);
        exit(1);
    }
}

//Executes the command from argc[]
void execute(char* argc[], int* childProcessStatus, int* background, char inputFile[], char outputFile[], struct sigaction sig_int) {
    //Variables
    int input = 0;
    int output = 0;
    int goal = 0;
    pid_t pid;

    pid = fork();
    switch (pid) {
        case -1:
            //Error
            perror("fork() failed\n");
            exit(1);
            break;

        case 0:
            childFork(argc, input, output, goal, inputFile, outputFile, sig_int);
            break;

        default:
            //Parent Fork
            if (*background && foreground) {
                //Execute in the background
                pid_t newPid = waitpid(pid, childProcessStatus, WNOHANG);
                printf("background pid is %d\n", pid);
                fflush(stdout);
            } else {
                //Execute normally
                pid_t newPid = waitpid(pid, childProcessStatus, 0);
            }
            break;
    }
}

//Shell
int main() {
    //Initialize Variables
    int pid = getpid();
    int exitStatus = 0;
    int background = 0;
    bool cont = true;
    char inputFile[256] = "";
    char outputFile[256] = "";
    char* input[512];
    //Enter NULL for all indexes
    for (int i = 0; i < 512; i++) {
        input[i] = NULL;
    }

    //CTRL Z
    sig_tstp.sa_handler = handleSIGTSTP;
    sigfillset(&sig_tstp.sa_mask);
    //Signal Handler
    sigaction(SIGTSTP, &sig_tstp, NULL);

    //CTRL C
    sig_int.sa_handler = SIG_IGN;
    sigfillset(&sig_int.sa_mask);
    //Signal Handler
    sigaction(SIGINT, &sig_int, NULL);

    //Shell until exit called
    while (cont) {
        //Obtain input
        getInput(input, &background, pid, inputFile, outputFile);

        //Commands
        //Comment or empty message
        if (input[0][0] == '\0' || input[0][0] == '#') {
            // Do nothing
        }
        //Status
        else if (strcmp(input[0], "status") == 0) {
            status(exitStatus);
        }
        //cd
        else if (strcmp(input[0], "cd") == 0) {
            cd(input);
        }
        //Exit
        else if (strcmp(input[0], "exit") == 0) {
            cont = false;
        }
        //Other
        else {
            execute(input, &exitStatus, &background, inputFile, outputFile, sig_int);
        }

        //Check for background processes
        pid_t pid;
        while ((pid = waitpid(-1, &exitStatus, WNOHANG)) > 0) {
            printf("Background process %d terminated\n", pid);
            status(exitStatus);
        }

        //Reset the variables
        for (int i = 0; input[i]; i++) {
            free(input[i]);
            input[i] = NULL;
        }
        inputFile[0] = '\0';
        outputFile[0] = '\0';
        background = 0;
    }

    exit(0);
}
