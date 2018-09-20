//简易版线程池
#include "threadpoolsimple.h"

/*
typedef struct _PoolTask
{
    int tasknum;//模拟任务编号
    void *arg;//回调函数参数
    void (*task_func)(void *arg);//任务的回调函数
}PoolTask ;

typedef struct _ThreadPool
{
    int max_job_num;//最大任务个数
    int job_num;//实际任务个数
    PoolTask *tasks;//任务队列数组
    int job_push;//入队位置
    int job_pop;// 出队位置

    int thr_num;//线程池内线程个数
    pthread_t *threads;//线程池内线程数组
    int shutdown;//是否关闭线程池
    pthread_mutex_t pool_lock;//线程池的锁
    pthread_cond_t empty_task;//任务队列为空的条件
    pthread_cond_t not_empty_task;//任务队列不为空的条件

}ThreadPool;
*/

/*
pthread_t pthread_self(void);
功能：获取线程号。

int pthread_equal(pthread_t t1, pthread_t t2);
功能：判断线程号 t1 和 t2 是否相等。为了方便移植，尽量使用函数来比较线程 ID。

int pthread_create(pthread_t *thread,const pthread_attr_t *attr,void *(*start_routine)(void *),void *arg );
功能：创建一个线程。

int pthread_join(pthread_t thread, void **retval);
功能：等待线程结束（此函数会阻塞），并回收线程资源，类似进程的 wait() 函数。如果线程已经结束，那么该函数会立即返回。

int pthread_attr_init(pthread_attr_t *attr);
功能：初始化线程属性函数，注意：应先初始化线程属性，再pthread_create创建线程

int pthread_mutex_init(pthread_mutex_t *restrict mutex,const pthread_mutexattr_t *restrict attr);
功能：初始化一个互斥锁。

int pthread_mutex_destroy(pthread_mutex_t *mutex);
功能：销毁指定的一个互斥锁。互斥锁在使用完毕后，必须要对互斥锁进行销毁，以释放资源。

int pthread_mutex_lock(pthread_mutex_t *mutex);
功能：对互斥锁上锁，若互斥锁已经上锁，则调用者一直阻塞，直到互斥锁解锁后再上锁。

int pthread_mutex_unlock(pthread_mutex_t *mutex);
功能：对指定的互斥锁解锁。

int pthread_cond_init(pthread_cond_t *restrict cond,const pthread_condattr_t *restrict attr);
功能：初始化一个条件变量

int pthread_cond_destroy(pthread_cond_t *cond);
功能：销毁一个条件变量

int pthread_cond_wait(pthread_cond_t *restrict cond,pthread_mutex_t *restrict mutex);
功能：
阻塞等待一个条件变量
a) 阻塞等待条件变量cond（参1）满足
b) 释放已掌握的互斥锁（解锁互斥量）相当于pthread_mutex_unlock(&mutex);
a) b) 两步为一个原子操作。
c) 当被唤醒，pthread_cond_wait函数返回时，解除阻塞并重新申请获取互斥锁pthread_mutex_lock(&mutex);


int pthread_attr_setdetachstate(pthread_attr_t *attr, int detachstate);
功能：设置线程分离状态
参数：
attr：已初始化的线程属性
detachstate： 分离状态
PTHREAD_CREATE_DETACHED（分离线程）
PTHREAD _CREATE_JOINABLE（非分离线程）
返回值：
成功：0
失败：非0

int pthread_attr_getdetachstate(const pthread_attr_t *attr, int *detachstate);
功能：获取线程分离状态
参数：
attr：已初始化的线程属性
detachstate： 分离状态
PTHREAD_CREATE_DETACHED（分离线程）
PTHREAD _CREATE_JOINABLE（非分离线程）
返回值：
成功：0
失败：非0

int pthread_cond_signal(pthread_cond_t *cond);
功能：
唤醒至少一个阻塞在条件变量上的线程
参数：
cond：指向要初始化的条件变量指针
返回值：
成功：0
失败：非0错误号

int pthread_cond_broadcast(pthread_cond_t *cond);
功能：
唤醒全部阻塞在条件变量上的线程
参数：
cond：指向要初始化的条件变量指针
返回值：
成功：0
失败：非0错误号

*/

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
			//唤醒至少一个阻塞在条件变量上的线程
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
/************************************************
 * 函数名:create_threadpool
 * 功能:创建线程池
 * 参数:thrnum  代表线程个数，maxtasknum 最大任务个数
 * 返回:无
************************************************/
void create_threadpool(int thrnum,int maxtasknum)
{
    printf("begin call %s-----\n",__FUNCTION__);
    thrPool = (ThreadPool*)malloc(sizeof(ThreadPool));

    thrPool->thr_num = thrnum; //线程池个数
    thrPool->max_job_num = maxtasknum; //最大任务个数
    thrPool->shutdown = 0;//是否摧毁线程池，1代表摧毁
    thrPool->job_push = 0;//任务队列添加的位置
    thrPool->job_pop = 0;//任务队列出队的位置
    thrPool->job_num = 0;//初始化的任务个数为0

    thrPool->tasks = (PoolTask*)malloc((sizeof(PoolTask)*maxtasknum));//申请最大的任务队列

    //初始化锁和条件变量
    pthread_mutex_init(&thrPool->pool_lock,NULL); //初始化一个互斥锁。
    pthread_cond_init(&thrPool->empty_task,NULL); //初始化一个条件变量
    pthread_cond_init(&thrPool->not_empty_task,NULL); //初始化一个条件变量

    int i = 0;
    thrPool->threads = (pthread_t *)malloc(sizeof(pthread_t)*thrnum);//申请n个线程id的空间
	
	pthread_attr_t attr;
	//初始化线程属性函数，注意：应先初始化线程属性，再pthread_create创建线程
	pthread_attr_init(&attr); 
	//设置线程分离状态
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    for(i = 0;i < thrnum;i++)
	{
        pthread_create(&thrPool->threads[i],&attr,thrRun,(void*)thrPool);//创建多个线程
    }
    //printf("end call %s-----\n",__FUNCTION__);
}

