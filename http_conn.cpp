/*
 * FileName: http_conn.cpp
 * Description: 对http_conn进行定义
 */

#include "http_conn.h"

//定义HTTP响应的一些状态信息
const char* ok_200_title = "OK";
const char* error_400_title = "Bad Request";
const char* error_400_form = "Your request has bad syntax or is inherently impossible to satisfy.\n";
const char* error_403_title = "Forbidden";
const char* error_403_form = "You do not have permission to get file from this server.\n";
const char* error_404_title = "Not Found";
const char* error_404_form = "The requested file was not found on this server.\n";
const char* error_500_title = "Internal Error";
const char* error_500_form = "There was an unusual problem serving the requestd file.\n";

//网站根目录
const char* doc_root = "/root/http_conn";


int setnonblocking(int fd)
{
	int old_option = fcntl(fd, F_GETFL);
	int new_option = old_option | O_NONBLOCK;
	fcntl(fd, F_SETFL, new_option);
	return old_option;
}

void addfd(int epollfd, int fd, bool one_shot)
{
	epoll_event event;
	event.data.fd = fd;
	//TCP连接被对方关闭，或者对方关闭了写操作，它由GUN引入
	event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
	if (one_shot)
	{
		//一个socket连接在任一时刻都只被一个线程处理
		event.events |= EPOLLONESHOT;
	}
	epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
	setnonblocking(fd);
}

void removefd(int epollfd, int fd)
{
	epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
	close(fd);
}

//修改epollfd标识的epoll内核事件表中注册fd上的事件
void modfd(int epollfd, int fd, int ev)
{
	epoll_event event;
	event.data.fd = fd;
	event.events = ev | EPOLLONESHOT | EPOLLET | EPOLLRDHUP;
	epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}

int http_conn::m_user_count = 0;
int http_conn::m_epollfd = -1;

void http_conn::close_conn(bool real_close)
{
	if (real_close && (m_sockfd != -1))
	{
		removefd(m_epollfd, m_sockfd);
		m_sockfd = -1;
		m_user_count--;
	}
}

void http_conn::init(int sockfd, const sockaddr_in &addr)
{
	m_sockfd = sockfd;
	m_address = addr;

	addfd(m_epollfd, sockfd, true);
	m_user_count++;

	init();
}

void http_conn::init()
{
	m_check_state = CHECK_STATE_REQUESTLINE;
	m_linger = false;

	m_method = GET;
	m_url = 0;
	m_version = 0;
	m_content_length = 0;
	m_host = 0;
	m_start_line = 0;
	m_checked_idx = 0;
	m_read_idx = 0;
	m_write_idx = 0;

	memset(m_read_buf, '\0', READ_BUFFER_SIZE);
	memset(m_write_buf, '\0', WRITE_BUFFER_SIZE);
	memset(m_real_file, '\0', FILENAME_LEN);
}

//从状态机
http_conn::LINE_STATUS http_conn::parse_line()
{
	char temp;
	for (; m_checked_idx < m_read_idx; ++m_checked_idx)
	{
		temp = m_read_buf[m_checked_idx];
		if (temp == '\r')
		{
			if ((m_checked_idx + 1) == m_read_idx)
			{
				return LINE_OPEN;
			}
			else if (m_read_buf[m_checked_idx + 1] == '\n')
			{
				m_read_buf[m_checked_idx++] = '\0';
				m_read_buf[m_checked_idx++] = '\0';
				return LINE_OK;
			}
			return LINE_BAD;
		}
		else if (temp == '\n')
		{
			if ((m_checked_idx > 1) && (m_read_buf[m_checked_idx - 1] == '\r'))
			{
				m_read_buf[m_checked_idx - 1] = '\0';
				m_read_buf[m_checked_idx++] = '\0';
				return LINE_OK;
			}
			return LINE_BAD;
		}
	}
	return LINE_OPEN;
}

