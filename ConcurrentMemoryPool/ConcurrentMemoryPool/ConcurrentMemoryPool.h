#pragma once

#include <iostream>
#include <assert.h>
#include <stdlib.h>
#include <mutex>
#include <vector>
#include <thread>
#include <map>
#include <unordered_map>

#ifdef _WIN32
#include <windows.h>
#endif

using std::cout;
using std::cin;
using std::endl;

//���������������ĳ���
const size_t NLISTS = 240;
//����ڴ��ֽ���
const size_t MAXBYTES = 64 * 1024 ;
//����һҳ���ֽ���
const size_t PAGE_SHIFT = 12;
//ҳ��
const size_t NPAGES = 129;

//���������ڱ����ڼ����չ�������Ĳ���������inline����û�к�����ڵ�ֱ���ڵ��õ�չ��
//����static����inline����,�޸��ⲿ��������Ϊ�ڲ���������
static inline bool SystemFree(void* ptr)
{
#ifdef _WIN32

	VirtualFree(ptr , 0 , MEM_RELEASE);//�����ڴ棬����������׵�ַ
	if (ptr == nullptr)
	{
		throw std::bad_alloc();
		return false;
	}
#else
	//brk mumap
#endif // _WIN32

	return true;
}

static inline void* SystemAlloc(size_t npage)
{
#ifdef _WIN32
	//��ϵͳ����4k�ڴ�
	void* ptr = VirtualAlloc(NULL, (npage) << PAGE_SHIFT, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
	if (ptr == nullptr)
	{
		throw std::bad_alloc();
	}
#else
	//brk mumap
#endif // _WIN32

	return ptr;
}

inline static  void*& NEXT_OBJ(void* obj)
{
	return *((void**)obj);
}

typedef size_t PageID;
struct Span
{
	PageID _pageid = 0; // ҳ��
	size_t _npage = 0;  // ҳ������

	Span* _next = nullptr;
	Span* _prev = nullptr;

	void* _objlist = nullptr; //������������
	size_t _objsize = 0;//�����С
	size_t _usecount = 0;//ʹ�ü���
};

class SpanList
{
public:
	SpanList()
	{
		_head = new Span;
		_head->_next = _head;
		_head->_prev = _head;
	}

	Span* Begin()
	{
		return _head->_next;
	}

	Span* End()
	{
		return _head->_prev;
	}
	
	bool Empty()
	{
		return _head->_next == _head;
	}

	void Insert(Span* cur, Span* newspan)
	{
		assert(cur);
		Span* prev = cur->_prev;

		//prev newspan cur
		prev->_next = newspan;
		newspan->_prev = prev;
		newspan->_next = cur;
		cur->_prev = newspan;
	}

	void Erase(Span* del)
	{
		assert(del != nullptr && del != _head);
		Span* prev = del->_prev;
		Span* next = del->_next;

		prev->_next = next;
		next->_prev = prev;
	}

	void PushBack(Span* span)
	{
		Insert(End(), span);
	}

	Span* PopBack()
	{
		Span* tail = _head->_prev;
		Erase(tail);

		return tail;
	}

	void PushFront(Span* span)
	{
		Insert(Begin(), span);
	}

	Span* PopFront()
	{
		Span* span = Begin();
		Erase(span);
		return span;
	}

public:
	std::mutex _mtx;

private:
	Span* _head = nullptr;
};

class FreeList
{
public:
	bool Empty()
	{
		return _list == nullptr;
	}

	void PushRange(void* start, void* end, size_t num)
	{
		NEXT_OBJ(end) = _list;
		_list = start;
		_size += num;
	}

	void* Clear()
	{
		_size = 0;
		void* list = _list;
		_list = nullptr;
		return list;
	}
	void* Pop()
	{
		assert(_list != nullptr);

		void* obj = _list;
		_list = NEXT_OBJ(obj);
		--_size;

		return obj;
	}

	void Push(void* obj)
	{
		NEXT_OBJ(obj) = _list;
		_list = obj;
		++_size;
	}

	size_t Size()
	{
		return _size;
	}

	size_t MaxSize()
	{
		return _maxsize;
	}
	
	void SetMaxSize(size_t maxsize)
	{
		_maxsize = maxsize;
	}

private:
	void* _list = nullptr;//ʡ�Թ��캯��
	size_t _size = 0;
	size_t _maxsize = 1;
};

//�������ӳ��
class ClassSize
{
	// ������12%���ҵ�����Ƭ�˷�
	// [1,128]				8byte���� freelist[0,16)
	// [129,1024]			16byte���� freelist[16,72)
	// [1025,8*1024]	    128byte���� freelist[72,128)
	// [8*1024+1,64*1024]	512byte���� freelist[128,240)

public:
	//����
	static inline size_t Alignmentsize(size_t size, size_t align)
	{
		return (size + align - 1) & ~(align- 1);
	}

	//��������С
	static inline size_t Alignup(size_t size)
	{
		assert(size <= MAXBYTES);

		if (size <= 128)
		{
			return Alignmentsize(size, 8);
		}

		else if (size <= 1024)
		{
			return Alignmentsize(size, 16);
		}
		else if (size <= 8192)
		{
			return Alignmentsize(size, 128);
		}
		else if (size <= 65536)
		{
			return Alignmentsize(size, 512);
		}
		else
		{ 
			return -1;
		}
	}

	static inline size_t _Index(size_t bytes, size_t align_shift)
	{
		return (bytes + (1 << align_shift) - 1) >> align_shift - 1;//(16 + 7) >> 4 - 1
	}
	
	//ӳ������������λ��
	static inline size_t Index(size_t bytes)
	{
		assert(bytes <= MAXBYTES);

		//ÿ�������ж��ٸ���������
		static int groop_array[4] = { 16, 56, 56, 112 };
		if (bytes <= 128)
		{
			return _Index(bytes, 3);
		}
		else if (bytes <= 1024)
		{
			return _Index(bytes - 128, 4) + groop_array[0];
		}
		else if (bytes <= 8192)
		{
			return _Index(bytes - 1024, 7) + groop_array[1] + groop_array[0];
		}
		else if (bytes <= 65536)
		{
			return _Index(bytes - 8192, 9) + groop_array[2] + groop_array[1]+groop_array[0];
		}
		return -1;
	}

	//���ǵ�Ч�ʣ�һ���ƶ��ĸ���
	static size_t NumMoveSize(size_t bytes)
	{
		if (bytes == 0)
		{
			return 0;
		}

		int num = static_cast<int>(MAXBYTES / bytes);//?
		if (num < 2)
			num = 2;

		if (num > 512)
			num = 512;

		return num;
	}

	//����һ����ϵͳ��ȡ����ҳ
	static size_t NumMovePage(size_t bytes)
	{
		size_t span_num = NumMoveSize(bytes);
		size_t npage = span_num * bytes;
		npage >>= 12;

		//�����ڴ治��4k
		if (npage == 0)
		{
			npage = 1;
		}

		return npage;
	}
};