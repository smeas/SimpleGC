#pragma once

struct gc_state;

void* gc_new(struct gc_state* gc, size_t size);
void* gc_new0(struct gc_state* gc, size_t size);
void gc_free(struct gc_state* gc, void* obj);
void gc_collect(struct gc_state* gc);
void gc_add_root(struct gc_state* gc, void* obj);
gc_state* gc_create();

size_t gc_get_object_count(struct gc_state* gc);