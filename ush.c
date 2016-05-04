#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <signal.h>
#include "parse.h"


char host[100];
static int pipeErr,pipeErr_prev;
static int first_cmd,last_cmd,pipe_number,pipe_count;
static int builtin,pipe_builtin;
static int in_fd,out_fd,err_fd;
static int nice_flag,nice_priority;
static int cmd_path_flag;
static int abort_pipe;
static int child_present;

void pipe_handler(int fd[][2]) {
        if (!(first_cmd&&last_cmd)) {
           if (first_cmd) {
              if(close(fd[pipe_number][0])!=0) {
                fprintf(stderr,"%s\n",strerror(errno));
                exit(0);
              }
              if(dup2(fd[pipe_number][1],1)==-1)
                fprintf(stderr,"%s\n",strerror(errno));
              if (pipeErr) {
                if(dup2(fd[pipe_number][1],2)==-1)
                  fprintf(stderr,"%s\n",strerror(errno));
                pipeErr=0;
                pipeErr_prev=1;
              }
           } else if (pipe_number==pipe_count+1) {
              if(close(fd[pipe_number-1][1])!=0) {
                  fprintf(stderr,"%s\n",strerror(errno));
                  exit(0);
              }
              if (pipeErr_prev) {
                if(close(fd[pipe_number-1][2])!=0) {
                    fprintf(stderr,"%s\n",strerror(errno));
                    exit(0);
                }
              }
              if(dup2(fd[pipe_number-1][0],0)==-1)
                fprintf(stderr,"%s\n",strerror(errno));
           } else {
              if(close(fd[pipe_number-1][1])!=0) {
                fprintf(stderr,"%s\n",strerror(errno));
                exit(0);
              }
              if (pipeErr_prev) {
                if(close(fd[pipe_number-1][2])!=0) {
                  fprintf(stderr,"%s\n",strerror(errno));
                  exit(0);
                }
                pipeErr_prev=0;
              }
              if(close(fd[pipe_number][0])!=0) {
                fprintf(stderr,"%s\n",strerror(errno));
                exit(0);
              }
              if(dup2(fd[pipe_number-1][0],0)==-1)
                fprintf(stderr,"%s\n",strerror(errno));
              if(dup2(fd[pipe_number][1],1)==-1)
                fprintf(stderr,"%s\n",strerror(errno));
              if (pipeErr) {
                if(dup2(fd[pipe_number][1],2)==-1)
                  fprintf(stderr,"%s\n",strerror(errno));
                pipeErr=0;
                pipeErr_prev=1;
              }
           }
        }
}

void cmd_handler(Cmd c,int fd[][2],int incode, int outcode) {
     if (builtin_type(c)&&(pipe_builtin==0)) {
         builtin = 1;
         if (c->next!=NULL) {
            child_present=1;
            execute_cmd(c,fd,incode,outcode);
            child_present=0;
         } else {
            builtin_handler(c,fd,incode,outcode);
         }
     } else {
           child_present=1;
           execute_cmd(c,fd,incode,outcode);
           child_present=0;
     }
     builtin=0;
}

char * where_cmd(Cmd c) {
        char *env_path = malloc(500);
        env_path = getenv("PATH");
        char *env_path_dup = malloc(500);
        strcpy(env_path_dup,env_path);
        char *path=malloc(500);
        char *res= malloc(500);
        char *result= malloc(500);
        int fd;
        path = strtok(env_path_dup,":");
        while (path!=NULL) {
            strcpy(res,path);
            strcat(res,"/");
            if (cmd_path_flag) 
               strcat(res,c->args[0]);
            else 
               strcat(res,c->args[1]);
            fd = open(res,O_RDONLY,0555);
            if (fd!=-1) {
               close(fd);
               return res;
            }
            path = strtok(NULL,":");
        }
}

int path_handler(Cmd c) {
    int res;
    if (strstr(c->args[0],"/")!=NULL) {
       res=1;
    }  else {
       res=0;
    }
    return res;
}

