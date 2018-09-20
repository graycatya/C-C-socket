//简易版线程池
#include "threadpoolsimple.h"
#include <unistd.h>
#include <fcntl.h>
ThreadPool *thrPool = NULL;

int beginnum = 1000;

void *thrRun(void *arg)
{
    //printf("begin call %s-----\n",__FUNCTION__);
    ThreadPool *pool = (ThreadPool*)arg;
    int taskpos = 0;//任务位置
    PoolTask *task = (PoolTask *)malloc(sizeof(PoolTask));

    while(1)
	{
        //获取任务，先要尝试加锁
        pthread_mutex_lock(&thrPool->pool_lock);

		//无任务并且线程池不是要摧毁
        while(thrPool->job_num <= 0 && !thrPool->shutdown )
		{
			//如果没有任务，线程会阻塞
            pthread_cond_wait(&thrPool->not_empty_task,&thrPool->pool_lock);
        }
        
        if(thrPool->job_num)
		{
            //有任务需要处理
            taskpos = (thrPool->job_pop++)%thrPool->max_job_num;
            //printf("task out %d...tasknum===%d tid=%lu\n",taskpos,thrPool->tasks[taskpos].tasknum,pthread_self());
			//为什么要拷贝？避免任务被修改，生产者会添加任务
            memcpy(task,&thrPool->tasks[taskpos],sizeof(PoolTask));
            task->arg = task;
            thrPool->job_num--;
            //task = &thrPool->tasks[taskpos];
            pthread_cond_signal(&thrPool->empty_task);//通知生产者
        }

        if(thrPool->shutdown)
		{
            //代表要摧毁线程池，此时线程退出即可
            //pthread_detach(pthread_self());//临死前分家
            pthread_mutex_unlock(&thrPool->pool_lock);
            free(task);
			pthread_exit(NULL);
        }

        //释放锁
        pthread_mutex_unlock(&thrPool->pool_lock);
       
        task->task_func(task->arg);//执行回调函数
      
    }
    
    //printf("end call %s-----\n",__FUNCTION__);
}

//创建线程池
void create_threadpool(int thrnum,int maxtasknum)
{
    printf("begin call %s-----\n",__FUNCTION__);
    thrPool = (ThreadPool*)malloc(sizeof(ThreadPool));

    thrPool->thr_num = thrnum;
    thrPool->max_job_num = maxtasknum;
    thrPool->shutdown = 0;//是否摧毁线程池，1代表摧毁
    thrPool->job_push = 0;//任务队列添加的位置
    thrPool->job_pop = 0;//任务队列出队的位置
    thrPool->job_num = 0;//初始化的任务个数为0

    thrPool->tasks = (PoolTask*)malloc((sizeof(PoolTask)*maxtasknum));//申请最大的任务队列

    //初始化锁和条件变量
    pthread_mutex_init(&thrPool->pool_lock,NULL);
    pthread_cond_init(&thrPool->empty_task,NULL);
    pthread_cond_init(&thrPool->not_empty_task,NULL);

    int i = 0;
    thrPool->threads = (pthread_t *)malloc(sizeof(pthread_t)*thrnum);//申请n个线程id的空间
	
	pthread_attr_t attr;
	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    for(i = 0;i < thrnum;i++)
	{
        pthread_create(&thrPool->threads[i],&attr,thrRun,(void*)thrPool);//创建多个线程
    }
    //printf("end call %s-----\n",__FUNCTION__);
}
//摧毁线程池
void destroy_threadpool(ThreadPool *pool)
{
    pool->shutdown = 1;//开始自爆
    pthread_cond_broadcast(&pool->not_empty_task);//诱杀 

    int i = 0;
    for(i = 0; i < pool->thr_num ; i++)
	{
        pthread_join(pool->threads[i],NULL);
    }

    pthread_cond_destroy(&pool->not_empty_task);
    pthread_cond_destroy(&pool->empty_task);
    pthread_mutex_destroy(&pool->pool_lock);

    free(pool->tasks);
    free(pool->threads);
    free(pool);
}

