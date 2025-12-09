#include <stdio.h>
#include <unistd.h>
#include "parser.h"
#include <sys/wait.h>
#include <fcntl.h>
#include <stdlib.h>

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

		int ncmds = line->ncommands;
		pid_t pid[ncmds];

		int npipes = ncmds - 1;
		int pipes[npipes][2];
		for (int i = 0; i < npipes; i++)
		{
			if (pipe(pipes[i]) < 0)
			{
				perror("pipe");
				return EXIT_FAILURE;
			}
		}

		for (int i = 0; i < ncmds; i++)
		{
			pid[i] = fork();
			if (pid[i] < 0)
			{
				perror("fork");
				return EXIT_FAILURE;
			}
			else if (pid[i] == 0)
			{
				if (i == 0 && line->redirect_input != NULL)
				{
					int fdin = open(line->redirect_input, O_RDONLY);
					if (fdin < 0 || dup2(fdin, STDIN_FILENO) < 0)
					{
						perror("redirect input");
						_exit(EXIT_FAILURE);
					}
					close(fdin);
				}

				if (i == ncmds - 1)
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

				for (int j = 0; j < npipes; j++)
				{
					close(pipes[j][0]);
					close(pipes[j][1]);
				}

				execvp(line->commands[i].filename, line->commands[i].argv);
				perror("execvp");
				_exit(EXIT_FAILURE);
			}
		}

		for (int i = 0; i < npipes; i++)
		{
			close(pipes[i][0]);
			close(pipes[i][1]);
		}

		for (int i = 0; i < ncmds; i++)
		{
			waitpid(pid[i], NULL, 0);
		}

		printf("msh>");
	}
	return 0;
}
