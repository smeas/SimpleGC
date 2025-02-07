#include "gc.h"

#include <stdlib.h>
#include <vector>
#include <unordered_set>
#include <algorithm>
#include <assert.h>
#include <mutex>
#include <thread>
#include <Windows.h>
#include <intrin.h>

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
	// FIXME: attempt to ret rid of padding.
	char _pad[8]; // Padding to ensure 16 byte alignment for allocations. See gc_new.
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

	// DANGER: This should be fixed to keep allocations aligned to 16 bytes, now they are essentially forced to 8 byte alignment.
	//size = (size + 7) & ~7;
	gc_header* alloc = (gc_header*)malloc(sizeof(gc_header) + size);
	if (!alloc) return NULL;

	alloc->value = size;

	void* objPtr = (alloc + 1);
	assert((uintptr_t)objPtr % 16 == 0); // we want user object allocations to be 16 byte aligned to mimic malloc behaviour.
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

void gc_mark_memory_range(struct gc_state* gc, void* start, void* end)
{
	static_assert(sizeof(uintptr_t) == 8, "");
	uintptr_t* a = (uintptr_t*)(((uintptr_t)start + 7) & ~7); // align to next 8
	uintptr_t* b = (uintptr_t*)((uintptr_t)end & ~7); // align to previous 8

	while (a < b)
	{
		void* value = (void*)*a;
		if (gc->objects.count(value) && !OBJ_GET_MARKED(value))
		{
			gc_mark_recursive(gc, value);
		}

		a += 1;
	}
}

void gc_mark_stack(struct gc_state* gc)
{
	NT_TIB *tib = (NT_TIB*)NtCurrentTeb();
	// this is an MSVC intrinsic that can be used instead of getting the rsp register via asm,
	// should give the same result since we are still in the current functions stack frame.
	void* pointerOnStack = _AddressOfReturnAddress();
	assert(tib->StackBase > pointerOnStack);
	assert(pointerOnStack > tib->StackLimit);
	gc_mark_memory_range(gc, pointerOnStack, tib->StackBase);
}

void gc_collect(gc_state* gc)
{
	gc_mark_stack(gc);

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