char * full_path(Cmd c) {

    char *res=malloc(500);
    if ((c->args[0][0]=='/')) {
       res=c->args[0];
    } else {
       res = getcwd(res,500); 
       strcat(res,"/");
       strcat(res,c->args[0]);
    }
    return res;
}

void check_file_status(char *path) {
     struct stat statbuf;
     stat(path,&statbuf);
     if (S_ISDIR(statbuf.st_mode)) {
        fprintf(stderr,"permission denied\n");
        exit(1);
     }
     int fd;
     fd = access(path,X_OK);
     if (fd==-1) { 
        fprintf(stderr,"permission denied\n");
        exit(1);
     }
}

void execute_cmd(Cmd c,int fd[][2],int incode,int outcode) {

  if (!builtin) {
     pid_t pid;
     pid = fork();

     if (pid==-1) {
        perror("can't fork the process\n");
        exit(-1);
     } else if (pid==0) {
        if (nice_flag ==1) { 
            if (setpriority(PRIO_PROCESS,getpid(),nice_priority)==-1)
               fprintf(stderr,"%s\n",strerror(errno));
        }
        if ((incode!=0)||((outcode!=0)&&(outcode<5)))  {
           redirection_handler(c,incode,outcode);
        }
        pipe_handler(fd);
        if (builtin_type(c)) {
           builtin_handler(c);
        }  else {
           char *res=malloc(500);
           if (!path_handler(c)) {
              cmd_path_flag=1;
              res= where_cmd(c);
              cmd_path_flag=0;
           } else {
              res = full_path(c);
              check_file_status(res);
           }
           if (res!=NULL) {
              extern char **environ;
              execvpe(res,c->args,environ);
           } else { 
              fprintf(stderr,"%s: command not found\n",c->args[0]);
              exit(1);
           }
        }
     } else {
        if (pipe_number!=1) {
           if(close(fd[pipe_number-1][0])) {
             fprintf(stderr,"%s\n",strerror(errno));
             exit(0);
           }
           if(close(fd[pipe_number-1][1])) {
             fprintf(stderr,"%s\n",strerror(errno));
             exit(0);
           }
           if (pipeErr_prev) {
              if(close(fd[pipe_number-1][2])) {
                fprintf(stderr,"%s\n",strerror(errno));
                exit(0);
              }
              pipeErr_prev=0;
           }
        }
        int exit_status;
        waitpid(pid,&exit_status,NULL);
        if (WIFEXITED(exit_status)==1) {
           if (WEXITSTATUS(exit_status)==1) 
              abort_pipe=1;
        }
     }
  } 
}

int builtin_type(Cmd c) {

    int match=0;
    if (!strcmp(c->args[0],"pwd")) 
       match=1;
    else if (!strcmp(c->args[0],"cd"))
       match=1;
    else if (!strcmp(c->args[0],"setenv"))
       match=1;
    else if (!strcmp(c->args[0],"unsetenv"))
       match=1;
    else if (!strcmp(c->args[0],"echo"))
       match=1;
    else if (!strcmp(c->args[0],"nice"))
       match=1;
    else if (!strcmp(c->args[0],"where"))
       match=1;
    return match;

}

