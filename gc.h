#pragma once

struct gc_state;

void* gc_new(struct gc_state* gc, size_t size);
void* gc_new0(struct gc_state* gc, size_t size);
void gc_free(struct gc_state* gc, void* obj);
void gc_collect(struct gc_state* gc);
void gc_add_root(struct gc_state* gc, void* obj);
void gc_register_module(struct gc_state* gc, /* HMODULE */ void* hModule);
size_t gc_get_object_count(struct gc_state* gc);
struct gc_state* gc_create();

#ifdef __cplusplus
#include <type_traits>
#include <new>

template<typename T, typename... Args>
T* gc_make(gc_state* gc, Args&&... args)
{
	static_assert(std::is_trivially_destructible<T>::value, "T must be trivially destructible");
	T* value = (T*)gc_new(gc, sizeof(T));
	new (value) T(std::forward<Args>(args)...);
	return value;
}
#endif
