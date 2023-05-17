#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <assert.h>
#include <dirent.h>
#include <stdbool.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <errno.h>

#ifndef BUFSIZE
#define BUFSIZE 1024
#endif

char *lineBuffer;
int linePos, lineSize;

typedef enum token_types token_type;

enum token_types{cd, pwd, in, out, comb, path, bare, term};

typedef struct token_info token;
typedef struct process_info process;

struct token_info{
    token_type type;
    char* chrPtr;
    int len;
    int wildcard;
    token* prev;
    token* next;
};

struct process_info{
    token_type type;
    char* path_name;
    char** arguments;
    int argCount;
    char* input;
    char* output;
    process* next;
    process* prev;
};

void free_tokens(token*);
void free_command(process*);
void free_commands(process*);
process* process_tokens(token*);
void append(char *, int);
token* make_tokens(void);
int execute_processes(process*);
char* find_executable(char*);
int check_executables(process*);
void find_wildcards(process*, char *, int);
int check_wildcard(char*, char*);

int main(int argc, char **argv){
    int fin, bytes, pos, lstart, status;
    char buffer[BUFSIZE];

    // open specified file or read from stdin
    if (argc > 1) {
	    fin = open(argv[1], O_RDONLY);
        if (fin == -1) {
            perror(argv[1]);
            exit(EXIT_FAILURE);
        }
    } else {
	    fin = 0;
    }
    
    // remind user if they are running in interactive mode
    if (isatty(fin)) {
        fputs("Welcome to my shell!\n", stderr);
        fputs("mysh> ", stderr);
    }

    // set up storage for the current line
    lineBuffer = (char *) malloc(BUFSIZE);
    lineSize = BUFSIZE;
    linePos = 0;
    status = 0;
    int check = 0;
    while ((bytes = read(fin, buffer, BUFSIZE)) > 0) {
        check = 0;
	    lstart = 0;
        for (pos = 0; pos < bytes; ++pos) {
            if (buffer[pos] == '\n') {
                int thisLen = pos - lstart + 1;
                append(buffer + lstart, thisLen);
                token* head = make_tokens();
                if(head != NULL){
                    process* commands = process_tokens(head);
                    if(commands == NULL){
                        status = 1;
                    }else{
                        if((status = check_executables(commands)) == 0){
                            status = execute_processes(commands);
                        }
                        free_commands(commands);
                    }
                    free_tokens(head);
                }
                linePos = 0;
                lstart = pos + 1;
                check = 1;
            }
        }
        if (lstart < bytes) {
            // partial line at the end of the buffer
            int thisLen = pos - lstart;
            append(buffer + lstart, thisLen);
        }
        if (check != 0) {
            if(isatty(fin)){
                if(status == 0){
                    fputs("mysh> ", stderr);
                } else if(status == 1){
                    fputs("!mysh> ", stderr);
                } else if(status == 2){
                    fputs("mysh: exiting\n", stderr);
                    exit(EXIT_SUCCESS);
                }
            }
            else if (status == 2){
                exit(EXIT_SUCCESS);
            }
        }
    }
    if (linePos > 0) {
        // file ended with partial line
        append("\n", 1);
        token* head = make_tokens();
        if(head != NULL){
            process* commands = process_tokens(head);
            if(commands == NULL){
                status = 1;
            }
            else{
                if((status = check_executables(commands)) == 0){
                    status = execute_processes(commands);
                }
                free_commands(commands);
            }
            free_tokens(head);
        }
    }  
    free(lineBuffer);
    close(fin);
    return EXIT_SUCCESS;
}


// add specified text the line buffer, expanding as necessary
// assumes we are adding at least one byte
void append(char *buf, int len){
    int newPos = linePos + len;
    
    if (newPos > lineSize) {
        lineSize *= 2;
        assert(lineSize >= newPos);
        lineBuffer = realloc(lineBuffer, lineSize);
        if (lineBuffer == NULL) {
            perror("line buffer");
            exit(EXIT_FAILURE);
        }
    }
    memcpy(lineBuffer + linePos, buf, len);
    linePos = newPos;
}

