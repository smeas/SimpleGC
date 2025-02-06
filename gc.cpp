#include "gc.h"

#include <stdlib.h>
#include <vector>
#include <unordered_set>
#include <algorithm>
#include <mutex>
#include <thread>
#include <Windows.h>

struct gc_state
{
	std::vector<void*> roots;
	std::unordered_set<void*> objects;
	std::vector<void*> toFree;
};


struct gc_header
{
	size_t size;
	int mark;
};
static_assert(sizeof(gc_header) % 8 == 0);

#define OBJ_HEADER(obj_ptr) ((struct gc_header*)(obj_ptr) - 1)
#define LIST_CONTAINS(list, elem) (std::find(std::begin(list), std::end(list), (elem)) != std::end(list))

void gc_add_root(struct gc_state* gc, void* obj)
{
	gc->roots.push_back(obj);
}

gc_state* gc_create()
{
	return new gc_state;
}

size_t gc_get_object_count(gc_state* gc)
{
	return gc->objects.size();
}

void* gc_new(struct gc_state* gc, size_t size)
{
	size = (size + 7) & ~7;
	gc_header* alloc = (gc_header*)malloc(sizeof(gc_header) + size);
	if (!alloc) return NULL;

	//*alloc = gc_header{};
	alloc->size = size;
	alloc->mark = 1;

	void* objPtr = (alloc + 1);
	gc->objects.insert(objPtr);
	return objPtr;
}

void* gc_new0(gc_state* gc, size_t size)
{
	void* obj = gc_new(gc, size);
	if (obj) memset(obj, 0, size);
	return obj;
}

void gc_free(gc_state* gc, void* obj)
{
	if (gc->objects.erase(obj))
		free(OBJ_HEADER(obj));
}

void gc_mark_recursive(gc_state* gc, void* obj)
{
	OBJ_HEADER(obj)->mark = 1;
	size_t size = OBJ_HEADER(obj)->size;
	for (size_t i = 0; i < size / 8; i++)
	{
		//void* testPtr = ((uintptr_t*)root + i);
		void* testPtr = ((uintptr_t**)obj)[i];

		// TODO: The list contain check could be optimized if we knew the bounds of the gc heap.
		if (gc->objects.count(testPtr) && !OBJ_HEADER(testPtr)->mark)
			gc_mark_recursive(gc, testPtr);
	}
}

void gc_collect(gc_state* gc)
{
	if (gc->objects.empty())
		return;

	for (void* obj : gc->objects)
		OBJ_HEADER(obj)->mark = 0;

	for (void* root : gc->roots)
		gc_mark_recursive(gc, root);

	for (void* obj : gc->objects)
	{
		if (!OBJ_HEADER(obj)->mark)
			gc->toFree.push_back(obj);
	}

	for (void* obj : gc->toFree)
	{
		gc->objects.erase(obj);
		free(OBJ_HEADER(obj));
	}

	gc->toFree.clear();
}