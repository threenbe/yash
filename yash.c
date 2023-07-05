/*This is my yash shell
Raiyan Chowdhury
rac4444
EE461S Lab 1
*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <readline/readline.h>
#include <readline/history.h>
#include <sys/stat.h>
#include <stdbool.h>

#define LINE_LENGTH 2000
//#define DEBUG 1

//structs for processes and jobs
//jobs will contain 2 processes at most in this project
typedef struct process {
	pid_t pid;
	char **args;
	int std_in;
	int std_out;
	int std_err;

} procType;

typedef struct job_t {
	pid_t pgid; //process group containing the processes of this job
	procType *list[2]; //1-2 processes in a job
	char *input; //command line input
	int num;//job num
	struct job_t *next;//the next job in the job list
	int status;//1 = running; 2 = stopped; 3 = done
	bool foreground;
} jobType;


//other stuff
pid_t shell_pid;
jobType *foreground_job;
jobType *recent_job;//the most recent background/stopped job
jobType *head;//the topmost level in the job list

void killJob(jobType *job, int num_of_procs);
void add_to_table(jobType *job);


void remove_from_table(jobType *job) {
	if (!job) return;
	if (!head) return;
	//printf("hi\n");
	bool was_recent = false;
	if (recent_job) {
		if (recent_job->pgid == job->pgid) {
			was_recent = true;
		}
	}
	if (head->pgid == job->pgid) {
		if (head->next) {
			head = head->next;
		} else {
			head = NULL;
		}
		if (was_recent) {//if the head was most recent, then there's no other jobs left
			recent_job = NULL;
		}
		//printf("hi\n");
		return;
	}

	jobType *child_prev = head;
	jobType *child = head->next;
	while (child) {
		if (child->pgid == job->pgid) {
			if (child->next) {
				child_prev->next = child->next;
			} else {
				child_prev->next = NULL;
			}
			if (was_recent) {
				recent_job = child_prev;
			}
			break;
		} else {
			child_prev = child;
			child = child->next;
		}
	}
}

void job_to_fg() {
	jobType *job = recent_job;
	if (job) {
		if (kill(-job->pgid, SIGCONT) < 0) {
			perror("kill");
			return;
		}
		//make sure there's no & because foreground
		if (job->input[strlen(job->input)-1] != '&') {
			printf("%s\n", job->input);
		} else {
			printf("%.*s\n", (int) strlen(job->input)-2, job->input);
		}
		job->foreground = true;
		foreground_job = job;
		remove_from_table(job);
		//printf("hi\n");

		//wait for it again like before
		if (!job->list[1]) {//only one process
			int status;
			waitpid(job->pgid, &status, WUNTRACED);

			if (WIFEXITED(status)) {//regular termination
				killJob(job, 1);
			} else if (WIFSIGNALED(status)) {//ctrl+c, SIGINT
				killJob(job, 1);
			} else if (WIFSTOPPED(status)) {//ctrl+z, SIGTSTP
				job->status = 2;
				//job->foreground = false;
				add_to_table(job);
				recent_job = job;
				foreground_job = NULL;
			}
		} else {//two processes
			int status;
			int status2;
			waitpid(job->list[0]->pid, &status, WUNTRACED);
			waitpid(job->list[1]->pid, &status2, WUNTRACED);

			if (WIFEXITED(status2)) {
				//printf("hi\n");
				killJob(job, 2);
			} else if (WIFSIGNALED(status2)) {
				killJob(job, 2);
			} else if (WIFSTOPPED(status2)) {
				job->status = 2;
				//job->foreground = false;
				add_to_table(job);
				recent_job = job;
				foreground_job = NULL;
			}
		}
	} else {
		printf("fg: current: no such job\n");
	}
}

jobType *get_most_recent_stopped() {
	if (!head) return NULL;
	jobType *job = recent_job;
	if (job) {
		if (job->status == 2) {
			return job;
		}
	}
	job = head;
	jobType *ret = NULL;
	while (job) {
		if (job->status == 2) {
			ret = job;
		}
		job = job->next;
	}
	return ret;
}

void job_to_bg() {
	jobType *job = get_most_recent_stopped();
	if (job) {
		if (kill(-job->pgid, SIGCONT) < 0) {
			perror("kill");
			return;
		}
		job->status = 1;
		//make sure there's a & here because it's background
		if (job->input[strlen(job->input)] == '&') {
			if (recent_job && job->pgid == recent_job->pgid)
				printf("[%d]+ %s\n", job->num, job->input);
			else
				printf("[%d]- %s\n", job->num, job->input);
		} else {
			if (recent_job && job->pgid == recent_job->pgid)
				printf("[%d]+ %s &\n", job->num, job->input);
			else
				printf("[%d]- %s &\n", job->num, job->input);
		}
	} else {
		printf("bg: current: no such job\n");
	}
}

void print_dones() {
	if (!head) return;//nothing to update

	jobType *job = head;
	//printf("%d\n", job->num);
	while (job) {
		//if a job is done, print job table entry and then kill it
		//format:
		//[<jobnum>]+/- <status> <command>
		if (job->status == 3) {
			//printf("%d\n", job->num);
			if (job->pgid == recent_job->pgid) {
				if (job->input[strlen(job->input)-1] != '&') {
					printf("[%d]+ Done %s\n", job->num, job->input);
				} else {
					printf("[%d]+ Done %.*s\n", job->num, (int) strlen(job->input)-2, job->input);
				}
			} else {
				if (job->input[strlen(job->input)-1] != '&') {
					printf("[%d]- Done %s\n", job->num, job->input);
				} else {
					printf("[%d]- Done %.*s\n", job->num, (int) strlen(job->input)-2, job->input);
				}
			}
			jobType *next = job->next;
			if (!job->list[1]) {
				killJob(job, 1);
			} else {
				killJob(job, 2);
			}
			job = next;
		} else {
			job = job->next;
		}
	}
}

void update_table() {
	if (!head) return;//nothing to update

	jobType *job = head;
	//printf("%d\n", job->num);
	while (job) {
		if (!job->list[1]) {//only one process
			int status;
			printf("%d\n", job->pgid);
			waitpid(job->pgid, &status, WNOHANG|WUNTRACED);

			if (WIFEXITED(status)) {//regular termination
				printf("exited\n");
				job->status = 3;
			} else if (WIFSIGNALED(status)) {//ctrl+c, SIGINT
				printf("signal\n");
				job->status = 3;
			} else if (WIFSTOPPED(status)) {//ctrl+z, SIGTSTP
				job->status = 2;
			} else if (WIFCONTINUED(status)) {
				job->status = 1;
			}
			printf("%d\n", job->status);
		} else {//two processes
			int status;
			int status2;
			waitpid(job->list[0]->pid, &status, WNOHANG|WUNTRACED);
			waitpid(job->list[1]->pid, &status2, WNOHANG|WUNTRACED);

			if (WIFEXITED(status2)) {
				job->status = 3;
			} else if (WIFSIGNALED(status2)) {
				job->status = 3;
			} else if (WIFSTOPPED(status2)) {
				job->status = 2;
			} else if (WIFCONTINUED(status2)) {
				job->status = 1;
			}
		}

		//if a job is done, print job table entry and then kill it
		//format:
		//[<jobnum>]+/- <status> <command>
		if (job->status == 3) {
			if (job->pgid == recent_job->pgid) {
				if (job->input[strlen(job->input)-1] != '&') {
					printf("[%d]+ Done %s\n", job->num, job->input);
				} else {
					printf("[%d]+ Done %.*s\n", job->num, (int) strlen(job->input)-2, job->input);
				}
			} else {
				if (job->input[strlen(job->input)-1] != '&') {
					printf("[%d]- Done %s\n", job->num, job->input);
				} else {
					printf("[%d]- Done %.*s\n", job->num, (int) strlen(job->input)-2, job->input);
				}
			}
			jobType *next = job->next;
			if (!job->list[1]) {
				killJob(job, 1);
			} else {
				killJob(job, 2);
			}
			job = next;
		} else {
			job = job->next;
		}
	}
}

void display_jobs() {
	//go through the job table and display each entry
	if (!head) return;//no jobs
	jobType *job = head;

	while (job) {
		if (job->status == 2) {
			if (job->pgid == recent_job->pgid) {
				if (job->input[strlen(job->input)-1] != '&') {
					printf("[%d]+ Stopped %s\n", job->num, job->input);
				} else {
					printf("[%d]+ Stopped %.*s\n", job->num, (int) strlen(job->input)-2, job->input);
				}
			} else {
				if (job->input[strlen(job->input)-1] != '&') {
					printf("[%d]- Stopped %s\n", job->num, job->input);
				} else {
					printf("[%d]- Stopped %.*s\n", job->num, (int) strlen(job->input)-2, job->input);
				}
			}
		} else if (job->status == 1) {
			if (job->pgid == recent_job->pgid) {
				if (job->input[strlen(job->input)-1] == '&') {
					printf("[%d]+ Running %s\n", job->num, job->input);
				} else {
					printf("[%d]+ Running %s &\n", job->num, job->input);
				}
			} else {
				if (job->input[strlen(job->input)-1] == '&') {
					printf("[%d]- Running %s\n", job->num, job->input);
				} else {
					printf("[%d]- Running %s &\n", job->num, job->input);
				}
			}
		}
		job = job->next;
	}
}

void killJob(jobType *job, int num_of_procs) {
	//first check if job is in table, then remove from table
	/*if (!job) return;
	if (!head) return;
	if (head->pgid == job->pgid) {
		if (head->next) {
			head = head->next;
		} else {
			head = NULL;
		}
	}

	jobType *child_prev = head;
	jobType *child = head->next;
	while (child) {
		if (child->pgid == job->pgid) {
			if (child->next) {
				child_prev->next = child->next;
			} else {
				child_prev->next = NULL;
			}
			break;
		} else {
			child_prev = child;
			child = child->next;
		}
	}*/
	remove_from_table(job);

	//free job and its processes
	for (int i = 0; i < num_of_procs; i++) {
		free(job->list[i]->args);
		free(job->list[i]);
	}
	free(job);
}

