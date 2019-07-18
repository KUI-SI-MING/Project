#include "utils.hpp"

enum _boundary_type
{
  BOUNDARY_NO = 0,
  BOUNDARY_FIRST,
  BOUNDARY_MIDDLE,
  BOUNDARY_LAST,
  BOUNDARY_PART//部分boundary
};

class Upload
{
  private:
    int _file_fd;
    int64_t content_len;
    string _file_name;
    string _f_boundary;
    string _m_boundary;
    string _l_boundary;

private:
    //匹配boundary
    //只要匹配带一个\r\n都假设可能是middle_boundary, blen(buffer的长度)匹配的长度
    int MatchBoundary(char* buf, size_t blen, int* boundary_pos)
    {
      //--boundary
      //first boundary: --------boundary
      //middle boundary: \r\n-------boundary\r\n
      //last boundary: \r\n---------boundary\r\n--
      //middle boundary 和 last boundary的长度是一样的
      //
      //匹配first boundary，若匹配成功，first boundaryj肯定是在这个的第一个位置，下标为0
      if(!memcmp(buf, _f_boundary.c_str(), _f_boundary.length()))
      {
        *boundary_pos = 0;
        return BOUNDARY_FIRST;
      }

      //匹配middle boundary和last boundary
      //匹配原则：如果剩余的长度大于boundary的长度就进行完全匹配，否则进行部分匹配
      for(size_t i = 0;i < blen;i++)
      {
        //完全匹配
        if((blen - i) >= _m_boundary.length())
        {
          if(!memcmp(buf + i, _m_boundary.c_str(), _m_boundary.length()))
          {
            *boundary_pos = i;
            return BOUNDARY_MIDDLE;
          }
          else if(!memcmp(buf + i, _l_boundary.c_str(), _l_boundary.length()))
          {
            *boundary_pos = i;
            return BOUNDARY_LAST;
          }
        }
        //部分匹配
        else{
          //剩余长度小于boundary的长度,防止出现半个boundary,所以进行部分匹配
          //int cmp_len = (blen - i)  > _m_boundary.length() ? _m_boundary.length() : (blen - i);
          //cmp_len是要匹配的长度
          int cmp_len = blen - i;
          if(!memcmp(buf + i, _m_boundary.c_str(), cmp_len))
          {
            *boundary_pos = i;
            return BOUNDARY_MIDDLE;
          }
          else if(!memcmp(buf + i, _l_boundary.c_str(), cmp_len))
          {
            *boundary_pos = i;
            return BOUNDARY_LAST;
          }

        }
      }

      return BOUNDARY_NO;
    }
    
    bool GetfileName(char* buf, int* content_pos)
    {
      char* ptr = nullptr;
      //查看是否是个完整的上传文件的头部,有没有\r\n\r\n
      ptr = strstr(buf, "\r\n\r\n");
      if(ptr == nullptr)
      {
        *content_pos = 0;
        return false;
      }

      *content_pos = ptr - buf +4;
      //将http上传文件的头部信息拿出来
      string head;
      head.assign(buf, ptr - buf);
      string _file_sep = "filename=\"";
      size_t pos = head.find(_file_sep);
      if(pos == string::npos)
      {
        return false;
      }

      string file_name;
      file_name = head.substr(pos + _file_sep.length());
      pos = file_name.find("\"");
      if(pos == string::npos)
      {
        return false;
      }
      //从文件后面的"删除到结尾
      file_name.erase(pos);
      //如果直接使用WWWROOT进行拼接获取文件所在路径和名字的时候这个时候就会每次上传的文件都在www目录下，不会发生改变
      //所以要使用实际路径在加上文件名就好了
      _file_name = WWWROOT;
      _file_name += "/";
      _file_name += file_name;

      return true;
    }

    bool CreateFile()
    {
      //文件不存在就创建该文件
      _file_fd = open(_file_name.c_str(), O_CREAT || O_WRONLY, 0664);
      if(_file_fd < 0)
      {
        return false;
      }

      return true;
    }

    bool CloseFile()
    {
      if(_file_fd != -1)
      {
          close(_file_fd);
          return true;
      }

      return false;
    }

    bool WriteFile(char* buf, int len)
    {
      if(_file_fd != -1)
      {
        write(_file_fd, buf, len);

      }

      return true;
    }

public:
    Upload():_file_fd(-1)
  {}

    //初识化boundary信息
    //进行umask文件掩码的设置
    //完成first_boundary --middle_boundary -- last_boundary的获取
    bool InitUploadInfo()
    {
      umask(0);
      char* ptr = getenv("Content-Length");
      if(ptr == nullptr)
      {
        fprintf(stderr, "have no content_length");
        return false;
      }

      content_len = Utils::StrToDigit(ptr);
        
      //获取文件类型中的boundary
      ptr = getenv("Content-Type");
      if(ptr == nullptr)
      {
        fprintf(stderr, "have no content_type");
        return false;
      }
      string boundary_sep = "boundary=";
      string content_type = ptr;
      size_t pos = content_type.find(boundary_sep);
      if(pos == string::npos)
      {
        fprintf(stderr, "content_type have no bundary");
        return false;
      }

      string boundary;
      boundary = content_type.substr(pos + boundary_sep.length());
      _f_boundary = "--" + boundary;
      _m_boundary = "\r\n" + _f_boundary + "\r\n";
      _l_boundary = "\r\n" + _f_boundary +"--";

      return  true;
    }