void set_type(token* head){
    token* ptr = head;
    while(ptr != NULL){
        if(strcmp(ptr->chrPtr, "cd") == 0){
            ptr->type = cd;
            ptr->wildcard = 0;
        } else if(strcmp(ptr->chrPtr, "pwd") == 0){
            ptr->type = pwd;
            ptr->wildcard = 0;
        } else if(strcmp(ptr->chrPtr, "<") == 0){
            ptr->type = in;
            ptr->wildcard = 0;
        } else if(strcmp(ptr->chrPtr, ">") == 0){
            ptr->type = out;
            ptr->wildcard = 0;
        } else if(strcmp(ptr->chrPtr, "|") == 0){
            ptr->type = comb;
            ptr->wildcard = 0;
        } else if(strcmp(ptr->chrPtr, "exit") == 0){
            ptr->type = term;
            ptr->wildcard = 0;
        } else{
            int i = 0;
            ptr->type = bare;
            while(ptr->chrPtr[i] != '\0'){
                if(ptr->chrPtr[i] == '/'){
                    ptr->type = path;
                    break;
                }
                i++;
            }
            i = 0;
            ptr->wildcard = 0;
            while(ptr->chrPtr[i] != '\0'){
                if(ptr->chrPtr[i] == '*'){
                    ptr->wildcard = 1;
                    break;
                }
                i++;
            } if(ptr->chrPtr[0] == '~' && (ptr->chrPtr[1] == '/' || ptr->chrPtr[1] == '\0')){
                ptr->type = path;
                int home_len = strlen(getenv("HOME"));
                char* temp = malloc(sizeof(char) * (strlen(ptr->chrPtr) + 1));
                strcpy(temp, ptr->chrPtr);
                ptr->chrPtr = realloc(ptr->chrPtr, sizeof(char) * ((strlen(temp) + 1) + (home_len + 1)));
                memcpy(ptr->chrPtr, getenv("HOME"), home_len);
                memcpy((ptr->chrPtr) + home_len, (temp + 1), strlen(temp));
                ptr->chrPtr[home_len + strlen(temp) - 1] = '\0';
                free(temp);
            }
        }
        ptr = ptr->next;
    }
}


token* make_tokens(void){
    token* head = malloc(sizeof(token));
    token* ptr = head;
    ptr->chrPtr = (char *) malloc(sizeof(char));
    ptr->prev = NULL;
    int l = 0, r = linePos - 2;
    int length = 0;
    int ctr = 0;
    char c;
    assert(lineBuffer[linePos-1] == '\n');

    // make token array
    while (l <= r) {
        length++;
        c = lineBuffer[l];

        if (c == ' '){
            if(length == 1){
                length = 0;
            } else{
                ptr->chrPtr = (char *) realloc(ptr->chrPtr, (length + 1) * sizeof(char));
                ptr->chrPtr[length-1] = '\0';
                token* temp = malloc(sizeof(token));
                temp->prev = ptr;
                temp->next = NULL;
                temp->len = 0;
                temp->chrPtr = (char * ) malloc(sizeof(char));
                ptr->next = temp;
                ptr = temp;
                ctr++;
                length = 0;
            }
        } else if(c == '|' || c == '>' || c == '<'){
            if(length == 1){
                ptr->chrPtr = (char *) realloc(ptr->chrPtr, 2 * sizeof(char));
                ptr->chrPtr[length-1] = c;
                ptr->chrPtr[length] = '\0';
                ptr->len = length;
                ptr->next = malloc(sizeof(token));
                ptr->next->prev = ptr;
                ptr->next->next = NULL;
                ptr->next->len = 0;
                ptr->next->chrPtr = (char * ) malloc(sizeof(char));
                ptr = ptr->next;
                ctr++;
                length = 0;
            } else{
                ptr->chrPtr = (char *) realloc(ptr->chrPtr, length * sizeof(char));
                ptr->chrPtr[length-1] = '\0';
                ctr++;
                ptr->next = malloc(sizeof(token));
                ptr->next->prev = ptr;
                ptr->next->next = NULL;
                ptr->next->chrPtr = (char * ) malloc(2 * sizeof(char));
                ptr->next->chrPtr[0] = c;
                ptr->next->chrPtr[1] = '\0';
                ptr->next->len = 1;
                ptr = ptr->next;
                ctr++;
                ptr->next = malloc(sizeof(token));
                ptr->next->prev = ptr;
                ptr->next->next = NULL;
                ptr->next->len = 0;
                ptr->next->chrPtr = (char * ) malloc(sizeof(char));
                ptr = ptr->next;
                length = 0;
            }
        } else{
            ptr->len = length;
            ptr->chrPtr = (char *) realloc(ptr->chrPtr, length * sizeof(char));
            ptr->chrPtr[length-1] = c;
        }
        ++l;
    }
    if(ptr->len == 0){
        ptr->prev->next = NULL;
        free(ptr->chrPtr);
        free(ptr);
    }
    if(length > 0){
        ctr++;
        ptr->chrPtr = (char *) realloc(ptr->chrPtr, (length + 1) * sizeof(char));
        ptr->chrPtr[length] = '\0';
        ptr->next = NULL;
    }
    if(ctr == 0){
        free(head->chrPtr);
        free(head);
        head = NULL;
    }
    set_type(head);
    return head;
}