//循环读取客户数据，直到无数据可读或者对方关闭连接
bool http_conn::read()
{
	if (m_read_idx >= READ_BUFFER_SIZE)
	{
		return false;
	}

	int bytes_read = 0;
	while (true)
	{
		bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);
		if (bytes_read == -1)
		{
			//EAGAIN: 应用程序现在没有数据可读请稍后再试
			if ((errno == EAGAIN) || (errno == EWOULDBLOCK))
			{
				break;
			}
			return false;
		}
		else if (bytes_read == 0)
		{
			return false;
		}
		m_read_idx += bytes_read;
	}
	return true;
}

//解析HTTP请求航，获得请求方法、目标URL，以及HTTP版本号
http_conn::HTTP_CODE http_conn::parse_request_line(char* text)
{
	//依次检验字符串s1中的字符，当被检验字符在字符串s2中也包含时，
	//则停止检验，并返回该字符位置，空字符null不包括在内。
	m_url = strpbrk(text, " \t");

	//如果请求行中没有空白字符或"\t"字符，则HTTP请求必有问题
	if (!m_url)
	{
		return BAD_REQUEST;
	}
	*m_url++ = '\0';

	char* method = text;
	//忽略大小写比较字符串
	if (strcasecmp(method, "GET") == 0)
	{
		//仅支持GET方法
		m_method = GET;
	}
	else
	{
		return BAD_REQUEST;
	}

	//返回字符串中第一个不在指定字符串中出现的字符下标
	//
	m_url += strspn(m_url, " \t");
	m_version = strpbrk(m_url, " \t");

	if (!m_version)
	{
		return BAD_REQUEST;
	}

	*m_version++ = '\0';
	m_version += strspn(m_version, " \t");

	//仅支持HTTP/1.1
	if (strcasecmp(m_version, "HTTP/1.1") != 0)
	{
		return BAD_REQUEST;
	}

	//检查URL是否合法
	if (strncasecmp(m_url, "http://", 7) == 0)
	{
		m_url += 7;
		//查找字符串s中首次出现字符c的位置，失败返回NULL
		m_url = strchr(m_url, '/');
	}

	if (!m_url || m_url[0] != '/')
	{
		return BAD_REQUEST;
	}

	m_check_state = CHECK_STATE_HEADER;
	return NO_REQUEST;
}

//解析HTTP请求的一个头部信息
http_conn::HTTP_CODE http_conn::parse_headers(char *text)
{
	//遇到空行，表示头部字段解析完毕
	if (text[0] == '\0')
	{
		//如果HTTP请求有消息体，则还需要读取m_content_length字节的消息体
		//状态机转移到CHECK_STATE_CONTENT
		if (m_content_length != 0)
		{
			m_check_state = CHECK_STATE_CONTENT;
			return NO_REQUEST;
		}

		//否则说明我们已经得到了一个完整的HTTP请求
		return GET_REQUEST;
	}
	//处理Connection头部字段
	else if (strncasecmp(text, "Connection:", 11) == 0)
	{
		text += 11;
		text += strspn(text, " \t");
		if (strcasecmp(text, "keep-alive") == 0)
		{
			m_linger = true;
		}
	}
	//处理Content-Length头部字段
	else if (strncasecmp(text, "Content-Length:", 15) == 0)
	{
		text += 15;
		text += strspn(text, " \t");
		//字符串转化为长整型
		m_content_length = atol(text);
	}
	//处理Host头部字段
	else if (strncasecmp(text, "Host:", 5) == 0)
	{
		text += 5;
		text += strspn(text, " \t");
		m_host = text;
	}
	else
	{
		printf("oop! unknow header %s\n", text);
	}

	return NO_REQUEST;
}

//我们没有真正解析HTTP请求的消息体，只是判断它是否被完整得读入
http_conn::HTTP_CODE http_conn::parse_content(char *text)
{
	if (m_read_idx >= (m_content_length + m_checked_idx))
	{
		text[m_content_length] = '\n';
		return GET_REQUEST;
	}
	return NO_REQUEST;
}

