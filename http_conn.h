/*
 * FileName: http_conn.h
 * Description: 将对http请求处理封装成类，当作线程池模板参数类
 */

#ifndef _HTTPCONNECTION_H_
#define _HTTPCONNECTION_H_

#include "locker.h"
#include <unistd.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <signal.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <assert.h>
#include <sys/stat.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <errno.h>


class http_conn
{
public:
	static const int FILENAME_LEN = 200;						//文件名的最大长度
	static const int READ_BUFFER_SIZE = 2048;					//读缓冲区的大小
	static const int WRITE_BUFFER_SIZE = 1024;					//写缓冲区的大小

	//HTTP请求方法，但我们只支持GET
	enum METHOD { GET, POST, HEAD, PUT, DELETE,
			TRACE, OPTIONS, CONNECT, PATCH };
	
	//解析客户请求时，主状态机所处的状态
	//分别表示：当前正在分析请求行，当前正在分析头部字段，当前正在分析正文
	enum CHECK_STATE { CHECK_STATE_REQUESTLINE, CHECK_STATE_HEADER,
				CHECK_STATE_CONTENT };

	/*
	 * 服务器处理HTTP请求的可能结果：
	 * NO_REQUEST：表示请求不完整，需要继续读取客户数据
	 * GET_REQUEST：表示获得一个完整的客户请求
	 * BAD_REQUEST：表示客户请求有语法错误
	 * NO_RESOURCE：表示客户请求资源不存在
	 * FORBIDDEN_REQUEST：表示客户对资源没有足够的访问权限
	 * INTERNAL_ERROR：表示服务器内部错误
	 * CLOSED_CONNECTION：表示客户端已经关闭连接了
	 */
	enum HTTP_CODE { NO_REQUEST, GET_REQUEST, BAD_REQUEST,
				NO_RESOURCE, FORBIDDEN_REQUEST, FILE_REQUEST,
				INTERNAL_ERROR, CLOSED_CONNECTION };

	/* 
	 * 行的读取状态, 从状态机的三种可能：
	 * LINE_OK： 读取一个完整的行
	 * LINE_BAD： 行出错
	 * LINE_OPEN： 行数据不完整
	 */
	enum LINE_STATUS { LINE_OK, LINE_BAD, LINE_OPEN };

public:
	http_conn() {}
	~http_conn() {}

public:
	void init(int sockfd, const sockaddr_in& addr);
	void close_conn(bool real_close = true);
	void process();
	bool read();
	bool write();

private:
	void init();
	HTTP_CODE process_read();
	bool process_write(HTTP_CODE ret);

	HTTP_CODE parse_request_line(char* text);
	HTTP_CODE parse_headers(char* text);
	HTTP_CODE parse_content(char* text);
	HTTP_CODE do_request();
	char* get_line() {return m_read_buf + m_start_line;}
	LINE_STATUS parse_line();

	void unmap();
	bool add_response(const char* format, ...);
	bool add_content(const char* content);
	bool add_status_line(int status, const char* title);
	void add_headers(int content_length);
	bool add_content_length(int content_length);
	bool add_linger();
	bool add_blank_line();

public:
	static int m_epollfd;	 			//所有socket上的事件都被注册到同一个epoll内核事件表中
	static int m_user_count;			//统计用户数量

private:
	int m_sockfd;					//HTTP连接的socket
	sockaddr_in m_address;				//对方的socket地址

	char m_read_buf[READ_BUFFER_SIZE];		//读缓冲区
	int m_read_idx;					//标识读缓冲中已经读入的客户数据的最后一个字节的下一个位置
	int m_checked_idx;				//当前正在分析的字符在读缓冲区中的位置
	int m_start_line;				//当前正在解析的行的起始位置
	char m_write_buf[WRITE_BUFFER_SIZE];		//写缓冲区
	int m_write_idx;				//写缓冲区中待发送的字节数

	CHECK_STATE m_check_state;			//主状态机当前所处的状态
	METHOD m_method;				//请求方法

	char m_real_file[FILENAME_LEN];			//客户请求的目标文件的完整路径
	char* m_url;					//客户请求的目标文件的文件名
	char* m_version;				//HTTP协议版本号
	char* m_host;					//主机名
	int m_content_length;				//HTTP请求的消息体的长度
	bool m_linger;					//HTTP请求是否保持连接

	char* m_file_address;				//客户请求的目标文件被mmap到内存中的起始位置
	struct stat m_file_stat;			//目标文件的状态

	struct iovec m_iv[2];
	int m_iv_count;					//m_iv_count表示被写内存块的数量
};
#endif