//摧毁线程池
/************************************************
 * 函数名:destroy_threadpool
 * 功能:摧毁线程池
 * 参数:pool摧毁的线程
 * 返回:无
************************************************/
void destroy_threadpool(ThreadPool *pool)
{
    pool->shutdown = 1;//开始自爆
	//唤醒全部阻塞在条件变量上的线程
    pthread_cond_broadcast(&pool->not_empty_task);//诱杀 

    int i = 0;
    for(i = 0; i < pool->thr_num ; i++)
	{
        pthread_join(pool->threads[i],NULL);
    }

	//销毁一个条件变量
    pthread_cond_destroy(&pool->not_empty_task);
	//销毁一个条件变量
    pthread_cond_destroy(&pool->empty_task);
	//销毁一个互斥锁
    pthread_mutex_destroy(&pool->pool_lock);

    free(pool->tasks);
    free(pool->threads);
    free(pool);
}

//添加任务到线程池
/************************************************
 * 函数名:addtask
 * 功能:添加任务到线程池
 * 参数:pool
 * 返回:无
************************************************/
void addtask(ThreadPool *pool)
{
    //printf("begin call %s-----\n",__FUNCTION__);
	//对互斥锁上锁，若互斥锁已经上锁，则调用者一直阻塞，直到互斥锁解锁后再上锁。
    pthread_mutex_lock(&pool->pool_lock);

	//实际任务总数大于最大任务个数则阻塞等待(等待任务被处理)
    while(pool->max_job_num <= pool->job_num)
	{
		//阻塞等待一个条件变量
        pthread_cond_wait(&pool->empty_task,&pool->pool_lock);
    }
	
	//等于队列最大个数,从0开始,队列核心
    int taskpos = (pool->job_push++)%pool->max_job_num;
    //printf("add task %d  tasknum===%d\n",taskpos,beginnum);
	//
    pool->tasks[taskpos].tasknum = beginnum++; //模拟任务编号
    pool->tasks[taskpos].arg = (void*)&pool->tasks[taskpos]; //回调函数参数
    pool->tasks[taskpos].task_func = taskRun; //任务回调函数
    pool->job_num++;
	
	//对指定的互斥锁解锁。
    pthread_mutex_unlock(&pool->pool_lock);
	
	//唤醒至少一个阻塞在条件变量上的线程
    pthread_cond_signal(&pool->not_empty_task);//通知包身工
    //printf("end call %s-----\n",__FUNCTION__);
}

//任务回调函数
/************************************************
 * 函数名:taskRun
 * 功能:任务回调函数
 * 参数:arg
 * 返回:无
************************************************/
void taskRun(void *arg)
{
    PoolTask *task = (PoolTask*)arg;
    int num = task->tasknum;
    printf("task %d is runing %lu\n",num,pthread_self());

    sleep(1);
    printf("task %d is done %lu\n",num,pthread_self());
}


int main()
{
    create_threadpool(3,20);
    int i = 0;
    for(i = 0;i < 50 ; i++)
	{
        addtask(thrPool);//模拟添加任务
    }

    sleep(20);
    destroy_threadpool(thrPool);

    return 0;
}