//主状态机,分析HTTP请求的入口函数
http_conn::HTTP_CODE http_conn::process_read()
{
	//记录当前行的读取状态
	LINE_STATUS line_status = LINE_OK;
	//记录HTTP请求的处理结果
	HTTP_CODE ret = NO_REQUEST;
	char* text = 0;

	while (((m_check_state == CHECK_STATE_CONTENT) && (line_status == LINE_OK)) 
		|| ((line_status = parse_line()) == LINE_OK))
	{
		//m_start_line是行在m_read_buf中的起始位置
		text = get_line();
		//记录下一行的起始位置
		m_start_line = m_checked_idx;
		printf("got 1 http line: %s\n", text);

		//m_check_state记录主状态机当前的状态
		switch (m_check_state)
		{
			//第一个状态，分析请求行
			case CHECK_STATE_REQUESTLINE:
			{
				ret = parse_request_line(text);	
				if (ret == BAD_REQUEST)
				{
					return BAD_REQUEST;
				}
				break;
			}
			//第二个状态，分析头部字段
			case CHECK_STATE_HEADER:
			{
				ret = parse_headers(text);
				if (ret == BAD_REQUEST)
				{
					return BAD_REQUEST;
				}
				else if (ret == GET_REQUEST)
				{
					return do_request();
				}
				break;
			}
			//若没有读取到一个完整的行，则表示还需要继续读取客户数据才能进一步分析
			case CHECK_STATE_CONTENT:
			{
				ret = parse_content(text);
				if (ret == GET_REQUEST)
				{
					return do_request();
				}
				line_status = LINE_OPEN;
				break;
			}
			default:
			{
				return INTERNAL_ERROR;
			}
		}
	}
	return NO_REQUEST;
}

//当得到一个完整、正确的HTTP请求时，我们就分析目标文件的属性。如果目标文件存在，对所有用户可读，
//且表示目录，则使用mmap将其映射到内存地址m_file_address处，并告诉调用者获取文件成功
http_conn::HTTP_CODE http_conn::do_request()
{
	strcpy(m_real_file, doc_root);
	int len = strlen(doc_root);
	strncpy(m_real_file + len, m_url, FILENAME_LEN - len - 1);

	if (stat(m_real_file, &m_file_stat) < 0)
	{
		return NO_RESOURCE;
	}

	//S_IROTH 00004 其他用户具可读取权限
	if (!(m_file_stat.st_mode & S_IROTH))
	{
		return FORBIDDEN_REQUEST;
	}

	//判断是否为目录
	if (S_ISDIR(m_file_stat.st_mode))
	{
		return BAD_REQUEST;
	}
	
	int fd = open(m_real_file, O_RDONLY);
	//PROT_READ 页内容可以被读取
	//MAP_PRIVATE 建立一个写入时拷贝的私有映射。内存区域的写入不会影响到原文件。
	m_file_address = (char*)mmap(0, m_file_stat.st_size, PROT_READ,
						MAP_PRIVATE, fd, 0);

	close(fd);
	return FILE_REQUEST;
}

//对内存映射区执行munmap操作
void http_conn::unmap()
{
	if (m_file_address)
	{
		munmap(m_file_address, m_file_stat.st_size);
		m_file_address = 0;
	}
}

//写HTTP响应
bool http_conn::write()
{
	int temp = 0;
	int bytes_have_send = 0;
	int bytes_to_send = m_write_idx;
	if (bytes_to_send == 0)
	{
		//EPOLLIN:数据可读
		modfd(m_epollfd, m_sockfd, EPOLLIN);
		init();
		return true;
	}

	while(1)
	{
		temp = writev(m_sockfd, m_iv, m_iv_count);
		if (temp <= -1)
		{
			modfd(m_epollfd, m_sockfd, EPOLLOUT);
			return true;
		}
		unmap();
		return false;
	}

	bytes_to_send -= temp;
	bytes_have_send += temp;
	if (bytes_to_send <= bytes_have_send)
	{
		//发送HTTP响应成功，根据HTTP请求中的Connection字段巨鼎是否立即关闭连接
		unmap();
		if (m_linger)
		{
			init();
			modfd(m_epollfd, m_sockfd, EPOLLIN);
			return true;
		}
		else
		{
			modfd(m_epollfd, m_sockfd, EPOLLIN);
			return true;
		}
	}
}

