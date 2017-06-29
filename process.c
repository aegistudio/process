#define _GNU_SOURCE
#include "unistd.h"

#include "sys/types.h"
#include "sys/wait.h"
#include "sys/stat.h"

#include "process.h"
#include "fcntl.h"
#include "errno.h"

#include "stdlib.h"
#include "string.h"

const int kValidMagic = 0x0eadbeef;
const int kPipeMagic  = 0x0eefdead;
const int kSizeString = sizeof(char* const);

// Side read and side write.
const int sR = 0; const int sW = 1;

// The masked pcb block inside the proc_t struct.
typedef struct {
	int magic;	// The valid magic number.
	int pipeMagic;	// The pipe created magic.
	pid_t pid;	// The process PID.

	int sRpin;	// The read side for pin, close to cause EOF.
	int sWpout;	// The write side for pout, close to cause EOF.
	int sWperr;	// The write side for perr, close to cause EOF.
} proccb_t;

// Implementation for the process creation function.
int proc_fork(proc_t* proc, 
	procinfo_t* pinfo,
	int fnum, int* fds) {

	if(proc == NULL) {  errno = EINVAL; return -1; }
	if(pinfo == NULL) { errno = EINVAL; return -1; }
	if((pinfo -> mode & PROC_RIN) && (pinfo -> mode & PROC_PIN)) {
		errno = EINVAL;	return -1; }
	if((pinfo -> mode & PROC_ROUT) && (pinfo -> mode & PROC_POUT)) {
		errno = EINVAL;	return -1; }
	if((pinfo -> mode & PROC_RERR) && (pinfo -> mode & PROC_PERR)) {
		errno = EINVAL;	return -1; }

	// Blanking data.
	proc -> retval = -1;
	proccb_t* pcb = (proccb_t*)proc -> pcb;
	if(pcb -> magic == kValidMagic) { errno = EAGAIN; return -1; }
	if(pcb -> pipeMagic == kPipeMagic) { errno = EAGAIN; return -1; }

	pcb -> magic = pcb -> pipeMagic = 0;
	pcb -> pid = 0;	pcb -> sWpout = pcb -> sWperr = -1;

	proc -> pin  = -1;
	int pins [2]; pins [sR] = -1;
	proc -> pout = -1;
	int pouts[2]; pouts[sW] = -1;
	proc -> perr = -1;
	int perrs[2]; perrs[sW] = -1;
	pcb -> pipeMagic = kPipeMagic;

	if(fnum >= 1) {
		pins [sR] = fds[0];
		if(pinfo -> mode & PROC_RIN)
			pcb -> sRpin = fds[0];
	}
	if(fnum >= 2) {
		pouts[sW] = fds[1];
		if(pinfo -> mode & PROC_ROUT)
			pcb -> sWpout = fds[1];
	}
	if(fnum >= 3) {
		perrs[sW] = fds[2];
		if(pinfo -> mode & PROC_RERR)
			pcb -> sWperr = fds[2];
	}

	// Creation of pipes.
	if(pinfo -> mode & PROC_PIN) {
		if(pipe2(pins, O_CLOEXEC)) {
			proc_join(proc);
			return -1;
		}
		pcb -> sRpin = pins[sR];
		proc -> pin = pins[sW];
		if(fnum >= 1) fds[0] = pins[sW];
	}

	if(pinfo -> mode & PROC_POUT) {
		if(pipe2(pouts, O_CLOEXEC)) {
			proc_join(proc);
			return -1;
		}
		proc -> pout = pouts[sR];
		pcb -> sWpout = pouts[sW];
		if(fnum >= 2) fds[1] = pouts[sR];
	}

	if(pinfo -> mode & PROC_PERR) {
		if(pipe2(perrs, O_CLOEXEC)) {
			proc_join(proc);
			return -1;
		}
		proc -> perr = perrs[sR];
		pcb -> sWperr = perrs[sW];
		if(fnum >= 3) fds[2] = perrs[sR];
	}

	// Fork and create process.
	pcb -> pid = fork();
	if(pcb -> pid == 0) {
		// Child process.
		// Open the read write file descriptor.
		int devNULL = open("/dev/null", O_RDWR);
		if(devNULL == -1) exit(-1);

		// Execution process.
		// Lets initialize the file descriptors.
		if(pins [sR] != 0) if(dup2(pins [sR] >= 0? 
			pins [sR] : devNULL, 0) == -1) exit(-1);
		if(pouts[sW] != 1) if(dup2(pouts[sW] >= 0? 
			pouts[sW] : devNULL, 1) == -1) exit(-1);
		if(perrs[sW] != 2) if(dup2(perrs[sW] >= 0? 
			perrs[sW] : devNULL, 2) == -1) exit(-1);

		{ int i; for(i = 3; i < fnum; i ++)
			if(dup2(fds[i], i)) exit(-1); }
		
		// Prepare the execution parameters.
		const char** args = NULL;
		const char** argst = NULL;
		int argv = pinfo -> argv;
		if(pinfo -> mode & PROC_NOPATH) {
			args = (const char**)malloc(kSizeString * (1 + argv));
			if(args == NULL) exit(-1);
			argst = args;
		}
		else {
			args = (const char**)malloc(kSizeString * (2 + argv));
			if(args == NULL) exit(-1);
			args[0] = pinfo -> path;
			argst = &args[1];
		}
		memcpy(argst, pinfo -> args, kSizeString * argv);
		argst[argv] = (const char*)NULL;

		// Copy environments if necessary, and execute.
		int envs = pinfo -> envs;
		if(envs > 0) {
			const char* *envp = (const char**)malloc(kSizeString * envs + 1);
			if(envp == NULL) exit(-1);
			memcpy(envp, pinfo -> envp, kSizeString * envs);
			envp[envs] = (const char*)NULL;
			exit(execvpe(pinfo -> path, (char* const*)args, (char* const*)envp));
		}
		else {
			exit(execv(pinfo -> path, (char* const*)args));
		}
	}
	else if(pcb -> pid > 0) {
		// Parent process.
		pcb -> magic = kValidMagic;
		return 0;
	}
	else {
		proc_join(proc);
		return -1;
	}
}

