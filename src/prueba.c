#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include "parser.h"

#define PROMPT "msh> "
#define CMD_CD "cd"

/* Variable global requerida por el parser */
tline *parsed_line;

/* Prototipos */
static void restaurar_fd(int in, int out, int err);
static int aplicar_redirecciones(void);
static void ejecutar_simple(void);
static void ejecutar_pipeline(void);
static void ejecutar_cd(void);
static int comando_invalido(char *cmd);

/* ========================= MAIN ========================= */

int main(void) {
    char buffer[1024];

    int fd_in  = dup(STDIN_FILENO);
    int fd_out = dup(STDOUT_FILENO);
    int fd_err = dup(STDERR_FILENO);

    signal(SIGINT, SIG_IGN);
    signal(SIGQUIT, SIG_IGN);

    printf(PROMPT);

    while (fgets(buffer, sizeof(buffer), stdin)) {

        parsed_line = tokenize(buffer);
        if (!parsed_line) {
            printf(PROMPT);
            continue;
        }

        aplicar_redirecciones();

        if (parsed_line->background) {
            fprintf(stderr, "Ejecución en background no soportada\n");
            restaurar_fd(fd_in, fd_out, fd_err);
            printf(PROMPT);
            continue;
        }

        if (parsed_line->ncommands == 1) {
            if (strcmp(parsed_line->commands[0].argv[0], CMD_CD) == 0) {
                ejecutar_cd();
            } else {
                ejecutar_simple();
            }
        } else {
            ejecutar_pipeline();
        }

        restaurar_fd(fd_in, fd_out, fd_err);
        printf(PROMPT);
    }

    return 0;
}

/* ========================= FUNCIONES ========================= */

static void restaurar_fd(int in, int out, int err) {
    dup2(in, STDIN_FILENO);
    dup2(out, STDOUT_FILENO);
    dup2(err, STDERR_FILENO);
}

static int aplicar_redirecciones(void) {
    int fd;

    if (parsed_line->redirect_input) {
        fd = open(parsed_line->redirect_input, O_RDONLY);
        if (fd < 0) {
            perror("Error entrada");
            return -1;
        }
        dup2(fd, STDIN_FILENO);
        close(fd);
    }

    if (parsed_line->redirect_output) {
        fd = creat(parsed_line->redirect_output, 0666);
        if (fd < 0) {
            perror("Error salida");
            return -1;
        }
        dup2(fd, STDOUT_FILENO);
        close(fd);
    }

    if (parsed_line->redirect_error) {
        fd = creat(parsed_line->redirect_error, 0666);
        if (fd < 0) {
            perror("Error error");
            return -1;
        }
        dup2(fd, STDERR_FILENO);
        close(fd);
    }

    return 0;
}

static void ejecutar_simple(void) {
    pid_t pid;
    int status;

    if (comando_invalido(parsed_line->commands[0].filename)) {
        fprintf(stderr, "Comando no encontrado\n");
        return;
    }

    signal(SIGINT, SIG_DFL);
    signal(SIGQUIT, SIG_DFL);

    pid = fork();

    if (pid == 0) {
        execvp(parsed_line->commands[0].argv[0],
               parsed_line->commands[0].argv);
        perror("exec");
        exit(EXIT_FAILURE);
    } else if (pid > 0) {
        waitpid(pid, &status, 0);
    } else {
        perror("fork");
    }
}

static void ejecutar_pipeline(void) {
    int i;
    int pipes[parsed_line->ncommands - 1][2];
    pid_t pid;

    for (i = 0; i < parsed_line->ncommands - 1; i++) {
        pipe(pipes[i]);
    }

    for (i = 0; i < parsed_line->ncommands; i++) {

        pid = fork();

        if (pid == 0) {

            signal(SIGINT, SIG_DFL);
            signal(SIGQUIT, SIG_DFL);

            if (i > 0) {
                dup2(pipes[i - 1][0], STDIN_FILENO);
            }
            if (i < parsed_line->ncommands - 1) {
                dup2(pipes[i][1], STDOUT_FILENO);
            }

            for (int j = 0; j < parsed_line->ncommands - 1; j++) {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }

            execvp(parsed_line->commands[i].argv[0],
                   parsed_line->commands[i].argv);
            perror("exec");
            exit(EXIT_FAILURE);
        }
    }

    for (i = 0; i < parsed_line->ncommands - 1; i++) {
        close(pipes[i][0]);
        close(pipes[i][1]);
    }

    for (i = 0; i < parsed_line->ncommands; i++) {
        wait(NULL);
    }
}

static void ejecutar_cd(void) {
    char *destino;
    char cwd[512];

    if (parsed_line->commands[0].argc == 1) {
        destino = getenv("HOME");
        if (!destino) {
            fprintf(stderr, "HOME no definida\n");
            return;
        }
    } else if (parsed_line->commands[0].argc == 2) {
        destino = parsed_line->commands[0].argv[1];
    } else {
        fprintf(stderr, "Uso: cd [directorio]\n");
        return;
    }

    if (chdir(destino) != 0) {
        perror("cd");
        return;
    }

    printf("Directorio actual: %s\n", getcwd(cwd, sizeof(cwd)));
}

static int comando_invalido(char *cmd) {
    return (cmd == NULL);
}
