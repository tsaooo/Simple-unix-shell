#include <iostream>
#include <vector>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <dirent.h>
using namespace std;

#define MAXCOMLENG 256
#define MAXINLENG 15000
#define ERRPIPE 0
#define NORMAL 1
#define NUMPIPE 2
#define OUTFILE 3

#define WRITE 1
#define READ 0

typedef struct token_list{
    string tok[5000];
    int length;
}token_list;

typedef struct w_node{
    int remain;
    int fd[2];
    int ch_count;
}w_node;

vector <w_node> npipe_list;
//vector <string> cmds;
vector <pid_t> pid_list;
const string builtin_list[3] = {"setenv", "printenv", "exit"};

token_list *cmds;
int p1_fd[2], p2_fd[2], mode, count;

void update_plist(){
    vector <w_node>::iterator it = npipe_list.begin();
    for(;it!=npipe_list.end(); it++)
        it->remain--;
}

int search_plist(int n){
    for(int i=0;i < npipe_list.size(); i++)
        if(npipe_list[i].remain == n)
            return i;
    return -1;
}
void insert_plist(int n, int *f, int c){
    w_node tmp = {n, f[0], f[1], c};
    npipe_list.push_back(tmp);
}

void split(string s, char delim, token_list *l){
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


bool handle_builtin(string input){
    int i=0;
    token_list params;
    for(;i<3 && input.find(builtin_list[i])==string::npos; i++);

    switch (i){
    case 0:
        split(input, ' ', &params);
        setenv(params.tok[1].c_str(), params.tok[2].c_str(), 1);
        break;
    case 1:
        split(input, ' ', &params);
        printf("%s\n",getenv(params.tok[1].c_str()));
        break;
    case 2:
        exit(0);
        break;
    default:
        return false;
        break;
    }
    return true;    
}
inline bool is_numpipe(string cmd, size_t *pos){
    //pos : index of last '|' 
    for (int i=0; i<cmd.length(); i++)
        if(!isdigit(cmd[i]) && cmd[i] != ' ' && cmd[i] != '+')
            return false;
    return true;
}

inline bool is_outredir(string cmd, size_t *pos){
    //pos : index of '>' being search for
    if((*pos = cmd.find('>'))!= string::npos)
        return true;
    return false;
}
inline bool is_errpipe(string cmd, size_t *pos){
    //pos : index of last '!' being search for
    if((*pos = cmd.find('!')) != string::npos){
        for (int i=*pos+1; i<cmd.length(); i++)
            if(!isdigit(cmd[i]) && cmd[i] != ' ')
                return false;
        return true;
    }
    return false;
}

int parse_cmd(string input){      
    size_t pos;
    token_list unsplit_cmds;
    int n;
    bool npipe = true, out_redir = false, errpipe = false;

    split(input, '|', &unsplit_cmds);
    n = unsplit_cmds.length;
    if(( out_redir = is_outredir(unsplit_cmds.tok[n-1], &pos) ))
        unsplit_cmds.tok[n-1].erase(pos, 1);
    if(( errpipe = is_errpipe(unsplit_cmds.tok[n-1], &pos) ))
        unsplit_cmds.tok[n-1].erase(pos, 1);
    if(( npipe = is_numpipe(unsplit_cmds.tok[n-1], &pos) ))
        n -= 1;
    count = n;
    cmds = new token_list[n];
    if(n > 1)
        for(int i=0; i<n; i++)
            split(unsplit_cmds.tok[i], ' ', &cmds[i]);
    else
        split(unsplit_cmds.tok[0], ' ', &cmds[0]);

    if(npipe){
        cmds[n-1].tok[cmds[n-1].length] = unsplit_cmds.tok[n];
        return NUMPIPE;
    }
    else if(errpipe){
        cmds[count-1].length--;
        return ERRPIPE;
    }
    else if(out_redir){
        cmds[count-1].length--;
        return OUTFILE;
    }
    else return NORMAL;
}

const char **tkltocstr(token_list c){
    const char **argv;
    int i = 0;
    argv = new const char*[c.length+1]{NULL};
    for(;i<c.length;i++)
        argv[i] = c.tok[i].c_str();
    return argv;
}

inline void redirect(int newfd, int oldfd){
    if(oldfd != newfd){
        dup2(newfd, oldfd);
        close(newfd);   
    }
}

void run(token_list cmd, int fd_in, int out, int err){
    redirect(fd_in, STDIN_FILENO);
    if(out != err){
        redirect(out, STDOUT_FILENO);
        redirect(err, STDERR_FILENO);
    }
    else{
        dup2(out, STDOUT_FILENO);
        dup2(out, STDERR_FILENO);
        close(out);
    }
    
    const char **argv = tkltocstr(cmd);
    execvp(argv[0], (char**)argv);
    if(errno == ENOENT)
        cerr << "Unknown command: [%s" << cmd.tok[0].c_str() << "].\n";
    exit(0);
}
int numpipe_parse(){
    int sum = 0;
    size_t pos1 = -1;
    size_t pos2;
    string exp = cmds[count-1].tok[cmds[count-1].length];
    while((pos2 = exp.find('+', pos1+1)) != string::npos){
        sum += stoi(exp.substr(pos1+1, pos2 - pos1 - 1));
        pos1 = pos2;
    }
    sum += stoi(exp.substr(pos2+1, string::npos));
    return sum;
}

void last_cmdcntl(int IN = STDIN_FILENO){
    int OUT = STDOUT_FILENO, ERR = STDERR_FILENO, fd[2];
    int index, n;
    int *last = fd;
    pid_t cur_pid;

    if(mode == ERRPIPE || mode == NUMPIPE){
        n = numpipe_parse();
        //n = stoi(cmds[count-1].tok[cmds[count-1].length]);
        index = search_plist(n);
        if(index != -1)
            last = npipe_list.at(index).fd;
        else{
            pipe(last);
            insert_plist(n, last, 1);
        }
        OUT = last[WRITE];
        if(mode == ERRPIPE) ERR = OUT;
    }
    while((cur_pid = fork()) < 0){
        waitpid(-1, NULL, 0);
        cout << "too much process, wait success\n";
    }
    if((cur_pid = fork()) == 0){
        if(mode == OUTFILE){
            const char * fd_name = cmds[count-1].tok[cmds[count-1].length].c_str();
            OUT = open(fd_name, O_WRONLY | O_CREAT, 0666);
            ftruncate(OUT, 0); 
            lseek(OUT, 0, SEEK_SET); 
        }
        else if(mode == ERRPIPE || mode == NUMPIPE) 
            close(last[READ]);
        run(cmds[count-1], IN, OUT, ERR);
    }
    else{
        if(mode != ERRPIPE && mode != NUMPIPE) pid_list.push_back(cur_pid);  
        if(IN != STDIN_FILENO) close(IN);

        vector <pid_t> :: iterator it = pid_list.begin();
        for(; it!=pid_list.end(); it++)
            waitpid(*it, NULL, 0);
    }
}

void pipe_control(int IN){
    pid_t pid1, pid2;
    int *front_pipe = p1_fd, *end_pipe = p2_fd;
    pipe(front_pipe);
    
    if((pid1 = fork()) == 0){
        close(front_pipe[READ]);
        run(cmds[0], IN, front_pipe[WRITE], STDERR_FILENO);
    }
    else{
        pid_list.push_back(pid1);
        close(front_pipe[WRITE]);
        if(IN != STDIN_FILENO) close(IN);
    }

    for(int i = 1;i < count-1; i++){
        //Pid1 --pipe1--> Pid2 --pipe2-->
        pipe(end_pipe);
        while((pid2 = fork()) < 0){
            waitpid(-1, NULL, 0);
            cout << "too many processes, wait success\n";
        }
        if(pid2 == 0){
            close(end_pipe[READ]);
            run(cmds[i], front_pipe[READ], end_pipe[WRITE], STDERR_FILENO);
        }
        else{
            pid_list.push_back(pid2);
            close(front_pipe[READ]);
            close(end_pipe[WRITE]);
            swap(front_pipe, end_pipe);
            swap(pid1, pid2);
        }
    }
    last_cmdcntl(front_pipe[READ]);
}

void init(){
    delete [] cmds;
    update_plist();
    p1_fd[WRITE] = 0;
    p1_fd[READ] = 0;
    p2_fd[WRITE] = 0;
    p2_fd[READ] = 0;
}

int main(int argc, char* const argv[]){
    string input_string;
    int IN = STDIN_FILENO, OUT = STDOUT_FILENO;
    int npipe_idx;
    
    setenv("PATH", "bin:.", 1);
    while(true){
        cout<<"% "<<flush;
        getline(cin, input_string);
        if(handle_builtin(input_string))
            continue;
        mode = parse_cmd(input_string);

        if((npipe_idx = search_plist(0)) != -1){
            IN = npipe_list.at(npipe_idx).fd[READ];
            close(npipe_list.at(npipe_idx).fd[WRITE]);
            npipe_list.erase(npipe_list.begin()+npipe_idx);
        }

        if(count == 1)
            last_cmdcntl(IN);
        else{
            pipe_control(IN);
        }
        init();
    }
}
