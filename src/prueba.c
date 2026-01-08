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

// maximo numero de procesos que se pueden almacenar para jobs
#define MAX_JOBS 50

// variable global que almacena la entrada parseada
tline *line;

// se define la estructura de un job
typedef struct job
{
    int job_id;      // [1], [2], ...
    pid_t pid;       // PID real
    char cmd[1024];  // "sleep 10"
    char estado[15]; // "RUNNING" o "STOPPED"
} t_job;

// array que almacena todos los procesos en background y su contador inicializado a 0
t_job jobs[MAX_JOBS];
int job_count = 0;

// declarar funciones
void restaurar_fd(int in, int out, int err);
int aplicar_redirecciones();
void ejecutar_simple();
void ejecutar_pipeline();
void ejecutar_cd();
int comandovalido(char *cmd);
void ejecutar_jobs();

// buffer que almacenara la linea introducida por el usuario y necesaria para el comando jobs
char buffer[1024];

int status;

int fd_in;
int fd_out;
int fd_err;

// implementacion del main
int main(void)
{
    // duplicamos los descriptores de fichero por defecto
    fd_in = dup(STDIN_FILENO);
    fd_out = dup(STDOUT_FILENO);
    fd_err = dup(STDERR_FILENO);

    // se ignoran por defecto las señales ctrl+c, ctrl+\ y ctrl+z
    signal(SIGINT, SIG_IGN);
    signal(SIGQUIT, SIG_IGN);
    signal(SIGTSTP, SIG_IGN);

    // se muestra el prompt
    printf("msh> ");

    // bucle infinito hasta que se produzca una interrupcion
    while (fgets(buffer, sizeof(buffer), stdin))
    {
        // se tokeniza la entrada
        line = tokenize(buffer);
        // si no hay entrada se muestra ek prompt y se vuelve al comienzo del bucle
        if (!line)
        {
            printf("msh> ");
            continue;
        }

        // se aplican redirecciones de entrada,salida y error si son necesarias
        aplicar_redirecciones();

        // si solo hay un comando
        if (line->ncommands == 1)
        {
            // se ejecuta cd
            if (strcmp(line->commands[0].argv[0], "cd") == 0)
            {
                ejecutar_cd();
            }
            // se ejecuta jobs
            else if (strcmp(line->commands[0].argv[0], "jobs") == 0)
            {
                ejecutar_jobs();
            }
            // se ejecuta otro comando
            else
            {
                ejecutar_simple();
            }
        }
        // si hay varios comandos
        else
        {
            ejecutar_pipeline();
        }

        // se reestauran las salidas y entradas estandar y el manejo de señales
        restaurar_fd(fd_in, fd_out, fd_err);
        signal(SIGINT, SIG_IGN);
        signal(SIGQUIT, SIG_IGN);
        signal(SIGTSTP, SIG_IGN);
        // se vuelve a mostrar el prompt
        printf("msh> ");
    }

    return 0;
}

// implementacion de las funciones

void restaurar_fd(int in, int out, int err)
{
    // se restauran los descriptores de fichero por defecto
    dup2(in, STDIN_FILENO);
    dup2(out, STDOUT_FILENO);
    dup2(err, STDERR_FILENO);
}

int aplicar_redirecciones()
{
    int fd;

    if (line->redirect_input)
    {
        // se abre el descriptor de fichero del fichero de entrada
        fd = open(line->redirect_input, O_RDONLY);
        if (fd < 0)
        {
            perror("Error entrada");
            return -1;
        }
        // se establece ese fd como entrada estándar
        dup2(fd, STDIN_FILENO);
        close(fd);
    }

    if (line->redirect_output)
    {
        // se abre el descriptor de fichero del fichero de salida, y si no existe se crea
        fd = open(line->redirect_output, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0)
        {
            perror("Error salida");
            return -1;
        }
        // se establece ese fd como salida estándar
        dup2(fd, STDOUT_FILENO);
        close(fd);
    }

    if (line->redirect_error)
    {
        // se abre el descriptor de fichero del fichero de salida de error, y si no existe se crea
        fd = open(line->redirect_output, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0)
        {
            perror("Error error");
            return -1;
        }
        // se establece ese fd como salida estándar de error
        dup2(fd, STDERR_FILENO);
        close(fd);
    }

    return 0;
}

// algoritmo para ejecutar lineas con un solo comando
void ejecutar_simple()
{
    // variable que almacenara el pid del proceso hijo
    pid_t pid;

    // comprobar que el comando es valido
    if (comandovalido(line->commands[0].filename))
    {
        fprintf(stderr, "Comando no encontrado\n");
        return;
    }

    // se crea el proceso hijo
    pid = fork();

    // proceso hijo
    if (pid == 0)
    {
        // se establecen las acciones por defecto para las señales
        signal(SIGINT, SIG_DFL);
        signal(SIGQUIT, SIG_DFL);
        signal(SIGTSTP, SIG_DFL);
        // se ejecuta el comando
        execvp(line->commands[0].argv[0], line->commands[0].argv);
        perror("exec");
        exit(EXIT_FAILURE);
    }
    // proceso padre
    else if (pid > 0)
    {
        if (!line->background)
        {
            // si no hay background
            waitpid(pid, &status, WUNTRACED);

            //si el proceso fue pausado
            if (WIFSTOPPED(status))
            {
                //se restauran los fd
                restaurar_fd(fd_in, fd_out, fd_err);
                //se guarda toda la info del proceso en un job
                strcpy(jobs[job_count].cmd, buffer);
                strcpy(jobs[job_count].estado, "STOPPED");
                jobs[job_count].job_id = job_count;
                jobs[job_count].pid = pid;
                // se incrementa el contador de jobs
                job_count += 1;
                // se muestra por pantalla el pid
                printf("[PID %d]\n", pid);
            }
        }
        else
        {
            // si hay background
            // se restauran los fd
            restaurar_fd(fd_in, fd_out, fd_err);
            // se guarda toda la info del proceso en un job
            strcpy(jobs[job_count].cmd, buffer);
            strcpy(jobs[job_count].estado, "RUNNING");
            jobs[job_count].job_id = job_count;
            jobs[job_count].pid = pid;
            // se incrementa el contador de jobs
            job_count += 1;
            // se muestra por pantalla el pid
            fprintf(stdout, "[PID %d]\n", pid);
        }
    }
    else
    {
        perror("fork");
    }
}

