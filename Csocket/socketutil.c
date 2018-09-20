
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>

#include "poolsocket.h"
#include "socketlog.h"

/**
 * readn - 读取固定字节数
 * @fd: 文件描述符
 * @buf: 接收缓冲区
 * @count: 要读取的字节数
 * 成功返回count，失败返回-1，读到EOF返回<count
 */
ssize_t readn(int fd, void *buf, size_t count)
{
	size_t nleft = count;
	ssize_t nread;
	char *bufp = (char*)buf;

	while (nleft > 0)
	{
		if ((nread = read(fd, bufp, nleft)) < 0)
		{
			if (errno == EINTR)
				continue;
			return -1;
		}
		else if (nread == 0)
			return count - nleft;

		bufp += nread;
		nleft -= nread;
	}

	return count;
}

/**
 * writen - 发送固定字节数
 * @fd: 文件描述符
 * @buf: 发送缓冲区
 * @count: 要读取的字节数
 * 成功返回count，失败返回-1
 */
ssize_t writen(int fd, const void *buf, size_t count)
{
	size_t nleft = count;
	ssize_t nwritten;
	char *bufp = (char*)buf;

	while (nleft > 0)
	{
		if ((nwritten = write(fd, bufp, nleft)) < 0)
		{
			if (errno == EINTR)
				continue;
			return -1;
		}
		else if (nwritten == 0)
			continue;

		bufp += nwritten;
		nleft -= nwritten;
	}

	return count;
}

/**
 * recv_peek - 仅仅查看套接字缓冲区数据，但不移除数据
 * @sockfd: 套接字
 * @buf: 接收缓冲区
 * @len: 长度
 * 成功返回>=0，失败返回-1
 */
ssize_t recv_peek(int sockfd, void *buf, size_t len)
{
	while (1)
	{
		int ret = recv(sockfd, buf, len, MSG_PEEK);
		if (ret == -1 && errno == EINTR)
			continue;
		return ret;
	}
}

/**
 * activate_noblock - 设置I/O为非阻塞模式
 * @fd: 文件描符符
 */
int activate_nonblock(int fd)
{
	int ret = 0;
	int flags = fcntl(fd, F_GETFL);
	if (flags == -1)
	{
		ret = flags;
		Socket_Log(__FILE__, __LINE__, SocketLevel[4], ret, "func activate_nonblock() err");
		return ret;
	}

	flags |= O_NONBLOCK;
	ret = fcntl(fd, F_SETFL, flags);
	if (ret == -1)
	{
		Socket_Log(__FILE__, __LINE__, SocketLevel[4], ret, "func activate_nonblock() err");
		return ret;
	}
	return ret;
}

/**
 * deactivate_nonblock - 设置I/O为阻塞模式
 * @fd: 文件描符符
 */
int deactivate_nonblock(int fd)
{
	int ret = 0;
	int flags = fcntl(fd, F_GETFL);
	if (flags == -1)
	{
		ret = flags;
		Socket_Log(__FILE__, __LINE__, SocketLevel[4], ret, "func deactivate_nonblock() err");
		return ret;
	}

	flags &= ~O_NONBLOCK;
	ret = fcntl(fd, F_SETFL, flags);
	if (ret == -1)
	{
		Socket_Log(__FILE__, __LINE__, SocketLevel[4], ret, "func deactivate_nonblock() err");
		return ret;
	}
	return ret;
}



/**
 * connect_timeout - connect
 * @fd: 套接字
 * @addr: 要连接的对方地址
 * @wait_seconds: 等待超时秒数，如果为0表示正常模式
 * 成功（未超时）返回0，失败返回-1，超时返回-1并且errno = ETIMEDOUT
 */