void free_tokens(token* head){
    token* ptr = head;
    while(ptr != NULL){
        token* temp = ptr;
        ptr = ptr->next;
        free(temp->chrPtr);
        free(temp);
    }
}

void free_commands(process* head){
    process* ptr = head;
    while(ptr != NULL){
        process* temp = ptr;
        ptr = ptr->next;
        free_command(temp);
    }
}

void free_command(process* command){
    if(command->path_name != NULL){
        free(command->path_name);
    }
    if(command->arguments != NULL){
        for(int i = 0; i < command->argCount; i++){
            free(command->arguments[i]);
        }
        free(command->arguments);
    }
    if(command->input != NULL){
        free(command->input);
    }
    if(command->output != NULL){
        free(command->output);
    }
    free(command);
}

process* process_tokens(token* head){
    process* command = (process *) malloc(sizeof(process));
    command->type = head->type;
    command->prev = NULL;
    command->next = NULL;
    command->input = NULL;
    command->output = NULL;
    command->arguments = NULL;
    command->path_name = NULL;
    token* ptr = head;
    switch(head->type){
        case cd:
        case pwd:
        case bare:
        case path:
        case term:
            command->argCount = 0;
            command->arguments = (char **) malloc(sizeof(char *));
            if(head->wildcard == 1){
                find_wildcards(command, ptr->chrPtr, command->type);
                if(command->argCount > 1){
                    errno = 1;
                    perror("Too many potential files");
                    free_command(command);
                    return NULL;
                } else{
                    command->path_name = malloc(sizeof(char) * (strlen(command->arguments[0]) + 1));
                    memcpy(command->path_name, command->arguments[0], strlen(command->arguments[0]));
                    command->path_name[strlen(command->arguments[0])] = '\0';
                }
            } else{
                command->arguments[0] = (char *) malloc(sizeof(char) * (strlen(ptr->chrPtr) + 1));
                strcpy(command->arguments[0], ptr->chrPtr);
                command->argCount++;
                if(command->type == bare){
                    command->path_name = find_executable(command->arguments[0]);
                    if(command->path_name == NULL){
                        free_command(command);
                        return NULL;
                    }
                } else{
                    command->path_name = malloc(sizeof(char) * (strlen(ptr->chrPtr) + 1));
                    strcpy(command->path_name, ptr->chrPtr);
                }
            }
            ptr = ptr->next;
            while(ptr != NULL){
                if(ptr->type == in){
                    ptr = ptr->next;
                    if(ptr != NULL){
                        if(ptr->type == in || ptr->type == out || ptr->type == comb){
                            free_command(command);
                            errno = 1;
                            perror("Double Special Character Error");
                            return NULL;
                        } else if(ptr->wildcard == 1){
                            free_command(command);
                            errno = 1;
                            perror("Input can't be redirected to a wildcard");
                            return NULL;
                        } else if(command->input == NULL){
                            command->input = malloc(sizeof(char) * (strlen(ptr->chrPtr) + 1));
                            strcpy(command->input, ptr->chrPtr);
                        } else{
                            errno = 1;
                            perror("Attempting Multiple Input Redirections");
                            free_command(command);
                            return NULL;
                        }
                    } else{
                        errno = 5;
                        perror("No Input Redirection Given");
                        free_command(command);
                        return NULL;
                    }
                } else if(ptr->type == out){
                    ptr = ptr->next;
                    if(ptr != NULL){
                        if(ptr->type == in || ptr->type == out || ptr->type == comb){
                            free_command(command);
                            errno = 1;
                            perror("Double Special Character Error");
                            return NULL;
                        } else if(ptr->wildcard == 1){
                            free_command(command);
                            errno = 1;
                            perror("Output can't be redirected to a wildcard");
                            return NULL;
                        } else if(command->output == NULL){
                            command->output = malloc(sizeof(char) * (strlen(ptr->chrPtr) + 1));
                            strcpy(command->output, ptr->chrPtr);
                        } else{
                            errno = 1;
                            perror("Attempting Multiple Output Redirections");
                            free_command(command);
                            return NULL;
                        }
                    } else{
                        errno = 5;
                        perror("No Output Redirection Given");
                        free_command(command);
                        return NULL;
                    }
                } else if(ptr->type == comb){
                    if(ptr->next == NULL){
                        errno = 1;
                        perror("No Command Given");
                        free_command(command);
                        return NULL;
                    }
                    command->next = process_tokens(ptr->next);
                    if(command->next == NULL){
                        free_command(command);
                        return NULL;
                    } else{
                        command->next->prev = command;
                        command->arguments = realloc(command->arguments, sizeof(char *) * (command->argCount + 1));
                        command->arguments[command->argCount] = NULL;
                        return command;
                    }
                } else{
                    if(ptr->wildcard == 1){
                        find_wildcards(command, ptr->chrPtr, ptr->type);
                    } else{
                        command->argCount++;
                        command->arguments = realloc(command->arguments, sizeof(char *) * (command->argCount));
                        command->arguments[command->argCount-1] = (char *) malloc(sizeof(char) * (strlen(ptr->chrPtr) + 1));
                        memcpy(command->arguments[command->argCount-1], ptr->chrPtr, strlen(ptr->chrPtr));
                        command->arguments[command->argCount-1][strlen(ptr->chrPtr)] = '\0';
                    }
                }
                ptr = ptr->next;
            }
            command->arguments = realloc(command->arguments, sizeof(char *) * (command->argCount + 1));
            command->arguments[command->argCount] = NULL;
            return command;
            break;
        case in:
        case out:
        case comb:
            free(command);
            errno = 1;
            perror("Command can't start with special character");
            return NULL;
            break;
    }
    return NULL;
}

