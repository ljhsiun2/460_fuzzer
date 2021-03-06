#include "exec.hpp"
#include <sys/ptrace.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/user.h>
#include <sys/wait.h>
#include <unistd.h>
#include <iostream>
#include <vector>
#include <string.h>
#include <unistd.h>

//return char ptr of so path
char* get_so_path(){
  //char result[152];
  //char result[152] = malloc(152);
  char* result = (char*)malloc(152);


  char exec_path[128];
  size_t end = readlink("/proc/self/exe",exec_path,128);
  //std::string str = "LD_PRELOAD=";

  exec_path[end-6] = 0;

  char ld_preload[14] =  "LD_PRELOAD=";
  char controlso[12] =  "control.so";
  //std::string myString(exec_path, 128);

  strcpy(result, ld_preload);
  strcat(result,exec_path);
  strcat(result,controlso);

  return result;
}

// Start child process
void start_child(std::string path, char *const argv[], int *stdio_pipes) {
  //std::vector<char> control_path(str.c_str(), str.c_str() + str.size() + 1);
  //char* control_path = "LD_PRELOAD=./control.so".c_str();

  char* ld_preload = get_so_path();
  char* envp[] = {ld_preload, NULL};
  close(stdio_pipes[1]); //stdin write
  dup2(stdio_pipes[0], STDIN_FILENO);
  close(stdio_pipes[0]);
  execve(path.c_str(), argv, envp);
  perror ("Error when executing child: ");

}


// Input: pid of child
// Output:
//wait for child, to pass control back to parent process
void close_parent_pipes(int server_read, int fuzzer_write) {
  close(server_read);
  close(fuzzer_write);

}

// Input: Path to child, args to pass to child
// Output: pid of child
//spawns child in a new process and attaches to it

pid_t child_exec(const std::string path, char *const argv[], int *stdio_pipes) {
  pid_t result;

  do {
    result = fork();
    switch (result) {
      case 0:  // child
        start_child(path, argv, stdio_pipes);
        break;
      case -1:  // error
        break;
      default:  // parent
        close(stdio_pipes[0]);
        break;
    }
  } while (result == -1 && errno == EAGAIN);

  return result;
}

//We always need communication for traces
void prepare_comm_pipes(int *trace_pipe){
  pipe(trace_pipe);
  auto trace_write = fcntl(trace_pipe[1], F_DUPFD, 200); //fork server trace writing end at 200
  int flags = fcntl(trace_pipe[0], F_GETFL);
  flags |= O_NONBLOCK;
  fcntl(trace_pipe[0], F_SETFL, flags);
  close(trace_pipe[1]);
  trace_pipe[1] = trace_write;
}

//If we are using the fork server, initialize the pipes
void prepare_fork_server(int *server_pipe, int *fuzzer_pipe){
  pipe(server_pipe);
  pipe(fuzzer_pipe);

  //TODO: do we need to set FD_CLOEXEC on the other ends to not have to close them 
  // in the server?
  auto read_end = fcntl(server_pipe[0], F_DUPFD, 198); //fork server reading end at 198
  auto write_end = fcntl(fuzzer_pipe[1], F_DUPFD, 199); //fork server writing end at 199
  
  //Close the old fds and replaced them with the duped fds
  close(server_pipe[0]);
  close(fuzzer_pipe[1]);
  server_pipe[0] = read_end;
  fuzzer_pipe[1] = write_end;
}