    //对正文进行处理,将文件数据进行存储(处理文件上传)
    //只有在获得完整的middle分隔符或者是last分隔符才可以关闭文件,否则就不关闭文件
    //对于每一个if都要进行查找一次分隔符,为了一次要将从管道中得到的数据一次性遍历完
    bool ProcessUpload()
    {
      //clen:当前已经读取的长度
      //blen: buffer的长度
      int64_t clen = 0;
      int64_t blen = 0;
      char buf[MAX_BUFF] = {0};

      while(clen < content_len)
      {
        //从管道中将数据读出来
        int len = read(0, buf + blen, MAX_BUFF - blen);
        blen += len;
        int boundary_pos;
        int content_pos;

        int flag = MatchBoundary(buf, blen, &boundary_pos);
        if(flag == BOUNDARY_FIRST)
        {
          //匹配到开始的boundary，
          //从boundary读取文件名
          //若获取文件名成功则创建文件,打开文件
          //将文件信息从buf中删除,剩下的数据进一步匹配
          if(GetfileName(buf, &boundary_pos))
          {
            CreateFile();
            //buf里的数据长度剪短
            blen -= content_pos;
            //匹配到就将数据删除,将从数据内容到结尾的数据向前移动覆盖前面的数据,第三个参数是blen，上一步已经减过
            memmove(buf, buf + content_pos, blen);
            memset(buf +blen, 0, content_pos);
          }
          else{
            //有可能不是上传文件,没有filename所以匹配到了_f_boundary也要去掉
            //没有匹配成功就把boundary分隔符的内容去除,因为此时的content_pos的位置未找到
            blen -= boundary_pos;
            memmove(buf, buf + boundary_pos, blen);
            memset(buf + blen, 0, boundary_pos);
          }
        }
        while(1)
        {
          //没有匹配到middle分隔符就跳出循环
          flag = MatchBoundary(buf, blen, &boundary_pos);
          if(flag != BOUNDARY_MIDDLE)
          {
            break;
          }

          //匹配middle_boundary成功 
          //将boundary之前的数据写入文件,将数据从buf中移除
          //关闭文件
          //看middle_boundary是否有文件名
          WriteFile(buf,boundary_pos);
          CloseFile();

          //将数据从buf移除
          blen -= boundary_pos;
          memmove(buf,buf + boundary_pos, blen);
          memset(buf + blen, 0, content_pos);
          
          if(GetfileName(buf, &content_pos))
          {
            CreateFile();
            //将内容以及middle分隔符头部去掉
            blen -= content_pos;
            memmove(buf, buf + content_pos, blen);
            memset(buf + blen, 0, content_pos);
          }
          else{
            //这里的middle分隔符,后面的数据不是为了上传文件
            //头信息不全跳出循环,等待再次从缓冲区中拿出数据再循环判断
            if(content_pos == 0)
            {
              //break;
            }

            //没有找到名字或者名字后面的"
            //没有匹配成功就把boundary去除,防止下次进入再找这个boundary
            blen -= _m_boundary.length();
            memmove(buf, buf + _m_boundary.length(), blen);
            memset(buf + blen, 0, content_pos);
          }
        }

        flag = MatchBoundary(buf, blen, &boundary_pos);
        if (flag == BOUNDARY_LAST)
        {
          //last_boundary匹配成功
          //1.将boundary之前的数据写入文件
          //2.关闭文件
          //3.传文件处理完毕，退出
          WriteFile(buf, boundary_pos); 
          CloseFile();
          return true;

        }

        flag = MatchBoundary(buf, blen, &boundary_pos);
        if(flag == BOUNDARY_PART)
        {
          //将boundary之前的数据写入文件
          //移除之前的数据
          //剩下的数据不动,重新接受数据,补全后匹配
          WriteFile(buf, boundary_pos);
          blen -= boundary_pos;
          memmove(buf, buf + boundary_pos, blen);
          memset(buf + blen, 0, boundary_pos);
        }

        flag = MatchBoundary(buf, blen, &boundary_pos);
        if(flag == BOUNDARY_NO)
        {
          //将所有数据写入文件
          WriteFile(buf,blen);
          blen = 0;
        }
        clen += len;
      }
      return true;
    }

};

int main()
{
  //将管道中的数据读取完毕,父进程才将html页面发送给浏览器
  Upload upload;
  string rsp_body;

  if(upload.InitUploadInfo() == false)
  {
    return 0;
  }
  if(upload.ProcessUpload() == false)
  {
    rsp_body = "<html><body><h1>FALSE</h1></body></html>";
  }
  else{
    rsp_body = "<html><body<h1>SUCCESS</h1></body><html>";
  }

  //将数据写到标准输出,就会写到管道中
  cout << rsp_body;
  fflush(stdout);
  return 0;
}
