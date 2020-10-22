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

vector <string> exec_list;
vector <w_node> p_list;
//vector <string> cmds;
const string builtin_list[3] = {"setenv", "printenv", "exit"};

token_list *cmds;
int p1_fd[2], p2_fd[2], mode, count;

void update_plist(){
    vector <w_node>::iterator it = p_list.begin();
    for(;it!=p_list.end(); it++)
        it->remain--;
}

int search_plist(int n){
    for(int i=0;i < p_list.size(); i++)
        if(p_list[i].remain == n)
            return i;
    return -1;
}
void insert_plist(int n, int *f, int c){
    w_node tmp = {n, f[0], f[1], c};
    p_list.push_back(tmp);
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

void update_execlist(string _path){
    token_list paths;
    split(_path, ':', &paths);
    DIR *dir;
    struct dirent *ent;
    struct stat buf;
    char cwd[PATH_MAX];

    getcwd(cwd, sizeof(cwd));
    exec_list.clear();
    for(int i=0; i<paths.length; i++){
        if((dir = opendir(paths.tok[i].c_str())) != NULL){
            chdir(paths.tok[i].c_str());
            while((ent = readdir(dir)) != NULL){
                stat(ent->d_name, &buf);
                if(buf.st_mode & S_IXUSR && !S_ISDIR(buf.st_mode))
                    exec_list.push_back(ent->d_name);
            }
        }
        closedir(dir);
    }   
    chdir(cwd);
}

bool handle_builtin(string input){
    int i=0;
    token_list params;
    for(;i<3 && input.find(builtin_list[i])==string::npos; i++);

    switch (i){
    case 0:
        split(input, ' ', &params);
        setenv(params.tok[1].c_str(), params.tok[2].c_str(), 1);
        update_execlist(params.tok[2]);
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
        if(!isdigit(cmd[i]) && cmd[i] != ' ')
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

inline bool is_vaild(string c){
    for(int i = 0; i<exec_list.size(); i++)
        if(c == exec_list[i])
            return true;
    return false;
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
    
    if(is_vaild(cmd.tok[0])){
        const char **argv = tkltocstr(cmd);
        execvp(argv[0], (char**)argv);
        delete [] argv;
        exit(errno);
    }
    else{
        fprintf(stderr,"Unknown command: [%s].\n", cmd.tok[0].c_str());
        exit(0);
    }
}

void last_cmdcntl(bool fst, pid_t lpid, int fd_in = STDIN_FILENO){
    int n, index, inpipe_WRITE, out = STDOUT_FILENO, err = STDERR_FILENO, fd[2];
    int *last = fd, wait_count;
    pid_t cur_pid;
    bool receive = false, ign = false, merge = false;

    if(fst)
        if((index = search_plist(0)) != -1){
            wait_count = p_list.at(index).ch_count;
            fd_in = p_list.at(index).fd[READ];
            close(p_list.at(index).fd[WRITE]);
            p_list.erase(p_list.begin()+index);
            receive = true;
        }
    if(mode == ERRPIPE || mode == NUMPIPE){
        n = stoi(cmds[count-1].tok[cmds[count-1].length]);
        index = search_plist(n);
        if(index != -1){
            last = p_list.at(index).fd;
            p_list.at(index).ch_count++;
        }
        else{
            pipe(last);
            insert_plist(n, last, 1);
        }
    }

    if((cur_pid = fork()) == 0){
        if(mode == OUTFILE){
            const char * fd_name = cmds[count-1].tok[cmds[count-1].length].c_str();
            out = open(fd_name, O_WRONLY | O_CREAT, 0666);
            ftruncate(out, 0); 
            lseek(out, 0, SEEK_SET); 
        }
        else if(mode == ERRPIPE || mode == NUMPIPE) {
            //if(receive) close(inpipe_WRITE);
            close(last[READ]);
            out = last[WRITE];
            if(mode == ERRPIPE) err = out;
        }
        run(cmds[count-1], fd_in, out, err);
    }
    else{
        int p;
        if(fd_in != STDIN_FILENO) close(fd_in);
        //if(mode == ERRPIPE || mode == NUMPIPE) close(last[WRITE]);  
        if(!fst) waitpid(lpid, NULL, 0);
        else if(receive){
            //close(inpipe_WRITE);
            for (int i = 0; i < wait_count;){
                
                if((p = wait(NULL)) != cur_pid) i++;
                else ign =true;
            }
        }
        if(mode != ERRPIPE && mode != NUMPIPE && !ign) waitpid(cur_pid, NULL, 0);
    }
}

void pipe_control(){
    pid_t pid1, pid2;
    int *front_pipe = p1_fd, *end_pipe = p2_fd;
    int fd_in = STDIN_FILENO, inpipe_WRITE, index, i, n, wait_count;
    bool PIPEIN = false, ign = false;
    pipe(front_pipe);
    
    if((index = search_plist(0)) != -1){
        fd_in = p_list.at(index).fd[READ];
        wait_count = p_list.at(index).ch_count;
        close(p_list.at(index).fd[WRITE]);
        p_list.erase(p_list.begin()+index);
    }
    if((pid1 = fork()) == 0){
        close(front_pipe[READ]);
        run(cmds[0], fd_in, front_pipe[WRITE], STDERR_FILENO);
    }
    //parent wait all previous round hang childs
    else{
        close(front_pipe[WRITE]);
        if(fd_in != STDIN_FILENO){
            close(fd_in);
            //close(inpipe_WRITE);
            for (int i = 0; i < wait_count;){
                int p;
                if((p = wait(NULL)) != pid1 )i++;
                else ign = true;
            }
        }
    }

    for(int i = 1;i < count-1; i++){
        //Pid1 --pipe1--> Pid2 --pipe2-->
        pipe(end_pipe);
        if((pid2 = fork()) == 0){
            close(end_pipe[READ]);
            run(cmds[i], front_pipe[READ], end_pipe[WRITE], STDERR_FILENO);
        }
        else{
            close(front_pipe[READ]);
            close(end_pipe[WRITE]);
            if(!ign) waitpid(pid1, NULL, 0);
            swap(front_pipe, end_pipe);
            swap(pid1, pid2);
            pipe(end_pipe);
        }
    }
    //if need to pipe stdout or stderr to next "n" line
    last_cmdcntl(false, pid1, front_pipe[READ]);
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
    const char prompt[] = "% ";
    int i, IN = STDIN_FILENO, OUT = STDOUT_FILENO;
    
    setenv("PATH", "bin:.", 1);
    update_execlist(getenv("PATH"));

    while(true){
        cout<<prompt<<flush;
        getline(cin, input_string);
        if(handle_builtin(input_string))
            continue;
        mode = parse_cmd(input_string);

        if(count == 1)
            last_cmdcntl(true, -1);
        else{
            pipe_control();
        }
        init();
    }
}