void builtin_handler(Cmd c,int fd[][2],int incode,int outcode) {
     redirection_handler(c,incode,outcode);  
     if (!strcmp(c->args[0],"pwd")) {
          char *wd;
          wd = (char *)malloc(5000);
          wd=getcwd(wd,5000);
          if (wd!=NULL) {
             fprintf(stdout,"%s\n",wd);
          } else {
             fprintf(stderr,"%s\n",strerror(errno));
          }
     } else if (!strcmp(c->args[0],"cd")) {
        if(c->nargs==2) {
           if(chdir(c->args[1])!=0) {
             fprintf(stderr,"%s\n",strerror(errno)); 
           }
        } else if (c->nargs==1) {
           char *home;
           home = malloc(500);
           home = getenv("HOME");
           if(chdir(home)!=0) {
             fprintf(stderr,"%s\n",strerror(errno)); 
           }
        }
     } else if (!strcmp(c->args[0],"echo")) {
        int i;
        if(c->nargs > 1) {
          for(i=1;c->args[i]!=NULL;i++) {
              if (c->args[i+1]!=NULL)
                 fprintf(stdout,"%s ",c->args[i]);
              else 
                 fprintf(stdout,"%s\n",c->args[i]);
          }
        } else {
            fprintf(stdout,"\n");
        }   
     } else if (!strcmp(c->args[0],"setenv")) {
        if (c->nargs > 1) {
          extern char **environ;
          char *newenv = malloc(500);
          char *env = malloc(500);
          int i=0;
          strcpy(newenv,c->args[1]);
          strcat(newenv,"=");
          if (c->args[2])
            strcat(newenv,c->args[2]);
          while(environ[i]) {
              if((strncmp(environ[i],c->args[1],strlen(c->args[1]))==0)&&((environ[i])[strlen(c->args[1])]=='=')) {
                environ[i] = newenv; 
                break;
              } else {
                putenv(newenv);
              }
              i++;
          } 
        } else {
           extern char **environ;
           int i=0;
           while(environ[i]) 
               fprintf(stdout,"%s\n",environ[i++]);
        } 
     } else if (!strcmp(c->args[0],"unsetenv")) {
        if (c->nargs > 1) {
           if(unsetenv(c->args[1])!=0) 
              fprintf(stderr,"%s\n",strerror(errno));
        }
     } else if (!strcmp(c->args[0],"nice")) {
        if (c->nargs >= 2) {
           int i,messi;
           int j=0;
           nice_flag=1;
           Cmd tmp; 
           tmp = c;
           messi = c->nargs;
           if ((c->args[1][0]=='+')||(c->args[1][0]=='-')) { 
              if (isdigit(c->args[1][1])) {
                nice_priority = atoi(c->args[1]);
              }
              for (i=2;i<c->nargs;i++) {
                  tmp->args[j] = c->args[i];
                  j++;
              } 
              tmp->nargs = j;
              tmp->maxargs = j;
           }  else {
             if (isdigit(c->args[1][0])) {
               nice_priority = atoi(c->args[1]); 
               for (i=2;i<c->nargs;i++) {
                  tmp->args[j] = c->args[i];
                  j++;
               } 
               tmp->nargs = j;
               tmp->maxargs = j;
             } else { 
               nice_priority = 4;
               for (i=1;i<c->nargs;i++) {
                  tmp->args[j] = c->args[i];
                  j++;
               } 
               tmp->nargs = j;
               tmp->maxargs = j;
             }
           }  
           int k;
           for (k=tmp->nargs;k<messi;k++) {
            c->args[k]=NULL;
           }
           builtin = 0;
           execute_cmd(tmp,fd,incode,outcode);
        } 
     } else if (!strcmp(c->args[0],"where")) {
       if (c->args[1]) {
          if (!strcmp(c->args[1],"pwd"))
             fprintf(stdout,"builtin\n");
          else if (!strcmp(c->args[1],"cd"))
             fprintf(stdout,"builtin\n");
          else if (!strcmp(c->args[1],"setenv"))
             fprintf(stdout,"builtin\n");
          else if (!strcmp(c->args[1],"unsetenv"))
             fprintf(stdout,"builtin\n");
          else if (!strcmp(c->args[1],"echo"))
             fprintf(stdout,"builtin\n");
          else if (!strcmp(c->args[1],"nice"))
             fprintf(stdout,"builtin\n");
          else if (!strcmp(c->args[1],"where"))
             fprintf(stdout,"builtin\n");
          else {
             char *res=malloc(500); 
             res=where_cmd(c);
             if (res!=NULL)
                fprintf(stdout,"%s\n",res);
          }
       }
     }
     if (pipe_builtin==0) {
        if (in_fd) 
           dup2(in_fd,STDIN_FILENO);
        if (out_fd)  
           dup2(out_fd,STDOUT_FILENO);
        if (err_fd)
           dup2(err_fd,STDERR_FILENO);
        in_fd  =0;
        out_fd =0;
        err_fd =0;
     } else {
        exit(0);
     }
}