//往写缓冲中写入待发送的数据
bool http_conn::add_response(const char* format, ...)
{
	if (m_write_idx >= WRITE_BUFFER_SIZE)
	{
		return false;
	}

	va_list arg_list;
	va_start(arg_list, format);
	//将可变参数格式化输出到一个字符数组
	int len = vsnprintf(m_write_buf + m_write_idx, WRITE_BUFFER_SIZE - 1 - m_write_idx, 
						format, arg_list);

	if (len >= (WRITE_BUFFER_SIZE - 1 - m_write_idx))
	{
		return false;
	}

	m_write_idx += len;
	va_end(arg_list);
	return true;
}

bool http_conn::add_status_line(int status, const char* title)
{
	return add_response("%s %d %s\r\n", "HTTP/1.1", status, title);
}

void http_conn::add_headers(int content_len)
{
	add_content_length(content_len);
	add_linger();
	add_blank_line();
}

bool http_conn::add_content_length(int content_len)
{
	return add_response("Content-Length: %d\r\n", content_len);
}

bool http_conn::add_linger()
{
	return add_response("Connection: %s\r\n", (m_linger == true) ? "keep-alive" : "close");
}

bool http_conn::add_blank_line()
{
	return add_response("%s", "\r\n");
}

bool http_conn::add_content(const char* content)
{
	return add_response("%s", content);
}

//根据服务器处理HTTP请求的结果，决定返回给客户端的内容
bool http_conn::process_write(HTTP_CODE ret)
{
	switch (ret)
	{
		case INTERNAL_ERROR:
		{
			add_status_line(500, error_500_title);
			add_headers(strlen(error_500_form));
			if (!add_content(error_500_form))
			{
				return false;
			}
			break;
		}

		case BAD_REQUEST:
		{
			add_status_line(400, error_400_title);
			add_headers(strlen(error_400_form));
			if (!add_content(error_400_form))
			{
				return false;
			}
			break;
		}

		case NO_RESOURCE:
		{
			add_status_line(404, error_404_title);
			add_headers(strlen(error_404_form));
			if (!add_content(error_404_form))
			{
				return false;
			}
			break;
		}

		case FORBIDDEN_REQUEST:
		{
			add_status_line(403, error_403_title);
			add_headers(strlen(error_403_form));
			if (!add_content(error_403_form))
			{
				return false;
			}
			break;
		}

		case FILE_REQUEST:
		{
			add_status_line(200, ok_200_title);

			if (m_file_stat.st_size != 0)
			{
				add_headers(m_file_stat.st_size);
				m_iv[0].iov_base = m_write_buf;
				m_iv[0].iov_len = m_write_idx;
				m_iv[1].iov_base = m_file_address;
				m_iv[1].iov_len = m_file_stat.st_size;
				m_iv_count = 2;
				return true;
			}
			else
			{
				const char* ok_string = "<html><body></body></html>";
				add_headers(strlen(ok_string));
				if (!add_content(ok_string))
				{
					return false;
				}
			}
		}
		default:
		{
			return false;
		}
	}

	m_iv[0].iov_base = m_write_buf;
	m_iv[0].iov_len = m_write_idx;
	m_iv_count = 1;
	return true;
}

//由线程池中的工作线程调用，这是处理HTTP请求的入口函数
void http_conn::process()
{
	HTTP_CODE read_ret = process_read();
	if (read_ret == NO_REQUEST)
	{
		modfd(m_epollfd, m_sockfd, EPOLLIN);
		return;
	}

	bool write_ret = process_write(read_ret);
	if (!write_ret)
	{
		close_conn();
	}
	modfd(m_epollfd, m_sockfd, EPOLLOUT);
} 