char* find_executable(char *chr){
    struct stat *buf = malloc(sizeof(struct stat));
    int strLength = strlen(chr);
    char first[] = "/usr/local/sbin/";
    char *temp = (char*) malloc(sizeof(char) * (strlen(first) + strLength + 1));
    strcpy(temp, first);
    strcat(temp, chr);
    if(stat(temp, buf) == 0){
        if(buf->st_mode && S_IXUSR){
            free(buf);
            return temp;
        }
        errno = 1;
        perror("Found but not executable");
        free(temp);
        free(buf);
        return NULL;
    }

    char second[] = "/usr/local/bin/";
    temp = (char*) realloc(temp, sizeof(char) * (strlen(second) + strLength + 1));
    strcpy(temp, second);
    strcat(temp, chr);
    if(stat(temp, buf) == 0){
        if(buf->st_mode && S_IXUSR){
            free(buf);
            return temp;
        }
        errno = 1;
        perror("Found but not executable");
        free(buf);
        free(temp);
        return NULL;
    }

    char third[] = "/usr/sbin/";
    temp = (char*) realloc(temp, sizeof(char) * (strlen(third) + strLength + 1));
    strcpy(temp, third);
    strcat(temp, chr);
    if(stat(temp, buf) == 0){
        if(buf->st_mode && S_IXUSR){
            free(buf);
            return temp;
        }
        errno = 1;
        perror("Found but not executable");
        free(buf);
        free(temp);
        return NULL;
    }

    char fourth[] = "/usr/bin/";
    temp = (char*) realloc(temp, sizeof(char) * (strlen(fourth) + strLength + 1));
    strcpy(temp, fourth);
    strcat(temp, chr);
    if(stat(temp, buf) == 0){
        if(buf->st_mode && S_IXUSR){
            free(buf);
            return temp;
        }
        errno = 1;
        perror("Found but not executable");
        free(buf);
        free(temp);
        return NULL;
    }

    char fifth[] = "/sbin/";
    temp = (char*) realloc(temp, sizeof(char) * (strlen(fifth) + strLength + 1));
    strcpy(temp, fifth);
    strcat(temp, chr);
    if(stat(temp, buf) == 0){
        if(buf->st_mode && S_IXUSR){
            free(buf);
            return temp;
        }
        errno = 1;
        perror("Found but not executable");
        free(buf);
        free(temp);
        return NULL;
    }

    char sixth[] = "/bin/";
    temp = (char*) realloc(temp, sizeof(char) * (strlen(sixth) + strLength + 1));
    strcpy(temp, sixth);
    strcat(temp, chr);
    if(stat(temp, buf) == 0){
        if(buf->st_mode && S_IXUSR){
            free(buf);
            return temp;
        }
        errno = 1;
        perror("Found but not executable");
        free(buf);
        free(temp);
        return NULL;
    }
    perror("Not Found");
    free(temp);
    free(buf);
    return NULL;

}

