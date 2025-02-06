#include "gc.h"

#include <stdlib.h>
#include <vector>
#include <algorithm>
#include <mutex>
#include <thread>
#include <Windows.h>

// bucket 8..64

struct gc_state
{
	std::vector<void*> roots;
	std::vector<void*> objects;
};


struct gc_header
{
	size_t size;
	int mark;
};

#define OBJ_HEADER(obj_ptr) ((struct gc_header*)(obj_ptr) - 1)
#define LIST_CONTAINS(list, elem) (std::find(std::begin(list), std::end(list), (elem)) != std::end(list))

void gc_add_root(struct gc_state* gc, void* obj)
{
	gc->roots.push_back(obj);
}

gc_state* gc_create()
{
	// TODO: Change after deleting C++...
	//gc_state* gc = (gc_state*)malloc(sizeof(gc_state));
	//memset(gc, 0, sizeof(gc_state));

	return new gc_state;
}

size_t gc_get_object_count(gc_state* gc)
{
	return gc->objects.size();
}

void* gc_new(struct gc_state* gc, size_t size)
{
	size = (size + 7) & ~7;
	void* alloc = malloc(sizeof(gc_header) + size);
	if (!alloc) return NULL;
	gc_header* header = (gc_header*)alloc;
	*header = gc_header {};
	header->size = size;
	header->mark = 1;
	void* obj = (char*)alloc + sizeof(gc_header);
	gc->objects.push_back(obj);
	return obj;
}

void* gc_new0(gc_state* gc, size_t size)
{
	void* obj = gc_new(gc, size);
	if (obj) memset(obj, 0, size);
	return obj;
}

void gc_free(gc_state* gc, void* obj)
{
	for (size_t i = 0; i < gc->objects.size(); i++)
	{
		if (gc->objects[i] == obj)
		{
			gc->objects.erase(gc->objects.begin() + i);
			free((char*)obj - sizeof(gc_header));
			return;
		}
	}
}

void gc_mark_root(gc_state* gc, void* root)
{
	OBJ_HEADER(root)->mark = 1;
	size_t size = OBJ_HEADER(root)->size;
	for (size_t i = 0; i < size / 8; i++)
	{
		//void* testPtr = ((uintptr_t*)root + i);
		void* testPtr = (((uintptr_t**)root)[i]);
		// The list contain check could be optimized if we knew the bounds of the gc heap.
		if (LIST_CONTAINS(gc->objects, testPtr) && !OBJ_HEADER(testPtr)->mark)
		{
			// Value is an unmarked obj ptr.
			//OBJ_HEADER(testPtr)->mark = 1;
			gc_mark_root(gc, testPtr);
		}
	}
}

void gc_collect(gc_state* gc)
{
	if (gc->objects.empty())
		return;

	for (void* obj : gc->objects)
		OBJ_HEADER(obj)->mark = 0;

	for (void* root : gc->roots)
		gc_mark_root(gc, root);

	size_t i = gc->objects.size() - 1;
	while (1)
	{
		if (!OBJ_HEADER(gc->objects[i])->mark)
		{
			free(OBJ_HEADER(gc->objects[i]));
			gc->objects.erase(gc->objects.begin() + i);
		}

		if (i == 0) break;
		i--;
	}
}