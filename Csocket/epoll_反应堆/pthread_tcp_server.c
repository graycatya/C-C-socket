#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include "wrap.h"
#include <pthread.h>
typedef struct _info
{
	int cfd;
	struct sockaddr_in client;

}INFO;
void *client_fun(void *arg)
{
	INFO *info = (INFO *)arg;
	char recvbuf[1024]="";
	int ret = 0;
	char buf_ip[16]="";
	printf("client ip=%s port=%d\n",inet_ntop(AF_INET,
					&info->client.sin_addr.s_addr,buf_ip,sizeof(buf_ip)),
				ntohs(info->client.sin_port));
	while(1)
	{
		ret = Read(info->cfd,recvbuf,sizeof(recvbuf));
		if(ret < 0)
		{
			perror("");
			break;
		}
		else if( 0== ret )
		{
			sleep(1);
			printf("client close\n");
			break;
		}
		else
		{
			printf("%s\n",recvbuf);
			Write(info->cfd,recvbuf,ret);

		}


	}
	close(info->cfd);

}
int main(int argc, char const *argv[])
{
	pthread_attr_t attr;
	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr,PTHREAD_CREATE_DETACHED);
	int lfd = tcp4bind(8000,NULL);
	//设置端口复用
	int opt = 1;
	setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

	listen(lfd,128);
	struct sockaddr_in cliaddr;
	socklen_t len = sizeof(cliaddr);
	pthread_t pthid;
	INFO *info=NULL;
	while(1)
	{
		int cfd = Accept(lfd,(struct sockaddr*)&cliaddr,&len);
		info = (INFO *)malloc(sizeof(INFO));
		info->cfd = cfd;
		info->client = cliaddr;
		pthread_create(&pthid,&attr,client_fun,info);


	}

	return 0;
}