int check_executables(process* head){
    process* ptr = head;
    while(ptr != NULL){
        if(ptr->prev != NULL && ptr->input != NULL){
            errno = 1; 
            perror("Multiple input directions");
            return 1;
        }
        if(ptr->next != NULL && ptr->output != NULL){
            errno = 1; 
            perror("Multiple output directions");
            return 1;
        }
        ptr = ptr->next;
    }
    return 0;
}

int execute_processes(process* head){
    int status = 0;
    process *ptr = head;
    int p[2];
    int fdd = -1;
    while(ptr != NULL){
        int in = -1;
        int out = -1;
        if(pipe(p) == -1){
            perror("Error with pipe");
            return 1;
        }
        switch(ptr->type){
            case cd:
                if(ptr->argCount > 2){
                    errno = 7; 
                    perror("Error with cd");
                }
                else if (ptr->argCount < 2){
                    if(chdir(getenv("HOME")) == -1){
                        perror("Error with cd"); 
                        return 1;
                    }
                }
                else{
                    if(chdir(ptr->arguments[1]) == -1){
                        perror("Error with cd");
                        return 1;
                    }
                }
                if(ptr->prev != NULL){
                    close(fdd);
                }
                if(ptr->next != NULL){
                    fdd = p[0];
                    close(p[1]);
                }
                break;
            case pwd:;
                char* buffer = getcwd(NULL, 0);
                int len = strlen(buffer);
                buffer = realloc(buffer, sizeof(char) * (len + 2));
                if(ptr->prev != NULL){
                    close(fdd);
                }
                if(buffer == NULL){
                    perror("pwd: couldn't get current working directory");
                    free(buffer);
                    return 1;
                }
                else{
                    buffer[len] = '\n';
                    buffer[len+1] = '\0';
                    if(ptr->output != NULL){
                        out = open(ptr->output, O_CREAT|O_TRUNC|O_WRONLY, 0640);
                        if(out < 0){
                            perror("pwd: ");
                            free(buffer);
                            return 1;
                        }
                        else{
                            write(out, buffer, strlen(buffer));
                            close(out);
                        }
                    }
                    else if(ptr->next != NULL){
                        write(p[1], buffer, strlen(buffer));
                        fdd = p[0];
                        close(p[1]);
                    }
                    else{
                        write(STDOUT_FILENO, buffer, strlen(buffer));
                    }
                }
                free(buffer);
                break;
            case path:
            case bare:;
                int child_status;
                if(ptr->input != NULL){
                    in = open(ptr->input, O_RDONLY);
                    if(in < 0){
                        perror("Error with open");
                        return 1;
                    }
                }
                if(ptr->output != NULL){
                    out = open(ptr->output, O_CREAT|O_TRUNC|O_WRONLY, 0640);
                    if(out < 0){
                        perror("Error with open");
                        return 1;
                    }
                }
                int pid = fork();
                if(pid == -1){
                    perror("Error with fork");
                    return 1;
                }
                else if(pid == 0){
                    if(in > 0){dup2(in, STDIN_FILENO);}
                    if(out > 0){dup2(out, STDOUT_FILENO);}
                    if(ptr->next != NULL){
                        dup2(p[1], STDOUT_FILENO);
                    }
                    if(ptr->prev != NULL){
                        dup2(fdd, STDIN_FILENO);
                    }
                    close(p[0]);
                    execvp(ptr->path_name, ptr->arguments);
                    perror(ptr->path_name);
                    _exit(EXIT_FAILURE);
                }
                else{
                    if(wait(&child_status) >= 0){
                        if(WIFEXITED(child_status)){
                            if(WEXITSTATUS(child_status) == EXIT_FAILURE){
                                return 1;
                            }
                        }
                    }
                    else{
                        perror(ptr->path_name);
                        return 1;
                    }
                    close(p[1]);
                    if(ptr->prev != NULL){close(fdd);}
                    fdd = p[0];
                }
                break;
            case term:
                if(ptr->prev != NULL){
                    close(fdd);
                }
                if(ptr->next != NULL){
                    close(p[1]);
                    fdd = p[0];
                }
                status = 2;
                break;
            default:
                break;
        }
        if(in > 0){close(in);}
        if(out > 0){close(out);}
        ptr = ptr->next;
    }
    return status;
}

