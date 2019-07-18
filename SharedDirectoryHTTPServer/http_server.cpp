#include "utils.hpp"
#include "thread_pool.hpp"

#define MAX_LISTEN 5
#define MAX_THREAD 4
#define _IP_ 1

//简历一个tcp服务端程序,接收新连接
//新连接组织一个线程池任务,添加到线程池中
class HttpServer
{
  private: 
    int _server_sock;
    //线程池对象,用于管理
    ThreadPool* _tp;
    
  private:
    //任务处理函数
    static bool HttpHandler(int sock)
    {
      ResquestInformation info;
      HttpRequest hreq(sock);
      HttpResponse hrep(sock);

      //接收HTTP头部
      if(hreq.RecvHttpHeader(info) == false)
      {
        goto out;
      }
      cout << "---------------------------RecvHttpHeader is success!\n";
      //解析HTTP头部
      if(hreq.PraseHttpHeader(info) == false)
      {
        goto out;
      }
      cout << "---------------------------PraseHttpHeader is success!\n";
      //判断请求是否是CGI请求
      if(info.RequestIsCGI())
      { 
        hrep.CGIHandler(info);
      }
      else{
        hrep.FileHandler(info);
      }

      close(sock);
      return true;
out:
      hrep.ErrorHandler(info);
      close(sock);
      return false;
    }
      
  public:
    HttpServer():_server_sock(-1), _tp(nullptr)
  {}

    //TCP服务器的初始化以及线程的初始化
    bool HttpServerInit(string ip, string port)
    {
      cout << "----------------HttpServer Initing!......" << endl;
      _server_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
      if(_server_sock < 0 )
      {
        LOG("sock error!:%s\n", strerror(errno));
        return false;
      }

      cout << "---------------get _server_sock: " << _server_sock << endl; 
      int opt = 1;
      //设置地址复用
      setsockopt(_server_sock, SOL_SOCKET, SO_REUSEADDR, (void*)&opt, sizeof(opt));
      
      sockaddr_in lst_addr;
      lst_addr.sin_family = AF_INET;
      lst_addr.sin_port = htons(atoi(port.c_str()));
#if _IP_
      lst_addr.sin_addr.s_addr = inet_addr(ip.c_str());
#else
      //绑定局域网的所有地址
      lst_addr.sin_addr.s_addr = INADDR_ANY;
#endif
      socklen_t len = sizeof(sockaddr_in);
      if(bind(_server_sock, (sockaddr*)&lst_addr, len) < 0 )
      {
        LOG("bind fileed:%s\n", strerror(errno));
        close(_server_sock);
        return false;
      }

      if(listen(_server_sock, MAX_LISTEN) < 0)
      {
        LOG("listen filed:%s\n", strerror(errno));
        close(_server_sock);
        return false;
      }

      _tp = new ThreadPool(MAX_THREAD);
      if(_tp == nullptr)
      {
        LOG("Thread pool new error!\n");
        return false;
      }

      if(_tp->ThreadPoolInit() == false)
      {
        LOG("Thread pool init error!\n");
        return false;
      }

      return true;
    }
    
    //获取客户端连接==》创建任务 ==》任务入队
    bool Start()
    {
      cout << "----------------HttpServer Start....." << endl;
      while(1)
      {
        sockaddr_in cli_addr;
        socklen_t len = sizeof(sockaddr_in);

        cout << "-----------Accepting...." <<  endl;
        int new_sock = accept(_server_sock, (sockaddr*)&cli_addr, &len);

        cout << "----------Accepted: "<< new_sock << endl;
        if(new_sock < 0)
        {
          LOG("accept filed: %s\n", strerror(errno));
          return false;
        }

        HttpTask ht;
        ht.SetHttpTask(new_sock,HttpHandler);
        cout << "------------SetHttpTask success!-------------- \n";
        _tp->PushTask(ht);
      }

      return true;
    }
};

void Usage(const string proc)
{
#ifdef _IP_
  cout << "Usage: " << proc << "ip Port" << endl;
#else
  cout << "Usage: " << proc << "Port" << emdl;
#endif
}

int main(int argc, char* argv[])
{
#ifdef _IP_
  if(argc != 3)
  {
    Usage(argv[0]);
    exit(1);
  }

  HttpServer server;
  if(server.HttpServerInit(argv[1], argv[2]) == false)
  {
    return -1;
  }
#else
  if(argc != 2)
  {
    Usage(argv[0]);
    exit(1);
  }
  HttpServer server;
  if(server.HttpServerInit(argv[0], argv[1]) == false)
  {
    Usage(argv[0]);
    exit(1);
  }
#endif
  signal(SIGPIPE, SIG_IGN);
  signal(SIGCHLD, SIG_IGN);

  if(server.Start() == false)
  {
    return -1;
  }

  return 0;
}

