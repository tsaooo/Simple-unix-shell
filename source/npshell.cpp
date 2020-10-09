#include <iostream>

#include <string.h>

#define MAXCOMLENG 256
#define MAXINLENG 15000
#define PIPE 1
#define NUMPIPE 2
#define SINGLE 3

using namespace std;

typedef struct token_list{
    string tok[100];
    int length;
}token_list;
const string builtin_list[3] = {"setenv", "printenv", "exit"};

void handle_builtin(string);
int parse_pipe(string, token_list **, int *);
void tokenizer(string , char, token_list *);

int main(int argc, char* const argv[]){
    setenv("PATH", "bin:.", 1);
    string input_string;
    token_list *commands, params;
    const char prompt[] = "$ ";
    int mode, command_count;

   
    while(true){
        int *num_com;
        printf("%s", prompt);
        getline(cin, input_string);
        handle_builtin(input_string);
        mode = parse_pipe(input_string, &commands, &command_count);
        
        switch (mode)
        {
        case PIPE :
            break;  
        case NUMPIPE :
            break;
        case SINGLE :

            break;
        default:
            break;
        }
    }
}
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
void handle_builtin(string input){
    int i=0;
    token_list params;
    for(;i<3 && input.find(builtin_list[i])==string::npos; i++);

    switch (i){
    case 0:
        tokenizer(input, ' ', &params);
        setenv(params.tok[1].c_str(), params.tok[2].c_str(), 1);
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

int parse_pipe(string input, token_list **commands, int *count){      
    size_t pos;
    token_list tmp_coms;
    int n;
    bool flag_npipe = true;
    
    tokenizer(input, '|', &tmp_coms);
    n = tmp_coms.length;
    if(n > 1){
        for (int i=0; i<tmp_coms.tok[n].length(); i++)
            if(!isdigit(tmp_coms.tok[n][i])){
                flag_npipe = false;
                break;
            }
        //[command] |[0-9]
        if(flag_npipe) n -= 1;
        *count = n;
        *commands = new token_list[n];
        for(int i=0; i<n; i++)
            tokenizer(tmp_coms.tok[i], ' ', &(*commands)[i]);
        if(flag_npipe) 
            return NUMPIPE;
        else 
            return PIPE;
    }
    else
        return SINGLE;
}
void parse_command(token_list com){
    
}


