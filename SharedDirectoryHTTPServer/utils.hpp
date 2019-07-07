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
#include <signal.h>
#include <dirent.h>
#include <netinet/in.h>
#include <sstream>
#include <arpa/inet.h>
#include <fcntl.h>

using namespace std;

#define MAX_HTTPHEADER 4096
#define WWWROOT "www"
#define MAX_PATH 256
#define MAX_BUFF 1024
#define LOG(...)do{\
  fprintf(stdout,__VA_ARGS__);\
}while(0)

//文件类型的映射
unordered_map<string, string> g_mime_type = {
  {"txt", "application/octet-steam"},
  {"html", "text/html"},
  {"htm", "text/html"},
  {"jpg", "image/jpeg"},
  {"gif", "image/gif"},
  {"zip", "application/zip"},
  {"mp3", "audio/mpeg"},
  {"mpeg", "video/mpeg"},
  {"unknow", "application/octet-steam"},
};

unordered_map<string, string> g_err_desc = {
  {"200", "OK"},
  {"400", "uthorized"},
  {"403", "Forbidden"},
  {"404", "Not Found"},
  {"405", "Method Not Allowed"},
};

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

    //gmtime 将一个时间戳转换为一个结构体
    //strftime 将一个时间转换为一个格式
    static void TimetoGmt(time_t t, string& gmt)
    {
      struct tm* mt = gmtime(&t);
      char tmp[128] = {0};
      int len = strftime(tmp, 127, "%a, %d %b %Y %H:%M:%S GMT", mt);

      gmt.assign(tmp, len);
    }

    static void MakeETag(int64_t ino, int64_t size, int64_t mtime, string& etag)
    {
      //"ino-size-mtime"
      stringstream ss;
      ss << "\"" << hex << ino << "-" << hex << size << "-" << hex << mtime << "\"";
      etag = ss.str();
    }

    static void DigitToStr(int64_t num, string& str)
    {
      stringstream ss;
      ss << num;
      str = ss.str();
    }

    static string DigitToStr(int64_t num)
    {
      stringstream ss;
      ss << num;
      return ss.str();
    }

    //通过文件路径获取文件类型
    static void GetMime(const string& file, string& mime)
    {
      size_t pos = file.find_last_of(".");
      if(pos == string::npos)
      {
        mime = g_mime_type["unknow"];
        return;
      }

      //后缀
      string suffix = file.substr(pos + 1);
      auto it = g_mime_type.find(suffix);
      if(it == g_mime_type.end())
      {
        mime = g_mime_type["unknow"];
        return;
      }
      else{
        mime = it->second;
      }
    }
    
    static int64_t StrToDigit(const string& str)
    {
      int64_t num;
      stringstream ss;
      ss << str;
      ss >> num;

      return num;
    }

    static const string GetErrDesc(string& code)
    {
      auto it = g_err_desc.find(code);
      if(it == g_err_desc.end())
      {
        return "Unknown Error!";
      }

      return it->second;
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
    bool RequestIsCGI()
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

    //初始化请求响应信息
    bool ResponseInit(ResquestInformation& req_info)
    {
      //Last_Modifed
      Utils::TimetoGmt(req_info._st.st_mtime, _mtime);
      //ETag: 文件的ETag: ino--size--mtime
      Utils::MakeETag(req_info._st.st_ino, req_info._st.st_size, req_info._st.st_mtime, _etag);
      //Date: 文件最后响应时间
      time_t t = time(nullptr);
      Utils::TimetoGmt(t, _date);
      //fsize
      Utils::DigitToStr(req_info._st.st_size, _fsize);
      //_mime文件类型
      Utils::GetMime(req_info._path_physic, _mime);
      return true;
    }

    bool SendDate(string buf)
    {
      if(send(_client_sock, buf.c_str(), buf.length(), 0) < 0)
      {
        return false;
      }

      return true;
    }

    //按照chuncked机制进行分块传输
    //chuncked发送数据的格式
    //发送hello
    //0x05\r\n  发送数据的大小--十六进制
    //helo\r\n  发送这么多的数据
    //最后一个分块
    //0\r\n\r\n 发送最后一个分块
    bool SendCDate(const string& buf)
    {
      if(buf.empty())
      {
        //最后一个分块
        SendDate("0\r\n\r\n");
      }

      stringstream ss;
      ss << hex << buf.length() << "\r\n";
      SendDate(ss.str());
      ss.clear();
      SendDate(buf);
      SendDate("\r\n");

      return true;
    }

    bool IsPartDownload(ResquestInformation& info)
    {
      cout << "------------------------In IsPartDownload:\n";
      auto it = info._head_list.find("If-Range");
      if(it == info._head_list.end())
      {
        return false;
      }
      else
      {
        if(it->second == _mtime || it->second == _etag);
        else
          return false;
      }

      it = info._head_list.find("Range");
      if(it == info._head_list.end())
      {
        return false;
      }
      else
      {
        string range = it->second;
        cout << "--------------------------In IsPartDownload: range: " << range << endl;
        info._part = Utils::Divide(range, ", ", info._part_list);
        return true;
      }
    }

    bool ErrorHandler(ResquestInformation& info)
    {
      string rsp_header;
      string rsp_body;
     //首行 : 协议版本  状态码 状态描述 \r\n
     //头部: Content-Length；Date
     //空行
     //正文: rsp_body = "<html><body><h1><404><h1></body></html>"
     rsp_header = info._version;
     rsp_header +=  " " + info._error_code + " ";
     rsp_header += Utils::GetErrDesc(info._error_code) +"\r\n";

     time_t t = time(nullptr);
     string gmt;
     Utils::TimetoGmt(t, gmt);
     rsp_header += "Date: " + gmt + "\r\n";

     string cont_len;
     rsp_body = "<html><body><h1>" +info._error_code + "<h1></body></html>";
     Utils::DigitToStr(rsp_body.length(), cont_len);
     rsp_body += "Content-Length: " + cont_len + "\r\n\r\n";

     //打印响应头和正文
     cout << "\n\n\n\n";
     cout << rsp_header << endl;
     cout << rsp_body << endl;
     cout << "\n\n\n\n\n";

     //测试网页是否可以发送出去
     char output[1024];
     memset(output, 0, sizeof(output));
     const char* h = "<h1>Hello World</h1>";
     sprintf(output, "HTTP/1.0 302 REDIRECT\nContent-Length:%lu\nLocation:https://www.taobao.com\n\n%s", strlen(h), h);
     send(_client_sock, rsp_header.c_str(), rsp_header.length(), 0);
     send(_client_sock, rsp_body.c_str(), rsp_body.length(), 0);
     return true;
    }

    bool ProcessPartDowned(ResquestInformation& info, int i)
    {
      cout << "---------------------In Part Downed: \n";
      cout << info._part_list[i] << endl;

      string range = info._part_list[i];
      if(i == 0)
      {
        range.erase(range.begin(), range.begin() + 6);
        cout << "Range: " << range << endl;
      }

      size_t pos = range.find("-");
      int64_t start = 0;
      int64_t end = 0;
      if(pos == 0)
      {
        end = Utils::StrToDigit(_fsize) - 1;
        start = end - Utils::StrToDigit(range.substr(pos + 1));
      }
      else if(pos == range.size() - 1)
      {
        end = Utils::StrToDigit(_fsize) - 1;
        range.erase(pos, 1);
        start = Utils::StrToDigit(range);
      }
      else
      {
        start = Utils::StrToDigit(range.substr(0, pos));
        end = Utils::StrToDigit(range.substr(pos + 1));
      }

      cout << "================PartDownload: start = " << start << " end = " << end << endl;
      string rsp_header;
      rsp_header = info._version + " 206 PARTIAL CONTENT\r\n";
      rsp_header += "Content-Type: " + _mime + "\r\n";

      string len;
      Utils::DigitToStr(end - start + 1, len);
      rsp_header += "Connection: close\r\n";
      rsp_header += "Content-Range: bytes " + Utils::DigitToStr(start) + "-" + Utils::DigitToStr(end) + "/" + _fsize + "\r\n";
      rsp_header += "Content-Length: " + len + "\r\n";
      rsp_header += "Accept-Ranges: bytes\r\n";
      if(info._part_list.size() > 1)
      {
        rsp_header += "Content-Type: multipart/byteranges\r\n";
      }

      rsp_header += "ETag: " + _etag + "\r\n";
      rsp_header += "Last-Modified: " + _mtime + "\r\n";
      rsp_header += "Date: "  + _date + "\r\n\r\n";
      SendDate(rsp_header);

      cout << "====================PartDownloading... rsp_header: \n" << rsp_header << endl;
      int fd = open(info._path_physic.c_str(), O_RDONLY);
      if(fd < 0)
      {
        info._error_code = "400";
        ErrorHandler(info);
        return false;
      }

      lseek(fd, start, SEEK_SET);
      int64_t slen = end- start + 1;
      int64_t rlen = 0;
      char tmp[MAX_BUFF];
      int64_t flen = 0;
      while((rlen = read(fd, tmp, MAX_BUFF)) > 0)
      {
        if(flen + rlen > slen)
        {
          send(_client_sock, tmp, slen - flen, 0);
          break;
        }
        else{
          flen += rlen;
        send(_client_sock, tmp , rlen , 0);
        }
      }
      close(fd);
      return true;
    }


    //CGI 响应
    bool ProcessCGI(ResquestInformation& info)
    {
      cout << "======================In ProcessCGI===========" << endl;
      //使用外部程序完成CGI请求处理---文件上传
      //将http头信息和正文全部交给子程序处理
      //使用环境变量传递头信息
      //使用管道传递正文数据
      //使用管道接收CGI程序的处理结果
      //流程：创建管道 ，创建子进程， 设置子进程环境变量，程序替换
      
      int in[2];//用于向子进程传递正文数据
      int out[2];//用于从子进程中读取处理结果
      if(pipe(in) || pipe(out))
      {
        info._error_code = "500";
        ErrorHandler(info);
        return false;
      }

      int pid = fork();
      if(pid < 0)
      {
        info._error_code = "500";
        ErrorHandler(info);
        return false;
      }
      else if(pid == 0)
      {
        //设置环境变量setenv和getenv
        //第三个参数表示对于已经存在的环境是否要覆盖当前的数据
        setenv("METHOD", info._method.c_str(), 1);
        setenv("VERSION", info._version.c_str(), 1);
        setenv("PATH_INFO", info._path_info.c_str(), 1);
        setenv("QUERY_STRING", info._inquire_string.c_str(), 1);

        cout << "==================indo of env:\n";
        for(auto it = info._head_list.begin(); it != info._head_list.end();it++)
        {
          cout << it->first << ": " << it->second << endl;
          //将所有的头信息放到环境变量中
          setenv(it->first.c_str(), it->second.c_str(), 1);
        }

        close(in[1]);//关闭写
        close (out[0]);//关闭读
        //子程序从标准输入读取数据
        dup2(in[0], 0);
        //子程序直接将打印结果传递给父进程
        //子程序将数据输出到标准输出
        dup2(out[1], 1);

        //进行程序替换，第一个参数代表要执行文件的路径，第二个参数表示如何执行这个二进制程序
        //程序替换后，原先该进程的数据全部改变
        execl(info._path_physic.c_str(), info._path_physic.c_str(), nullptr);
        exit(0);
      }

      close(in[0]);
      close(out[1]);
      //父进程通过in管道将正文数据传递给子进程
      auto it = info._head_list.find("Conent-Length");
      //没有找到Content-Length，不需要提交正文数据给子进程
      //http请求头中有"Content-Length"字段，需要父进程传递正文数据给子进程
      if(it != info._head_list.end())
      {
        char buf[MAX_BUFF] = {0};
        int64_t content_len = Utils::StrToDigit(it->second);

        //循环读取正文，防止没有读完，直到读取正文大小等于Content-Length
        
        int tlen = 0;
        while(tlen < content_len)
        {
          //防止粘包
          int len = MAX_BUFF > (content_len - tlen) ? (content_len - tlen) : MAX_BUFF;
          int rlen = recv(_client_sock, buf, len, 0);
          if(rlen <= 0)
          {
            //响应错误给客户端
            return false;
          }

          //子进程没有读取，直接写可能管道已满，将会阻塞
          if(write(in[1], buf, rlen) < 0)
          {
            return false;
          }

          tlen += rlen;
        }
      }

      //通过out管道读取子进程的处理结果直到返回0
      //将处理结果组织http资源，响应给客户端
      string rsp_header;
      rsp_header = info._version + "200 OK\r\n";
      rsp_header += "Content-Type: ";
      rsp_header += "text/html";
      rsp_header += "\r\n";
      rsp_header += "Conetction: close\r\n";
      rsp_header += "Etag: " + _etag + "\r\n";
      rsp_header += "Content-Length: " + _fsize + "\r\n";
      rsp_header += "Last-Modified: " + _mtime + "\r\n";
      rsp_header += "Date: " + _date + "\r\n\r\n";
      SendDate(rsp_header);

      cout << "================In ProcessCGI:rsp_header\n" << rsp_header << endl;

      while(1)
      {
        char buf[MAX_BUFF] =  {0};
        int rlen = read(out[0], buf, MAX_BUFF);
        if(rlen == 0)
        {
          break;
        }
        //读取子进程的处理结果并且发送给浏览器
        send(_client_sock, buf, rlen, 0);
      }

      string rsp_body;
      rsp_body = "<html lang=\"zh-cn\"><body><meta charset='UTF-8'><h1>UPLOAD SUCCESS!</h1><hr />";
      
      rsp_body += "<a href=\"http://10.114.95.6:8888/\" title=\"点击返回\">点击返回</a>";
      rsp_body += "</body></html>";
      SendDate(rsp_body);

      //rsp_body += "<html><body><h1>UPLOAD SUCCESS!</h1></body></html>";
      //SendDate(rsp_body);
      cout << "======================In ProcessCGI:rsp_body: " << rsp_body << endl;
      close(in[1]);
      close(out[0]);

      return true;
    }

    bool CGIHandler(ResquestInformation& info)
    {
      //初始化CGI信息
      ResponseInit(info);
      //执行CGI响应
      ProcessCGI(info);
      return true;
    }

    bool FileIsDir(ResquestInformation& info)
    {
      if(info._st.st_mode & S_IFDIR)
      {
        if(info._path_info.back() != '/')
          info._path_info.push_back('/');
        if(info._path_physic.back() != '/')
          info._path_physic.push_back('/');
        return true;
      }

      return false;
    }

    bool ProcessList(ResquestInformation& info)
    {
      //组织头部信息
      //首行
      //Content-Length： text/html\r\n
      //ETag: ino-size-mtime
      //Date: 文件的响应时间
      //Connection: close\r\n
      //Transfer-Encoding: chunked\r\n\r\n 分块传输
      //正文
      //每一个目录下的文件都要输出一个html标签信息
      string rsp_header;
      rsp_header = info._version + " 200 OK\r\n";
      rsp_header += "Connection: close\r\n";
      if(info._version == "HTTP/1.1")
      {
        //只有HTTP1.1版本的才可以使用Transfer-Encoding:chuncked进行传输
        rsp_header += "Transfer-Encoding: chunked\r\n";
      }

      //ETag标记文本是否修改
      rsp_header += "ETag: " + _etag + "\r\n";
      //基本同Etag
      rsp_header += "Last-Modified: " + _mtime + "\r\n";
      rsp_header += "Date: " + _date + "\r\n\r\n";
      SendDate(rsp_header);

      string rsp_body;
      rsp_body = "<html><head>";

      rsp_body += "<title>Index of ";
      rsp_body += info._path_info;
      rsp_body += "</title>";
      //meta就是对于一个html页面中的元信息
      rsp_body += "<meta charset='UTF-8'>";
      rsp_body += "<h1>Index of ";
      rsp_body += info._path_info;
      rsp_body += "</h1></head><link rel=\"shortcut ico\n\", href=\"/favicon.ico\" /><body>";
      
      //<hr />是一个横线
      //rsp_body += "<h1>Welocome to my server";
      //rsp_body += "</h1>";
      //form表单为了出现两个按钮，请求的资源是action请求的方法是POST
      rsp_body += "<form action='/upload' method='POST' enctype='multipart/form-data'>";
      //测试
      //rsp_body += "<input type='file' name='FileUpload' />";
      rsp_body += "<input type='file' name='FileUpload' />";
      rsp_body += "<input type='submit' value='上传' />";
      rsp_body += "</form>";
      rsp_body += "<form action='/search' method= 'POST' >";
      rsp_body += "<input type='text' name='find' />";
      rsp_body += "<input type='submit' name='搜索' /></form>";
      //<ol>是进行排序
      rsp_body += "<hr /><ol>";
      SendDate(rsp_body);

      //发送每一个文物的数据
      //scandir函数
      //获取目录下的每一个文件，组织html信息，chunck传输
      struct dirent** p_dirent = nullptr;
      //第三个参数为nullptr表示对于该目录下的文件都进行查找不进行过滤，并且所有将所有查找出来的文件放到目录结构dirent的里面
      int num = scandir(info._path_physic.c_str(), &p_dirent, nullptr, alphasort);
      for(int i = 0;i < num;i++)
      {
        string file_html;
        string file_path;
        file_path += info._path_physic + p_dirent[i]->d_name;
        //存放文件信息
        struct stat st;
        //获取文件信息
        if(stat(file_path.c_str(), &st) < 0)
        {
          continue;
        }

        string mtime;
        Utils::TimetoGmt(st.st_mtime, mtime);
        string mime;
        Utils::GetMime(p_dirent[i]->d_name, mime);
        string fsize;
        Utils::DigitToStr(st.st_size / 1024 , fsize);

        //给页面加个一个href + 路径， 一点击的话就会连接， 进入到一个文件或者目录之后会给这个文件目录的网页地址前面加上路径
        //例如列表的时候访问根目录下的所有文件
        //_path_info就变成--/.-- / .. -- /hello.dat --  / html -- /text.txt
        //然后在网页中点击的时候，向服务器发送请求报文
        //请求的路径:[info._path_info/文件名],然后服务器返回一个html页面，对于多次的话，网页会进行缓存有的时候就会直接进行跳转，不在向服务器发送http请求
        //直接根据这个网页路径来进行跳转网页，这就是网页缓存
        file_html += "<li><strong><a href ='" + info._path_info;
        file_html += p_dirent[i]->d_name;
        file_html += "'>";
        //打印名字
        file_html += p_dirent[i]->d_name;
        file_html += "</a></strong>";
        file_html += "<br/><small>";
        file_html += "modifid: " + mtime + "<br />";
        file_html += mime + " - " + fsize + "kbytes";
        file_html += "<br /><br /></small></ li>";
        SendCDate(file_html);
      }

      rsp_body = "</ol><hr /><a href=\"http://www.bitbug.net/\" target=\"_black\"><img src=\"http://www.bitbug.net/mypagerank.php?style=9\" border=\"0\" alt=\"My Google Pagegank\" /></a></body></html>";
      SendCDate(rsp_body);
      //进行分块发送的时候告诉已经发送完毕了，不用让客户进行下一个等待正文的过程了
      SendCDate("");

      return true;

    }

    //文件下载功能
    bool ProcessFile(ResquestInformation& info)
    {
      cout << "=======================In ProcessFile===========" << endl;
      string rsp_header;
      rsp_header = info._version + " 200 OK\r\n";
      rsp_header += "Content-Type: " + _mime + "\r\n";
      rsp_header += "Connection: close\r\n";
      rsp_header += "Content-Length: " + _fsize + "\r\n";
      rsp_header += "ETag: " + _etag + "\r\n";
      rsp_header += "Last-Modified: " + _mtime +"\r\n";
      rsp_header += "Date: " + _date + "\r\n\r\n";
      SendDate(rsp_header);

      int fd = open(info._path_physic.c_str(), O_RDONLY);
      if(fd < 0)
      {
        info._error_code = "400";
        ErrorHandler(info);
        return false;
      }

      int rlen = 0;
      char tmp[MAX_BUFF];
      while((rlen = read(fd, tmp, MAX_BUFF)) > 0)
      {
        //有这样的一种情况，文本中的数据都是\0那么就会在第一次发送过去的时候，发送0个数据
        //tmp[rlen + 1] = '\0';
        //SendData(tmp);
        //发送文件数据的时候不能用string发送
        //对端关闭连接，发送数据send就会收到SIGPIPE信号，默认处理就是终止进程
        send(_client_sock, tmp, rlen, 0); 
      }
      close(fd);
      return true;
    }

    bool FileHandler(ResquestInformation& info)
    {
      //初始化文件响应信息
      ResponseInit(info);
      //执行文件列表响应
      if(FileIsDir(info))
      {
        ProcessList(info);
      }
      else{//执行文件下载响应
        if(IsPartDownload(info))
        {
          for(int i = 0;i < info._part;i++)
          {
            ProcessPartDowned(info, i);
          }
        }
        else{
          ProcessFile(info);
        }
      }
      return true;
    }
};
