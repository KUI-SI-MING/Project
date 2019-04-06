#pragma once

#include "ConcurrentMemoryPool.h"

class ThreadCache
{
public:
	//������ͷ��ڴ����
	void* Allocate(size_t size);
	void Deallocate(void* ptr, size_t size);

	//�����Ļ����ȡ����
	void* ApplyFromCentralCache(size_t index, size_t size);

	//�����еĶ���̫�࣬��ʼ����
	void ListTooLong(FreeList* freelist, size_t byte);
private:
	FreeList _freelist[NLISTS];//��������
};

static _declspec(thread) ThreadCache* tls_threadcache = nullptr;//��������ƽ̨�¶�֧�־�̬tls���������ö�̬tls