void add_to_table(jobType *job) {//should all be stopped/bg
	int num = 1;
	//add to job table
	if (!head) {
		job->num = 1;
		head = job;
		//printf("added job\n");
	} else {
		num = head->num + 1;
		jobType *prev_child = head;
		jobType *child = head->next;
		while (child) {//keep going til null child
			num = child->num + 1;
			prev_child = child;
			child = child->next;
		}
		job->num = num;
		child = job;
		prev_child->next = job;
		//printf("%d\n", child->num);
		//printf("%d %d\n", head->num, head->next->num);
	}
	recent_job = job;
	//printf("head/job nums are %d/%d\n", head->num, job->num);
}

/*This executes a command with one pipe, i.e. two processes*/
void execute_cmd2(jobType *job) {
	pid_t pid, pid2;
	int pipefd[2];
	procType *proc = job->list[0];//child 1
	procType *proc2 = job->list[1];//child 2

	if (pipe(pipefd) < 0) {
		perror("pipe");
		exit(1);
	}

	//proc->std_out = pipefd[1];
	//proc2->std_in = pipefd[0];

	pid = fork();
	if (pid < 0) {
		perror("fork");
		exit(1);
	} else if (pid > 0) {//parent
		pid2 = fork();
		if (pid2 < 0) {
			perror("fork");
			exit(1);

		} else if (pid2 > 0) {//parent again
			close(pipefd[0]);
			close(pipefd[1]);
			proc->pid = pid;
			proc2->pid = pid2;
			job->pgid = pid;
			setpgid(proc->pid, job->pgid);
			setpgid(proc2->pid, job->pgid);
			int status;
			int status2;

			if (job->foreground == true) {
				foreground_job = job;
				//if proc2 finishes/stops then so should proc, 
				//so this should be sufficient I think
				waitpid(pid, &status, WUNTRACED);
				waitpid(pid2, &status2, WUNTRACED);

				if (WIFEXITED(status2)) {
					killJob(job, 2);
				} else if (WIFSIGNALED(status2)) {
					killJob(job, 2);
				} else if (WIFSTOPPED(status2)) {
					job->status = 2;
					//job->foreground = false;
					add_to_table(job);
					recent_job = job;
					foreground_job = NULL;
				}
			} else {
				job->status = 1;
				add_to_table(job);
			}
		} else {//child 2
			proc2->pid = getpid();
			job->pgid = pid;//this should give child 1's pid still?
			setpgid(proc2->pid, job->pgid);
			close(pipefd[1]);
			proc2->std_in = pipefd[0];

			//not sure if all 3 ifs are needed here?
			if (proc2->std_in != STDIN_FILENO) {
				dup2(proc2->std_in, STDIN_FILENO);
				close(proc2->std_in);
			}
			if (proc2->std_out != STDOUT_FILENO) {
				dup2(proc2->std_out, STDOUT_FILENO);
				close(proc2->std_out);
			}
			if (proc2->std_err != STDERR_FILENO) {
				dup2(proc2->std_err, STDERR_FILENO);
				close(proc2->std_err);
			}

			execvp(proc2->args[0], proc2->args);

			perror("execvp");
			exit(1);
		}
	} else {//child 1
		proc->pid = getpid();
		job->pgid = proc->pid;
		setpgid(proc->pid, job->pgid);
		close(pipefd[0]);
		proc->std_out = pipefd[1];

		if (proc->std_in != STDIN_FILENO) {
			dup2(proc->std_in, STDIN_FILENO);
			close(proc->std_in);
		}
		if (proc->std_out != STDOUT_FILENO) {
			dup2(proc->std_out, STDOUT_FILENO);
			close(proc->std_out);
		}
		if (proc->std_err != STDERR_FILENO) {
			dup2(proc->std_err, STDERR_FILENO);
			close(proc->std_err);
		}

		execvp(proc->args[0], proc->args);

		perror("execvp");
		exit(1);
	}

	if (proc->std_in != STDIN_FILENO) close(proc->std_in);
	if (proc->std_out != STDOUT_FILENO) close(proc->std_out);
	if (proc->std_err != STDERR_FILENO) close(proc->std_err);

	if (proc2->std_in != STDIN_FILENO) close(proc2->std_in);
	if (proc2->std_out != STDOUT_FILENO) close(proc2->std_out);
	if (proc2->std_err != STDERR_FILENO) close(proc2->std_err);

}


