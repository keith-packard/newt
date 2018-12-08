/*
 * Copyright © 2018 Keith Packard <keithp@keithp.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

#include "newt.h"

static uint8_t		*compile;
static newt_offset_t	compile_size, compile_alloc;

#define COMPILE_INC	32

static newt_offset_t
compile_extend(newt_offset_t n, void *data)
{
	newt_offset_t start = compile_size;

	if (compile_size + n > compile_alloc) {
		uint8_t *new_compile = newt_alloc(compile_alloc + COMPILE_INC);
		if (!new_compile)
			return 0xffff;
		memcpy(new_compile, compile, compile_size);
		compile_alloc += COMPILE_INC;
		compile = new_compile;
	}
	if (data)
		memcpy(compile + compile_size, data, n);
	compile_size += n;
	return start;
}

static int
newt_op_extra_size(newt_op_t op)
{
	switch (op) {
	case newt_op_num:
		return sizeof (float);
	case newt_op_string:
		return sizeof (newt_offset_t);
	case newt_op_list:
		return sizeof (newt_offset_t);
	case newt_op_id:
		return sizeof (newt_id_t);
	case newt_op_call:
		return sizeof (newt_offset_t);
	case newt_op_slice:
		return 1;
	case newt_op_assign:
		return sizeof (newt_id_t);
	case newt_op_if:
	case newt_op_branch:
	case newt_op_forward:
		return sizeof (newt_offset_t);
	default:
		return 0;
	}
}

newt_offset_t
newt_code_current(void)
{
	return compile_size;
}

newt_offset_t
newt_code_add_op(newt_op_t op)
{
	return compile_extend(1, &op);
}

newt_offset_t
newt_code_add_op_id(newt_op_t op, newt_id_t id)
{
	newt_offset_t offset = compile_extend(1, &op);
	compile_extend(sizeof (newt_id_t), &id);
	return offset;
}

newt_offset_t
newt_code_add_number(float number)
{
	newt_op_t op = newt_op_num;
	newt_offset_t offset;

	offset = compile_extend(1, &op);
	compile_extend(sizeof(float), &number);
	return offset;
}

newt_offset_t
newt_code_add_string(char *string)
{
	newt_op_t op = newt_op_string;
	newt_offset_t offset, strpos;
	newt_offset_t s;

	newt_poly_stash(newt_string_to_poly(string));
	offset = compile_extend(1, &op);
	strpos = compile_extend(sizeof (newt_offset_t), NULL);
	string = newt_poly_to_string(newt_poly_fetch());
	s = newt_pool_offset(string);
	memcpy(compile + strpos, &s, sizeof (newt_offset_t));
	return offset;
}

newt_offset_t
newt_code_add_list(newt_offset_t size)
{
	newt_op_t op = newt_op_list;
	newt_offset_t offset;

	offset = compile_extend(1, &op);
	compile_extend(sizeof (newt_offset_t), &size);
	return offset;
}

newt_offset_t
newt_code_add_op_branch(newt_op_t op)
{
	newt_offset_t offset;

	offset = compile_extend(1, &op);
	compile_extend(sizeof (newt_offset_t), NULL);
	return offset;
}

newt_offset_t
newt_code_add_forward(newt_forward_t forward)
{
	newt_op_t op = newt_op_forward;
	newt_offset_t offset;

	offset = compile_extend(1, &op);
	compile_extend(sizeof (newt_forward_t), &forward);
	if (sizeof (newt_forward_t) < sizeof (newt_offset_t))
		compile_extend(sizeof (newt_offset_t) - sizeof (newt_forward_t), NULL);
	return offset;
}

newt_offset_t
newt_code_add_call(newt_offset_t nactual)
{
	newt_offset_t	offset;
	newt_op_t	op = newt_op_call;

	offset = compile_extend(1, &op);
	compile_extend(sizeof (newt_offset_t), &nactual);
	return offset;
}

static inline uint8_t
bit(bool val, uint8_t pos)
{
	return val ? pos : 0;
}

newt_offset_t
newt_code_add_slice(bool has_start, bool has_end, bool has_stride)
{
	uint8_t	insn[2];
	insn[0] = newt_op_slice;
	insn[1] = (bit(has_start, NEWT_OP_SLICE_START) |
		   bit(has_end,   NEWT_OP_SLICE_END) |
		   bit(has_stride, NEWT_OP_SLICE_STRIDE));
	return compile_extend(2, insn);
}

void
newt_code_patch_branch(newt_offset_t branch, newt_offset_t target)
{
	memcpy(compile + branch + 1, &target, sizeof (newt_offset_t));
}

void
newt_code_patch_forward(newt_offset_t start, newt_forward_t forward, newt_offset_t target)
{
	newt_offset_t ip = start;

	while (ip < compile_size) {
		newt_op_t op = compile[ip++];
		bool push = (op & newt_op_push) != 0;
		newt_forward_t f;
		op &= ~newt_op_push;
		switch (op) {
		case newt_op_forward:
			memcpy(&f, &compile[ip], sizeof (newt_forward_t));
			if (f == forward) {
				compile[ip-1] = newt_op_branch | (push ? newt_op_push : 0);
				memcpy(&compile[ip], &target, sizeof(newt_offset_t));
			}
			break;
		default:
			break;
		}
		ip += newt_op_extra_size(op);
	}
}

void
newt_code_set_push(newt_offset_t offset)
{
	compile[offset] |= newt_op_push;
}

newt_code_t *
newt_code_finish(void)
{
	newt_code_t *code = newt_alloc(sizeof (newt_code_t) + compile_size);
	memcpy(&code->code, compile, compile_size);
	code->size = compile_size;
	compile_size = 0;
	return code;
}

static bool
newt_true(newt_poly_t a)
{
	if (newt_is_float(a))
		return newt_poly_to_float(a) != 0.0f;
	return !newt_is_null(a);
}

static float
newt_bool_float(bool b)
{
	return b ? 1.0f : 0.0f;
}

static newt_poly_t
newt_bool(bool b)
{
	return newt_float_to_poly(b ? 1.0f : 0.0f);
}

//#define DEBUG_COMPILE
//#define DEBUG_EXEC
#if defined(DEBUG_COMPILE) || defined(DEBUG_EXEC)

static const char *newt_op_names[] = {
	[newt_op_nop] = "nop",
	[newt_op_num] = "num",
	[newt_op_string] = "string",
	[newt_op_list] = "list",
	[newt_op_id] = "id",

	[newt_op_and] = "and",
	[newt_op_or] = "or",
	[newt_op_not] = "not",

	[newt_op_eq] = "eq",
	[newt_op_ne] = "ne",
	[newt_op_gt] = "gt",
	[newt_op_lt] = "lt",
	[newt_op_ge] = "ge",
	[newt_op_le] = "le",

	[newt_op_plus] = "plus",
	[newt_op_minus] = "minus",
	[newt_op_times] = "times",
	[newt_op_divide] = "divide",
	[newt_op_div] = "div",
	[newt_op_mod] = "mod",

	[newt_op_call] = "call",

	[newt_op_uminus] = "uminus",

	[newt_op_array_fetch] = "array_fetch",
	[newt_op_array_store] = "array_store",
	[newt_op_slice] = "slice",


	[newt_op_assign] = "assign",

	[newt_op_if] = "if",
	[newt_op_branch] = "branch",
	[newt_op_forward] = "forward",
};

newt_offset_t
newt_code_dump_instruction(newt_code_t *code, newt_offset_t ip)
{
	float		f;
	newt_id_t	id;
	newt_offset_t	o;

	printf("%6d:  ", ip);
	newt_op_t op = code->code[ip++];
	bool push = (op & newt_op_push) != 0;
	op &= ~newt_op_push;
	printf("%-12s %c ", newt_op_names[op], push ? '^' : ' ');
	switch(op) {
	case newt_op_num:
		memcpy(&f, &code->code[ip], sizeof(float));
		ip += sizeof(float);
		printf("%g\n", f);
		break;
	case newt_op_string:
		memcpy(&o, &code->code[ip], sizeof(newt_offset_t));
		ip += sizeof (newt_offset_t);
		printf("%s\n", (char *) newt_pool_ref(o));
		break;
	case newt_op_list:
		memcpy(&o, &code->code[ip], sizeof(newt_offset_t));
		printf("%u\n", (o);
		break;
	case newt_op_id:
	case newt_op_assign:
		memcpy(&id, &code->code[ip], sizeof(newt_id_t));
		ip += sizeof (newt_id_t);
		printf("%s\n", newt_name_string(id));
		break;
	case newt_op_call:
		memcpy(&o, &code->code[ip], sizeof(newt_offset_t));
		ip += sizeof (newt_offset_t);
		printf("%d actuals\n", o);
		break;
	case newt_op_slice:
		if (code->code[ip] & NEWT_OP_SLICE_START) printf(" start");
		if (code->code[ip] & NEWT_OP_SLICE_END) printf(" end");
		if (code->code[ip] & NEWT_OP_SLICE_STRIDE) printf(" stride");
		ip++;
		break;
	case newt_op_if:
	case newt_op_branch:
		memcpy(&o, &code->code[ip], sizeof (newt_offset_t));
		printf("%d\n", o);
		ip += sizeof (newt_offset_t);
		break;
	default:
		printf("\n");
		break;
	}
	return ip;
}
#endif


#ifdef DEBUG_COMPILE
void
newt_code_dump(newt_code_t *code)
{
	newt_offset_t	ip = 0;

	while (ip < code->size) {
		ip = newt_code_dump_instruction(code, ip);
	}
}

#else
#define newt_code_dump(code)
#endif

static newt_poly_t
newt_binary(newt_poly_t a, newt_op_t op, newt_poly_t b)
{
	switch (op) {
	case newt_op_and:
		return newt_bool(newt_true(a) && newt_true(b));
	case newt_op_or:
		return newt_bool(newt_true(a) || newt_true(b));
	case newt_op_eq:
		return newt_bool(newt_poly_equal(a, b));
	case newt_op_ne:
		return newt_bool(!newt_poly_equal(a, b));
	default:
		break;
	}
	if (op == newt_op_array_fetch) {
		switch (newt_poly_type(a)) {
		case newt_list:
			break;
		case newt_string:
			if (newt_is_float(b))
				a = newt_string_to_poly(newt_string_make(newt_string_fetch(newt_poly_to_string(a), (int) newt_poly_to_float(b))));
			break;
		default:
			break;
		}
	} else if (newt_is_float(a) && newt_is_float(b)) {
		float	af = newt_poly_to_float(a);
		float	bf = newt_poly_to_float(b);
		switch (op) {
		case newt_op_plus:
			af = af + bf;
			break;
		case newt_op_minus:
			af = af - bf;
			break;
		case newt_op_times:
			af = af * bf;
			break;
		case newt_op_divide:
			af = af / bf;
			break;
		case newt_op_div:
			af = (float) ((int32_t) af / (int32_t) bf);
			break;
		case newt_op_mod:
			af = (float) ((int32_t) af % (int32_t) bf);
			break;
		case newt_op_lt:
			af = newt_bool_float(af < bf);
			break;
		case newt_op_gt:
			af = newt_bool_float(af > bf);
			break;
		case newt_op_le:
			af = newt_bool_float(af <= bf);
			break;
		case newt_op_ge:
			af = newt_bool_float(af >= bf);
			break;
		default:
			break;
		}
		a = newt_float_to_poly(af);
	} else if (newt_poly_type(a) == newt_list && newt_poly_type(b) == newt_list) {
		newt_list_t *al = newt_poly_to_list(a);
		newt_list_t *bl = newt_poly_to_list(b);

		switch(op) {
		case newt_op_plus:
			al = newt_list_plus(al, bl);
			if (!al)
				return NEWT_ZERO;
			a = newt_list_to_poly(al);
			break;
		case newt_op_eq:
			a = newt_float_to_poly(newt_bool_float(al == bl || newt_list_equal(al, bl)));
			break;
		case newt_op_ne:
			a = newt_float_to_poly(newt_bool_float(al != bl && !newt_list_equal(al, bl)));
			break;
		default:
			break;
		}
	}
	return a;
}

static newt_poly_t
newt_unary(newt_op_t op, newt_poly_t a)
{
	switch (op) {
	case newt_op_not:
		return newt_bool(!newt_true(a));
	default:
		break;
	}
	if (newt_is_float(a)) {
		float af = newt_poly_to_float(a);
		switch (op) {
		case newt_op_uminus:
			af = -af;
			break;
		default:
			break;
		}
		a = newt_float_to_poly(af);
	}
	return a;
}

static newt_soffset_t
newt_poly_to_soffset(newt_poly_t a)
{
	if (newt_is_float(a))
		return (newt_soffset_t) newt_poly_to_float(a);
	return 0;
}

static int
newt_len(newt_poly_t a)
{
	switch (newt_poly_type(a)) {
	case newt_string:
		return strlen(newt_poly_to_string(a));
	case newt_list:
		return newt_poly_to_list(a)->size;
	default:
		return 0;
	}
}

static newt_poly_t
newt_slice(newt_poly_t a, uint8_t bits)
{
	newt_slice_t	slice = {
		.start = NEWT_SLICE_DEFAULT,
		.end = NEWT_SLICE_DEFAULT,
		.stride = NEWT_SLICE_DEFAULT,

		.len = 0,
		.count = 0,
		.pos = 0,
	};
	bool used_a = false;

	if (bits & NEWT_OP_SLICE_STRIDE) {
		slice.stride = newt_poly_to_soffset(a);
		used_a = true;
	}

	if (bits & NEWT_OP_SLICE_END) {
		slice.end = newt_poly_to_soffset(used_a ? newt_stack_pop() : a);
		used_a = true;
	}

	if (bits & NEWT_OP_SLICE_START) {
		slice.start = newt_poly_to_soffset(used_a ? newt_stack_pop() : a);
		used_a = true;
	}

	if (used_a)
		a = newt_stack_pop();

	slice.len = newt_len(a);

	if (!newt_slice_canon(&slice))
		return a;

	switch (newt_poly_type(a)) {
	case newt_string:
		a = newt_string_to_poly(newt_string_slice(newt_poly_to_string(a), &slice));
		break;
	case newt_list:
		a = newt_list_to_poly(newt_list_slice(newt_poly_to_list(a), &slice));
		break;
	default:
		break;
	}
	return a;
}


newt_poly_t	newt_stack[NEWT_STACK];
newt_offset_t	newt_stackp = 0;

int
newt_stack_size(void *addr)
{
	(void) addr;
	return 0;
}

void
newt_stack_mark(void *addr)
{
	newt_offset_t s;
	(void) addr;
	for (s = 0; s < newt_stackp; s++)
		newt_poly_mark(newt_stack[s], 1);
}

void
newt_stack_move(void *addr)
{
	newt_offset_t s;
	(void) addr;
	for (s = 0; s < newt_stackp; s++)
		newt_poly_move(&newt_stack[s], 1);
}

const newt_mem_t newt_stack_mem = {
	.size = newt_stack_size,
	.mark = newt_stack_mark,
	.move = newt_stack_move,
	.name = "stack",
};

newt_poly_t
newt_code_run(newt_code_t *code)
{
	newt_poly_t 	a = NEWT_ZERO, b = NEWT_ZERO;
	newt_id_t	id;
	newt_offset_t	ip = 0;
	newt_offset_t	o;

	while (code) {
		while (ip < code->size) {
#ifdef DEBUG_EXEC
			newt_code_dump_instruction(code, ip);
#endif
			newt_op_t op = code->code[ip++];
			bool push = (op & newt_op_push) != 0;
			op &= ~newt_op_push;
			switch(op) {
			case newt_op_nop:
				break;
			case newt_op_num:
				memcpy(&a, &code->code[ip], sizeof(float));
				ip += sizeof(float);
				break;
			case newt_op_string:
				memcpy(&o, &code->code[ip], sizeof(newt_offset_t));
				ip += sizeof (newt_offset_t);
				a = newt_poly_offset(o, newt_string);
				break;
			case newt_op_list:
				memcpy(&o, &code->code[ip], sizeof(newt_offset_t));
				ip += sizeof (newt_offset_t);
				newt_list_t *l = newt_list_imm(o);
				if (!l)
					newt_stack_drop(o);
				else
					a = newt_list_to_poly(l);
				break;
			case newt_op_id:
				memcpy(&id, &code->code[ip], sizeof(newt_id_t));
				ip += sizeof (newt_id_t);
				a = newt_id_fetch(id);
				break;
			case newt_op_uminus:
			case newt_op_not:
				a = newt_unary(op, a);
				break;
			case newt_op_plus:
			case newt_op_minus:
			case newt_op_times:
			case newt_op_divide:
			case newt_op_div:
			case newt_op_mod:
			case newt_op_eq:
			case newt_op_ne:
			case newt_op_lt:
			case newt_op_gt:
			case newt_op_ge:
			case newt_op_le:
			case newt_op_and:
			case newt_op_or:
			case newt_op_array_fetch:
				b = newt_stack_pop();
				a = newt_binary(b, op, a);
				break;
			case newt_op_call:
				memcpy(&o, &code->code[ip], sizeof (newt_offset_t));
				a = newt_stack_pick(o);
				if (newt_poly_type(a) != newt_func) {
					newt_stack_drop(o);
					break;
				}
				if (!newt_func_push(newt_poly_to_func(a), o, code, ip - 1)) {
					newt_stack_drop(o);
					break;
				}
				newt_stack_drop(1);	/* drop function */
				code = newt_pool_ref(newt_poly_to_func(a)->code);
				ip = 0;
				push = false;	/* will pick up push on return */
				break;
			case newt_op_slice:
				a = newt_slice(a, code->code[ip]);
				ip++;
				break;
			case newt_op_assign:
				memcpy(&id, &code->code[ip], sizeof (newt_id_t));
				newt_id_assign(id, a);
				ip += sizeof (newt_id_t);
				break;
			case newt_op_if:
				if (!newt_true(a))
					memcpy(&ip, &code->code[ip], sizeof (newt_offset_t));
				else
					ip += sizeof (newt_offset_t);
				break;
			case newt_op_branch:
				memcpy(&ip, &code->code[ip], sizeof (newt_offset_t));
				break;
			default:
				break;
			}
			if (push)
				newt_stack_push(a);
