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
	static const int FILENAME_LEN = 200;						//�ļ�������󳤶�
	static const int READ_BUFFER_SIZE = 2048;					//���������Ĵ�С
	static const int WRITE_BUFFER_SIZE = 1024;					//д�������Ĵ�С

	//HTTP���󷽷���������ֻ֧��GET
	enum METHOD { GET = 0, POST, HEAD, PUT, DELETE,
					TRACE, OPTIONS, CONNECT, PATCH };
	
	//�����ͻ�����ʱ����״̬��������״̬
	//�ֱ��ʾ����ǰ���ڷ��������У���ǰ���ڷ���ͷ���ֶΣ���ǰ���ڷ�������
	enum CHECK_STATE { CHECK_STATE_REQUESTLINE = 0,
						CHECK_STATE_HEADER,
						CHECK_STATE_CONTENT };

	/*
	����������HTTP����Ŀ��ܽ��
	NO_REQUEST����ʾ������������Ҫ������ȡ�ͻ�����
	GET_REQUEST����ʾ���һ�������Ŀͻ�����
	BAD_REQUEST����ʾ�ͻ��������﷨����
	NO_RESOURCE����ʾ�ͻ�������Դ������
	FORBIDDEN_REQUEST����ʾ�ͻ�����Դû���㹻�ķ���Ȩ��
	FILE_REQUEST��
	INTERNAL_ERROR����ʾ�������ڲ�����
	CLOSED_CONNECTION����ʾ�ͻ����Ѿ��ر�������
	*/
	enum HTTP_CODE { NO_REQUEST, GET_REQUEST, BAD_REQUEST,
					NO_RESOURCE, FORBIDDEN_REQUEST, FILE_REQUEST,
					INTERNAL_ERROR, CLOSED_CONNECTION };

	//�еĶ�ȡ״̬,��״̬�������ֿ���״̬�����еĶ�ȡ״̬
	//�ֱ��ʾ����ȡһ���������У��г�������������Ҳ�����
	enum LINE_STATUS { LINE_OK = 0, LINE_BAD, LINE_OPEN };

public:
	http_conn() {}
	~http_conn() {}

public:
	//��ʼ���½��ܵ�����
	void init(int sockfd, const sockaddr_in& addr);
	//�ر�����
	void close_conn(bool real_close = true);
	//����ͻ�����
	void process();
	//������������
	bool read();
	//������д����
	bool write();

private:
	//��ʼ������
	void init();
	//����HTTP����
	HTTP_CODE process_read();
	//���HTTPӦ��
	bool process_write(HTTP_CODE ret);

	//����һ�麯����process_read�����Է���HTTP����
	HTTP_CODE parse_request_line(char* text);
	HTTP_CODE parse_headers(char* text);
	HTTP_CODE parse_content(char* text);
	HTTP_CODE do_request();
	char* get_line() {return m_read_buf + m_start_line;}
	LINE_STATUS parse_line();

	//����һ�麯����process_write���������HTTPӦ��
	void unmap();
	bool add_response(const char* format, ...);
	bool add_content(const char* content);
	bool add_status_line(int status, const char* title);
	void add_headers(int content_length);
	bool add_content_length(int content_length);
	bool add_linger();
	bool add_blank_line();

public:
	//����socket�ϵ�ʱ�䶼��ע�ᵽͬһ��epoll�ں��¼����У����Խ�epoll�ļ�����������Ϊ��̬��
	static int m_epollfd;
	//ͳ���û�����
	static int m_user_count;

private:
	//��HTTP���ӵ�socket�ͶԷ���socket��ַ
	int m_sockfd;
	sockaddr_in m_address;

	//��������
	char m_read_buf[READ_BUFFER_SIZE];
	//��ʶ���������Ѿ�����Ŀͻ����ݵ����һ���ֽڵ���һ��λ��
	int m_read_idx;
	//��ǰ���ڷ������ַ��ڶ��������е�λ��
	int m_checked_idx;
	//��ǰ���ڽ������е���ʼλ��
	int m_start_line;
	//д������
	char m_write_buf[WRITE_BUFFER_SIZE];
	//д�������д����͵��ֽ���
	int m_write_idx;

	//��״̬����ǰ������״̬
	CHECK_STATE m_check_state;
	//���󷽷�
	METHOD m_method;

	//�ͻ������Ŀ���ļ�������·���������ݵ���doc_root+m_url��doc_root����վ��Ŀ¼
	char m_real_file[FILENAME_LEN];
	//�ͻ������Ŀ���ļ����ļ���
	char* m_url;
	//HTTPЭ��汾�ţ����ǽ�֧��HTTP/1.1 
	char* m_version;
	//������
	char* m_host;
	//HTTP�������Ϣ��ĳ���
	int m_content_length;
	//HTTP�����Ƿ񱣳�����
	bool m_linger;

	//�ͻ������Ŀ���ļ���mmap���ڴ��е���ʼλ��
	char* m_file_address;
	//Ŀ���ļ���״̬��ͨ�������ǿ����ж��ļ��Ƿ���ڣ��Ƿ�ΪĿ¼���Ƿ�ɶ�������ȡ�ļ���С����Ϣ
	struct stat m_file_stat;

	//���ǽ�����writev��ִ��д���������Զ�������������Ա������m_iv_count��ʾ��д�ڴ�������
	struct iovec m_iv[2];
	int m_iv_count;
};
#endif
