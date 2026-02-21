//
// This file tests the GC implementation. Please note that the stack test will
// fail on optimized builds, because the variable is optimized away or put in a
// register.
//

#include <cstring>
#include <malloc.h>

#include "gc.h"

#undef NDEBUG
#include <assert.h>

// Compiler hacks to ease feature testing.
// See helpers.asm
extern "C" void mov_to_r15(void*);
extern "C" void noop(void*);

__declspec(noinline)
void black_box(void* value)
{
    //printf("black_box: %p\n", value);
    noop(value);
}

__declspec(noinline)
void clear_stack_above()
{
    int count = 64000;
    char* ptr = (char*)alloca(count);
    memset(ptr, 0xbb, count);
    noop(ptr);
}

struct top_obj
{
    int* foo;
    const char** bar;
    short data;
    int* baz;
};

int* g_globalIntBss;
int* g_globalIntData = (int*)123;

int main(int argc, char* argv[])
{
    gc_state* gc = gc_create();

    top_obj* obj = gc_make<top_obj>(gc);
    //gc_add_root(gc, obj);

    obj->foo = (int*)gc_new0(gc, sizeof(int));
    *obj->foo = 15;
    obj->bar = (const char**)gc_new0(gc, sizeof(const char*));
    *obj->bar = "Hello, world!";
    obj->data = 0x1234;
    obj->baz = (int*)gc_new0(gc, sizeof(int));
    *obj->baz = 42;

    assert(gc_get_object_count(gc) == 4 && "allocations should have produced objects");

    clear_stack_above();
    gc_collect(gc); // 4 obj left
    assert(gc_get_object_count(gc) == 4 && "first collection should not collect any objects");


    // CLEAR SUB-OBJECT REF
    obj->baz = NULL;

    clear_stack_above();
    gc_collect(gc); // 3 obj left
    assert(gc_get_object_count(gc) == 3 && "sub-object reference should be collected");


    // STACK REF
    int* number = (int*)gc_new0(gc, sizeof(int));
    assert(gc_get_object_count(gc) == 4 && "stack ref should be allocated");

    clear_stack_above();
    gc_collect(gc);
    assert(gc_get_object_count(gc) == 4 && "stack ref should be tracked");

    number = NULL;

    clear_stack_above();
    gc_collect(gc);
    assert(gc_get_object_count(gc) == 3 && "nulled stack ref should be collected");


    // REGISTER REF
    int* number2 = (int*)gc_new0(gc, sizeof(int));
    assert(gc_get_object_count(gc) == 4 && "stack ref should be allocated");
    mov_to_r15(number2); // set register
    number2 = NULL; // clear stack ref
    // now pointer should be only in the r15 register
    clear_stack_above();
    gc_collect(gc);
    assert(gc_get_object_count(gc) == 4 && "register ref should be tracked");

    mov_to_r15(0); // clear register
    clear_stack_above();
    gc_collect(gc);
    assert(gc_get_object_count(gc) == 3 && "register ref should be cleared");


    // GLOBALS
    g_globalIntBss = (int*)gc_new0(gc, sizeof(int));
    g_globalIntData = (int*)gc_new0(gc, sizeof(int));
    assert(gc_get_object_count(gc) == 5 && "global refs should be allocated");

    clear_stack_above();
    gc_collect(gc);
    assert(gc_get_object_count(gc) == 5 && "global refs should be tracked");
    black_box(g_globalIntBss);
    black_box(g_globalIntData);

    g_globalIntBss = 0;
    g_globalIntData = 0;

    clear_stack_above();
    gc_collect(gc);
    assert(gc_get_object_count(gc) == 3 && "global refs should be cleared");
    black_box(g_globalIntBss);
    black_box(g_globalIntData);


    // keep obj alive in optimized builds
    black_box(obj);
    return 0;
}
