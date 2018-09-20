
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <sys/wait.h>

#include <fcntl.h>
#include <sys/time.h>
#include <sys/time.h>

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <pthread.h>

#include "poolsocket.h"
#include "socketlog.h"

//Socket连接池结构
typedef struct _SockePoolHandle
{
	int *			fdArray;			//Socket连接池
	int	*			statusArray;		//每条连接的状态  eg: statusArray[0] =  1表示 链接有效 statusArray[0] =  0表示 链接无效
	int				valid;				//Socket有效连接数目 
	int				nvalid;				//Socket无效连接数目 
	int				bounds;				//Socket连接池的容量

	char 			serverip[128];
	int 			serverport;

	int 			connecttime;
	int 			sendtime;
	int 			revtime;
	int				sTimeout; //没有连接时，等待之间
	pthread_mutex_t foo_mutex;
	//判断连接池是否已经终止
	int				terminated;  //1已经终止 0没有终止
}SockePoolHandle;


//客户端 socket池初始化
int sckCltPool_init(void **handle, SCKClitPoolParam *param)
{
	int		ret = 0, i = 0;

	SockePoolHandle *hdl = NULL;

	//初始化 句柄
	hdl = (SockePoolHandle *)malloc(sizeof(SockePoolHandle));
	if (hdl == NULL)
	{
		ret = Sck_ErrMalloc;
		Socket_Log(__FILE__, __LINE__, SocketLevel[4], ret, "func sckCltPool_init(), check malloc err");
		return ret;
	}
	memset(hdl, 0, sizeof(hdl));

	strcpy(hdl->serverip, param->serverip);
	hdl->serverport = param->serverport;

	hdl->connecttime = param->connecttime;
	hdl->sendtime = param->sendtime;
	hdl->revtime = param->revtime;

	//处理连接数
	hdl->bounds = param->bounds;
	hdl->valid = 0;
	hdl->nvalid = param->bounds;
	hdl->sTimeout = 1;

	pthread_mutex_init(&(hdl->foo_mutex), NULL);
	pthread_mutex_lock(&(hdl->foo_mutex)); //流程加锁


	//为连接句柄分配内存
	hdl->fdArray = (int *)malloc(hdl->bounds * sizeof(int));
	if (hdl->fdArray == NULL)
	{
		ret = Sck_ErrMalloc;
		Socket_Log(__FILE__, __LINE__, SocketLevel[4], ret, "func sckCltPool_init(), check malloc err");
		goto END;
	}
	hdl->statusArray = (int *)malloc(hdl->bounds * sizeof(int));
	if (hdl->statusArray == NULL)
	{
		ret = Sck_ErrMalloc;
		Socket_Log(__FILE__, __LINE__, SocketLevel[4], ret, "func sckCltPool_init(), check malloc err");
		goto END;
	}

	ret = sckClient_init();
	if (ret != 0)
	{
		printf("func sckClient_init() err:%d\n", ret);
		goto END;
	}

	for (i = 0; i < hdl->bounds; i++)
	{
		ret = sckClient_connect(hdl->serverip, hdl->serverport, hdl->connecttime, &(hdl->fdArray[i]));
		if (ret != 0)
		{
			Socket_Log(__FILE__, __LINE__, SocketLevel[4], ret, "func sckClient_connect() err");
			break;
		}
		else
		{
			hdl->statusArray[i] = 1;
			hdl->valid++;			//Socket有效连接数目
			hdl->nvalid--;			//Socket无效连接数目
		}
	}

	if (hdl->valid < hdl->bounds) //若有效连接数 小于 总数
	{
		ret = Sck_Err_Pool_CreateConn;
		Socket_Log(__FILE__, __LINE__, SocketLevel[4], ret, "有效连接数 小于 总数");
		Socket_Log(__FILE__, __LINE__, SocketLevel[4], ret, "func sckClient_init() create connect num err:%d,  hdl->valid: %d , hdl->bounds:%d", ret, hdl->valid, hdl->bounds);
		for (i = 0; i < hdl->bounds; i++)
		{
			if (hdl->statusArray[i] == 1)
			{
				sckClient_closeconn(hdl->fdArray[i]);
			}
		}
	}

END:
	pthread_mutex_unlock(&(hdl->foo_mutex)); //解锁
	if (ret != 0)
	{
		if (hdl->fdArray != NULL) 		free(hdl->fdArray);
		if (hdl->statusArray != NULL) 	free(hdl->statusArray);
		free(hdl);
		Socket_Log(__FILE__, __LINE__, SocketLevel[4], ret, "func pthread_mutex_unlock() err");
		return ret;
	}

	*handle = hdl; //间接赋值
	return ret;
}

