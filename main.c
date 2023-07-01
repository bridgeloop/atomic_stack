// aiden@cmp.bz

#include <stdalign.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdatomic.h>

#ifndef CACHELINE
#define CACHELINE 64
#endif
enum atomic_stack_field_state {
	asfs_available, asfs_pushing, asfs_ready, asfs_popping,
};
struct atomic_stack_field {
	alignas(CACHELINE) _Atomic enum atomic_stack_field_state state;
	void *ptr;
};
struct atomic_stack {
	alignas(CACHELINE) atomic_uint_fast64_t rc;
	alignas(CACHELINE) int_fast64_t cap;
	alignas(CACHELINE) atomic_int_fast64_t idx;
	alignas(CACHELINE) struct atomic_stack_field fields[];
};
struct atomic_stack *new(int_fast64_t n) {
	struct atomic_stack *stack = malloc(sizeof(struct atomic_stack) + sizeof(struct atomic_stack_field) * n);
	if (stack == NULL) {
		return NULL;
	}
	stack->rc = 1;
	stack->cap = n;
	stack->idx = 0;
	return stack;
}
bool push(struct atomic_stack *stack, bool block, void *ptr) {
	entry:;
	int_fast64_t idx = atomic_fetch_add(&(stack->idx), 1);
	if (idx < 0) {
		do {
			atomic_compare_exchange_strong(&(stack->idx), &(idx), 0);
		} while (idx < 0);
		goto entry;
	}
	if (idx >= stack->cap) {
		if (block) {
			while (stack->idx >= stack->cap) {
				__builtin_ia32_pause();
			}
			goto entry;
		} else {
			return false;
		}
	}

	struct atomic_stack_field *field = &(stack->fields[idx]);
	enum atomic_stack_field_state available = asfs_available;
	while (!atomic_compare_exchange_strong(&(field->state), &(available), asfs_pushing)) {
		available = asfs_available;
		__builtin_ia32_pause();
	}

	field->ptr = ptr;

	atomic_store(&(field->state), asfs_ready);
	return true;
}
#define atomic_sub_fetch(object, operand) (atomic_fetch_sub(object, operand) - operand)
bool pop(struct atomic_stack *stack, bool block, void **out) {
	entry:;
	int_fast64_t idx = atomic_sub_fetch(&(stack->idx), 1);
	if (idx >= stack->cap) {
		do {
			atomic_compare_exchange_strong(&(stack->idx), &(idx), stack->cap);
		} while (idx >= stack->cap);
		goto entry;
	}
	if (idx < 0) {
		if (block) {
			while (stack->idx < 0) {
				__builtin_ia32_pause();
			}
			goto entry;
		} else {
			return false;
		}
	}

	struct atomic_stack_field *field = &(stack->fields[idx]);
	enum atomic_stack_field_state ready = asfs_ready;
	while (!atomic_compare_exchange_strong(&(field->state), &(ready), asfs_popping)) {
		ready = asfs_ready;
		__builtin_ia32_pause();
	}

	*out = field->ptr;

	atomic_store(&(field->state), asfs_available);
	return true;
}

struct atomic_stack *clone_ref(struct atomic_stack *stack) {
	atomic_fetch_add(&(stack->rc), 1);
	return stack;
}
void drop(struct atomic_stack *stack, void (*callback)(struct atomic_stack *stack, void *arg), void *callback_arg) {
	if (atomic_sub_fetch(&(stack->rc), 1) == 0) {
		if (callback != NULL && stack->idx > 0) {
			callback(stack, callback_arg);
		}
		free(stack);
	}
	return;
}

#include <stdio.h>
#define STRINGBOOL(b) (b ? "true" : "false")
void callback(struct atomic_stack *stack, void *_) {
	void *ptr;
	while (pop(stack, false, &(ptr))) {
		printf("%p\n", ptr);
	}
	return;
}
int main(void) {
	struct atomic_stack *stack = new(128);
	printf("%s\n", STRINGBOOL(push(stack, false, NULL)));
	drop(stack, callback, NULL);
	return 0;
}