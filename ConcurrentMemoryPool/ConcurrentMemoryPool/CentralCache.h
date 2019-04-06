#pragma once
#include "ConcurrentMemoryPool.h"

class CentralCache
{
public:
	static CentralCache* GetInstance()
	{
		return &_inst;
	}

	//��Page Cache��ȡһ��span
	Span* GetOneSpan(SpanList* spanlist, size_t bytes);

	//��һ�������Ķ�������thread cache
	size_t DivideRangeObj(void*& start, void*& end, size_t n, size_t bytes);

	//��һ�������Ķ����ͷŵ�span���
	void ReleaseListToSpans(void* start, size_t byte_size);

private:
	//���Ļ�����������
	SpanList _spanList[NLISTS];
private:
	CentralCache() = default;
	CentralCache(const CentralCache&) = delete;
	CentralCache& operator=(const CentralCache&) = delete;

	//��������
	static CentralCache _inst;
};