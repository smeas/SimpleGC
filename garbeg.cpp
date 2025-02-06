#include <assert.h>

#include "gc.h"

int main(int argc, char* argv[])
{
    gc_state* gc = gc_create();

    struct top
    {
        int* foo;
        const char** bar;
        short data;
        int* baz;
    }* obj = NULL;

    obj = (top*)gc_new0(gc, sizeof(top));
    gc_add_root(gc, obj);

    obj->foo = (int*)gc_new0(gc, sizeof(int));
    *obj->foo = 42;
    obj->bar = (const char**)gc_new0(gc, sizeof(const char*));
    *obj->bar = "Hello, world!";
    obj->data = 0x1234;
    obj->baz = (int*)gc_new0(gc, sizeof(int));
    *obj->baz = 42;

    assert(gc_get_object_count(gc) == 4);

    gc_collect(gc); // 4 obj left
    assert(gc_get_object_count(gc) == 4);

    obj->baz = NULL;

    gc_collect(gc); // 3 obj left
    assert(gc_get_object_count(gc) == 3);

    // // STACK REF
    // int* number = (int*)gc_new0(gc, sizeof(int));
    // assert(gc_get_object_count(gc) == 4);
    //
    // gc_collect(gc);
    // assert(gc_get_object_count(gc) == 4);

    return 0;
}
