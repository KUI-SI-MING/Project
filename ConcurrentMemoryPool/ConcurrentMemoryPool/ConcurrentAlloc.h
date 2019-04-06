#pragma once

#include "ThreadCache.h"
#include "PageCache.h"

void* ConcurrentAlloc(size_t size)
{
	if (size > MAXBYTES)
	{
		size_t roundsize = ClassSize::Alignmentsize(size,  1 << PAGE_SHIFT);//������ 2^10 = 1K //?
		size_t npage = roundsize >> PAGE_SHIFT;//��ȡҳ��

		Span* span = PageCache::GetInstance()->NewSpan(npage);
		void* ptr = (void*)(span->_pageid << PAGE_SHIFT);
		
		return ptr;
	}
	else
	{
		//ͨ��tsl����ȡ�߳��Լ���Thread Cache
		if (tls_threadcache == nullptr)
		{
			tls_threadcache = new ThreadCache;
		}
		return tls_threadcache->Allocate(size);
	}
}

void ConcurrentFree(void* ptr)
{
	Span* span = PageCache::GetInstance()->MapObjectToSpan(ptr);
	size_t size = span->_objsize;

	if (size > MAXBYTES)
	{
		PageCache::GetInstance()->ReleaseSpanToPageCache(span);
	}
	else
	{
		tls_threadcache->Deallocate(ptr, size);
	}
}