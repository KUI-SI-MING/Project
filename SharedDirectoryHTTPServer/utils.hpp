#pragma once 

#include <iostream>
#include <vector>
#include <string.h>
#include <unordered_map>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>

using namespace std;

#define MAX_HTTPHEADER 4096


//HTTPRequest解析出的请求信息
class ResquestInformation
{
  public:
    //请求方法
    string _method;
    //协议版本
    string _version;
    //资源路径
    string _path_info;
    //资源实际路径
    string _path_physic;
    //查询字符串
    string _inquire_string;
    //头部中的键值对
    unordered_map<string, string> _head_list;
    //获取文件信息
    struct stat _st;

    int _part;
    vector<string> _part_list;

    string _error_code;

  public:
    void SetErrorCode(const string& code)
    {
      _error_code = code;
    }

    //判断请求是否为CGI请求
    bool ReqestIsCGI()
    {
      if((_method == "GET" && !_inquire_string.empty()) || (_method == "POST"))
        return true;

      return false;
    }
};

//HTTP数据的接收，解析
class HttpRequest
{
  private:
    int _client_sock;
    string _http_header;

    ResquestInformation _req_info;

  public:
    HttpRequest(int sock):_client_sock(sock)
  {}
    
    //接收HTTP数据请求头
    bool RecvHttpHeader(ResquestInformation& info)
    {
      //设定HTTP头部最大值
      char head[MAX_HTTPHEADER] = {0};
      while(1)
      {
        //预先存取,不从缓冲区中拿出数据
        int ret = recv(_client_sock, head, MAX_HTTPHEADER, MSG_PEEK);
        //读取出错或者对端关闭链接
        if(ret <= 0)
        {
          //EINTR表示操作被信号中断,EAGAIN表示当前缓冲区中没有数据
          if(errno == EINTR || errno == EAGAIN)
          {
            continue;
          }

          info.SetErrorCode("500");
          return false;
        }

        //ptr为nullptr, 表示head里无\r\n\r\n
        char* ptr = strstr(head, "\r\n\r\n");
        //当读取字节数大于MAX_HTTPHEADER，头部还未读完说明头部过长
        if((ptr == nullptr) && (ret == MAX_HTTPHEADER))
        {
          info.SetErrorCode("413");
          return false;
        }
        //当读字节数小于MAX_HTTPHEADER，且没有空行出现,说明数据还没有从发送端发送完,所以接收缓冲区需要等待一下再次读取数据
        else if(ptr == nullptr && ret < MAX_HTTPHEADER)
        {
          usleep(1000);
          continue;
        }

        int head_len = ptr - head;//实际头部长度
        _http_header.assign(head, head_len);//将有效头部取出

        //将缓冲区的所有头部删除
        recv(_client_sock, head, head_len + 4, 0);
        
        LOG("------------------------In RecvHttpHeader\n%s\n", _http_header.c_str());
        break;
      }
      return true;
    }
};

//文件请求接口
//文件下载接口
//文件列表接口
//CGI请求接口
class HttpResponse
{
  private:
    int _client_sock;
    //表明这个文件是否是源文件，是否修改过
    //ETAG: "inode-fsize-mtime"\r\n
    string _etag;
    //文件最后一次修改时间
    string _mtime;
    //系统响应时间
    string _date;
    //文件大小
    string _fsize;
    //文件类型
    string _mime;

  public:
    HttpResponse(int sock):_client_sock(sock)
  {}


};