static int connect_timeout(int fd, struct sockaddr_in *addr, unsigned int wait_seconds)
{
	int ret;
	socklen_t addrlen = sizeof(struct sockaddr_in);

	if (wait_seconds > 0)
		activate_nonblock(fd);

	ret = connect(fd, (struct sockaddr*)addr, addrlen);
	if (ret < 0 && errno == EINPROGRESS)
	{
		//printf("11111111111111111111\n");
		fd_set connect_fdset;
		struct timeval timeout;
		FD_ZERO(&connect_fdset);
		FD_SET(fd, &connect_fdset);
		timeout.tv_sec = wait_seconds;
		timeout.tv_usec = 0;
		do
		{
			// 一但连接建立，则套接字就可写  所以connect_fdset放在了写集合中
			ret = select(fd + 1, NULL, &connect_fdset, NULL, &timeout);
		} while (ret < 0 && errno == EINTR);
		if (ret == 0)
		{
			ret = -1;
			errno = ETIMEDOUT;
		}
		else if (ret < 0)
			return -1;
		else if (ret == 1)
		{
			//printf("22222222222222222\n");
			/* ret返回为1（表示套接字可写），可能有两种情况，一种是连接建立成功，一种是套接字产生错误，*/
			/* 此时错误信息不会保存至errno变量中，因此，需要调用getsockopt来获取。 */
			int err;
			socklen_t socklen = sizeof(err);
			int sockoptret = getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &socklen);
			if (sockoptret == -1)
			{
				return -1;
			}
			if (err == 0)
			{
				//printf("3333333333333\n");
				ret = 0;
			}
			else
			{
				//printf("4444444444444444:%d\n", err);
				errno = err;
				ret = -1;
			}
		}
	}
	if (wait_seconds > 0)
	{
		deactivate_nonblock(fd);
	}
	return ret;
}


/**
 * write_timeout - 写超时检测函数，不含写操作
 * @fd: 文件描述符
 * @wait_seconds: 等待超时秒数，如果为0表示不检测超时
 * 成功（未超时）返回0，失败返回-1，超时返回-1并且errno = ETIMEDOUT
 */
int write_timeout(int fd, unsigned int wait_seconds)
{
	int ret = 0;
	if (wait_seconds > 0)
	{
		fd_set write_fdset;
		struct timeval timeout;

		FD_ZERO(&write_fdset);
		FD_SET(fd, &write_fdset);

		timeout.tv_sec = wait_seconds;
		timeout.tv_usec = 0;
		do
		{
			ret = select(fd + 1, NULL, &write_fdset, NULL, &timeout);
		} while (ret < 0 && errno == EINTR);

		if (ret == 0)
		{
			ret = -1;
			errno = ETIMEDOUT;
		}
		else if (ret == 1)
			ret = 0;
	}

	return ret;
}


/**
 * read_timeout - 读超时检测函数，不含读操作
 * @fd: 文件描述符
 * @wait_seconds: 等待超时秒数，如果为0表示不检测超时
 * 成功（未超时）返回0，失败返回-1，超时返回-1并且errno = ETIMEDOUT
 */
int read_timeout(int fd, unsigned int wait_seconds)
{
	int ret = 0;
	if (wait_seconds > 0)
	{
		fd_set read_fdset;
		struct timeval timeout;

		FD_ZERO(&read_fdset);
		FD_SET(fd, &read_fdset);

		timeout.tv_sec = wait_seconds;
		timeout.tv_usec = 0;

		//select返回值三态
		//1 若timeout时间到（超时），没有检测到读事件 ret返回=0
		//2 若ret返回<0 &&  errno == EINTR 说明select的过程中被别的信号中断（可中断睡眠原理）
		//2-1 若返回-1，select出错
		//3 若ret返回值>0 表示有read事件发生，返回事件发生的个数

		do
		{
			ret = select(fd + 1, &read_fdset, NULL, NULL, &timeout);

		} while (ret < 0 && errno == EINTR);

		if (ret == 0)
		{
			ret = -1;
			errno = ETIMEDOUT;
		}
		else if (ret == 1)
			ret = 0;
	}

	return ret;
}



//函数声明
//客户端环境初始化
int sckClient_init()
{
	return 0;
}


int sckClient_connect(char *ip, int port, int connecttime, int *connfd)
{

	int ret = 0;

	if (ip == NULL || connfd == NULL || port <= 0 || port > 65537 || connecttime < 0)
	{
		ret = Sck_ErrParam;
		Socket_Log(__FILE__, __LINE__, SocketLevel[4], ret, "func sckClient_connect() err, check  (ip==NULL || connfd==NULL || port<=0 || port>65537 || connecttime < 0)");
		return ret;
	}

	//
	int sockfd;
	sockfd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (sockfd < 0)
	{
		ret = errno;
		printf("func socket() err:  %d\n", ret);
		return ret;
	}

	struct sockaddr_in servaddr;
	memset(&servaddr, 0, sizeof(servaddr));
	servaddr.sin_family = AF_INET;
	servaddr.sin_port = htons(port);
	servaddr.sin_addr.s_addr = inet_addr(ip);

	ret = connect_timeout(sockfd, (struct sockaddr_in*) (&servaddr), (unsigned int)connecttime);
	if (ret < 0)
	{
		if (ret == -1 && errno == ETIMEDOUT)
		{
			ret = Sck_ErrTimeOut;
			return ret;
		}
		else
		{
			//printf("func connect_timeout() err:  %d\n", ret);
			Socket_Log(__FILE__, __LINE__, SocketLevel[4], ret, "func connect_timeout() err");
		}
	}

	*connfd = sockfd;

	return ret;

}


