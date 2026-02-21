#include "gc.h"

#include <stdlib.h>
#include <assert.h>
#include <vector>
#include <unordered_set>
#include <Windows.h>
#include <intrin.h>

struct gc_data_segment
{
	void* start;
	void* end;
};

struct gc_state
{
	std::vector<void*> roots;
	std::unordered_set<void*> objects;
	std::vector<void*> toFree;
	std::vector<gc_data_segment> dataSegments;
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

#if _DEBUG
	#define dprintf(...) fprintf(stderr, __VA_ARGS__)
#else
	#define dprintf()
#endif

bool gc_find_segment(HMODULE hModule, const char* segmentName, void** outPointer, size_t* outSize)
{
	if (hModule == NULL)
		return false;

	//GetModuleHandle()
	IMAGE_DOS_HEADER* dosHeader = (IMAGE_DOS_HEADER*)hModule;
	if (dosHeader->e_magic != IMAGE_DOS_SIGNATURE)
		return false;

	IMAGE_NT_HEADERS* ntHeaders = (IMAGE_NT_HEADERS*)((char*)hModule + dosHeader->e_lfanew);
	if (ntHeaders->Signature != IMAGE_NT_SIGNATURE)
		return false;

	IMAGE_SECTION_HEADER* section = IMAGE_FIRST_SECTION(ntHeaders);
	WORD numSections = ntHeaders->FileHeader.NumberOfSections;

	for (int i = 0; i < numSections; i++)
	{
		if (strcmp((char*)section[i].Name, segmentName) == 0)
		{
			*outPointer = (void*)((char*)hModule + section[i].VirtualAddress);
			*outSize = section[i].Misc.VirtualSize;
			dprintf("segment %s @ %p : %i\n", segmentName, *outPointer, (int)*outSize);
			return true;
		}
	}

	return false;
}

// ---------------

void gc_add_root(struct gc_state* gc, void* obj)
{
	gc->roots.push_back(obj);
}

void gc_register_module(struct gc_state* gc, void* hModule)
{
	void* segmentPtr;
	size_t segmentSize;
	// The .bss segment may have been merged into the .data segment by the linker, and will in that case not be found.
	if (gc_find_segment((HMODULE)hModule, ".bss", &segmentPtr, &segmentSize))
		gc->dataSegments.push_back({segmentPtr, (char*)segmentPtr + segmentSize});

	if (gc_find_segment((HMODULE)hModule, ".data", &segmentPtr, &segmentSize))
		gc->dataSegments.push_back({segmentPtr, (char*)segmentPtr + segmentSize});
}

gc_state* gc_create()
{
	gc_state* gc = new gc_state;

	HMODULE module = GetModuleHandle(NULL);
	if (module != NULL)
		gc_register_module(gc, module);

	return gc;
}

size_t gc_get_object_count(gc_state* gc)
{
	return gc->objects.size();
}

void* gc_new(struct gc_state* gc, size_t size)
{
	static_assert(sizeof(gc_header) % 8 == 0, "");
	assert(size < HI_BIT64);

	gc_header* alloc = (gc_header*)malloc(sizeof(gc_header) + size);
	if (!alloc) return NULL;

	alloc->value = size;

	void* objPtr = (alloc + 1);
	assert((uintptr_t)objPtr % 16 == 0); // We want user object allocations to be 16 byte aligned to mimic malloc behaviour.
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
	dprintf("gc_mark_memory_range: %p -> %p\n", start, end);
	static_assert(sizeof(uintptr_t) == 8, "");
	uintptr_t* a = (uintptr_t*)(((uintptr_t)start + 7) & ~7); // align to next 8
	uintptr_t* b = (uintptr_t*)((uintptr_t)end & ~7); // align to previous 8

	while (a < b)
	{
		void* value = (void*)*a;
		if (gc->objects.count(value) && !OBJ_GET_MARKED(value))
		{
			dprintf("gc_mark_memory_range: object ptr %p @ %p\n", value, a);
			gc_mark_recursive(gc, value);
		}

		a += 1;
	}
}

// NOTE: This function CAN NOT be inlined nor have internal linkage!
//
// This could potentially cause the compiler to optimize away the return value, which is needed to prevent the compiler
// from making a tail call. A tail call would cause a return address not to be stored for this function, which would
// mean the callee's return address pointer would be used instead. The issue with this is that the set_jmp data for
// register scanning that the gc_mark_stack function sets up would then not be detected. Inlining would cause similar
// issues with the return address pointer.
//
// TLDR: Inlining, internal linkage, or tail call optimization might break register scanning.
__declspec(noinline)
int gc_mark_stack_inner(struct gc_state* gc)
{
	// This is an MSVC intrinsic that can be used instead of getting the rsp register via asm,
	// should give the same result since we are still in the current functions stack frame.
	void* pointerOnStack = _AddressOfReturnAddress();
	NT_TIB *tib = (NT_TIB*)NtCurrentTeb();

	assert(tib->StackBase > pointerOnStack);
	assert(pointerOnStack > tib->StackLimit);
	dprintf("gc_mark_stack: %p -> %p\n", pointerOnStack, tib->StackBase);

	gc_mark_memory_range(gc, pointerOnStack, tib->StackBase);
	// Register scanning will break if the optimizer inserts a tail call. We mustn't allow that. See comment at the top
	// of the function for more info.
	return 0;
}

void gc_mark_stack(struct gc_state* gc)
{
	// NOTE: This ensures any important registers are dumped to the stack, which will be scanned for GC pointers below.
	jmp_buf jmpbuf;
	setjmp(jmpbuf);

	// This function call must NOT be inlined to ensure that the jmp_buf is included in the stack scan.
	gc_mark_stack_inner(gc);
}

void gc_collect_continuation(gc_state* gc)
{
	size_t objCountBefore = gc_get_object_count(gc);

	for (const gc_data_segment& segment : gc->dataSegments)
		gc_mark_memory_range(gc, segment.start, segment.end);

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

	size_t objCountAfter = gc_get_object_count(gc);
	dprintf("collected %llu of %llu objects, %llu left alive.\n", objCountBefore - objCountAfter, objCountBefore, objCountAfter);
}

void gc_collect(gc_state* gc)
{
	gc_mark_stack(gc);
	// Continuation is split so that the stack is as small as possible for gc_mark_stack above. This reduces the scan
	// space a little bit and can also slightly lower the risk of false positives.
	gc_collect_continuation(gc);
}