//添加任务到线程池
void addtask(ThreadPool *pool,int fd,struct epoll_event *e)
{
    printf("begin call %s-----\n",__FUNCTION__);
    pthread_mutex_lock(&pool->pool_lock);

	//实际任务总数大于最大任务个数则阻塞等待(等待任务被处理)
    while(pool->max_job_num <= pool->job_num)
	{
        pthread_cond_wait(&pool->empty_task,&pool->pool_lock);
    }

    int taskpos = (pool->job_push++)%pool->max_job_num;
    //printf("add task %d  tasknum===%d\n",taskpos,beginnum);
    //pool->tasks[taskpos].tasknum = beginnum++;
    pool->tasks[taskpos].arg = (void*)&pool->tasks[taskpos];
    pool->tasks[taskpos].task_func = taskRun;
    pool->tasks[taskpos].fd = fd;
     pool->tasks[taskpos].e = e;
    pool->job_num++;

    pthread_mutex_unlock(&pool->pool_lock);

    pthread_cond_signal(&pool->not_empty_task);//通知包身工
    printf("end call %s-----\n",__FUNCTION__);
}

//任务回调函数
void taskRun(void *arg)
{
   
    PoolTask *task = (PoolTask*)arg;
     char buf[1024]="";
    
     while(1)
          {
                        char buf[1024]="";
                        int n = Read(task->fd , buf,sizeof(buf));
                        if(n < 0 )
                        {
                            if(errno ==  EAGAIN)//缓冲区如果读干净了,read返回值小于0,errno设置成EAGAIN
                                break;
                            close(task->fd );//关闭cfd
                            //epoll_ctl(epfd,EPOLL_CTL_DEL,task->fd ,&evs[i]);//将cfd上树
                            epoll_ctl(task->epfd,EPOLL_CTL_DEL,task->fd,task->e);//将cfd
                            printf("client err\n");
                            break;
                        }
                        else if(n == 0)
                        {
                             close(task->fd );//关闭cfd
                            epoll_ctl(task->epfd,EPOLL_CTL_DEL,task->fd,task->e);//将cfd
                            printf("client close\n");
                            break;

                        }
                        else
                        {
                            printf("%s\n",buf );
                           Write(task->fd ,buf,n);

                        }
         }



        free(task->e);
 
   
}


int main()
{
    create_threadpool(3,20);
    int i = 0;
    //创建套接字,绑定
    int lfd = tcp4bind(8000,NULL);
    //监听
    listen(lfd,128);
    //创建树
    int epfd = epoll_create(1);
    struct epoll_event ev,evs[1024];
    ev.data.fd = lfd;
    ev.events = EPOLLIN;//监听读事件
    //将ev上树
    epoll_ctl(epfd,EPOLL_CTL_ADD,lfd,&ev);
    while(1)
    {
        int nready = epoll_wait(epfd,evs,1024,-1);
        if(nready < 0)
            perr_exit("err");
        else if(nready == 0)
            continue;
        else if(nready > 0 )
        {
            for(int i=0;i<nready;i++)
            {
                if(evs[i].data.fd == lfd && evs[i].events & EPOLLIN)//如果是lfd变化,并且是读事件
                {
                        struct sockaddr_in cliaddr;
                        char buf_ip[16]="";
                        socklen_t len  = sizeof(cliaddr);
                        int cfd = Accept(lfd,(struct sockaddr *)&cliaddr,&len);
                        printf("client ip=%s port=%d\n",inet_ntop(AF_INET,
                        &cliaddr.sin_addr.s_addr,buf_ip,sizeof(buf_ip)),
                        ntohs(cliaddr.sin_port));
                        ev.data.fd = cfd;//cfd上树
                        //ev.events = EPOLLIN;//监听读事件
                        ev.events = EPOLLIN | EPOLLET ;//监听读事件
                        //设置文件描述符cfd为非阻塞
                        int flag = fcntl(cfd,F_GETFL);
                        flag |= O_NONBLOCK;
                        fcntl(cfd,F_SETFL,flag);
                        epoll_ctl(epfd,EPOLL_CTL_ADD,cfd,&ev);//将cfd上树

                }
                else if(evs[i].events & EPOLLIN)//普通的读事件
                {
                   
                    struct epoll_event *e = (struct epoll_event*)malloc(sizeof(struct epoll_event));
                    memcpy(e,&evs[i],sizeof(struct epoll_event));
                    addtask(thrPool,evs[i].data.fd,e);
                   
              }


            }



        }


    }
    close(lfd);

   
    destroy_threadpool(thrPool);

    return 0;
}
