#include "gc.h"

#include <stdlib.h>
#include <vector>
#include <unordered_set>
#include <algorithm>
#include <assert.h>
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
	// 1. most significant bit stores the mark.
	// 2. the rest of the value stores the size of the allocated object, excluding the header.
	size_t value;
};

constexpr size_t HI_BIT64 = (size_t)1 << 63;

#define OBJ_HEADER(obj_ptr) ((struct gc_header*)(obj_ptr) - 1)
#define OBJ_GET_SIZE(obj_ptr) (OBJ_HEADER(obj_ptr)->value & ~HI_BIT64)
#define OBJ_GET_MARKED(obj_ptr) (OBJ_HEADER(obj_ptr)->value > (HI_BIT64 - 1))
#define OBJ_SET_MARKED(obj_ptr) (OBJ_HEADER(obj_ptr)->value |= HI_BIT64)
#define OBJ_SET_UNMARKED(obj_ptr) (OBJ_HEADER(obj_ptr)->value &= ~HI_BIT64)

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
	static_assert(sizeof(gc_header) % 8 == 0, "");
	assert(size < HI_BIT64);

	//size = (size + 7) & ~7;
	gc_header* alloc = (gc_header*)malloc(sizeof(gc_header) + size);
	if (!alloc) return NULL;

	alloc->value = size;

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
	OBJ_SET_MARKED(obj);
	size_t size = OBJ_GET_SIZE(obj);
	static_assert(sizeof(uintptr_t) == 8, "");

	for (size_t i = 0; i < size / 8; i++)
	{
		void* testPtr = ((uintptr_t**)obj)[i];

		// TODO: The list contain check could be optimized if we knew the bounds of the gc heap.
		if (gc->objects.count(testPtr) && !OBJ_GET_MARKED(testPtr))
			gc_mark_recursive(gc, testPtr);
	}
}

void gc_collect(gc_state* gc)
{
	for (void* root : gc->roots)
		gc_mark_recursive(gc, root);

	for (void* obj : gc->objects)
	{
		if (!OBJ_GET_MARKED(obj))
			gc->toFree.push_back(obj);

		OBJ_SET_UNMARKED(obj);
	}

	for (void* obj : gc->toFree)
	{
		gc->objects.erase(obj);
		free(OBJ_HEADER(obj));
	}

	gc->toFree.clear();
}