#ifdef DEBUG_EXEC
			printf("\t\ta= "); newt_poly_print(a);
			for (int i = newt_stackp; i;) {
				printf(", [%d]= ", newt_stackp - i);
				newt_poly_print(newt_stack[--i]);
			}
			printf("\n");
#endif
		}
		code = newt_frame_pop(&ip);
		if (code) {
			newt_op_t op = code->code[ip++];
			bool push = (op & newt_op_push) != 0;
			op &= ~newt_op_push;
			switch (op) {
			case newt_op_call:
				ip += sizeof (newt_offset_t);
				break;
			default:
				break;
			}
			if (push)
				newt_stack_push(a);
		}
	}
	return a;
}

static int
newt_code_size(void *addr)
{
	newt_code_t *code = addr;

	return sizeof (newt_code_t) + code->size;
}

static void
newt_code_mark(void *addr)
{
	newt_code_t	*code = addr;
	newt_offset_t	ip = 0;

	while (ip < code->size) {
		newt_op_t op = code->code[ip++] & ~newt_op_push;
		newt_offset_t o;
		switch (op) {
		case newt_op_string:
			memcpy(&o, &code->code[ip], sizeof (newt_offset_t));
			newt_mark(&newt_string_mem, newt_pool_ref(o));
			break;
		default:
			break;
		}
		ip += newt_op_extra_size(op);
	}
}

static void
newt_code_move(void *addr)
{
	newt_code_t	*code = addr;
	newt_offset_t	ip = 0;

	while (ip < code->size) {
		newt_op_t op = code->code[ip++] & ~newt_op_push;
		newt_offset_t o, p;
		switch (op) {
		case newt_op_string:
			memcpy(&o, &code->code[ip], sizeof (newt_offset_t));
			p = o;
			newt_move_offset(&p);
			if (o != p)
				memcpy(&code->code[ip], &p, sizeof (newt_offset_t));
			break;
		default:
			break;
		}
		ip += newt_op_extra_size(op);
	}
}

const newt_mem_t newt_code_mem = {
	.size = newt_code_size,
	.mark = newt_code_mark,
	.move = newt_code_move,
	.name = "code",
};
