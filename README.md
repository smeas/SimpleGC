## A simple conservative garbage collector implementation written in C-style C++.

### Implemented Features
- It will automatically detect GC pointers in globals, stack, and registers
- It is possible to add additional custom roots if desired

### Limitations
- It currently only works on Windows with MSVC
- It will only work on a single thread
- It will not automatically detect loaded DLLs for globals scanning, but you can register them manually