/*This executes a command with no pipes*/
void execute_cmd(jobType *job) {
	pid_t pid;
	procType *proc = job->list[0];
	#ifdef DEBUG
		printf("--- cmd: %s\n", proc->args[0]);
	#endif

	pid = fork();
	if (pid < 0) {//failure
		perror("fork");
		exit(1);
	} else if (pid == 0) {//child
		#ifdef DEBUG
			printf("--- In child now\n");
		#endif
		proc->pid = getpid();
		job->pgid = proc->pid;
		#ifdef DEBUG
			printf("--- PID/PGID is %d\n", proc->pid);
		#endif
		setpgid(proc->pid, job->pgid);
		#ifdef DEBUG
			printf("--- Child has set process group with args %d and %d\n", proc->pid, job->pgid);
		#endif

		if (proc->std_in != STDIN_FILENO) {
			dup2(proc->std_in, STDIN_FILENO);
			close(proc->std_in);
		}
		if (proc->std_out != STDOUT_FILENO) {
			dup2(proc->std_out, STDOUT_FILENO);
			close(proc->std_out);
		}
		if (proc->std_err != STDERR_FILENO) {
			dup2(proc->std_err, STDERR_FILENO);
			close(proc->std_err);
		}

		execvp(proc->args[0], proc->args);

		perror("execvp");//if the program comes here then execvp didn't work
		exit(1);
	} else {//parent
		#ifdef DEBUG
			printf("--- Parent's pid is %d btw\n", getpid());
		#endif
		proc->pid = pid;
		job->pgid = pid;
		setpgid(pid, pid);//pid and pgid are just the same in this case
		#ifdef DEBUG
			printf("--- Parent has just set child's process group %d, about to wait\n", pid);
		#endif
		//wait
		int status;
		if (job->foreground == true) {
			foreground_job = job;
			waitpid(pid, &status, WUNTRACED);

			if (WIFEXITED(status)) {//regular termination
				killJob(job, 1);
			} else if (WIFSIGNALED(status)) {//ctrl+c, SIGINT
				killJob(job, 1);
			} else if (WIFSTOPPED(status)) {//ctrl+z, SIGTSTP
				job->status = 2;
				//job->foreground = false;
				add_to_table(job);
				recent_job = job;
				foreground_job = NULL;
			}
		} else {
			job->status = 1;
			//printf("%d\n", job->status);
			add_to_table(job);
			//printf("status %d pid %d pid %d\n", job->status, job->pgid, pid);
		}
	}

	if (proc->std_in != STDIN_FILENO) close(proc->std_in);
	if (proc->std_out != STDOUT_FILENO) close(proc->std_out);
	if (proc->std_err != STDERR_FILENO) close(proc->std_err);

	//job_to_fg(job);
}