void redirection_handler(Cmd  c,int incode,int outcode) {
         

     int fd;
     if (incode==1) {
	fd=open(c->infile,O_RDONLY);
        if (fd==-1) {
           fprintf(stderr,"%s\n",strerror(errno));
           exit(0);
        }
        in_fd=dup(0);
        if(dup2(fd,0)==-1)
          perror("stdin is not taken from the input file\n");
        if(close(fd)!=0) {
          fprintf(stderr,"%s\n",strerror(errno));
          exit(0);
        }
     }
     if (outcode!=0 && outcode <5&&outcode >0) {
        switch(outcode) {
            case 1 :
               fd = open(c->outfile,O_CREAT|O_WRONLY|O_TRUNC,0777);
               if (fd==-1) {
                  fprintf(stderr,"%s\n",strerror(errno));
                  exit(0);
               }
               out_fd = dup(1);
               if(dup2(fd,1)==-1)
                  fprintf(stderr,"%s\n",strerror(errno));
               if(close(fd)!=0) {
                  fprintf(stderr,"%s\n",strerror(errno));
                  exit(0);
               }
               break;
            case 2 :
               fd = open(c->outfile,O_CREAT|O_WRONLY|O_APPEND,0777);
               if (fd==-1) {
                  fprintf(stderr,"%s\n",strerror(errno));
                  exit(0);
               }
               out_fd = dup(1);
               if(dup2(fd,1)==-1)
                  fprintf(stderr,"%s\n",strerror(errno));
               if(close(fd)!=0) {
                  fprintf(stderr,"%s\n",strerror(errno));
                  exit(0);
               }
               break;
            case 3 :
               fd = open(c->outfile,O_CREAT|O_WRONLY|O_TRUNC,0777);
               if (fd==-1) {
                  fprintf(stderr,"%s\n",strerror(errno));
                  exit(0);
               }
               out_fd = dup(1);
               err_fd = dup(2);
               if(dup2(fd,1)==-1)
                  fprintf(stderr,"%s\n",strerror(errno));
               if(dup2(fd,2)==-1)
                  fprintf(stderr,"%s\n",strerror(errno));
               if(close(fd)!=0) {
                  fprintf(stderr,"%s\n",strerror(errno));
                  exit(0);
               }
               break;
            case 4 :
               fd = open(c->outfile,O_CREAT|O_WRONLY|O_APPEND,0777);
               if (fd==-1) {
                  fprintf(stderr,"%s\n",strerror(errno));
                  exit(0);
               }
               out_fd = dup(1);
               err_fd = dup(2);
               if(dup2(fd,1)==-1)
                  fprintf(stderr,"%s\n",strerror(errno));
               if(dup2(fd,2)==-1)
                  fprintf(stderr,"%s\n",strerror(errno));
               if(close(fd)!=0) {
                  fprintf(stderr,"%s\n",strerror(errno));
                  exit(0);
               }
               break;
            default :
               perror("This is not expected\n");
	       exit(-1);
        }
    }
}

static void prCmd(Cmd c,int fd[][2])
{
  int i;
  int incode;
  int outcode;

  incode=0;
  if ( c ) {
    if ( c->in == Tin )
       incode=1;
    if (c->out==TpipeErr)
       pipeErr=1;
    switch ( c->out ) {
      case Tout:
        outcode=1;
        break;
      case Tapp:
        outcode=2;
        break;
      case ToutErr:
        outcode=3;
        break;
      case TappErr:
	outcode=4;
        break;
      default:
        outcode=0;
      }

      cmd_handler(c,fd,incode,outcode);
  }
}

