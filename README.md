# ObjectAllocator
This is a custom memory manager that can allocate and free memory blocks. It also can use padding bytes, header blocks, check for double frees and corruption, and more. The Object Allocator (OA) works by creating pages which can hold a certain amount of objects with equal size. When these objects are free, they will be added to the free list. When allocated, they get removed from the free list and are sent to the client.

driver.cpp, PRNG.cpp, Makefile are not written by me. Those files were given to me by my university for this project.