//客户端发送报文
int sckClient_send(int connfd, int sendtime, unsigned char *data, int datalen)
{
	int 	ret = 0;

	if (data == NULL || datalen <= 0)
	{
		ret = Sck_ErrParam;
		Socket_Log(__FILE__, __LINE__, SocketLevel[4], ret, "func sckClient_send() err, check (data == NULL || datalen <= 0)");
		return ret;
	}

	ret = write_timeout(connfd, sendtime);
	if (ret == 0)
	{
		int writed = 0;
		unsigned char *netdata = (unsigned char *)malloc(datalen + 4);
		if (netdata == NULL)
		{
			ret = Sck_ErrMalloc;
			printf("func sckClient_send() mlloc Err:%d\n ", ret);
			return ret;
		}
		int netlen = htonl(datalen);
		memcpy(netdata, &netlen, 4);
		memcpy(netdata + 4, data, datalen);

		writed = writen(connfd, netdata, datalen + 4);
		if (writed < (datalen + 4))
		{
			if (netdata != NULL)
			{
				free(netdata);
				netdata = NULL;
			}
			return writed;
		}

		if (netdata != NULL)  //wangbaoming 20150630 modify bug
		{
			free(netdata);
			netdata = NULL;
		}
	}

	if (ret < 0)
	{
		//失败返回-1，超时返回-1并且errno = ETIMEDOUT
		if (ret == -1 && errno == ETIMEDOUT)
		{
			ret = Sck_ErrTimeOut;
			printf("func sckClient_send() mlloc Err:%d\n ", ret);
			return ret;
		}
		return ret;
	}

	return ret;
}


//客户端端接受报文
int sckClient_rev(int connfd, int revtime, unsigned char **out, int *outlen)
{

	int		ret = 0;
	unsigned char *tmpBuf = NULL;

	if (out == NULL || outlen == NULL)
	{
		ret = Sck_ErrParam;
		printf("func sckClient_rev() timeout , err:%d \n", Sck_ErrTimeOut);
		return ret;
	}

	ret = read_timeout(connfd, revtime); //bugs modify bombing
	if (ret != 0)
	{
		if (ret == -1 || errno == ETIMEDOUT)
		{
			ret = Sck_ErrTimeOut;
			Socket_Log(__FILE__, __LINE__, SocketLevel[4], ret, "func read_timeout() timeout");
			return ret;
		}
		else
		{
			Socket_Log(__FILE__, __LINE__, SocketLevel[4], ret, "func read_timeout() err");
			return ret;
		}
	}

	int netdatalen = 0;
	ret = readn(connfd, &netdatalen, 4); //读包头 4个字节
	if (ret == -1)
	{
		//printf("func readn() err:%d \n", ret);
		Socket_Log(__FILE__, __LINE__, SocketLevel[4], ret, "func readn() err");
		return ret;
	}
	else if (ret < 4)
	{
		ret = Sck_ErrPeerClosed;
		//printf("func readn() err peer closed:%d \n", ret);
		Socket_Log(__FILE__, __LINE__, SocketLevel[4], ret, "func readn() err, peer closed");
		return ret;
	}

	int n;
	n = ntohl(netdatalen);
	tmpBuf = (unsigned char *)malloc(n + 1);
	if (tmpBuf == NULL)
	{
		ret = Sck_ErrMalloc;
		Socket_Log(__FILE__, __LINE__, SocketLevel[4], ret, "malloc() err");
		return ret;
	}


	ret = readn(connfd, tmpBuf, n); //根据长度读数据
	if (ret == -1)
	{
		//printf("func readn() err:%d \n", ret);
		Socket_Log(__FILE__, __LINE__, SocketLevel[4], ret, "readn() err");
		return ret;
	}
	else if (ret < n)
	{
		ret = Sck_ErrPeerClosed;
		//printf("func readn() err peer closed:%d \n", ret);
		Socket_Log(__FILE__, __LINE__, SocketLevel[4], ret, "func readn() err,  peer closed");
		return ret;
	}

	*out = tmpBuf;
	*outlen = n;
	tmpBuf[n] = '\0'; //多分配一个字节内容，兼容可见字符串 字符串的真实长度仍然为n
	return 0;
}