int get_pipe_count(Pipe p) {

   int count=0;
   Cmd c;
   c = p->head;
   while(c->next!=NULL) {
      count++;
      c=c->next;
   }
   return count;
}

void parse_pipe(Pipe p) {

  int i = 0;
  Cmd c;
  abort_pipe=0;
  if ( p == NULL )
    return;
  pipe_count=0;
  pipe_number=0;

  pipeErr=0;
  pipeErr_prev=0;
  pipe_count=get_pipe_count(p);
  int fd[pipe_count+1][2];
  int j;
  for (j=1;j<=pipe_count;j++) 
       if(pipe(fd[j])==-1) {
          fprintf(stderr,"%s\n",strerror(errno));
          exit(0);
       }

  for ( c = p->head; c != NULL; c = c->next ) {
    if (abort_pipe) { 
       abort_pipe=0;
       break;
    }
    if (!strcmp(c->args[0],"logout"))
       exit(0);
    pipe_number++;
    if ((c->next!=NULL)&&builtin_type(c)) {
       pipe_builtin=1;
    }
    if ((c==p->head)&&(c->next==NULL)) {
      first_cmd=1;
      last_cmd=1;
      prCmd(c,fd); 
    } else if (c==p->head) {
      first_cmd=1;
      prCmd(c,fd);
    } else if (c->next==NULL) {
      last_cmd=1;
      prCmd(c,fd);
    } else {
      prCmd(c,fd);
    }
     first_cmd=0;
     last_cmd=0;
     pipe_builtin=0;
  }
  parse_pipe(p->next);
}

void init_rc() {
     Pipe p;
     int fd,tmp_fd;
     char *home = malloc(500);
     char *home_dup = malloc(500);
     home = getenv("HOME");
     strcpy(home_dup,home);
     strcat(home_dup,"/.ushrc");
     if (access(home_dup,F_OK)!=-1) {
        fd = open(home_dup,O_RDONLY);
        if (fd==-1) {
           perror("failure in opening .ushrc\n");
        }
        tmp_fd = dup(STDIN_FILENO);
        dup2(fd,STDIN_FILENO);
        p=parse();
        if (p!=NULL) {
          while(strcmp(p->head->args[0],"end")!=0) {
            parse_pipe(p);
            freePipe(p); 
            p=parse();
            if (p==NULL)
              break;
          }
       }
       close(fd);
       dup2(tmp_fd,STDIN_FILENO);  
     }
}

void sigquit (int sig) {

     signal(SIGQUIT,SIG_IGN);
     printf("\r\n");
     fflush(STDIN_FILENO);
     printf("%s%% ",host);
     fflush(STDIN_FILENO);
     signal(SIGQUIT,sigquit);
}

void sigint(int sig) {

  if (!child_present) {
     signal(SIGINT,SIG_IGN);
     printf("\r\n");
     fflush(STDIN_FILENO);
     printf("%s%% ",host);
     fflush(STDIN_FILENO);
     signal(SIGINT,sigint);
  } else {
     signal(SIGINT,SIG_IGN);
     printf("\r\n");
     fflush(STDIN_FILENO);
     signal(SIGINT,sigint);
  }
}

void sigterm(int sig) {

  if (!child_present) {
     signal(SIGTERM,SIG_IGN);
     printf("\r\n");
     fflush(STDIN_FILENO);
     printf("%s%% ",host);
     fflush(STDIN_FILENO);
     signal(SIGTERM,sigint);
  } else {
     signal(SIGTERM,SIG_IGN);
     printf("\r\n");
     fflush(STDIN_FILENO);
     signal(SIGTERM,sigint);
  }
}

int main(int argc, char *argv[])
{
  Pipe p;
  gethostname(host,100);
  signal(SIGQUIT,sigquit);
  signal(SIGINT,sigint);
  signal(SIGTERM,sigterm);
  init_rc();
  while ( 1 ) {
    printf("%s%% ", host);
    fflush(stdout);
    p = parse();
    parse_pipe(p);
    freePipe(p);
  }
}
