/*
 * FileName: threadpool.hpp
 * Description: 定义一个线程池的模板类，主线程向工作队列中插入任务，工作线程通过竞争来取得任务并执行
 *              实例化线程池之后，通过函数append向工作队列中添加任务
 */

#ifndef _THREADPOOL_H_
#define _THREADPOOL_H_

#include "locker.h"
#include <list>
#include <exception>
#include <pthread.h>
#include <cstdio>

/**
 * 线程池类,定义为模板类
 * 模板参数T是任务类
 */
template< typename T >
class threadpool
{
public:

	threadpool(int thread_number = 8, int max_requests = 10000);
	~threadpool();
	
	//往请求队列中添加任务
	bool append(T* request);

private:
	static void* worker(void* arg);
	void run();

private:
	int m_thread_number;			//线程池中线程数
	int m_max_requests;			//请求队列中允许的最大请求数
	pthread_t* m_threads;			//描述线程池的数组，其大小为m_thread_number
	std::list< T* > m_workqueue;		//请求队列
	locker m_queuelocker;			//保护请求队列的互斥锁
	sem m_queuestat;			//是否有任务需要处理
	bool m_stop;				//是否结束线程
};

/* 
 * Description: 初始化成员变量，创建工作线程，并将工作线程设置为脱离线程
 * Input： thread_number：线程池中线程的数量  max_requests： 请求队列中最多允许的、等待处理的请求数量
 */
template<typename T>
threadpool<T>::threadpool(int thread_number, int max_requests)
		: m_thread_number(thread_number), m_max_requests(max_requests),
		m_stop(false), m_threads(NULL)
{
	if ((thread_number <= 0) || (max_requests <= 0))
	{
		throw std::exception();
	}

	m_threads = new pthread_t[m_thread_number];
	if (!m_threads)
	{
		throw std::exception();
	}

	//创建thread_number个线程，并将它们都设置为脱离线程
	for (int i = 0; i < m_thread_number; ++i)
	{
		printf("create the %dth thread\n", i);
		if (pthread_create(m_threads + i, NULL, worker, this) != 0)
		{
			delete [] m_threads;
			throw std::exception();
		}
		//脱离线程，并且线程运行结束后自动释放所有资源
		if (pthread_detach(m_threads[i]))
		{
			delete [] m_threads;
			throw std::exception();
		}
	}
}

/* 
 * Description: 释放线程池数组的内存，修改标志位终止工作线程
 */
template<typename T>
threadpool<T>::~threadpool()
{
	delete [] m_threads;
	m_stop = true;
}

/* 
 * Description: 向工作队列中添加任务
 * Input： request： 类型为T的工作任务
 * Return： ture：添加成功  false： 工作队列中任务书已经超过最大限制
 */
template<typename T>
bool threadpool<T>::append(T* request)
{
	//操作工作队列时一定要加锁，因为它被所有线程共享
	m_queuelocker.lock();
	if (m_workqueue.size() > m_max_requests)
	{
		m_queuelocker.unlock();
		return false;
	}
	m_workqueue.push_back(request);
	m_queuelocker.unlock();
	m_queuestat.post();
	return true;
}

/* 
 * Description: 线程的工作函数
 * Input： arg： 类型为void*的this指针
 */
template<typename T>
void* threadpool<T>::worker(void* arg)
{
	threadpool* pool = (threadpool*)arg;
	pool->run();
	return pool;
}

/* 
 * Description: 工作线程循环运转，在工作队列中提取任务
 */
template<typename T>
void threadpool<T>::run()
{
	while (!m_stop)
	{
		m_queuestat.wait();
		m_queuelocker.lock();
		if (m_workqueue.empty())
		{
			m_queuelocker.unlock();
			continue;
		}
		T* request = m_workqueue.front();
		m_workqueue.pop_front();
		m_queuelocker.unlock();
		if(!request)
		{
			continue;
		}
		request->process();
	}
}
#endif
