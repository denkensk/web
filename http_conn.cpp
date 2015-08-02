#include "http_conn.h"

//����HTTP��Ӧ��һЩ״̬��Ϣ
const char* ok_200_title = "OK";
const char* error_400_title = "Bad Request";
const char* error_400_form = "Your request has bad syntax or is inherently impossible to satisfy.\n";
const char* error_403_title = "Forbidden";
const char* error_403_form = "You do not have permission to get file from this server.\n";
const char* error_404_title = "Not Found";
const char* error_404_form = "The requested file was not found on this server.\n";
const char* error_500_title = "Internal Error";
const char* error_500_form = "There was an unusual problem serving the requestd file.\n";

//��վ��Ŀ¼
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
	//TCP���ӱ��Է��رգ����߶Է��ر���д����������GUN����
	event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
	if (one_shot)
	{
		//һ��socket��������һʱ�̶�ֻ��һ���̴߳���
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

//�޸�epollfd��ʶ��epoll�ں��¼�����ע��fd�ϵ��¼�
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

//��״̬��
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

//ѭ����ȡ�ͻ����ݣ�ֱ�������ݿɶ����߶Է��ر�����
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
			//EAGAIN: Ӧ�ó�������û�����ݿɶ����Ժ�����
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

//����HTTP���󺽣�������󷽷���Ŀ��URL���Լ�HTTP�汾��
http_conn::HTTP_CODE http_conn::parse_request_line(char* text)
{
	//���μ����ַ���s1�е��ַ������������ַ����ַ���s2��Ҳ����ʱ��
	//��ֹͣ���飬�����ظ��ַ�λ�ã����ַ�null���������ڡ�
	m_url = strpbrk(text, " \t");

	//�����������û�пհ��ַ���"\t"�ַ�����HTTP�����������
	if (!m_url)
	{
		return BAD_REQUEST;
	}
	*m_url++ = '\0';

	char* method = text;
	//���Դ�Сд�Ƚ��ַ���
	if (strcasecmp(method, "GET") == 0)
	{
		//��֧��GET����
		m_method = GET;
	}
	else
	{
		return BAD_REQUEST;
	}

	//�����ַ����е�һ������ָ���ַ����г��ֵ��ַ��±�
	//
	m_url += strspn(m_url, " \t");
	m_version = strpbrk(m_url, " \t");

	if (!m_version)
	{
		return BAD_REQUEST;
	}

	*m_version++ = '\0';
	m_version += strspn(m_version, " \t");

	//��֧��HTTP/1.1
	if (strcasecmp(m_version, "HTTP/1.1") != 0)
	{
		return BAD_REQUEST;
	}

	//���URL�Ƿ�Ϸ�
	if (strncasecmp(m_url, "http://", 7) == 0)
	{
		m_url += 7;
		//�����ַ���s���״γ����ַ�c��λ�ã�ʧ�ܷ���NULL
		m_url = strchr(m_url, '/');
	}

	if (!m_url || m_url[0] != '/')
	{
		return BAD_REQUEST;
	}

	m_check_state = CHECK_STATE_HEADER;
	return NO_REQUEST;
}

//����HTTP�����һ��ͷ����Ϣ
http_conn::HTTP_CODE http_conn::parse_headers(char *text)
{
	//�������У���ʾͷ���ֶν������
	if (text[0] == '\0')
	{
		//���HTTP��������Ϣ�壬����Ҫ��ȡm_content_length�ֽڵ���Ϣ��
		//״̬��ת�Ƶ�CHECK_STATE_CONTENT
		if (m_content_length != 0)
		{
			m_check_state = CHECK_STATE_CONTENT;
			return NO_REQUEST;
		}

		//����˵�������Ѿ��õ���һ��������HTTP����
		return GET_REQUEST;
	}
	//����Connectionͷ���ֶ�
	else if (strncasecmp(text, "Connection:", 11) == 0)
	{
		text += 11;
		text += strspn(text, " \t");
		if (strcasecmp(text, "keep-alive") == 0)
		{
			m_linger = true;
		}
	}
	//����Content-Lengthͷ���ֶ�
	else if (strncasecmp(text, "Content-Length:", 15) == 0)
	{
		text += 15;
		text += strspn(text, " \t");
		//�ַ���ת��Ϊ������
		m_content_length = atol(text);
	}
	//����Hostͷ���ֶ�
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

//����û����������HTTP�������Ϣ�壬ֻ���ж����Ƿ������ö���
http_conn::HTTP_CODE http_conn::parse_content(char *text)
{
	if (m_read_idx >= (m_content_length + m_checked_idx))
	{
		text[m_content_length] = '\n';
		return GET_REQUEST;
	}
	return NO_REQUEST;
}

//��״̬��,����HTTP�������ں���
http_conn::HTTP_CODE http_conn::process_read()
{
	//��¼��ǰ�еĶ�ȡ״̬
	LINE_STATUS line_status = LINE_OK;
	//��¼HTTP����Ĵ�����
	HTTP_CODE ret = NO_REQUEST;
	char* text = 0;

	while (((m_check_state == CHECK_STATE_CONTENT) && (line_status == LINE_OK)) 
		|| ((line_status = parse_line()) == LINE_OK))
	{
		//m_start_line������m_read_buf�е���ʼλ��
		text = get_line();
		//��¼��һ�е���ʼλ��
		m_start_line = m_checked_idx;
		printf("got 1 http line: %s\n", text);

		//m_check_state��¼��״̬����ǰ��״̬
		switch (m_check_state)
		{
			//��һ��״̬������������
			case CHECK_STATE_REQUESTLINE:
			{
				ret = parse_request_line(text);	
				if (ret == BAD_REQUEST)
				{
					return BAD_REQUEST;
				}
				break;
			}
			//�ڶ���״̬������ͷ���ֶ�
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
			//��û�ж�ȡ��һ���������У����ʾ����Ҫ������ȡ�ͻ����ݲ��ܽ�һ������
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

//���õ�һ����������ȷ��HTTP����ʱ�����Ǿͷ���Ŀ���ļ������ԡ����Ŀ���ļ����ڣ��������û��ɶ���
//�ұ�ʾĿ¼����ʹ��mmap����ӳ�䵽�ڴ��ַm_file_address���������ߵ����߻�ȡ�ļ��ɹ�
http_conn::HTTP_CODE http_conn::do_request()
{
	strcpy(m_real_file, doc_root);
	int len = strlen(doc_root);
	strncpy(m_real_file + len, m_url, FILENAME_LEN - len - 1);

	if (stat(m_real_file, &m_file_stat) < 0)
	{
		return NO_RESOURCE;
	}

	//S_IROTH 00004 �����û��߿ɶ�ȡȨ��
	if (!(m_file_stat.st_mode & S_IROTH))
	{
		return FORBIDDEN_REQUEST;
	}

	//�ж��Ƿ�ΪĿ¼
	if (S_ISDIR(m_file_stat.st_mode))
	{
		return BAD_REQUEST;
	}
	
	int fd = open(m_real_file, O_RDONLY);
	//PROT_READ ҳ���ݿ��Ա���ȡ
	//MAP_PRIVATE ����һ��д��ʱ������˽��ӳ�䡣�ڴ������д�벻��Ӱ�쵽ԭ�ļ���
	m_file_address = (char*)mmap(0, m_file_stat.st_size, PROT_READ,
						MAP_PRIVATE, fd, 0);

	close(fd);
	return FILE_REQUEST;
}

//���ڴ�ӳ����ִ��munmap����
void http_conn::unmap()
{
	if (m_file_address)
	{
		munmap(m_file_address, m_file_stat.st_size);
		m_file_address = 0;
	}
}

//дHTTP��Ӧ
bool http_conn::write()
{
	int temp = 0;
	int bytes_have_send = 0;
	int bytes_to_send = m_write_idx;
	if (bytes_to_send == 0)
	{
		//EPOLLIN:���ݿɶ�
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
		//����HTTP��Ӧ�ɹ�������HTTP�����е�Connection�ֶξ޶��Ƿ������ر�����
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

//��д������д������͵�����
bool http_conn::add_response(const char* format, ...)
{
	if (m_write_idx >= WRITE_BUFFER_SIZE)
	{
		return false;
	}

	va_list arg_list;
	va_start(arg_list, format);
	//���ɱ������ʽ�������һ���ַ�����
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

//���ݷ���������HTTP����Ľ�����������ظ��ͻ��˵�����
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

//���̳߳��еĹ����̵߳��ã����Ǵ���HTTP�������ں���
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