// Implementation for the process clean-up function.
void proc_join(proc_t* proc) {
	if(proc == NULL) return;
	proccb_t* pcb = (proccb_t*)proc -> pcb;

	// Wait for the child process to join.
	if(pcb -> magic == kValidMagic && pcb -> pid > 0) {
		waitpid(pcb -> pid, &(proc -> retval), 0);
		pcb -> magic = 0;
		pcb -> pid = -1;
	}

	// Close open pipes.
	if(pcb -> pipeMagic == kPipeMagic) {
		if(pcb -> sRpin  >= 0) { close(pcb -> sRpin ); pcb -> sRpin  = -1; }
		if(pcb -> sWpout >= 0) { close(pcb -> sWpout); pcb -> sWpout = -1; }
		if(pcb -> sWperr >= 0) { close(pcb -> sWperr); pcb -> sWperr = -1; }
		pcb -> pipeMagic = 0;
	}
}

// Implementation for the signal sending function.
int proc_kill(proc_t* proc, int signum) {
	if(proc == NULL) { errno = EINVAL; return -1; }
	proccb_t* pcb = (proccb_t*)proc -> pcb;
	
	// Set no child process.
	if(pcb -> magic != kValidMagic) { errno = ECHILD; return -1;}

	return kill(pcb -> pid, signum);
}

// Implementation for retriving PID.
pid_t proc_pid(proc_t* proc) {
	if(proc == NULL) return -1;
	proccb_t* pcb = (proccb_t*)proc -> pcb;
	if(pcb -> magic != kValidMagic) return -1;
	return pcb -> pid;
}
