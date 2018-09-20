/* ************************************************************************
 *       Filename:  pipe_epoll.c
 *    Description:  
 *        Version:  1.0
 *        Created:  2018年06月16日 11时44分19秒
 *       Revision:  none
 *       Compiler:  gcc
 *         Author:  YOUR NAME (), 
 *        Company:  
 * ************************************************************************/


#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <string.h>
#include <sys/epoll.h>

int main(int argc, char *argv[])
{
	int fd[2];
	pipe(fd);
	pid_t pid = fork();
	if(pid < 0)
		perror("");
	else if( 0 == pid)//son process
	{
		close(fd[0]);
		char buf[6]="";
		char ch = 'a';
		while(1)
		{
			memset(buf,ch++,6);
			write(fd[1],buf,6);
			sleep(1);
		}
	}
	else
	{
		close(fd[1]);
		int epfd = epoll_create(1);
		struct epoll_event ev ,evs[10];
		ev.events = EPOLLIN;
		ev.data.fd = fd[0];
		epoll_ctl(epfd,EPOLL_CTL_ADD,fd[0],&ev);
		while(1)
		{
			int n = epoll_wait(epfd,evs,10,-1);
			if(n > 0)
			{
				char buf[10]="";
				read(evs[0].data.fd,buf,10);

				printf("buf=%s\n",buf);
			
			}
		
		}


	
	
	}
	return 0;
}

