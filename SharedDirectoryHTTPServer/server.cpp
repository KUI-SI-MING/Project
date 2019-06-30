#include <stdlib.h>
#include "utils.hpp"

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
    static bool HttpHandleer(int sock)
    {
      ResquestInformation info;
      HttpRequest hreq(sock);
      HttpResponse hres(sock);

      //接收HTTP头部
      if(hreq.RecvHttpHeader(info))
      {
        goto out;
      }
      cout << "---------------------------RecvHttpHeader is success!\n";
      //解析HTTP头部
      //判断请求是否是CGI请求
      
      close(sock);
      return true;
out:
      hres.ErrHandler(info);
      close(sock);
      return false;
    }
      
  public:
    HttpServer():_server_sock(-1), _tp(nullptr)
  {}

};


