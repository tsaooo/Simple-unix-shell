#include <iostream>
#include <vector>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <errno.h>
#include <dirent.h>
using namespace std;

#define MAXCOMLENG 256
#define MAXINLENG 15000
#define PIPE 1
#define NUMPIPE 2
#define SINGLE 3
#define WRITE 1
#define READ 0

typedef struct token_list{
    string tok[100];
    int length;
}token_list;
vector <string> path_list;
const string builtin_list[3] = {"setenv", "printenv", "exit"};

token_list *commands;
int p1_fd[2], p2_fd[2];

void tokenizer(string s, char delim, token_list *l){
    size_t pos = 0;
    int n = 0;
    while((pos = s.find(delim)) != string::npos){
        if(pos!=0){
            l->tok[n] = s.substr(0, pos);
            n++;
        }
        s.erase(0, pos+1);
    }
    if(s.length()!=0){
        l->tok[n] = s;
        n++;
    }
    l->length = n;
}

void modify_pathlist(string _path){
    token_list paths;
    tokenizer(_path, ':', &paths);
    DIR *dir;
    struct dirent *ent;
    struct stat buf;
    char cwd[PATH_MAX];

    getcwd(cwd, sizeof(cwd));
    path_list.clear();
    for(int i=0; i<paths.length; i++){
        if((dir = opendir(paths.tok[i].c_str())) != NULL){
            chdir(paths.tok[i].c_str());
            while((ent = readdir(dir)) != NULL){
                stat(ent->d_name, &buf);
                if(buf.st_mode & S_IXUSR && !S_ISDIR(buf.st_mode))
                    path_list.push_back(ent->d_name);
            }
        }
        closedir(dir);
    }
    chdir(cwd);
}

void handle_builtin(string input){
    int i=0;
    token_list params;
    for(;i<3 && input.find(builtin_list[i])==string::npos; i++);

    switch (i){
    case 0:
        tokenizer(input, ' ', &params);
        setenv(params.tok[1].c_str(), params.tok[2].c_str(), 1);
        modify_pathlist(params.tok[2]);
        break;
    case 1:
        tokenizer(input, ' ', &params);
        printf("%s\n",getenv(params.tok[1].c_str()));
        break;
    case 2:
        exit(0);
        break;
    default:
        break;
    }       
}

int parse_pipe(string input, int *count){      
    size_t pos;
    token_list untok_comds;
    int n;
    bool flag_npipe = true;
    
    tokenizer(input, '|', &untok_comds);
    n = untok_comds.length;
    if(n > 1){
        for (int i=0; i<untok_comds.tok[n].length(); i++)
            if(!isdigit(untok_comds.tok[n][i])){
                flag_npipe = false;
                break;
            }
        //[command] |[0-9]
        if(flag_npipe) n -= 1;
        *count = n;
        commands = new token_list[n];
        for(int i=0; i<n; i++)
            tokenizer(untok_comds.tok[i], ' ', &commands[i]);
        if(flag_npipe) 
            return NUMPIPE;
        else 
            return PIPE;
    }
    else
        commands = new token_list[n];
        tokenizer(untok_comds.tok[0], ' ', &commands[0]);
        return SINGLE;
}

void redirect(int oldfd, int newfd){
    if(oldfd != newfd){
        dup2(newfd, oldfd);
        close(newfd);   
    }
}

void execute(token_list c, int in, int out, int err){
    char *const *argv;
    argv = new char*[c.length];         //waste efficiency, modify data structure later
    for(int i=0;i<c.length;i++)
        *argv[i] = *c.tok[i].c_str();

    redirect(STDIN_FILENO, in);
    redirect(STDOUT_FILENO, out);
    redirect(STDERR_FILENO, err);
    execvp(argv[0], argv);
    exit(errno);
}

bool is_vaild(string c){
    for(int i = 0; i<path_list.size(); i++)
        if(c == path_list[i])
            return true;
    return false;
}

int main(int argc, char* const argv[]){
    setenv("PATH", "bin:.", 1);
    modify_pathlist(getenv("PATH"));
    string input_string;
    pid_t pid1, pid2;
    const char prompt[] = "$ ";
    int mode, count, STATUS, in = STDIN_FILENO;
    int *front_pipe = p1_fd, *end_pipe = p2_fd;
   
    while(true){
        printf("%s", prompt);
        getline(cin, input_string);
        handle_builtin(input_string);
        mode = parse_pipe(input_string, &count);
        pipe(p1_fd);
        pipe(p2_fd);

        if(is_vaild(commands[0].tok[0])){
            if((pid1 = fork()) == 0){
                close(front_pipe[READ]);
                close(end_pipe[READ]);
                close(end_pipe[WRITE]);
                execute(commands[0], in, front_pipe[WRITE], STDERR_FILENO);
            }
        }        
        for(int i = 1;i < count-1; i++){
            //Pid1 --pipe1--> Pid2 --pipe2-->
            if((pid2 = fork()) == 0){
                close(front_pipe[WRITE]);
                close(end_pipe[READ]);
                execute(commands[i], front_pipe[READ], end_pipe[WRITE], STDERR_FILENO);
                //exec fail
                
            }
            close(front_pipe[READ]);
            close(front_pipe[WRITE]);
            close(end_pipe[READ]);
            close(end_pipe[WRITE]);
            waitpid(pid1, &STATUS, 0);
            swap(front_pipe, end_pipe);
            swap(pid1, pid2);
            pipe(end_pipe);
        }
        //last command, need check numpipe,errpipe
        if((pid2 = fork()) == 0){
            close(front_pipe[WRITE]);
            close(end_pipe[READ]);
        }
    }
}
