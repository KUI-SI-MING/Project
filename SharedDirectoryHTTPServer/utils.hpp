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
#include <stdio.h>
#include <stdlib.h>
#include <queue>
#include <pthread.h>

using namespace std;

#define MAX_HTTPHEADER 4096
#define WWWROOT "www"
#define MAX_PATH 256
#define LOG(...)do{\
  fprintf(stdout,__VA_ARGS__);\
}while(0)

class Utils
{
  public:
    static int Divide(string& src, const string& seg, vector<string>& list)
    {
      //分割多少数据
      int count = 0;
      size_t idx = 0;//起始位置
      size_t poss = 0;// \r\n的位置
      
      while(idx < src.length())
      {
        poss = src.find(seg, idx);
        if(poss == string::npos)
        {
          break;
        }

        list.push_back(src.substr(idx, poss - idx));
        count++;
        idx = poss + seg.length();
      }

      //最后一条信息
      if(idx < seg.length())
      {
        list.push_back(seg.substr(idx, poss - idx));
        count++;
      }

      return count;
    }

};

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

    
    bool PraseFirstLine(string& line, ResquestInformation& info)
    {
      vector<string> line_list;
      if(Utils::Divide(line, " ", line_list) != 3)
      {
        info._error_code = "400";
        return false;
      }

      //打印取出的首行成分
      cout << "\n\n\n\n";
      for(size_t i = 0;i < line_list.size();i++)
      {
        cout << line_list[i] << endl;
      }
      cout << "\n\n\n\n";

      string url;
      info._method = line_list[0];
      url = line_list[1];
      info._version = line_list[2];

      if(info._method != "GET" && info._method != "POST" && info._method != "HEAD")
      {
        info._error_code = "405";
        return false;
      }

      if(info._version != "HTTP/0.9" && info._version != "HTTP/1.0" && info._version != "HTTP/1.1")
      {
        info._error_code = "400";
        return false;
      }

      //解析URL
      //url: /upload?key=val&key=val
      size_t pos = 0;
      pos = url.find("?");
      if(pos == string::npos)
      {
        info._path_info = url;
      }
      else
      {
        info._path_info = url.substr(0, pos);
        info._inquire_string = url.substr(pos + 1);
      }

      PathIsLeagal(info._path_info, info);
      //realpath()将相对路径转换为绝对路径,若不存在则直接奔溃
      //info._path_physic = WWWROOT + info._path_info;
      
      return true;
    }

    //判断地址是否合法,并将相对路径转换为绝对路径
    bool PathIsLeagal(string& path, ResquestInformation& info)
    {
      //GET / HTTP/1.1
      //file = www/
      string file = WWWROOT + path;

      //打印文件路径
      cout << "\n\n\n\n";
      cout << file << "\n\n\n\n";

      //文件存在，就将相对路径转换为绝对路径
      char tmp[MAX_PATH] = {0};
      //使用realpath函数将虚拟路径转换为物理路径,自动去掉最后面的/
      realpath(file.c_str(), tmp);

      info._path_physic = tmp;
      //判断相对路径,防止出现将相对路径改为绝对路径时,绝对路径中没有根目录,也就是没有访问权限
      if(info._path_physic.find(WWWROOT) == string::npos)
      {
        info._error_code = "403";
        return false;
      }

      //stat函数，通过路径获取文件信息
      //stat函数需要物理路径来获取文件的信息,
      if(stat(info._path_physic.c_str(), &(info._st)) < 0)
      {
        info._error_code = "404";
        return false;
      }

      return true;
    }

    bool PraseHttpHeader(ResquestInformation& info)
    {
      //HTTP请求头解析
      //请求方法 URL 协议版本\r\n
      //key:val\r\nkey:val
      vector<string> head_list;//头信息的数组
      Utils::Divide(_http_header, "\r\n", head_list);
      
      //打印出被分割的头部信息
      for(size_t i = 0;i < head_list.size();i++)
      {
        cout << head_list[i] << endl;
      }
      cout << "\n\n\n\n";

      PraseFirstLine(head_list[0], info);
      //删除首行
      head_list.erase(head_list.begin());
      //存放所有的key:val 键值对
      for(size_t i = 1;i < head_list.size();i++)
      {
        size_t pos = head_list[i].find(": ");
        info._head_list[head_list[i].substr(0, pos)] = head_list[i].substr(pos + 2);
      }

      //打印存放后的头部
      for(auto it = info._head_list.begin();it != info._head_list.end();it++)
      {
        cout << '[' << it->first << ']' << ' '  << '[' << it->second << ']';
      }
      cout << "\n\n\n\n";
      //若是测试打印出错，返回页面"404"
      //info._error_code = "404";
      //return false;
      
      return true;
    }

    ResquestInformation& GetRequestInfo();
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