/*This processes the command line input*/
void process_input(char* input) {
	//check if input is fg, bg, jobs, then handle accordingly
	if (!strcmp(input, "fg")) {
		job_to_fg();
		free(input);
		return;
	} else if (!strcmp(input, "bg")) {
		job_to_bg();
		free(input);
		return;
	} else if (!strcmp(input, "jobs")) {
		//update_table();
		print_dones();
		display_jobs();
		free(input);
		return;
	}

	//create a job for the command line input
	jobType *job = (jobType *) malloc(sizeof(jobType));
	//printf("malloced this address %p\n", job);
	memset(job, 0, sizeof(jobType));
	job->input = strdup(input);
	#ifdef DEBUG
		printf("%s\n", job->input);
	#endif
	job->foreground = true;

	//tokenize input
	//first count num of args
	char *cmdinput = strdup(input);
	int arg_num = 0;
	char* token = strtok(cmdinput, " ");
	while (token != NULL) {
		arg_num++;
		token = strtok(NULL, " ");
	}

	char *args[arg_num+1];//number of args + space for NULL
	//char **args = (char**) malloc(arg_num+1);
	cmdinput = strdup(input);
	args[0] = strtok(cmdinput, " ");
	#ifdef DEBUG
		printf("--- token: %s\n", args[0]);
	#endif
	for (int i = 1; i < arg_num; i++) {
		args[i] = strtok(NULL, " ");
		#ifdef DEBUG
			printf("--- token: %s\n", args[i]);
		#endif
	}
	args[arg_num] = NULL;
	//printf("--- arg num is %d\n", arg_num);

	//create a process based on command and its args
	//this process gets added to the job above
	//if there's two processes (bc pipe) then add both to the job above
	procType *proc = (procType *) malloc(sizeof(procType));
	//printf("malloced this address %p\n", proc);
	memset(proc, 0, sizeof(procType));
	proc->std_in = STDIN_FILENO;
	proc->std_out = STDOUT_FILENO;
	proc->std_err = STDERR_FILENO;
	
	//handle arguments with file redir/pipes/etc
	//then add processes to jobs accordingly
	char **proc_args = (char **) malloc(sizeof(char *) * (arg_num+1));
	//char **proc2_args;
	//procType *proc2;
	//printf("--- malloced arg array with size %d\n", arg_num+1);
	int proc_args_len = 0;
	int idx = 0;
	int job_idx = 0;//for piping
	while (idx < arg_num) {
		if (!strcmp(args[idx], ">")) {//file redirection
			idx++;//go to file name
			char* out = strdup(args[idx]);
			proc->std_out = open(out, O_WRONLY|O_CREAT|O_TRUNC, S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH);
			idx++;//skip past file name
		} else if (!strcmp(args[idx], "<")) {
			idx++;
			char* in = strdup(args[idx]);
			proc->std_in = open(in, O_RDONLY);
			idx++;
			if (proc->std_in < 0) {
				free(proc);
				free(proc_args);
				free(job);
				perror("open");
				return;
			}
		} else if (!strcmp(args[idx], "2>")) {
			idx++;
			char* err = strdup(args[idx]);
			proc->std_err = open(err, O_WRONLY|O_CREAT|O_TRUNC, S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH);
			idx++;
		} else if (!strcmp(args[idx], "|")) {//piping
			//the args after "|" will go to a new process
			proc->args = proc_args;
			job->list[job_idx] = proc;
			job_idx++;//this way the next process goes to the correct index
			proc_args_len = 0;
			proc_args = (char **) malloc(sizeof(char *) * (arg_num+1));//just use same size bc why not
			proc = (procType *) malloc(sizeof(procType));
			memset(proc, 0, sizeof(procType));
			proc->std_in = STDIN_FILENO;
			proc->std_out = STDOUT_FILENO;
			proc->std_err = STDERR_FILENO;
			idx++;//next command
		} else if (!strcmp(args[idx], "&")) {//background job
			job->foreground = false;
			idx++;
		} else {//normal arg
			proc_args[proc_args_len] = strdup(args[idx]);
			proc_args_len++;
			//printf("--- %d <= %d\n", proc_args_len, arg_num);
			proc_args[proc_args_len] = NULL;
			idx++;
		}
	}

	proc->args = proc_args;
	job->list[job_idx] = proc;//add process to job, job idx alleviates the process of adding 2 processes

	//change later
	//foreground_job = job;
	//recent_job = job;

	//execute
	if (job_idx == 0) {//one command
		execute_cmd(job);
	} else {//two commands
		execute_cmd2(job);
	}
	//move this stuff later when job control functionality is added, as some jobs might get 
	//backgrounded and therefore wouldn't get freed here
	/*free(job->list[0]->args);
	free(job->list[0]);
	if (job_idx == 1) {
		free(job->list[1]->args);
		free(job->list[1]);
	}
	free(job);*/
	//i think this one can stay here
	free(input);
}

