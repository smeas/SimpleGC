## A simple conservative garbage collector implementation

I created this project in order to learn how to implement a simple garbage collector. My implementation is inspired by how the Boehm garbage collector works.

This is just a simple proof of concept project for learning, it should not be used in any production project.

### Implemented Features
- C API
- It will detect GC pointers in globals, stack, and registers
- It is possible to add additional custom roots if desired

### Current Limitations
- It currently only works on Windows with MSVC
- It will only work on a single thread.
- It only works with simple C-style data types. Destructors are not executed.
- It will not automatically detect loaded DLLs for globals scanning, but you can register them manually with a simple function call.

### Usage Example

```c++
gc_state* gc = gc_create();

int* intPtr = (int*)gc_new0(gc, sizeof(int));
DataType* data = gc_make<DataType>(gc);

gc_collect(gc); // no effect

// Later...
data = nullptr;

gc_collect(gc); // `data` may be collected
```