#pragma once

#include <unistd.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * This file defines the structure for creating
 * UNIX processes. Since the popen() and pclose()
 * provides too little capacities for controlling.
 */

// Defines the process creation mode.
typedef enum procmode {
	// Please set the mode to PROC_NONE if no higher
	// flags should be used.
	PROC_NONE	= 0,

	// The PROC_PXX will force create a pipe on the
	// file descriptor. When corresponding flag not
	// set, it will scan the descriptors list provided
	// and replace it at desired position.
	PROC_PIN	= 1 << 0,
	PROC_POUT	= 1 << 1,
	PROC_PERR	= 1 << 2,

	// Do not auto-append the process path at the start
	// of the arguments.
	PROC_NOPATH	= 1 << 3,

	// The PROC_RXX indicates the subprocess owns the pipe
	// and auto-close the pipe on closed.
	// Should not be used with the corresponding PROC_PXX 
	// and will get errno == EINVAL as regarded as invalid
	// input value.
	PROC_RIN	= 1 << 4,
	PROC_ROUT	= 1 << 5,
	PROC_RERR	= 1 << 6,
} procmode_t;

// Defines the metadata for creating the process.
// Including the executable file path, the arguments,
// and the environment. 
typedef struct procinfo {
	// The actual path for creating the process.
	const char* path;

	// The arguments for creating the process.
	// The path will be automatically append to
	// the front of args if PROC_NOPATH is not
	// set.
	int argv; const char** args;

	// The environments for creating the process.
	// The process environments will be inherited.
	int envs; const char** envp;

	// See also the enum above.
	procmode_t mode;
} procinfo_t;

// Defines the process control block. Will clean up
// after process kill.
typedef struct proc { 
	int pin, pout, perr;
	int retval;
	char pcb[32];
} proc_t;

// Create an process and execute it.
// The fnum indicates the scanning size of the fds,
// entries in fds which is not -1 will be mapped to
// specific value.
// If PROC_PXX is set, the data in fds will be ignored
// and set to the created pipe.
int proc_fork(proc_t* proc, 
	procinfo_t* pinfo, 
	int fnum, int* fds);

// Wait for process to finish execution and cause
// pipes to flushes.
// Please notice when calling such method, the write
// side of the input pipe will be closed, if any.
// Will not return any error even if failed.
void proc_join(proc_t*);

// Send signal to certain process.
int proc_kill(proc_t*, int);

// Retrieve the current PID from the process.
pid_t proc_pid(proc_t*);

#ifdef __cplusplus
}
#endif