static void sigint_handler(int signo) {
	if (!foreground_job) {
		return;
	} else {
		pid_t fpid = foreground_job->pgid;
		kill(-fpid, SIGINT);
	}
}

static void sigtstp_handler(int signo) {
	if (!foreground_job) {
		return;
	} else {
		pid_t fpid = foreground_job->pgid;
		kill(-fpid, SIGTSTP);
	}
}

static void sigchld_handler(int signo) {
	//printf("hi\n");
	while (1) {
		int status;
		pid_t fpid = waitpid(-1, &status, WNOHANG);
		if (fpid <= 0) break;
		//printf("hi\n");
		//handler code
		jobType *job = head;
		bool two_procs = false;
		bool found = false;
		if (job->list[1]) {
			two_procs = true;
			//printf("yes\n");
		} else {
			two_procs =false;
		}
		while (job) {
			//printf("hi\n");
			//printf("%d %d\n", job->pgid, fpid);
			if (job->pgid == fpid) {
				//printf("maybe 1-2 %d\n", job->pgid);
				found = true;
				break;
			}
			if (two_procs) {
				if (job->list[1]->pid == fpid) {
					//printf("two %d\n", job->list[1]->pid);
					found = true;
					break;
				}
			}
			job = job->next;
			if (job && job->list[1]) {
				two_procs = true;
				//printf("yes\n");
			} else {
				two_procs = false;
			}
		}

		if (found == false) {
			return;
		}

		if (two_procs == false) {
			if (WIFEXITED(status)) {//regular termination
				job->status = 3;
			} else if (WIFSIGNALED(status)) {//ctrl+c, SIGINT
				job->status = 3;
			} else if (WIFSTOPPED(status)) {//ctrl+z, SIGTSTP
				job->status = 2;
			} else if (WIFCONTINUED(status)) {
				job->status = 1;
			}
		} else {
			/*bool exited = false;
			bool signaled = false;
			bool stopped = false;
			bool cont = false;
			if (WIFEXITED(status)) {
				//printf("Hi\n");
				if (job->status == 3) {
					return;
				} else {
					exited = true;
				}
			} else if (WIFSIGNALED(status)) {
				if (job->status == 3) {
					return;
				} else {
					signaled = true;
				}
			} else if (WIFSTOPPED(status)) {
				if (job->status == 2) {
					return;
				} else {
					stopped = true;
				}
			} else if (WIFCONTINUED(status)) {
				if (job->status == 1) {
					return;
				} else {
					cont = true;
				}
			}*/
			int status2;
			int idx;
			if (job->list[0]->pid == fpid) {
				idx = 1;
			} else {
				idx = 0;
			}
			waitpid(job->list[idx]->pid, &status2, WNOHANG);

			if (WIFEXITED(status2)) {
				//printf("exit\n");
				job->status = 3;
			} else if (WIFSIGNALED(status2)) {
				job->status = 3;
			} else if (WIFSTOPPED(status2)) {
				job->status = 2;
			} else if (WIFCONTINUED(status2)) {
				job->status = 1;
			}
		}
	}
}

/*Initialize signals handlers here*/
void sig_init() {
	signal(SIGINT, sigint_handler);
	signal(SIGTSTP, sigtstp_handler);
	signal(SIGCHLD, sigchld_handler);
}

int main() {
	sig_init();

	shell_pid = getpid();
	if (setpgid(shell_pid, shell_pid) != 0) {
		perror("setpgid");
		exit(1);
	}

	//get input from the command line
	while (1) {
		char* input = NULL;
		//this apparently removes the newline automatically
		//make sure to free this at some point
		input = readline("# ");
		if (input == NULL) {//this means EOF so ctrl+D was pressed
			//need to do anything else? unsure
			exit(0);
		}

		size_t input_length = strlen(input);
		if (input_length <= 0) {//no input
			free(input);
			continue;
		}

		//process input
		process_input(input);
		print_dones();
		//update_table();
	}
}