void find_wildcards(process* proc, char* name, int type){
    struct dirent* de;
    char* dir = malloc(sizeof(char) * 100);
    char* pattern = malloc(sizeof(char) * 100);
    int check_begin = 0;

    if(type == path){
        int index;
        for(int i = 0; i < strlen(name); i++){
            if(name[i] == '/'){
                index = i;
            }
        }
        dir = realloc(dir, sizeof(char) * (index + 1));
        strncpy(dir, name, index);
        dir[index] = '\0';
        pattern = realloc(pattern, sizeof(char) * (strlen(name) - index));
        strcpy(pattern, name + index + 1);
    }
    else{
        dir = realloc(dir, sizeof(char) * 3);
        strcpy(dir, ".");
        pattern = realloc(pattern, sizeof(char) * (strlen(name) + 1));
        strcpy(pattern, name);
    }
    if(pattern[0] == '*'){
        check_begin = 1;
    }
    DIR *dp = opendir(dir);
    if(!dp){
        perror("Error with opening directory");
        return;
    }
    int count = 0;
    int dirLen = strlen(dir);
    while((de = readdir(dp))){
        if(check_wildcard(de->d_name, pattern) == 1){
            if(check_begin == 1 && de->d_name[0] == '.'){
                continue;
            }
            int dlen = strlen(de->d_name);
            if(type == path){
                proc->argCount++;
                proc->arguments = realloc(proc->arguments, sizeof(char *) * (proc->argCount));
                proc->arguments[proc->argCount - 1] = malloc(sizeof(char) * (dlen + dirLen + 2));
                memcpy(proc->arguments[proc->argCount-1], dir, dirLen);
                proc->arguments[proc->argCount-1][dirLen] = '/';
                memcpy(proc->arguments[proc->argCount-1] + dirLen + 1, de->d_name,dlen);
                proc->arguments[proc->argCount-1][dirLen + 1 + dlen] = '\0';
                count++;
            }
            else if(type == bare){
                proc->argCount++;
                proc->arguments = realloc(proc->arguments, sizeof(char *) * (proc->argCount));
                proc->arguments[proc->argCount - 1] = malloc(sizeof(char) * (dlen + 1));
                memcpy(proc->arguments[proc->argCount-1], de->d_name, dlen);
                proc->arguments[proc->argCount-1][dlen] = '\0';
                count++;
            }
        }
    }
    if(count == 0){
        proc->argCount++;
        proc->arguments = realloc(proc->arguments, sizeof(char *) * (proc->argCount));
        proc->arguments[proc->argCount - 1] = malloc(sizeof(char) * (strlen(name) + 1));
        memcpy(proc->arguments[proc->argCount-1], name, strlen(name));
        proc->arguments[proc->argCount-1][strlen(name)] = '\0';
    }
    closedir(dp);
    free(dir);
    free(pattern);
}

int check_wildcard(char* file, char* pat){
    int file_len = strlen(file);
    int pat_len = strlen(pat);
    int i = 0; int j = 0;
    int index_file = -1; int index_pat = -1;
    while(i < file_len){
        if (j < pat_len && file[i] == pat[j])
        {
            i++;
            j++;
        }
        else if (j < pat_len && pat[j] == '*')
        {
            index_file = i;
            index_pat = j;
            j++;
        }
        else if (index_pat != -1)
        {
            j = index_pat + 1;
            i = index_file + 1;
            index_file++;
        }
        else
        {
            return 0;
        }
    }
    while(j < pat_len && pat[j] == '*'){
        j++;
    }
    if(j == pat_len){
        return 1;
    }
    return 0;
}
