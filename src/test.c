#include <stdio.h>
#include <unistd.h>
#include "parser.h"
#include <sys/wait.h>
#include <fcntl.h>

int main(void)
{
	char buf[1024];
	tline *line;

	printf("msh>");
	while (fgets(buf, 1024, stdin))
	{

		line = tokenize(buf);
		if (line == NULL)
		{
			continue;
		}

		if (line->background)
		{
			printf("comando a ejecutarse en background\n");
		}



		//ARRAY DE PIDS
		pid_t pid[line->ncommands];


		
		// CREAR LOS PIPES
		int array_pipes[line->ncommands - 1][2];
		if(line->ncommands>1){
			

			// INICIALIZAR LOS PIPES
			for (int i = 0; i < line->ncommands - 1; i++){
				pipe(array_pipes[i]);
			}
		}




		//CREAR LOS PROCESOS HIJOS
		for (int i = 0; i < line->ncommands; i++)
		{
			pid[i] = fork();

			if (pid[i] < 0)
			{
				fprintf(stderr, "Error en en el fork");
			}
			else if (pid[i] == 0)
			{
				if(i==0){
					if (line->redirect_input != NULL) // STDIN
					{
						int fdin = open(line->redirect_input, O_RDONLY);
						dup2(fdin, STDIN_FILENO);
					}
					if(line->ncommands>1){
						close(array_pipes[i][0]); //CERRAR EL PRIMER PIPE PARA LECTURA
						dup2(array_pipes[i][1], STDOUT_FILENO);	//REDIRIGIR LA SALIDA ESTANDAR A LA POSICION DE ESCRITURA DEL PRIMER PIPE
					}
				}else if(i==line->ncommands-1){
					if (line->redirect_output != NULL) // STDOUT
					{
						int fdout = open(line->redirect_output, O_WRONLY);
						dup2(fdout, STDOUT_FILENO);
					}
					if (line->redirect_error != NULL) // STDERR
					{
						int fderr = open(line->redirect_error, O_WRONLY);
						dup2(fderr, STDERR_FILENO);
					}

					close(array_pipes[i-1][1]);	//CERRAR EL ULTIMO PIPE PARA ESCRITURA
					dup2(array_pipes[i-1][0], STDIN_FILENO);

				}else{
					close(array_pipes[i-1][1]);
					close(array_pipes[i][0]);
					dup2(array_pipes[i-1][0], STDIN_FILENO);
					dup2(array_pipes[i][1], STDOUT_FILENO);
				}


				execvp(line->commands[i].filename, line->commands[i].argv);
				printf("SIGGO VIVO");
			}
		}
		for (int i = 0; i < line->ncommands; i++)
		{
			wait(NULL);
		}
		dup2(STDIN_FILENO, STDERR_FILENO);
		printf("msh>");
	}
	return 0;
}