//客户端 关闭与服务端的连接
int sckClient_closeconn(int connfd)
{
	if (connfd >= 0)
	{
		close(connfd);
	}
	return 0;
}

//客户端 释放内存
int sck_FreeMem(void **buf)
{
	if (buf == NULL)
	{
		return 0;
	}
	if (*buf != NULL)
	{
		free(*buf);
	}
	*buf = NULL; //简介修改实参的值
	return 0;
}

// 客户端环境释放 
int sckClient_destroy()
{
	return 0;
}



/////////////////////////////////////////////////////////////////////////////////////
//函数声明
//服务器端初始化
int sckServer_init(int port, int *listenfd)
{
	int 	ret = 0;
	int mylistenfd;
	struct sockaddr_in servaddr;
	memset(&servaddr, 0, sizeof(servaddr));
	servaddr.sin_family = AF_INET;
	servaddr.sin_port = htons(port);
	servaddr.sin_addr.s_addr = htonl(INADDR_ANY);


	mylistenfd = socket(PF_INET, SOCK_STREAM, 0);
	if (mylistenfd < 0)
	{
		ret = errno;
		Socket_Log(__FILE__, __LINE__, SocketLevel[4], ret, "func socket() err");
		return ret;
	}


	int on = 1;
	ret = setsockopt(mylistenfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
	if (ret < 0)
	{
		ret = errno;
		Socket_Log(__FILE__, __LINE__, SocketLevel[4], ret, "func setsockopt() err");
		return ret;
	}


	ret = bind(mylistenfd, (struct sockaddr*)&servaddr, sizeof(servaddr));
	if (ret < 0)
	{
		ret = errno;
		Socket_Log(__FILE__, __LINE__, SocketLevel[4], ret, "func bind() err");
		return ret;
	}

	ret = listen(mylistenfd, SOMAXCONN);
	if (ret < 0)
	{
		ret = errno;
		Socket_Log(__FILE__, __LINE__, SocketLevel[4], ret, "func listen() err");
		return ret;
	}

	*listenfd = mylistenfd;

	return 0;
}

/**
 * accept_timeout - 带超时的accept
 * @fd: 套接字
 * @addr: 输出参数，返回对方地址
 * @wait_seconds: 等待超时秒数，如果为0表示正常模式
 * 成功（未超时）返回已连接套接字，超时返回-1并且errno = ETIMEDOUT
 */
int accept_timeout(int fd, struct sockaddr_in *addr, unsigned int wait_seconds)
{
	int ret;
	socklen_t addrlen = sizeof(struct sockaddr_in);

	if (wait_seconds > 0)
	{
		fd_set accept_fdset;
		struct timeval timeout;
		FD_ZERO(&accept_fdset);
		FD_SET(fd, &accept_fdset);
		timeout.tv_sec = wait_seconds;
		timeout.tv_usec = 0;
		do
		{
			ret = select(fd + 1, &accept_fdset, NULL, NULL, &timeout);
		} while (ret < 0 && errno == EINTR);
		if (ret == -1)
			return -1;
		else if (ret == 0)
		{
			errno = ETIMEDOUT;
			return -1;
		}
	}

	//一但检测出 有select事件发生，表示对等方完成了三次握手，客户端有新连接建立
	//此时再调用accept将不会堵塞
	if (addr != NULL)
		ret = accept(fd, (struct sockaddr*)addr, &addrlen); //返回已连接套接字
	else
		ret = accept(fd, NULL, NULL);
	if (ret == -1)
	{
		ret = errno;
		Socket_Log(__FILE__, __LINE__, SocketLevel[4], ret, "func accept() err");
		return ret;
	}


	return ret;
}

int sckServer_accept(int listenfd, int timeout, int *connfd)
{
	int	ret = 0;

	ret = accept_timeout(listenfd, NULL, (unsigned int)timeout);
	if (ret < 0)
	{
		if (ret == -1 && errno == ETIMEDOUT)
		{
			ret = Sck_ErrTimeOut;
			//printf("func accept_timeout() timeout err:%d \n", ret);
			return ret;
		}
		else
		{
			ret = errno;
			Socket_Log(__FILE__, __LINE__, SocketLevel[4], ret, "func accept_timeout() err");
			return ret;
		}
	}

	*connfd = ret;
	return 0;
}
//服务器端发送报文
int sckServer_send(int connfd, int timeout, unsigned char *data, int datalen)
{
	int 	ret = 0;

	ret = write_timeout(connfd, timeout);
	if (ret == 0)
	{
		int writed = 0;
		unsigned char *netdata = (unsigned char *)malloc(datalen + 4);
		if (netdata == NULL)
		{
			ret = Sck_ErrMalloc;
			Socket_Log(__FILE__, __LINE__, SocketLevel[4], ret, "func malloc() err");
			return ret;
		}
		int netlen = htonl(datalen);
		memcpy(netdata, &netlen, 4);
		memcpy(netdata + 4, data, datalen);

		writed = writen(connfd, netdata, datalen + 4);
		if (writed < (datalen + 4))
		{
			if (netdata != NULL)
			{
				free(netdata);
				netdata = NULL;
			}
			return writed;
		}

	}

	if (ret < 0)
	{
		//失败返回-1，超时返回-1并且errno = ETIMEDOUT
		if (ret == -1 && errno == ETIMEDOUT)
		{
			ret = Sck_ErrTimeOut;
			Socket_Log(__FILE__, __LINE__, SocketLevel[4], ret, "func sckServer_send() err");
			return ret;
		}
		return ret;
	}

	return ret;
}
//服务器端端接受报文
int sckServer_rev(int connfd, int timeout, unsigned char **out, int *outlen)
{
	int		ret = 0;
	unsigned char *tmpBuf = NULL;

	if (out == NULL || outlen == NULL)
	{
		ret = Sck_ErrParam;
		Socket_Log(__FILE__, __LINE__, SocketLevel[4], ret, "func sckServer_rev() err, check (out==NULL || outlen==NULL)");
		return ret;
	}

	ret = read_timeout(connfd, timeout); //bugs modify bombing
	if (ret != 0)
	{
		if (ret == -1 || errno == ETIMEDOUT)
		{
			ret = Sck_ErrTimeOut;
			Socket_Log(__FILE__, __LINE__, SocketLevel[4], ret, "func read_timeout() err");
			return ret;
		}
		else
		{
			Socket_Log(__FILE__, __LINE__, SocketLevel[4], ret, "func read_timeout() err");
			return ret;
		}
	}

	int netdatalen = 0;
	ret = readn(connfd, &netdatalen, 4); //读包头 4个字节
	if (ret == -1)
	{
		Socket_Log(__FILE__, __LINE__, SocketLevel[4], ret, "func readn() err");
		return ret;
	}
	else if (ret < 4)
	{
		ret = Sck_ErrPeerClosed;
		Socket_Log(__FILE__, __LINE__, SocketLevel[4], ret, "func readn() err, peer closed");
		return ret;
	}

	int n;
	n = ntohl(netdatalen);

	tmpBuf = (unsigned char *)malloc(n + 1);
	if (tmpBuf == NULL)
	{
		ret = Sck_ErrMalloc;
		Socket_Log(__FILE__, __LINE__, SocketLevel[4], ret, "func malloc() err");
		return ret;
	}

	ret = readn(connfd, tmpBuf, n); // 服务端 根据长度读数据
	if (ret == -1)
	{
		Socket_Log(__FILE__, __LINE__, SocketLevel[4], ret, "func readn() err");
		return ret;
	}
	else if (ret < n)
	{
		ret = Sck_ErrPeerClosed;
		Socket_Log(__FILE__, __LINE__, SocketLevel[4], ret, "func readn() err. peer closed");
		return ret;
	}

	*out = tmpBuf; //间接赋值
	*outlen = n;
	tmpBuf[n] = '\0';

	return 0;
}

//服务器端 关闭连接
int sckServer_close(int connfd)
{
	close(connfd);
	return 0;
}

//服务器端环境释放 
int sckServer_destroy()
{
	return 0;
}