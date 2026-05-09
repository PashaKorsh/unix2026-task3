#include <string.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/param.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define MAXPROC 64
#define MAXLINE 512

pid_t pid_list[MAXPROC];
char lines[MAXPROC][MAXLINE];
int pid_count = 0;
int nprocs = 0;

int log_fd;
char config_path[MAXLINE];
int got_hup = 0;

void die(const char *msg) {
    perror(msg);
    exit(1);
}

void log_write(const char *msg) {
    char buf[MAXLINE+64];
    int n = snprintf(buf, sizeof(buf), "%s\n", msg);
    write(log_fd, buf, n);
}

void hup_handler(int sig) {
    got_hup = 1;
}

pid_t start_child(int p)
{
    int fd_in, fd_out, n;
    char tmp[MAXLINE];
    char *tokens[MAXLINE/2+1];
    char *stdin_file, *stdout_file, *tok;
    char msg[MAXLINE+32];
    pid_t cpid;

    n = 0;
    strncpy(tmp, lines[p], MAXLINE-1);
    tok = strtok(tmp, " \t\n");
    while (tok) { tokens[n++] = tok; tok = strtok(NULL, " \t\n"); }
    if (n < 3) return -1;

    stdin_file  = tokens[n-2];
    stdout_file = tokens[n-1];
    tokens[n-2] = NULL;

    if (tokens[0][0] != '/' || stdin_file[0] != '/' || stdout_file[0] != '/') {
        fprintf(stderr, "paths must be absolute\n");
        exit(1);
    }

    cpid = fork();
    if (cpid == -1) die("fork");

    if (cpid == 0) {
        fd_in  = open(stdin_file,  O_RDONLY|O_CREAT, 0644);
        if (fd_in < 0) die("open stdin");
        fd_out = open(stdout_file, O_WRONLY|O_CREAT|O_TRUNC, 0644);
        if (fd_out < 0) die("open stdout");
        dup2(fd_in,  0);
        dup2(fd_out, 1);
        close(fd_in);
        close(fd_out);
        execv(tokens[0], tokens);
        die("execv");
    }

    snprintf(msg, sizeof(msg), "started pid=%d: %s", cpid, tokens[0]);
    log_write(msg);
    return cpid;
}

void kill_all()
{
    int p, status;
    char msg[64];

    for (p = 0; p < nprocs; p++)
        if (pid_list[p] > 0)
            kill(pid_list[p], SIGTERM);

    for (p = 0; p < nprocs; p++) {
        if (pid_list[p] > 0) {
            waitpid(pid_list[p], &status, 0);
            snprintf(msg, sizeof(msg), "killed pid=%d", pid_list[p]);
            log_write(msg);
            pid_list[p] = 0;
        }
    }
}

void load_config()
{
    int fd, p;
    ssize_t r;
    char buf[MAXLINE*MAXPROC];
    char *line;

    nprocs = 0;
    pid_count = 0;

    fd = open(config_path, O_RDONLY);
    if (fd < 0) { log_write("cannot open config"); return; }
    r = read(fd, buf, sizeof(buf)-1);
    close(fd);
    buf[r] = '\0';

    line = strtok(buf, "\n");
    while (line && nprocs < MAXPROC) {
        if (strlen(line) > 1) {
            strncpy(lines[nprocs], line, MAXLINE-1);
            nprocs++;
        }
        line = strtok(NULL, "\n");
    }

    for (p = 0; p < nprocs; p++) {
        pid_list[p] = start_child(p);
        pid_count++;
    }
}

int main(int argc, char **argv)
{
    int fd, p;
    pid_t cpid;
    int status;
    struct rlimit flim;
    struct sigaction sa;
    char *log_file = "/tmp/myinit.log";
    char *pid_file = "/tmp/myinit.pid";
    char *message  = "myinit started\n";
    char pbuf[32];

    if (argc != 2) {
        fprintf(stderr, "Usage: %s config\n", argv[0]);
        exit(1);
    }
    if (argv[1][0] != '/') {
        fprintf(stderr, "config path must be absolute\n");
        exit(1);
    }
    strncpy(config_path, argv[1], MAXLINE-1);

    if (getppid() != 1)
    {
        signal(SIGTTOU, SIG_IGN);
        signal(SIGTTIN, SIG_IGN);
        signal(SIGTSTP, SIG_IGN);

        if (fork() != 0)
            exit(0);

        setsid();
    }

    getrlimit(RLIMIT_NOFILE, &flim);
    for (fd = 0; fd < (int)flim.rlim_max && fd < 1024; fd++)
        close(fd);

    if (chdir("/") != 0)
        return 1;

    log_fd = open(log_file, O_CREAT|O_TRUNC|O_WRONLY, 0600);
    write(log_fd, message, strlen(message));

    fd = open(pid_file, O_CREAT|O_TRUNC|O_WRONLY, 0644);
    write(fd, pbuf, snprintf(pbuf, sizeof(pbuf), "%d\n", getpid()));
    close(fd);

    sa.sa_handler = hup_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGHUP, &sa, NULL);

    load_config();

    while (1)
    {
        cpid = waitpid(-1, &status, 0);

        if (got_hup) {
            got_hup = 0;
            log_write("got SIGHUP, reloading config");
            kill_all();
            load_config();
            continue;
        }

        if (cpid <= 0)
            continue;

        for (p = 0; p < nprocs; p++)
        {
            if (pid_list[p] == cpid)
            {
                char msg[MAXLINE+64];
                if (WIFEXITED(status))
                    snprintf(msg, sizeof(msg), "child pid=%d exited code %d", cpid, WEXITSTATUS(status));
                else if (WIFSIGNALED(status))
                    snprintf(msg, sizeof(msg), "child pid=%d killed by signal %d", cpid, WTERMSIG(status));
                else
                    snprintf(msg, sizeof(msg), "child pid=%d stopped", cpid);
                log_write(msg);

                pid_list[p] = start_child(p);
                break;
            }
        }
    }

    return 0;
}
