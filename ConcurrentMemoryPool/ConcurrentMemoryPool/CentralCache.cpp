#include "CentralCache.h"
#include "PageCache.h"

CentralCache CentralCache::_inst;

//��Page Cache��ȡһ��span
Span* CentralCache::GetOneSpan(SpanList* spanlist, size_t bytes)
{
	Span* span = spanlist->Begin();
	while (span != spanlist->End())
	{
		if (span->_objlist != nullptr)//��ǰspan�ж���
			return span;

		span = span->_next;
	}

	//��pagecache����һ���µĺ��ʴ�С��span
	size_t npage = ClassSize::NumMovePage(bytes);//��ȡҳ��
	Span* newspan = PageCache::GetInstance()->NewSpan(npage);//����ҳ������span

	//��span���ڴ��и��һ����bytes��С�Ķ��������
	char* start = (char*)(newspan->_pageid << PAGE_SHIFT);
	char* end = start + (newspan->_npage << PAGE_SHIFT);
	char* cur = start;
	char* next = cur + bytes;

	while (next < end)
	{
		NEXT_OBJ(cur) = next;
		cur = next;
		next = cur + bytes;
	}
	NEXT_OBJ(cur) = nullptr;
	newspan->_objlist = start;
	newspan->_objsize = bytes;
	newspan->_usecount = 0;

	//��newsapn���뵽spanlist
	spanlist->PushFront(newspan);
	return newspan;
}

//��һ�������Ķ�������thread cache
size_t CentralCache::DivideRangeObj(void*& start, void*& end, size_t num, size_t bytes)
{
	size_t index = ClassSize::Index(bytes);
	SpanList* spanlist = &_spanList[index];

	//��Ͱ����
	std::unique_lock<std::mutex> lock(spanlist->_mtx);

	Span* span = GetOneSpan(spanlist, bytes);
	void* cur = span->_objlist;
	void* prev = cur;
	size_t realnum = 0;

	while (cur != nullptr && realnum < num)
	{
		prev = cur;
		cur = NEXT_OBJ(cur);
		++realnum;
	}

	start = span->_objlist;
	end = prev;
	NEXT_OBJ(end) = nullptr;

	span->_objlist = cur;
	span->_usecount += realnum;
	
	//��һ��spanΪ�գ���span�ƶ���β��
	if (span->_objlist == nullptr)
	{
		spanlist->Erase(span);
		spanlist->PushBack(span);
	}

	return realnum;
}

void CentralCache::ReleaseListToSpans(void* start, size_t byte)
{
	size_t index = ClassSize::Index(byte);
	SpanList* spanlist = &_spanList[index];

	std::unique_lock<std::mutex> lock(spanlist->_mtx);//ԭ�Ӳ���
	while (start)
	{
		void *next = NEXT_OBJ(start);
		Span* span = PageCache::GetInstance()->MapObjectToSpan(start);//�õ������span

		//���ͷŶ���ص��յ�span����span�Ƶ�β�ϣ����Ч��
		if (span->_objlist == nullptr)
		{
			spanlist->Erase(span);
			spanlist->PushBack(span);
		}

		//�������span
		NEXT_OBJ(start) = span->_objlist;
		span->_objlist = start;

		//usecount==0��ʾspan�г�ȥ�Ķ��󶼻�����
		//�ͷ�span�ص�pagescache���кϲ�
		if (--span->_usecount == 0)
		{
			spanlist->Erase(span);

			span->_objlist = nullptr;
			span->_objlist = 0;
			span->_prev = nullptr;
			span->_next = nullptr;

			PageCache::GetInstance()->ReleaseSpanToPageCache(span);
		}

		start = next;
	}
}