//客户端 socket池 获取一条连接 
int sckCltPool_getConnet(void *handle, int *connfd)
{
	int		ret = 0;

	SockePoolHandle *hdl = NULL;

	if (handle == NULL || connfd == NULL)
	{
		ret = Sck_ErrParam;
		Socket_Log(__FILE__, __LINE__, SocketLevel[4], ret, "func sckCltPool_getConnet() (handle == NULL || connfd==NULL) err");
		return ret;
	}

	hdl = (SockePoolHandle *)handle;
	pthread_mutex_lock(&(hdl->foo_mutex)); //流程加锁 pthread_mutex_unlock(& (hdl->foo_mutex) ); //解锁


	//若 已终止
	if (hdl->terminated == 1)
	{
		ret = Sck_Err_Pool_terminated;
		Socket_Log(__FILE__, __LINE__, SocketLevel[4], ret, "func sckCltPool_getConnet() (terminated == 1)");
		goto END;
	}

	//若 有效连数 = 0
	if (hdl->valid == 0)
	{
		usleep(hdl->sTimeout); //等上几微妙

		if (hdl->valid == 0)
		{
			ret = Sck_Err_Pool_GetConn_ValidIsZero;
			Socket_Log(__FILE__, __LINE__, SocketLevel[4], ret, "func sckCltPool_getConnet() Sck_Err_Pool_GetConn_ValidIsZero err");
			goto END;
		}

		//若 已终止
		if (hdl->terminated == 1)
		{
			ret = Sck_Err_Pool_terminated;
			Socket_Log(__FILE__, __LINE__, SocketLevel[4], ret, "func sckCltPool_getConnet() (terminated == 1)");
			goto END;
		}
	}

	//判断现有连接的状态
	if (hdl->statusArray[hdl->valid - 1] == 0)
	{
		//首先断开坏掉的连接
		if (hdl->fdArray[hdl->valid - 1] == 0)
		{
			ret = sckClient_closeconn(hdl->fdArray[hdl->valid - 1]);
			if (ret != 0)
			{
				Socket_Log(__FILE__, __LINE__, SocketLevel[4], ret, " func sckClient_closeconn() err 断开坏掉的连接失败");
				hdl->fdArray[hdl->valid - 1] = 0;
				//出错不做错误处理
			}
		}

		//断链修复 重新连接 1次
		ret = sckClient_connect(hdl->serverip, hdl->serverport, hdl->connecttime, &(hdl->fdArray[hdl->valid - 1]));
		if (ret != 0)
		{
			Socket_Log(__FILE__, __LINE__, SocketLevel[4], ret, " func sckClient_connect() err 断链修复 重新连接失败");
			hdl->fdArray[hdl->valid - 1] = 0;
			goto END;
		}
	}

END:
	if (ret == 0)
	{
		*connfd = hdl->fdArray[--(hdl->valid)]; //注 有效连接数 减1
	}

	pthread_mutex_unlock(&(hdl->foo_mutex)); //解锁
	//printf("valid=%d;nvalid=%d;bounds=%d \n", hdl->valid, hdl->nvalid, hdl->bounds);
	return ret;
}

//客户端 socket池 发送数据 
int sckCltPool_send(void *handle, int  connfd, unsigned char *data, int datalen)
{
	int		ret = 0;
	SockePoolHandle *hdl = NULL;

	if (handle == NULL || connfd < 0 || data == NULL || datalen <= 0)
	{
		ret = Sck_ErrParam;
		Socket_Log(__FILE__, __LINE__, SocketLevel[4], ret, " func sckCltPool_send() err (handle==NULL || connfd<0 || data==NULL || datalen<=0) ");
		return ret;
	}
	hdl = (SockePoolHandle *)handle;

	//客户端 发送报文
	ret = sckClient_send(connfd, hdl->sendtime, data, datalen);
	if (ret != 0)
	{
		Socket_Log(__FILE__, __LINE__, SocketLevel[4], ret, " func sckClient_send() err");
		return ret;
	}

	return ret;
}

//客户端 socket池 接受数据
int sckCltPool_rev(void *handle, int  connfd, unsigned char **out, int *outlen)
{
	int		ret = 0;
	SockePoolHandle *hdl = NULL;

	if (handle == NULL || connfd < 0 || out == NULL || outlen == NULL)
	{
		ret = Sck_ErrParam;
		Socket_Log(__FILE__, __LINE__, SocketLevel[4], ret, " func sckCltPool_rev() err, check (handle==NULL || connfd<0 || out==NULL || outlen==NULL )");
		return ret;
	}
	hdl = (SockePoolHandle *)handle;

	//客户端 接受报文
	ret = sckClient_rev(connfd, hdl->revtime, out, outlen); //1
	if (ret != 0)
	{
		Socket_Log(__FILE__, __LINE__, SocketLevel[4], ret, " func sckClient_rev() err");
		return ret;
	}

	return ret;
}

