#include "GuardSupport.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
extern char **environ;
static volatile sig_atomic_t interrupted;
static void interrupt_handler(int sig) { (void)sig; interrupted = 1; }
static void pause_briefly(void) { struct timespec ts = {0, 20000000}; nanosleep(&ts, NULL); }
int main(int argc, char **argv) {
    if (argc != 1) { fprintf(stderr, "No options or arbitrary worker path accepted.\n"); return 2; }
    struct sigaction sa = {0}; sa.sa_handler = interrupt_handler; sigemptyset(&sa.sa_mask);
    if (sigaction(SIGINT, &sa, NULL) || sigaction(SIGTERM, &sa, NULL)) return 2;
    signal(SIGPIPE, SIG_IGN);
    if (!g0_ac_connected() || !g0_lid_open()) { fprintf(stderr,"AC power and open lid required.\n"); return 2; }
    char worker[PATH_MAX];
    if (!realpath(argv[0], worker)) { perror("realpath"); return 2; }
    char *slash = strrchr(worker, '/');
    if (!slash || (size_t)(slash-worker) + sizeof("/g614jvr-gpi0-worker") > sizeof(worker)) return 2;
    strcpy(slash, "/g614jvr-gpi0-worker");
    struct stat st;
    if (stat(worker, &st) || !S_ISREG(st.st_mode) || (st.st_mode & (S_IWGRP|S_IWOTH))) {
        fprintf(stderr,"Worker must be a non-group/world-writable regular file.\n"); return 2;
    }
    int guard_pipe[2];
    if (pipe(guard_pipe)) return 2;
    CFMutableDictionaryRef props = CFDictionaryCreateMutable(kCFAllocatorDefault, 0,
        &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    int level = kIOPMAssertionLevelOn, seconds = G0_GUARD_MAX_SECONDS;
    CFNumberRef level_num = CFNumberCreate(NULL,kCFNumberIntType,&level);
    CFNumberRef timeout_num = CFNumberCreate(NULL,kCFNumberIntType,&seconds);
    if (!props || !level_num || !timeout_num) return 2;
    CFDictionarySetValue(props,kIOPMAssertionTypeKey,kIOPMAssertionTypePreventUserIdleSystemSleep);
    CFDictionarySetValue(props,kIOPMAssertionLevelKey,level_num);
    CFDictionarySetValue(props,kIOPMAssertionNameKey,CFSTR("G614JVR G0 supervisor (30s maximum)"));
    CFDictionarySetValue(props,kIOPMAssertionTimeoutKey,timeout_num);
    CFDictionarySetValue(props,kIOPMAssertionTimeoutActionKey,kIOPMAssertionTimeoutActionRelease);
    IOPMAssertionID assertion = kIOPMNullAssertionID;
    IOReturn rc = IOPMAssertionCreateWithProperties(props,&assertion);
    CFRelease(props); CFRelease(level_num); CFRelease(timeout_num);
    if (rc != kIOReturnSuccess) { close(guard_pipe[0]); close(guard_pipe[1]); fprintf(stderr,"Sleep guard creation failed: 0x%x\n",rc); return 2; }
    const double start = g0_now();
    fprintf(stderr,"DO NOT CLOSE LID OR MANUALLY SLEEP DURING OBSERVATION\nGUARD_PID=%d GUARD_MAX_LIFETIME=30s\n",getpid());
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addclose(&actions,guard_pipe[1]);
    if (guard_pipe[0] != G0_GUARD_FD) {
        posix_spawn_file_actions_adddup2(&actions,guard_pipe[0],G0_GUARD_FD);
        posix_spawn_file_actions_addclose(&actions,guard_pipe[0]);
    }
    posix_spawnattr_t attr; posix_spawnattr_init(&attr);
    sigset_t defaults; sigemptyset(&defaults); sigaddset(&defaults,SIGINT); sigaddset(&defaults,SIGTERM); sigaddset(&defaults,SIGPIPE);
    posix_spawnattr_setsigdefault(&attr,&defaults);
    posix_spawnattr_setflags(&attr,POSIX_SPAWN_SETSIGDEF);
    pid_t child = -1; char *child_argv[] = {worker,NULL};
    int spawn_error = interrupted ? EINTR : posix_spawn(&child,worker,&actions,&attr,child_argv,environ);
    posix_spawn_file_actions_destroy(&actions); posix_spawnattr_destroy(&attr); close(guard_pipe[0]);
    if (spawn_error) { close(guard_pipe[1]); IOPMAssertionRelease(assertion); fprintf(stderr,"Worker spawn failed: %s\n",strerror(spawn_error)); return 2; }
    G0GuardToken token = {G0_GUARD_MAGIC,assertion,getpid(),0,start+G0_WORKER_MAX_SECONDS};
    ssize_t sent = write(guard_pipe[1],&token,sizeof(token)); close(guard_pipe[1]);
    int status = 0, reaped = 0, success = 0, stopping = sent != sizeof(token);
    double grace_end = stopping ? g0_now()+G0_CLEANUP_GRACE_SECONDS : 0;
    if (stopping) kill(child,SIGTERM);
    for (;;) {
        const double now = g0_now();
        if (!reaped) {
            pid_t got = waitpid(child,&status,WNOHANG);
            if (got == child) {
                reaped = 1; success = WIFEXITED(status) && WEXITSTATUS(status)==0;
                if (success && !stopping) break;
                if (!stopping) { stopping=1; grace_end=now+G0_CLEANUP_GRACE_SECONDS; }
            } else if (got < 0 && errno != EINTR) {
                reaped=1; if (!stopping) { stopping=1; grace_end=now+G0_CLEANUP_GRACE_SECONDS; }
            }
        }
        if (!stopping && (interrupted || now-start >= G0_WORKER_MAX_SECONDS)) {
            stopping=1; grace_end=now+G0_CLEANUP_GRACE_SECONDS; if (!reaped) kill(child,SIGTERM);
        }
        if ((stopping && now >= grace_end) || now-start >= G0_GUARD_MAX_SECONDS-1) {
            if (!reaped) { kill(child,SIGKILL); (void)waitpid(child,&status,WNOHANG); }
            success=0; break;
        }
        pause_briefly();
    }
    IOPMAssertionRelease(assertion);
    fprintf(stderr,"GUARD_RELEASED=YES DURATION_SECONDS=%.3f WORKER_SUCCESS=%s\n",g0_now()-start,success?"YES":"NO");
    return success ? 0 : 1;
}