// algoritmo para ejecutar lineas con varios comandos
void ejecutar_pipeline()
{
    // variable que almacenara el indice de comando
    int i;

    // se crean los ncmds - 1 pipes
    int pipes[line->ncommands - 1][2];

    // variable que almacenara el pid de cada comando
    pid_t pid;

    for (i = 0; i < line->ncommands - 1; i++)
    {
        // se inicializan los pipes
        pipe(pipes[i]);
    }

    for (i = 0; i < line->ncommands; i++)
    {
        // se crean tantos hijos como comandos haya
        pid = fork();

        // proceso hijo
        if (pid == 0)
        {
            // se establece la accion por defecto para las señales
            signal(SIGINT, SIG_DFL);
            signal(SIGQUIT, SIG_DFL);
            signal(SIGTSTP, SIG_DFL);

            // si el proceso no es el primero
            if (i > 0)
            {
                // se establece el extremo de lectura de su pipe como entrada estandar
                dup2(pipes[i - 1][0], STDIN_FILENO);
            }
            // si el proceso no es el ultimo
            if (i < line->ncommands - 1)
            {
                // se establece el extremo de escritura de su pipe como salida estandar
                dup2(pipes[i][1], STDOUT_FILENO);
            }

            // se cierran todos los pipes para el proceso hijo
            for (int j = 0; j < line->ncommands - 1; j++)
            {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }

            // se ejecuta el comando
            execvp(line->commands[i].argv[0], line->commands[i].argv);
            perror("exec");
            exit(EXIT_FAILURE);
        }
    }

    // se cierran todos los pipes para el padre
    for (i = 0; i < line->ncommands - 1; i++)
    {
        close(pipes[i][0]);
        close(pipes[i][1]);
    }

    if (!line->background)
    {
        for (i = 0; i < line->ncommands; i++)
        {
            // si no hay background
            waitpid(pid, &status, WUNTRACED);

            //si el comando fue pausado
            if (WIFSTOPPED(status))
            {
                // se restauran los fd
                restaurar_fd(fd_in, fd_out, fd_err);
                // se guarda toda la info del proceso en un job
                strcpy(jobs[job_count].cmd, buffer);
                strcpy(jobs[job_count].estado, "STOPPED");
                jobs[job_count].job_id = job_count;
                jobs[job_count].pid = pid;
                 // se incrementa el contador de jobs
                job_count += 1;
                // se muestra por pantalla el pid
                printf("[PID %d]\n", pid);
            }
        }
    }
    else
    {
        // si hay background
        // se restauran los fd
        restaurar_fd(fd_in, fd_out, fd_err);
        // se guarda toda la info del proceso en un job
        strcpy(jobs[job_count].cmd, buffer);
        strcpy(jobs[job_count].estado, "RUNNING");
        jobs[job_count].job_id = job_count;
        jobs[job_count].pid = pid;
        // se incrementa el contador de jobs
        job_count += 1;
        // se muestra por pantalla el pid
        fprintf(stdout, "[PID %d]\n", pid);
    }
}

void ejecutar_cd()
{
    // variable que almacenara la ruta a home
    char *ruta;
    // variable que almacemara el directorio de trabajo actual
    char cwd[512];

    // si solo hay un argumento, es decir el comando introducido es "cd"
    if (line->commands[0].argc == 1)
    {
        // se guarda en ruta la variable de entorno HOME, que almacena la ruta absoluta al directorio home
        ruta = getenv("HOME");
        if (!ruta)
        {
            fprintf(stderr, "HOME no definida\n");
            return;
        }
    }
    // si hay dos argumentos, es decir, el comando introducido es "cd ---------"
    else if (line->commands[0].argc == 2)
    {
        // se guarda en ruta el segundo argumento
        ruta = line->commands[0].argv[1];
    }
    else
    {
        fprintf(stderr, "Uso: cd [directorio]\n");
        return;
    }
    // se cambia el directorio actual por el directorio establecido en la variable ruta
    if (chdir(ruta) != 0)
    {
        perror("cd");
        return;
    }

    // se muestra por pantalla el directorio actuañ
    printf("Directorio actual: %s\n", getcwd(cwd, sizeof(cwd)));
}

int comandovalido(char *cmd)
{
    return (cmd == NULL);
}

void ejecutar_jobs()
{
    // se recorre el array de jobs
    for (int i = 0; i < job_count; i++)
    {
        // se muestra por pantalla el id del job y su comando correspondiente
        printf("[%d]\t%s\t%s\n", jobs[i].job_id, jobs[i].estado, jobs[i].cmd);
    }
}