//客户端 socket池 把连接放回 socket池中 
int sckCltPool_putConnet(void *handle, int connfd, int validFlag)
{
	int		ret = 0, i = 0;

	SockePoolHandle *hdl = NULL;

	if (handle == NULL || connfd < 0)
	{
		ret = Sck_ErrParam;
		Socket_Log(__FILE__, __LINE__, SocketLevel[4], ret, " func sckCltPool_putConnet() err, check (handle == NULL || connfd==NULL)");
		goto END;
	}

	hdl = (SockePoolHandle *)handle;
	pthread_mutex_lock(&(hdl->foo_mutex)); //流程加锁 

	//若 已终止
	if (hdl->terminated == 1)
	{
		ret = Sck_Err_Pool_terminated;
		Socket_Log(__FILE__, __LINE__, SocketLevel[4], ret, " func sckCltPool_putConnet() err, check (func sckCltPool_putConnet() (terminated == 1))");
		hdl->fdArray[hdl->valid] = connfd;
		hdl->valid++;
		goto END;
	}

	//判断连接是否已经被 放进来 		//判断该连接是否已经被释放
	for (i = 0; i < hdl->valid; i++)
	{
		if (hdl->fdArray[i] == connfd)
		{
			ret = Sck_Err_Pool_HaveExist;
			Socket_Log(__FILE__, __LINE__, SocketLevel[4], ret, " func sckCltPool_putConnet() err, check Sck_Err_Pool_HaveExist ");
			goto END;
		}
	}

	//判断有效连接数是否已经到达最大值
	if (hdl->valid >= hdl->bounds)
	{
		ret = Sck_Err_Pool_ValidBounds;
		Socket_Log(__FILE__, __LINE__, SocketLevel[4], ret, " func sckCltPool_putConnet() err, check (hdl->valid >= hdl->bounds) ");
		goto END;
	}

	//判断释放的连接是否有效
	if (validFlag == 1)
	{
		hdl->fdArray[hdl->valid] = connfd;
		hdl->statusArray[hdl->valid] = 1; //连接有效
		hdl->valid++;  //
	}
	else
	{
		int tmpconnectfd = 0;
		//首先断开坏掉的连接
		ret = sckClient_closeconn(connfd);
		if (ret != 0)
		{
			Socket_Log(__FILE__, __LINE__, SocketLevel[4], ret, " func sckClient_closeconn() err, check (hdl->valid >= hdl->bounds) ");
			//失败不处理
		}

		//断链修复 重新连接 1次 若重新连接成功则再加入连接池中；若重新连接失败，则不需要加入到连接池中
		ret = sckClient_connect(hdl->serverip, hdl->serverport, hdl->connecttime, &tmpconnectfd);
		if (ret != 0)
		{
			Socket_Log(__FILE__, __LINE__, SocketLevel[4], ret, " func sckClient_connect() err, 断链修复 重新连接失败");
		}
		else
		{
			//有效连接数加1
			hdl->fdArray[hdl->valid] = tmpconnectfd;
			hdl->statusArray[hdl->valid] = 1; //连接有效
			hdl->valid++;  //
		}
	}

END:

	pthread_mutex_unlock(&(hdl->foo_mutex)); //解锁
	//printf("valid=%d;nvalid=%d;bounds=%d \n", hdl->valid, hdl->nvalid, hdl->bounds);

	return ret;
}

//客户端 socket池 销毁连接
int sckCltPool_destroy(void *handle)
{
	int		ret = 0, i = 0;

	SockePoolHandle *hdl = NULL;

	if (handle == NULL)
	{
		ret = Sck_ErrParam;
		Socket_Log(__FILE__, __LINE__, SocketLevel[4], ret, " func sckCltPool_destroy() err, check (handle == NULL)");
		return ret;
	}

	hdl = (SockePoolHandle *)handle;
	pthread_mutex_lock(&(hdl->foo_mutex)); //流程加锁 

	//若 已终止
	hdl->terminated = 1; //连接池设置成终止 状态


	for (i = 0; i < hdl->bounds; i++)
	{
		if (hdl->fdArray[i] != 0)
		{
			sckClient_closeconn(hdl->fdArray[i]);
		}
	}

	if (hdl->fdArray)
	{
		free(hdl->fdArray); hdl->fdArray = NULL;
	}

	if (hdl->statusArray)
	{
		free(hdl->statusArray); hdl->statusArray = NULL;
	}

	sckClient_destroy();
	pthread_mutex_unlock(&(hdl->foo_mutex)); //解锁
	//printf("valid=%d;nvalid=%d;bounds=%d \n", hdl->valid, hdl->nvalid, hdl->bounds);

	free(hdl);

	return ret;
}
