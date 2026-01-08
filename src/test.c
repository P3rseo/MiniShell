#include <stdio.h>
#include <unistd.h>
#include "parser.h"
#include <sys/wait.h>
#include <fcntl.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>

int main(void)
{


    char buf[1024];
    tline *line;

    printf("msh> ");
    while (fgets(buf, 1024, stdin))
    {

        line = tokenize(buf);
        if (line == NULL)
        {
            continue;
        }

        // Crear variable con el numero de comandos. (Linea 47)
        int ncmds = line->ncommands;
        // Crear tantos PIDs como comandos haya.
        pid_t pid[ncmds];

        // Crear el numero de pipes necesarios.
        int npipes = ncmds - 1;
        int pipes[npipes][2];

        // Abrimos los pipes con un bucle.
        for (int i = 0; i < npipes; i++)
        {
            if (pipe(pipes[i]) < 0)
            {
                perror("pipe");
                return EXIT_FAILURE;
            }
        }

        for (int i = 0; i < ncmds; i++) // Si usamos line->ncommands da error por que comparamos un int con un puntero.
        {
            pid[i] = fork(); // Creamos proceso
            if (pid[i] < 0)
            {
                perror("fork");
                return EXIT_FAILURE;
            }
            else if (pid[i] == 0)
            {
                if (i == 0 && line->redirect_input != NULL) // Primer comando, con redireccion de entrada
                {
                    int fdin = open(line->redirect_input, O_RDONLY);
                    if (fdin < 0 || dup2(fdin, STDIN_FILENO) < 0)
                    {
                        perror("redirect input");
                        _exit(EXIT_FAILURE);
                    }
                    close(fdin);
                }

                if (i == ncmds - 1) // Ultimo comando, con redireccion de salida y redireccion de error
                {
                    if (line->redirect_output != NULL)
                    {
                        int fdout = open(line->redirect_output, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                        if (fdout < 0 || dup2(fdout, STDOUT_FILENO) < 0)
                        {
                            perror("redirect output");
                            _exit(EXIT_FAILURE);
                        }
                        close(fdout);
                    }
                    if (line->redirect_error != NULL)
                    {
                        int fderr = open(line->redirect_error, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                        if (fderr < 0 || dup2(fderr, STDERR_FILENO) < 0)
                        {
                            perror("redirect error");
                            _exit(EXIT_FAILURE);
                        }
                        close(fderr);
                    }
                }

                if (i > 0)
                {
                    if (dup2(pipes[i - 1][0], STDIN_FILENO) < 0)
                    {
                        perror("dup2 stdin");
                        _exit(EXIT_FAILURE);
                    }
                }
                if (i < ncmds - 1)
                {
                    if (dup2(pipes[i][1], STDOUT_FILENO) < 0)
                    {
                        perror("dup2 stdout");
                        _exit(EXIT_FAILURE);
                    }
                }

                for (int j = 0; j < npipes; j++) // Cerrar pipes que no se van a usar.
                {
                    close(pipes[j][0]);
                    close(pipes[j][1]);
                }

                execvp(line->commands[i].filename, line->commands[i].argv);
                perror("execvp");
                _exit(EXIT_FAILURE);
            }
        }

        signal(SIGCHLD, SIG_IGN);
        signal(SIGQUIT, SIG_IGN);

        for (int i = 0; i < npipes; i++) // Cerrar los pipes ya usados.
        {
            close(pipes[i][0]);
            close(pipes[i][1]);
        }

        if (!line->background)
        {
            for (int i = 0; i < ncmds; i++)
                waitpid(pid[i], NULL, 0);
        }
        else
        {
            printf("[PID %d]\n", pid[ncmds - 1]);
        }

        printf("msh> ");
    }
    return 0;
}
