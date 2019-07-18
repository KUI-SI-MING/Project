#include "utils.hpp"

typedef bool (*Handler) (int sock);

//HTTP请求处理的任务
//包含成员socket
//包含任务处理函数
class HttpTask
{
  private:
    int _client_sock;
    Handler TaskHandler;

  public:
    HttpTask():_client_sock(-1)
  {}

    HttpTask(int sock, Handler handler):_client_sock(sock),TaskHandler(handler)
  {}

    //设置任务,对于任务类进行初始化
    void SetHttpTask(int sock, Handler handler)
    {
      _client_sock = sock;
      TaskHandler = handler;
    }

    //执行任务处理函数
    void Run()
    {
      TaskHandler(_client_sock);
    }


};

//线程池类
//创建指定数量的线程
//创建一个线程安全的任务队列
//提供任务的入队和出对.线程池的销毁/初始化接口
class ThreadPool
{
  private:
    //当前线程池中的最大线程数
    int _max_thr;
    //当前线程池中的线程数
    int _cur_thr;
    bool _is_stop;

    queue<HttpTask> _task_queue;
    pthread_mutex_t _mutex;
    pthread_cond_t _cond;

  private:
    void QueueLock()
    {
      pthread_mutex_lock(&_mutex);
    }

    void QueueUnLock()
    {
      pthread_mutex_unlock(&_mutex);
    }

    bool IsStop()
    {
      return _is_stop;
    }

    void ThreadExit()
    {
      _cur_thr--;
      pthread_exit(nullptr);
    }

    void ThreadWait()
    {
      if(IsStop())
      {
        //若线程池销毁则,无需等待直接退出
        QueueUnLock();
        ThreadExit();
      }

      pthread_cond_wait(&_cond, &_mutex);
    }

    void ThreadWakeUpOne()
    {
      pthread_cond_signal(&_cond);
    }

    void ThreadWeakUpAll()
    {
      pthread_cond_broadcast(&_cond);
    }

    bool QueueIsEmpty()
    {
      return _task_queue.empty();
    }
    
  private:
    //线程获取任务,处理任务
    static void* thread_start(void* arg)
    {
      while(1)
      {
        ThreadPool* tp = (ThreadPool*)arg;
        tp->QueueLock();
        while(tp->QueueIsEmpty())
        {
          tp->ThreadWait();
        }

        HttpTask ht;
        tp->PopTask(ht);
        tp->QueueUnLock();
        ht.Run();
      }
      return nullptr;
    }

  public:
    ThreadPool(int max):_max_thr(max), _cur_thr(0), _is_stop(false)
  {}

    ~ThreadPool()
    {
      pthread_mutex_destroy(&_mutex);
      pthread_cond_destroy(&_cond);
    }

    //线程创建，互斥锁，条件变量初始化
    bool ThreadPoolInit()
    {
      cout << "------------------------ThreadPool Initing........." << endl;
      pthread_t tid;

      for(int i = 0;i < _max_thr;i++)
      {
        int ret = pthread_create(&tid, nullptr, thread_start, this);
        if(ret != 0)
        {
          LOG("Thread create filed!\n");
          return false;
        }

        pthread_detach(tid);
        _cur_thr++;
      }

      pthread_mutex_init(&_mutex, nullptr);
      pthread_cond_init(&_cond, nullptr);

      return true;
    }

    //线程安全的任务入队
    bool PushTask(HttpTask& tt)
    {
      QueueLock();
      _task_queue.push(tt);
      ThreadWakeUpOne();
      QueueUnLock();
      return true;
    }

    //线程安全的任务出队
    bool PopTask(HttpTask& tt)
    {
      //出队之前在线程接口中会加锁，所以这里无需加锁
      tt = _task_queue.front();
      _task_queue.pop();

      return true;
    }

    //销毁线程池
    bool ThreadDestory()
    {
      if(!IsStop())
      {
        _is_stop = true;
      }

      while(_cur_thr > 0)
      {
        ThreadWeakUpAll();
      }

      return true;
    }
};
