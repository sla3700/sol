#define _GNU_SOURCE
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <stdint.h>
#include <dlfcn.h>
#ifndef NO_READLINE
#include <readline/readline.h>
#include <readline/history.h>
#endif
#include <termios.h>
#include <sys/ioctl.h>
#include "ast.h"
#include "dsl/dsl.h"

// XXX hardcoded buffer sizes

#define STDIO_CHUNK_SIZE 4096

static char *_itoa(int i) {
	int n = 33;
	char *s = malloc(n);
	snprintf(s, n, "%d", i);
	return s;
}

static char *_ftoa(double f) {
	int n = 65;
	char *s = malloc(n);
	snprintf(s, n, "%f", f);
	return s;
}

sol_object_t *sol_f_not_impl(sol_state_t *state, sol_object_t *args) {
	char buffer[64];
	snprintf(buffer, 64, "Undefined method (%s on %s)", state->calling_meth, state->calling_type);
	return sol_set_error_string(state, buffer);
}

sol_object_t *sol_f_default_cmp(sol_state_t *state, sol_object_t *args) {
	sol_object_t *a = sol_list_get_index(state, args, 0), *b = sol_list_get_index(state, args, 1);
	sol_object_t *res = sol_new_int(state, a != b);
	sol_obj_free(a);
	sol_obj_free(b);
	return res;
}

sol_object_t *sol_f_default_tostring(sol_state_t *state, sol_object_t *args) {
	sol_object_t *obj = sol_list_get_index(state, args, 0);
	char s[64];
	snprintf(s, 64, "<%s object at %p>", obj->ops->tname, obj);
	sol_obj_free(obj);
	return sol_new_string(state, s);
}

sol_object_t *sol_f_default_repr(sol_state_t *state, sol_object_t *args) {
	sol_object_t *obj = sol_list_get_index(state, args, 0), *res = CALL_METHOD(state, obj, tostring, args);
	sol_obj_free(obj);
	return res;
}

sol_object_t *sol_f_default_tobuffer(sol_state_t *state, sol_object_t *args) {
	sol_object_t *obj = sol_list_get_index(state, args, 0), *str = CALL_METHOD(state, obj, tostring, args);
	sol_object_t *res = sol_new_buffer(state, strdup(str->str), strlen(str->str), OWN_FREE, NULL, NULL);
	sol_obj_free(obj);
	sol_obj_free(str);
	return res;
}

sol_object_t *sol_f_tbang(sol_state_t *state, sol_object_t *args) {
	sol_object_t *a = sol_list_get_index(state, args, 0), *b = sol_list_get_index(state, args, 1);
	int refa = a->refcnt, refb = b->refcnt;
	sol_object_t c = *b;
	*b = *a;
	*a = c;
	b->refcnt = refa;
	a->refcnt = refb;
	sol_obj_free(a);
	sol_obj_free(b);
	return sol_incref(state->None);
}

sol_object_t *sol_f_no_op(sol_state_t *state, sol_object_t *args) {
	if(state) {
		return sol_incref(state->None);
	}
	return NULL;
}

sol_object_t *sol_f_toint(sol_state_t *state, sol_object_t *args) {
	sol_object_t *obj = sol_list_get_index(state, args, 0);
	sol_object_t *res = CALL_METHOD(state, obj, toint, args);
	sol_obj_free(obj);
	return res;
}

sol_object_t *sol_f_tofloat(sol_state_t *state, sol_object_t *args) {
	sol_object_t *obj = sol_list_get_index(state, args, 0);
	sol_object_t *res = CALL_METHOD(state, obj, tofloat, args);
	sol_obj_free(obj);
	return res;
}

sol_object_t *sol_f_tostring(sol_state_t *state, sol_object_t *args) {
	sol_object_t *obj = sol_list_get_index(state, args, 0);
	sol_object_t *res = CALL_METHOD(state, obj, tostring, args);
	sol_obj_free(obj);
	return res;
}

sol_object_t *sol_f_tobuffer(sol_state_t *state, sol_object_t *args) {
	sol_object_t *obj = sol_list_get_index(state, args, 0);
	sol_object_t *res = CALL_METHOD(state, obj, tobuffer, args);
	sol_obj_free(obj);
	return res;
}

sol_object_t *sol_f_try(sol_state_t *state, sol_object_t *args) {
	sol_object_t *func = sol_list_get_index(state, args, 0), *fargs = sol_list_sublist(state, args, 1);
	sol_object_t *ls = sol_new_list(state), *one = sol_new_int(state, 1);
	sol_object_t *res;
	sol_list_insert(state, fargs, 0, func);
	res = CALL_METHOD(state, func, call, fargs);
	sol_obj_free(func);
	sol_obj_free(fargs);
	if(sol_has_error(state)) {
		sol_object_t *err = sol_get_error(state);
		sol_clear_error(state);
		sol_object_t *zero = sol_new_int(state, 0);
		sol_obj_free(res);
		sol_obj_free(one);
		sol_list_insert(state, ls, 0, err);
		sol_obj_free(err);
		sol_list_insert(state, ls, 0, zero);
		sol_obj_free(zero);
		sol_list_insert(state, ls, 2, state->traceback);
		sol_obj_free(state->traceback);
		state->traceback = NULL;
		return ls;
	}
	sol_list_insert(state, ls, 0, res);
	sol_obj_free(res);
	sol_list_insert(state, ls, 0, one);
	sol_obj_free(one);
	return ls;
}

sol_object_t *sol_f_apply(sol_state_t *state, sol_object_t *args) {
	sol_object_t *func = sol_list_get_index(state, args, 0), *arglist = sol_list_get_index(state, args, 1), *rest = sol_list_sublist(state, args, 2);
	if(!sol_is_list(arglist)) {
		sol_obj_free(func);
		sol_obj_free(arglist);
		sol_obj_free(rest);
		return sol_set_error_string(state, "apply with non-list");
	}
	sol_list_append(state, rest, arglist);
	sol_obj_free(arglist);
	sol_list_insert(state, rest, 0, func);
	sol_object_t *res = CALL_METHOD(state, func, call, rest);
	sol_obj_free(rest);
	sol_obj_free(func);
	return res;
}

sol_object_t *sol_f_error(sol_state_t *state, sol_object_t *args) {
	sol_object_t *arg = sol_list_get_index(state, args, 0), *res;
	res = sol_set_error(state, arg);
	sol_obj_free(arg);
	return res;
}

sol_object_t *sol_f_type(sol_state_t *state, sol_object_t *args) {
	sol_object_t *obj = sol_list_get_index(state, args, 0);
	sol_object_t *res = sol_new_buffer(state, strdup(obj->ops->tname), strlen(obj->ops->tname), OWN_FREE, NULL, NULL);
	sol_obj_free(obj);
	return res;
}

static dsl_seq *seen = NULL;

int test_seen(sol_object_t *obj) {
	dsl_seq_iter *iter;
	if(seen) {
		iter = dsl_new_seq_iter(seen);
		while(!dsl_seq_iter_is_invalid(iter)) {
			if(dsl_seq_iter_at(iter) == obj) {
				return 1;
			}
			dsl_seq_iter_next(iter);
		}
		dsl_free_seq_iter(iter);
		dsl_seq_insert(seen, dsl_seq_len(seen), obj);
	}
	return 0;
}

void ob_print(sol_object_t *obj) {
	sol_object_t *cur;
	dsl_seq_iter *iter;
	int i;
	if(test_seen(obj)) {
		return;
	}
	switch(obj->type) {
		case SOL_SINGLET:
			printf("%s", obj->str);
			break;

		case SOL_INTEGER:
			printf("%ld", obj->ival);
			break;

		case SOL_FLOAT:
			printf("%f", obj->fval);
			break;

		case SOL_STRING:
			printf("\"%s\"", obj->str);
			break;

		case SOL_LIST:
			printf("[");
			iter = dsl_new_seq_iter(obj->seq);
			while(!dsl_seq_iter_is_invalid(iter)) {
				ob_print(dsl_seq_iter_at(iter));
				printf(", ");
				dsl_seq_iter_next(iter);
			}
			dsl_free_seq_iter(iter);
			printf("]");
			break;

		case SOL_MCELL:
			printf("<<");
			ob_print(obj->key);
			printf("=");
			ob_print(obj->val);
			printf(">>");

		case SOL_MAP:
			printf("{");
			iter = dsl_new_seq_iter(obj->seq);
			while(!dsl_seq_iter_is_invalid(iter)) {
				printf("[");
				ob_print(AS_OBJ(dsl_seq_iter_at(iter))->key);
				printf("] = ");
				ob_print(AS_OBJ(dsl_seq_iter_at(iter))->val);
				printf(", ");
				dsl_seq_iter_next(iter);
			}
			dsl_free_seq_iter(iter);
			printf("}");
			break;

		case SOL_FUNCTION:
			if(obj->fname) {
				printf("<Function %s>", obj->fname);
			} else {
				printf("<Function>");
			}
			break;

		case SOL_CFUNCTION:
			printf("<CFunction>");
			break;

		case SOL_STMT:
			st_print(NULL, obj->node); //TODO: FIXME
			break;

		case SOL_EXPR:
			ex_print(NULL, obj->node); //TODO: FIXME
			break;

		case SOL_BUFFER:
			/*
			if(obj->sz == -1) {
				printf("<Buffer @%p>", obj->buffer);
			} else {
				printf("<Buffer @%p size %ld>", obj->buffer, obj->sz);
			}
			*/
			fwrite(obj->buffer, sizeof(char), obj->sz, stdout);
			break;

		case SOL_CDATA:
			printf("<CData>");
			break;

			/*default:
				cur = sol_cast_string(state, obj);
				printf("%s", cur->str);
				sol_obj_free(cur);*/
	}
}

sol_object_t *sol_f_prepr(sol_state_t *state, sol_object_t *args) {
	int i, sz = sol_list_len(state, args);
	sol_object_t *obj, *str;
	seen = dsl_seq_new_array(NULL, NULL);
	for(i = 0; i < sz; i++) {
		obj = sol_list_get_index(state, args, i);
		str = sol_cast_repr(state, obj);
		sol_printf(state, "%s", str->str);
		sol_printf(state, " ");
		sol_obj_free(obj);
		sol_obj_free(str);
	}
	sol_printf(state, "\n");
	printf("\n");
	dsl_free_seq(seen);
	seen = NULL;
	return sol_incref(state->None);
}

sol_object_t *sol_f_print(sol_state_t *state, sol_object_t *args) {
	int i, sz = sol_list_len(state, args);
	sol_object_t *obj, *str;
	seen = dsl_seq_new_array(NULL, NULL);
	for(i = 0; i < sz; i++) {
		obj = sol_list_get_index(state, args, i);
		str = sol_cast_string(state, obj);
		sol_printf(state, "%s", str->str);
		sol_printf(state, " ");
		sol_obj_free(obj);
		sol_obj_free(str);
	}
	sol_printf(state, "\n");
	dsl_free_seq(seen);
	seen = NULL;
	return sol_incref(state->None);
}

sol_object_t *sol_f_rawget(sol_state_t *state, sol_object_t *args) {
	sol_object_t *obj = sol_list_get_index(state, args, 0), *key, *res;
	if(!sol_is_map(obj)) {
		return sol_set_error_string(state, "Rawset of non-map");
	}
	key = sol_list_get_index(state, args, 1);
	res = sol_map_get(state, obj, key);
	sol_obj_free(key);
	sol_obj_free(obj);
	return res;
}


sol_object_t *sol_f_rawset(sol_state_t *state, sol_object_t *args) {
	sol_object_t *obj = sol_list_get_index(state, args, 0), *key, *val;
	if(!sol_is_map(obj)) {
		return sol_set_error_string(state, "Rawset of non-map");
	}
	key = sol_list_get_index(state, args, 1);
	val = sol_list_get_index(state, args, 2);
	sol_map_set(state, obj, key, val);
	sol_obj_free(val);
	sol_obj_free(key);
	sol_obj_free(obj);
	return sol_incref(state->None);
}

sol_object_t *sol_f_range(sol_state_t *state, sol_object_t *args) {
	sol_object_t *res = sol_new_list(state), *bound = sol_list_get_index(state, args, 0);
	sol_object_t *boundi = sol_cast_int(state, bound);
	int i;
	sol_obj_free(bound);
	if(sol_has_error(state)) {
		sol_obj_free(boundi);
		sol_obj_free(res);
		return sol_incref(state->None);
	}
	for(i = 0; i < boundi->ival; i++) {
		sol_list_insert(state, res, sol_list_len(state, res), sol_new_int(state, i));
	}
	sol_obj_free(boundi);
	return res;
}

/*
sol_object_t *sol_f_exec(sol_state_t *state, sol_object_t *args) {
	sol_object_t *prg = sol_list_get_index(state, args, 0), *prgstr = sol_cast_string(state, prg);
	stmt_node *program;

	program = sol_compile(prgstr->str);
	if(!program) {
		return sol_set_error_string(state, "Compilation failure");
	}
	// XXX should st_free(program);

	sol_exec(state, program);
	return sol_incref(state->None);
}

sol_object_t *sol_f_eval(sol_state_t *state, sol_object_t *args) {
	sol_object_t *prg = sol_list_get_index(state, args, 0), *prgstr = sol_cast_string(state, prg);
	stmt_node *program;

	program = sol_compile(prgstr->str);
	if(!program) {
		return sol_set_error_string(state, "Compilation failure");
	}
	if(program->type != ST_LIST || program->stmtlist->stmt->type != ST_EXPR) {
		return sol_set_error_string(state, "Not an expression");
	}
	// XXX should st_free(program);

	return sol_eval(state, program->stmtlist->stmt->expr);
}
*/

sol_object_t *sol_f_execfile(sol_state_t *state, sol_object_t *args) {
	sol_object_t *prg = sol_list_get_index(state, args, 0), *prgstr = sol_cast_string(state, prg);
	stmt_node *program;
	FILE *f = fopen(prgstr->str, "r");
	char *s;
	long sz;

	if(!f) {
		return sol_set_error_string(state, "File open failure");
	}

	fseek(f, 0, SEEK_END);
	sz = ftell(f);
	fseek(f, 0, SEEK_SET);
	s = malloc(sz + 1);
	if(!s) {
		fclose(f);
		return sol_set_error_string(state, "File memory allocation failure");
	}
	fread(s, 1, sz, f);
	s[sz] = 0;
	fclose(f);

	program = sol_compile(s);
	free(s);
	if(!program) {
		return sol_set_error_string(state, "Compilation failure");
	}

	sol_exec(state, program);
	st_free(program);
	return sol_incref(state->None);
}

sol_object_t *sol_f_parse(sol_state_t *state, sol_object_t *args) {
	sol_object_t *prg = sol_list_get_index(state, args, 0), *prgstr;
	stmt_node *program;
	if(sol_is_buffer(prg)) {
		if(prg->sz >= 0) {
			program = sol_compile_buffer(prg->buffer, prg->sz);
		} else {
			sol_obj_free(prg);
			return sol_set_error_string(state, "parse unsized buffer");
		}
	} else {
		prgstr = sol_cast_string(state, prg);
		program = sol_compile(prgstr->str);
		sol_obj_free(prgstr);
	}
	sol_obj_free(prg);
	if(!program) {
		return sol_set_error_string(state, "Compilation failure");
	}
	return sol_new_stmtnode(state, program);
}

sol_object_t *sol_f_ord(sol_state_t *state, sol_object_t *args) {
	sol_object_t *arg = sol_list_get_index(state, args, 0), *str = sol_cast_string(state, arg);
	sol_object_t *arg2, *iarg, *res;
	long idx = 0;
	size_t len = strlen(str->str);
	sol_obj_free(arg);
	if(sol_list_len(state, args) > 1) {
		arg2 = sol_list_get_index(state, args, 1);
		iarg = sol_cast_int(state, arg2);
		sol_obj_free(arg2);
		idx = iarg->ival;
		sol_obj_free(iarg);
	}
	if(idx < 0) {
		idx += len;
	}
	if(idx < 0 || idx >= len) {
		sol_obj_free(str);
		return sol_set_error_string(state, "Compute ord of out-of-bounds index");
	}
	res = sol_new_int(state, str->str[idx]);
	sol_obj_free(str);
	return res;
}

sol_object_t *sol_f_chr(sol_state_t *state, sol_object_t *args) {
	sol_object_t *arg = sol_list_get_index(state, args, 0), *iarg = sol_cast_int(state, arg);
	char cbuf[2] = {iarg->ival, 0};
	sol_object_t *res = sol_new_buffer(state, strdup(cbuf), 1, OWN_FREE, NULL, NULL);
	sol_obj_free(arg);
	sol_obj_free(iarg);
	return res;
}


sol_object_t *sol_f_debug_getref(sol_state_t *state, sol_object_t *args) {
	sol_object_t *obj = sol_list_get_index(state, args, 0);
	sol_object_t *res = sol_new_int(state, obj->refcnt - 2); // NB: We grabbed a reference, and there's one in the arglist, so account for them.
	sol_obj_free(obj);
	return res;
}

sol_object_t *sol_f_debug_setref(sol_state_t *state, sol_object_t *args) {
	sol_object_t *obj = sol_list_get_index(state, args, 0), *cnt = sol_list_get_index(state, args, 1);
	obj->refcnt = sol_cast_int(state, cnt)->ival + 2; // NB: As above.
	sol_obj_free(cnt);
	sol_obj_free(obj);
	return sol_incref(state->None);
}

sol_object_t *sol_f_debug_closure(sol_state_t *state, sol_object_t *args) {
	sol_object_t *func = sol_list_get_index(state, args, 0);
	sol_object_t *res = sol_incref(func->closure);
	sol_obj_free(func);
	return res;
}

sol_object_t *sol_f_debug_globals(sol_state_t *state, sol_object_t *args) {
	return sol_list_get_index(state, state->scopes, sol_list_len(state, state->scopes) - 1);
}

sol_object_t *sol_f_debug_locals(sol_state_t *state, sol_object_t *args) {
	return sol_list_get_index(state, state->scopes, 0);
}

sol_object_t *sol_f_debug_scopes(sol_state_t *state, sol_object_t *args) {
	return sol_incref(state->scopes);
}

sol_object_t *sol_f_debug_getops(sol_state_t *state, sol_object_t *args) {
	sol_object_t *obj = sol_list_get_index(state, args, 0), *res = sol_new_map(state);
	sol_map_borrow_name(state, res, "name", sol_new_buffer(state, obj->ops->tname, strlen(obj->ops->tname), OWN_NONE, NULL, NULL));
	sol_map_borrow_name(state, res, "flags", sol_new_int(state, obj->ops->tflags));
	sol_map_borrow_name(state, res, "add", sol_new_cfunc(state, obj->ops->add, "add"));
	sol_map_borrow_name(state, res, "sub", sol_new_cfunc(state, obj->ops->sub, "sub"));
	sol_map_borrow_name(state, res, "mul", sol_new_cfunc(state, obj->ops->mul, "mul"));
	sol_map_borrow_name(state, res, "div", sol_new_cfunc(state, obj->ops->div, "div"));
	sol_map_borrow_name(state, res, "mod", sol_new_cfunc(state, obj->ops->mod, "mod"));
	sol_map_borrow_name(state, res, "pow", sol_new_cfunc(state, obj->ops->pow, "pow"));
	sol_map_borrow_name(state, res, "tbang", sol_new_cfunc(state, obj->ops->tbang, "tbang"));
	sol_map_borrow_name(state, res, "band", sol_new_cfunc(state, obj->ops->band, "band"));
	sol_map_borrow_name(state, res, "bor", sol_new_cfunc(state, obj->ops->bor, "bor"));
	sol_map_borrow_name(state, res, "bxor", sol_new_cfunc(state, obj->ops->bxor, "bxor"));
	sol_map_borrow_name(state, res, "blsh", sol_new_cfunc(state, obj->ops->blsh, "blsh"));
	sol_map_borrow_name(state, res, "brsh", sol_new_cfunc(state, obj->ops->brsh, "brsh"));
	sol_map_borrow_name(state, res, "bnot", sol_new_cfunc(state, obj->ops->bnot, "bnot"));
	sol_map_borrow_name(state, res, "cmp", sol_new_cfunc(state, obj->ops->cmp, "cmp"));
	sol_map_borrow_name(state, res, "call", sol_new_cfunc(state, obj->ops->call, "call"));
	sol_map_borrow_name(state, res, "index", sol_new_cfunc(state, obj->ops->index, "index"));
	sol_map_borrow_name(state, res, "setindex", sol_new_cfunc(state, obj->ops->setindex, "setindex"));
	sol_map_borrow_name(state, res, "len", sol_new_cfunc(state, obj->ops->len, "len"));
	sol_map_borrow_name(state, res, "iter", sol_new_cfunc(state, obj->ops->iter, "iter"));
	sol_map_borrow_name(state, res, "toint", sol_new_cfunc(state, obj->ops->toint, "toint"));
	sol_map_borrow_name(state, res, "tofloat", sol_new_cfunc(state, obj->ops->tofloat, "tofloat"));
	sol_map_borrow_name(state, res, "tostring", sol_new_cfunc(state, obj->ops->tostring, "tostring"));
	sol_map_borrow_name(state, res, "tobuffer", sol_new_cfunc(state, obj->ops->tobuffer, "tobuffer"));
	sol_map_borrow_name(state, res, "repr", sol_new_cfunc(state, obj->ops->repr, "repr"));
	sol_map_borrow_name(state, res, "init", sol_new_cfunc(state, obj->ops->init, "init"));
	sol_map_borrow_name(state, res, "free", sol_new_cfunc(state, obj->ops->free, "free"));
	sol_obj_free(obj);
	return res;
}

sol_object_t *sol_f_debug_fnstack(sol_state_t *state, sol_object_t *args) {
	return sol_incref(state->fnstack);
}

#ifndef NO_READLINE
sol_object_t *sol_f_readline_readline(sol_state_t *state, sol_object_t *args) {
	sol_object_t *obj, *objstr, *res;
	char *line;
	if(sol_list_len(state, args) > 0) {
		obj = sol_list_get_index(state, args, 0);
		objstr = sol_cast_string(state, obj);
		line = readline(objstr->str);
		sol_obj_free(obj);
		sol_obj_free(objstr);
	} else {
		line = readline("");
	}
	if(line) {
		res = sol_new_buffer(state, strdup(line), strlen(line), OWN_FREE, NULL, NULL);
		free(line);
	} else {
		res = sol_new_buffer(state, strdup(""), 0, OWN_FREE, NULL, NULL);
	}
	return res;
}

sol_object_t *sol_f_readline_add_history(sol_state_t *state, sol_object_t *args) {
	sol_object_t *line = sol_list_get_index(state, args, 0), *linestr = sol_cast_string(state, line);
	add_history(linestr->str);
	sol_obj_free(linestr);
	sol_obj_free(line);
	return sol_incref(state->None);
}
#endif

void _sol_freef_seq_iter(void *iter, size_t sz) {
	dsl_free_seq_iter((dsl_seq_iter *) iter);
}

sol_object_t *sol_f_iter_str(sol_state_t *state, sol_object_t *args) {
	sol_object_t *obj = sol_list_get_index(state, args, 0), *local = sol_list_get_index(state, args, 1);
	sol_object_t *index = sol_map_get_name(state, local, "idx"), *max = sol_map_get_name(state, local, "sz"), *res;
	char temp[2] = {0, 0};
	if(sol_is_none(state, index) || sol_is_none(state, max)) {
		sol_obj_free(index);
		index = sol_new_buffer(state, (void *) 0, sizeof(void *), OWN_NONE, NULL, NULL);
		sol_map_set_name(state, local, "idx", index);
		sol_obj_free(max);
		max = sol_new_int(state, strlen(obj->str));
		sol_map_set_name(state, local, "sz", max);
	}
	if(((size_t) index->buffer) >= max->ival) {
		sol_obj_free(index);
		sol_obj_free(obj);
		sol_obj_free(local);
		return sol_incref(state->None);
	}
	temp[0] = obj->str[((size_t) index->buffer)];
	res = sol_new_string(state, temp);
	index->buffer = (void *) ((size_t) index->buffer + 1);
	sol_obj_free(index);
	sol_obj_free(local);
	sol_obj_free(obj);
	return res;
}

sol_object_t *sol_f_iter_buffer(sol_state_t *state, sol_object_t *args) {
	sol_object_t *obj = sol_list_get_index(state, args, 0), *local = sol_list_get_index(state, args, 1);
	sol_object_t *idx = sol_map_get_name(state, local, "idx"), *sz = sol_map_get_name(state,local, "sz"), *res;
	if(sol_is_none(state, idx) || sol_is_none(state, sz)) {
		sol_obj_free(idx);
		sol_obj_free(sz);
		idx = sol_new_buffer(state, (void *) 0, sizeof(void *), OWN_NONE, NULL, NULL);
		sz = sol_new_int(state, obj->sz);
		sol_map_set_name(state, local, "idx", idx);
		sol_map_set_name(state, local, "sz", sz);
	}
	if(((size_t) idx->buffer) >= sz->ival) {
		sol_obj_free(idx);
		sol_obj_free(sz);
		sol_obj_free(local);
		return sol_incref(state->None);
	}
	res = sol_new_buffer(state, ((char *) obj->buffer) + ((size_t) idx->buffer), 1, OWN_NONE, NULL, NULL);
	idx->buffer = (void *) ((size_t) idx->buffer + 1);
	sol_obj_free(idx);
	sol_obj_free(sz);
	sol_obj_free(local);
	return res;
}

sol_object_t *sol_f_iter_list(sol_state_t *state, sol_object_t *args) {
	sol_object_t *obj = sol_list_get_index(state, args, 0), *local = sol_list_get_index(state, args, 1);
	sol_object_t *index = sol_map_get_name(state, local, "idx"), *res;
	if(sol_is_none(state, index)) {
		sol_obj_free(index);
		index = sol_new_buffer(state, dsl_new_seq_iter(obj->seq), sizeof(dsl_seq_iter), OWN_CALLF, _sol_freef_seq_iter, NULL);
		sol_map_set_name(state, local, "idx", index);
		sol_obj_free(index);
	}
	if(dsl_seq_iter_is_invalid(index->buffer)) {
		sol_obj_free(index);
		sol_obj_free(obj);
		sol_obj_free(local);
		return sol_incref(state->None);
	}
	res = sol_incref(AS_OBJ(dsl_seq_iter_at(index->buffer)));
	dsl_seq_iter_next(index->buffer);
	sol_obj_free(local);
	sol_obj_free(obj);
	return res;
}

sol_object_t *sol_f_iter_map(sol_state_t *state, sol_object_t *args) {
	sol_object_t *obj = sol_list_get_index(state, args, 0), *local = sol_list_get_index(state, args, 1);
	sol_object_t *index = sol_map_get_name(state, local, "idx"), *res;
	if(sol_is_none(state, index)) {
		sol_obj_free(index);
		index = sol_new_buffer(state, dsl_new_seq_iter(obj->seq), sizeof(dsl_seq_iter), OWN_CALLF, _sol_freef_seq_iter, NULL);
		sol_map_set_name(state, local, "idx", index);
		sol_obj_free(index);
	}
	if(dsl_seq_iter_is_invalid(index->buffer)) {
		sol_obj_free(index);
		sol_obj_free(obj);
		sol_obj_free(local);
		return sol_incref(state->None);
	}
	res = sol_incref(AS_OBJ(dsl_seq_iter_at(index->buffer))->key);
	dsl_seq_iter_next(index->buffer);
	sol_obj_free(local);
	sol_obj_free(obj);
	return res;
}

sol_object_t *sol_f_ast_print(sol_state_t *state, sol_object_t *args) {
	sol_object_t *obj = sol_list_get_index(state, args, 0);
	if(sol_is_aststmt(obj)) {
		st_print(state, obj->node);
	} else {
		ex_print(state, obj->node);
	}
	sol_obj_free(obj);
	return sol_incref(state->None);
}

sol_object_t *sol_f_singlet_tostring(sol_state_t *state, sol_object_t *args) {
	sol_object_t *obj = sol_list_get_index(state, args, 0), *res = sol_new_string(state, obj->str);
	sol_obj_free(obj);
	return res;
}

sol_object_t *sol_f_singlet_tobuffer(sol_state_t *state, sol_object_t *args) {
	sol_object_t *obj = sol_list_get_index(state, args, 0), *res = sol_new_buffer(state, strdup(obj->str), strlen(obj->str), OWN_FREE, NULL, NULL);
	sol_obj_free(obj);
	return res;
}

sol_object_t *sol_f_int_add(sol_state_t *state, sol_object_t *args) {
	sol_object_t *a = sol_list_get_index(state, args, 0), *b = sol_list_get_index(state, args, 1), *bint = sol_cast_int(state, b);
	sol_object_t *res = sol_new_int(state, a->ival + bint->ival);
	sol_obj_free(a);
	sol_obj_free(b);
	sol_obj_free(bint);
	if(sol_has_error(state)) {
		sol_obj_free(res);
		return sol_incref(state->None);
	}
	return res;
}

sol_object_t *sol_f_int_sub(sol_state_t *state, sol_object_t *args) {
	sol_object_t *a = sol_list_get_index(state, args, 0), *b = sol_list_get_index(state, args, 1), *bint = sol_cast_int(state, b);
	sol_object_t *res = sol_new_int(state, a->ival - bint->ival);
	sol_obj_free(a);
	sol_obj_free(b);
	sol_obj_free(bint);
	if(sol_has_error(state)) {
		sol_obj_free(res);
		return sol_incref(state->None);
	}
	return res;
}

sol_object_t *sol_f_int_mul(sol_state_t *state, sol_object_t *args) {
	sol_object_t *a = sol_list_get_index(state, args, 0), *b = sol_list_get_index(state, args, 1), *bint = sol_cast_int(state, b);
	sol_object_t *res = sol_new_int(state, a->ival * bint->ival);
	sol_obj_free(a);
	sol_obj_free(b);
	sol_obj_free(bint);
	if(sol_has_error(state)) {
		sol_obj_free(res);
		return sol_incref(state->None);
	}
	return res;
}

sol_object_t *sol_f_int_div(sol_state_t *state, sol_object_t *args) {
	sol_object_t *a = sol_list_get_index(state, args, 0), *b = sol_list_get_index(state, args, 1), *bint = sol_cast_int(state, b);
	sol_object_t *res;
	sol_obj_free(b);
	if(bint->ival == 0) {
		sol_obj_free(a);
		sol_obj_free(bint);
		return sol_set_error_string(state, "integer divide by zero");
	}
	res = sol_new_int(state, a->ival / bint->ival);
	sol_obj_free(a);
	sol_obj_free(bint);
	if(sol_has_error(state)) {
		sol_obj_free(res);
		return sol_incref(state->None);
	}
	return res;
}

sol_object_t *sol_f_int_mod(sol_state_t *state, sol_object_t *args) {
	sol_object_t *a = sol_list_get_index(state, args, 0), *b = sol_list_get_index(state, args, 1), *bint = sol_cast_int(state, b);
	sol_object_t *res;
	sol_obj_free(b);
	if(bint->ival == 0) {
		sol_obj_free(a);
		sol_obj_free(bint);
		return sol_set_error_string(state, "integer modulus by zero");
	}
	res = sol_new_int(state, a->ival % bint->ival);
	sol_obj_free(a);
	sol_obj_free(bint);
	if(sol_has_error(state)) {
		sol_obj_free(res);
		return sol_incref(state->None);
	}
	return res;
}

sol_object_t *sol_f_int_pow(sol_state_t *state, sol_object_t *args) {
	sol_object_t *a = sol_list_get_index(state, args, 0), *b = sol_list_get_index(state, args, 1), *bint = sol_cast_int(state, b);
	sol_object_t *res = sol_new_int(state, (long) pow((double) a->ival, bint->ival));
	sol_obj_free(a);
	sol_obj_free(b);
	sol_obj_free(bint);
	if(sol_has_error(state)) {
		sol_obj_free(res);
		return sol_incref(state->None);
	}
	return res;
}

sol_object_t *sol_f_int_band(sol_state_t *state, sol_object_t *args) {
	sol_object_t *a = sol_list_get_index(state, args, 0), *b = sol_list_get_index(state, args, 1), *bint = sol_cast_int(state, b);
	sol_object_t *res = sol_new_int(state, a->ival & bint->ival);
	sol_obj_free(a);
	sol_obj_free(b);
	sol_obj_free(bint);
	if(sol_has_error(state)) {
		sol_obj_free(res);
		return sol_incref(state->None);
	}
	return res;
}

sol_object_t *sol_f_int_bor(sol_state_t *state, sol_object_t *args) {
	sol_object_t *a = sol_list_get_index(state, args, 0), *b = sol_list_get_index(state, args, 1), *bint = sol_cast_int(state, b);
	sol_object_t *res = sol_new_int(state, a->ival | bint->ival);
	sol_obj_free(a);
	sol_obj_free(b);
	sol_obj_free(bint);
	if(sol_has_error(state)) {
		sol_obj_free(res);
		return sol_incref(state->None);
	}
	return res;
}

sol_object_t *sol_f_int_bxor(sol_state_t *state, sol_object_t *args) {
	sol_object_t *a = sol_list_get_index(state, args, 0), *b = sol_list_get_index(state, args, 1), *bint = sol_cast_int(state, b);
	sol_object_t *res = sol_new_int(state, a->ival ^ bint->ival);
	sol_obj_free(a);
	sol_obj_free(b);
	sol_obj_free(bint);
	if(sol_has_error(state)) {
		sol_obj_free(res);
		return sol_incref(state->None);
	}
	return res;
}

sol_object_t *sol_f_int_blsh(sol_state_t *state, sol_object_t *args) {
	sol_object_t *a = sol_list_get_index(state, args, 0), *b = sol_list_get_index(state, args, 1), *bint = sol_cast_int(state, b);
	sol_object_t *res = sol_new_int(state, a->ival << bint->ival);
	sol_obj_free(a);
	sol_obj_free(b);
	sol_obj_free(bint);
	if(sol_has_error(state)) {
		sol_obj_free(res);
		return sol_incref(state->None);
	}
	return res;
}

sol_object_t *sol_f_int_brsh(sol_state_t *state, sol_object_t *args) {
	sol_object_t *a = sol_list_get_index(state, args, 0), *b = sol_list_get_index(state, args, 1), *bint = sol_cast_int(state, b);
	sol_object_t *res = sol_new_int(state, a->ival >> bint->ival);
	sol_obj_free(a);
	sol_obj_free(b);
	sol_obj_free(bint);
	if(sol_has_error(state)) {
		sol_obj_free(res);
		return sol_incref(state->None);
	}
	return res;
}

sol_object_t *sol_f_int_bnot(sol_state_t *state, sol_object_t *args) {
	sol_object_t *a = sol_list_get_index(state, args, 0);
	sol_object_t *res = sol_new_int(state, ~a->ival);
	sol_obj_free(a);
	return res;
}

sol_object_t *sol_f_int_cmp(sol_state_t *state, sol_object_t *args) {
	sol_object_t *a = sol_list_get_index(state, args, 0), *b = sol_list_get_index(state, args, 1);
	sol_object_t *res;
	if(sol_is_int(b)) {
		res = sol_new_int(state, a->ival == b->ival ? 0 : (a->ival < b->ival ? -1 : 1));
	} else {
		res = sol_new_int(state, 1);
	}
	sol_obj_free(a);
	sol_obj_free(b);
	return res;
}

sol_object_t *sol_f_int_toint(sol_state_t *state, sol_object_t *args) {
	return sol_list_get_index(state, args, 0);
}

sol_object_t *sol_f_int_tofloat(sol_state_t *state, sol_object_t *args) {
	sol_object_t *a = sol_list_get_index(state, args, 0);
	sol_object_t *res = sol_new_float(state, (double) a->ival);
	sol_obj_free(a);
	return res;
}

sol_object_t *sol_f_int_tostring(sol_state_t *state, sol_object_t *args) {
	sol_object_t *a = sol_list_get_index(state, args, 0);
	char *s = _itoa(a->ival);
	sol_object_t *res = sol_new_string(state, s);
	sol_obj_free(a);
	free(s);
	return res;
}

sol_object_t *sol_f_float_add(sol_state_t *state, sol_object_t *args) {
	sol_object_t *a = sol_list_get_index(state, args, 0), *b = sol_list_get_index(state, args, 1), *bflt = sol_cast_float(state, b);
	sol_object_t *res = sol_new_float(state, a->fval + bflt->fval);
	sol_obj_free(a);
	sol_obj_free(b);
	sol_obj_free(bflt);
	if(sol_has_error(state)) {
		sol_obj_free(res);
		return sol_incref(state->None);
	}
	return res;
}

sol_object_t *sol_f_float_sub(sol_state_t *state, sol_object_t *args) {
	sol_object_t *a = sol_list_get_index(state, args, 0), *b = sol_list_get_index(state, args, 1), *bflt = sol_cast_float(state, b);
	sol_object_t *res = sol_new_float(state, a->fval - bflt->fval);
	sol_obj_free(a);
	sol_obj_free(b);
	sol_obj_free(bflt);
	if(sol_has_error(state)) {
		sol_obj_free(res);
		return sol_incref(state->None);
	}
	return res;
}

sol_object_t *sol_f_float_mul(sol_state_t *state, sol_object_t *args) {
	sol_object_t *a = sol_list_get_index(state, args, 0), *b = sol_list_get_index(state, args, 1), *bflt = sol_cast_float(state, b);
	sol_object_t *res = sol_new_float(state, a->fval * bflt->fval);
	sol_obj_free(a);
	sol_obj_free(b);
	sol_obj_free(bflt);
	if(sol_has_error(state)) {
		sol_obj_free(res);
		return sol_incref(state->None);
	}
	return res;
}

sol_object_t *sol_f_float_div(sol_state_t *state, sol_object_t *args) {
	sol_object_t *a = sol_list_get_index(state, args, 0), *b = sol_list_get_index(state, args, 1), *bflt = sol_cast_float(state, b);
	sol_object_t *res;
	sol_obj_free(b);
	if(bflt->fval == 0.0) {
		sol_obj_free(a);
		sol_obj_free(bflt);
		return sol_set_error_string(state, "floating division by zero");
	}
	res = sol_new_float(state, a->fval / bflt->fval);
	sol_obj_free(a);
	sol_obj_free(bflt);
	if(sol_has_error(state)) {
		sol_obj_free(res);
		return sol_incref(state->None);
	}
	return res;
}

sol_object_t *sol_f_float_pow(sol_state_t *state, sol_object_t *args) {
	sol_object_t *a = sol_list_get_index(state, args, 0), *b = sol_list_get_index(state, args, 1), *bflt = sol_cast_float(state, b);
	sol_object_t *res = sol_new_float(state, pow(a->fval, bflt->fval));
	sol_obj_free(b);
	sol_obj_free(a);
	sol_obj_free(bflt);
	if(sol_has_error(state)) {
		sol_obj_free(res);
		return sol_incref(state->None);
	}
	return res;
}

sol_object_t *sol_f_float_cmp(sol_state_t *state, sol_object_t *args) {
    sol_object_t *a = sol_list_get_index(state, args, 0), *b = sol_list_get_index(state, args, 1);
    sol_object_t *res;
	if(sol_is_float(b)) {
		res = sol_new_int(state, a->fval==b->fval? 0 : (a->fval<b->fval? -1 : 1));
	} else {
		res = sol_new_int(state, 1);
	}
    sol_obj_free(a);
    sol_obj_free(b);
    return res;
}

sol_object_t *sol_f_float_toint(sol_state_t *state, sol_object_t *args) {
	sol_object_t *a = sol_list_get_index(state, args, 0);
	sol_object_t *res = sol_new_int(state, (int) a->fval);
	sol_obj_free(a);
	return res;
}

sol_object_t *sol_f_float_tofloat(sol_state_t *state, sol_object_t *args) {
	return sol_list_get_index(state, args, 0);
}

sol_object_t *sol_f_float_tostring(sol_state_t *state, sol_object_t *args) {
	sol_object_t *a = sol_list_get_index(state, args, 0);
	char *s = _ftoa(a->fval);
	sol_object_t *res = sol_new_string(state, s);
	sol_obj_free(a);
	free(s);
	return res;
}

sol_object_t *sol_f_str_add(sol_state_t *state, sol_object_t *args) {
	sol_object_t *a = sol_list_get_index(state, args, 0), *b = sol_list_get_index(state, args, 1), *bstr = sol_cast_string(state, b);
	sol_object_t *res = sol_string_concat(state, a, bstr);
	sol_obj_free(a);
	sol_obj_free(b);
	sol_obj_free(bstr);
	if(sol_has_error(state)) {
		sol_obj_free(res);
		return sol_incref(state->None);
	}
	return res;
}

sol_object_t *sol_f_str_mul(sol_state_t *state, sol_object_t *args) {
	sol_object_t *a = sol_list_get_index(state, args, 0), *b = sol_list_get_index(state, args, 1), *bint = sol_cast_int(state, b);
	int n = strlen(a->str) * bint->ival + 1;
	char *s = malloc(n);
	int i;
	s[0] = '\0';
	for(i = 0; i < b->ival; i++) {
		strncat(s, a->str, n);
	}
	sol_object_t *res = sol_new_string(state, s);
	sol_obj_free(a);
	sol_obj_free(b);
	sol_obj_free(bint);
	free(s);
	if(sol_has_error(state)) {
		sol_obj_free(res);
		return sol_incref(state->None);
	}
	return res;
}

sol_object_t *sol_f_str_cmp(sol_state_t *state, sol_object_t *args) {
    sol_object_t *a = sol_list_get_index(state, args, 0), *b = sol_list_get_index(state, args, 1);
    sol_object_t *sb, *res;
	if(sol_is_buffer(b)) {
		sb = sol_cast_string(state, b);
		sol_obj_free(b);
		b = sb;
	}
	if(sol_is_string(b)) {
		res = sol_new_int(state, strcmp(a->str, b->str));
	} else {
		res = sol_new_int(state, 1);
	}
    sol_obj_free(a);
    sol_obj_free(b);
    return res;
}

sol_object_t *sol_f_str_len(sol_state_t *state, sol_object_t *args) {
	sol_object_t *a = sol_list_get_index(state, args, 0);
	sol_object_t *res = sol_new_int(state, strlen(a->str));
	sol_obj_free(a);
	return res;
}

sol_object_t *sol_f_str_index(sol_state_t *state, sol_object_t *args) {
	sol_object_t *str = sol_list_get_index(state, args, 0), *key = sol_list_get_index(state, args, 1), *idx, *funcs, *res;
	char buf[2] = {0, 0};
	if(sol_is_string(key)) {
		funcs = sol_get_methods_name(state, "string");
		res = sol_map_get(state, funcs, key);
		sol_obj_free(funcs);
		return res;
	}
	idx = sol_cast_int(state, key);
	if(idx->ival >= 0 && idx->ival < strlen(str->str)) {
		buf[0] = str->str[idx->ival];
	}
	sol_obj_free(str);
	sol_obj_free(key);
	sol_obj_free(idx);
	return sol_new_string(state, buf);
}

sol_object_t *sol_f_str_iter(sol_state_t *state, sol_object_t *args) {
	return sol_new_cfunc(state, sol_f_iter_str, "iter.str");
}

sol_object_t *sol_f_str_toint(sol_state_t *state, sol_object_t *args) {
	sol_object_t *a = sol_list_get_index(state, args, 0);
	sol_object_t *res = sol_new_int(state, atoi(a->str));
	sol_obj_free(a);
	return res;
}

sol_object_t *sol_f_str_tofloat(sol_state_t *state, sol_object_t *args) {
	sol_object_t *a = sol_list_get_index(state, args, 0);
	sol_object_t *res = sol_new_float(state, atof(a->str));
	sol_obj_free(a);
	return res;
}

sol_object_t *sol_f_str_tostring(sol_state_t *state, sol_object_t *args) {
	return sol_list_get_index(state, args, 0);
}

sol_object_t *sol_f_str_tobuffer(sol_state_t *state, sol_object_t *args) {
	sol_object_t *str = sol_list_get_index(state, args, 0);
	sol_object_t *res = sol_new_buffer(state, strdup(str->str), strlen(str->str), OWN_FREE, NULL, NULL);
	sol_obj_free(str);
	return res;
}

sol_object_t *sol_f_str_repr(sol_state_t *state, sol_object_t *args) {
	sol_object_t *obj = sol_list_get_index(state, args, 0), *cur = sol_new_string(state, "\""), *next = sol_string_concat(state, cur, obj);
	sol_obj_free(cur);
	cur = next;
	next = sol_string_concat_cstr(state, cur, "\"");
	sol_obj_free(cur);
	return next;
}

sol_object_t *sol_f_str_sub(sol_state_t *state, sol_object_t *args) {
	sol_object_t *str = sol_list_get_index(state, args, 0), *low = sol_list_get_index(state, args, 1), *high = sol_list_get_index(state, args, 2);
	sol_object_t *ilow, *ihigh;
	long l, h;
	size_t len = strlen(str->str), i;
	char *s;
	if(sol_is_none(state, low)) {
		ilow = sol_new_int(state, 0);
	} else {
		ilow = sol_cast_int(state, low);
	}
	if(sol_is_none(state, high)) {
		ihigh = sol_new_int(state, len);
	} else {
		ihigh = sol_cast_int(state, high);
	}
	sol_obj_free(low);
	sol_obj_free(high);
	l = ilow->ival;
	h = ihigh->ival;
	sol_obj_free(ihigh);
	sol_obj_free(ilow);
	if(l < 0) {
		l += len;
		if(l < 0) {
			l = 0;
		}
	}
	if(l > len) {
		l = len;
	}
	if(h < 0) {
		h += len;
		if(h < 0) {
			h = 0;
		}
	}
	if(h > len) {
		h = len;
	}
	if(l >= h) {
		sol_obj_free(str);
		return sol_new_string(state, "");
	}
	s = malloc(h - l + 1);
	for(i = l; i < h; i++) {
		s[i - l] = str->str[i];
	}
	s[h - l] = '\0';
	sol_obj_free(str);
	return sol_new_string(state, s);
}

sol_object_t *sol_f_str_split(sol_state_t *state, sol_object_t *args) {
	sol_object_t *str = sol_list_get_index(state, args, 0), *tok = sol_list_get_index(state, args, 1), *stok = sol_cast_string(state, tok);
	sol_object_t *res = sol_new_list(state), *opart;
	char *s = strdup(str->str);
	char *part = strtok(s, stok->str);
	sol_obj_free(tok);
	sol_obj_free(str);
	if(!part) {
		sol_obj_free(res);
		sol_obj_free(stok);
		return sol_incref(state->None);
	}
	opart = sol_new_string(state, part);
	sol_list_insert(state, res, 0, opart);
	sol_obj_free(opart);
	while(part = strtok(NULL, stok->str)) {
		opart = sol_new_string(state, part);
		sol_list_insert(state, res, sol_list_len(state, res), opart);
		sol_obj_free(opart);
	}
	sol_obj_free(stok);
	return res;
}

sol_object_t *sol_f_str_find(sol_state_t *state, sol_object_t *args) {
	sol_object_t *str = sol_list_get_index(state, args, 0), *substr = sol_list_get_index(state, args, 1), *ssubstr = sol_cast_string(state, substr);
	char *ptr = strstr(str->str, ssubstr->str);
	sol_object_t *res = sol_new_int(state, ptr ? ptr - str->str : -1);
	sol_obj_free(str);
	sol_obj_free(substr);
	sol_obj_free(ssubstr);
	return res;
}

sol_object_t *sol_f_list_add(sol_state_t *state, sol_object_t *args) {
	sol_object_t *a = sol_list_get_index(state, args, 0), *b = sol_list_get_index(state, args, 1), *ls;
	if(!sol_is_list(b)) {
		sol_obj_free(a);
		sol_obj_free(b);
		return sol_set_error_string(state, "Adding list to non-list");
	}
	ls = sol_list_copy(state, a);
	sol_list_append(state, ls, b);
	sol_obj_free(a);
	sol_obj_free(b);
	return ls;
}

sol_object_t *sol_f_list_mul(sol_state_t *state, sol_object_t *args) {
	sol_object_t *a = sol_list_get_index(state, args, 0), *b = sol_list_get_index(state, args, 1), *bint = sol_cast_int(state, b), *ls;
	int i;
	if(sol_has_error(state)) {
		sol_obj_free(a);
		sol_obj_free(b);
		sol_obj_free(bint);
		return sol_incref(state->None);
	}
	ls = sol_new_list(state);
	for(i = 0; i < b->ival; i++) {
		sol_list_append(state, ls, a);
		if(sol_has_error(state)) {
			sol_obj_free(a);
			sol_obj_free(b);
			sol_obj_free(bint);
			return sol_incref(state->None);
		}
	}
	return ls;
}

sol_object_t *sol_f_list_cmp(sol_state_t *state, sol_object_t *args) {
	sol_object_t *a = sol_list_get_index(state, args, 0), *b = sol_list_get_index(state, args, 1), *item, *ls, *tmp;
	int i, alen, blen;
	if(!sol_is_list(b)) {
		sol_obj_free(b);
		sol_obj_free(a);
		return sol_new_int(state, 1);  // XXX lists always greater than non-lists?
	}
	alen = sol_list_len(state, a);
	blen = sol_list_len(state, b);
	if(alen != blen) {
		sol_obj_free(b);
		sol_obj_free(a);
		return sol_new_int(state, alen > blen ? 1 : -1);
	}
	ls = sol_new_list(state);
	sol_list_insert(state, ls, 0, state->None);
	sol_list_insert(state, ls, 1, state->None);
	for(i = 0; i < alen; i++) {
		tmp = sol_list_get_index(state, a, i);
		sol_list_set_index(state, ls, 0, tmp);
		item = sol_list_get_index(state, b, i);
		sol_list_set_index(state, ls, 1, item);
		sol_obj_free(item);
		item = CALL_METHOD(state, tmp, cmp, ls);
		sol_obj_free(tmp);
		if(item->ival != 0) {
			sol_obj_free(ls);
			sol_obj_free(b);
			sol_obj_free(a);
			return item;
		}
		sol_obj_free(item);
	}
	sol_obj_free(ls);
	sol_obj_free(b);
	sol_obj_free(a);
	return sol_new_int(state, 0);
}

sol_object_t *sol_f_list_index(sol_state_t *state, sol_object_t *args) {
	sol_object_t *ls = sol_list_get_index(state, args, 0), *b = sol_list_get_index(state, args, 1), *ival;
	sol_object_t *res, *funcs;
	if(sol_is_name(b)) {
		funcs = sol_get_methods_name(state, "list");
		res = sol_map_get(state, funcs, b);
		sol_obj_free(funcs);
	} else {
		ival = sol_cast_int(state, b);
		res = sol_list_get_index(state, ls, ival->ival);
		sol_obj_free(ival);
	}
	sol_obj_free(ls);
	sol_obj_free(b);
	return res;
}

sol_object_t *sol_f_list_setindex(sol_state_t *state, sol_object_t *args) {
	sol_object_t *ls = sol_list_get_index(state, args, 0), *b = sol_list_get_index(state, args ,1), *bint = sol_cast_int(state, b);
	sol_object_t *val = sol_list_get_index(state, args, 2);
	sol_list_set_index(state, ls, bint->ival, val);
	sol_obj_free(ls);
	sol_obj_free(b);
	sol_obj_free(bint);
	sol_obj_free(val);
	return sol_incref(state->None);
}

sol_object_t *sol_f_list_len(sol_state_t *state, sol_object_t *args) {
	sol_object_t *ls = sol_list_get_index(state, args, 0);
	sol_object_t *res = sol_new_int(state, sol_list_len(state, ls));
	sol_obj_free(ls);
	return res;
}

sol_object_t *sol_f_list_iter(sol_state_t *state, sol_object_t *args) {
	return sol_new_cfunc(state, sol_f_iter_list, "iter.list");
}

sol_object_t *sol_f_list_tostring(sol_state_t *state, sol_object_t *args) {
	sol_object_t *cur = sol_new_string(state, "["), *next, *str, *obj = sol_list_get_index(state, args, 0), *item;
	dsl_seq_iter *iter = dsl_new_seq_iter(obj->seq);
	char s[64];
	while(!dsl_seq_iter_is_invalid(iter)) {
		item = AS_OBJ(dsl_seq_iter_at(iter));
		if(test_seen(item)) {
			snprintf(s, 64, "... (%p)", item);
			next = sol_string_concat_cstr(state, cur, s);
		} else {
			str = sol_cast_repr(state, item);
			next = sol_string_concat(state, cur, str);
			sol_obj_free(str);
		}
		sol_obj_free(cur);
		cur = next;
		if(!dsl_seq_iter_at_end(iter)) {
			next = sol_string_concat_cstr(state, cur, ", ");
			sol_obj_free(cur);
			cur = next;
		}
		dsl_seq_iter_next(iter);
	}
	next = sol_string_concat_cstr(state, cur, "]");
	sol_obj_free(cur);
	dsl_free_seq_iter(iter);
	return next;
}

sol_object_t *sol_f_list_copy(sol_state_t *state, sol_object_t *args) {
	sol_object_t *list = sol_list_get_index(state, args, 0);
	sol_object_t *res = sol_list_copy(state, list);
	sol_obj_free(list);
	return res;
}

sol_object_t *sol_f_list_insert(sol_state_t *state, sol_object_t *args) {
    sol_object_t *list = sol_list_get_index(state, args, 0), *idx =  sol_list_get_index(state, args, 1), *iidx = sol_cast_int(state, idx), *obj = sol_list_get_index(state, args, 2);
    sol_list_insert(state, list, iidx->ival, obj);
    sol_obj_free(list);
	sol_obj_free(idx);
	sol_obj_free(iidx);
	sol_obj_free(obj);
    return sol_incref(state->None);
}

sol_object_t *sol_f_list_remove(sol_state_t *state, sol_object_t *args) {
    sol_object_t *list = sol_list_get_index(state, args, 0), *idx = sol_list_get_index(state, args, 1), *iidx = sol_cast_int(state, idx);
    sol_object_t *res = sol_list_remove(state, list, iidx->ival);
    sol_obj_free(list);
	sol_obj_free(idx);
	sol_obj_free(iidx);
    return res;
}

sol_object_t *sol_f_list_truncate(sol_state_t *state, sol_object_t *args) {
    sol_object_t *list = sol_list_get_index(state, args, 0), *idx = sol_list_get_index(state, args, 1), *iidx = sol_cast_int(state, idx);
    sol_object_t *res = sol_list_truncate(state, list, iidx->ival);
    sol_obj_free(list);
	sol_obj_free(idx);
	sol_obj_free(iidx);
    return res;
}

sol_object_t *sol_f_list_map(sol_state_t *state, sol_object_t *args) {
	sol_object_t *olist = sol_list_get_index(state, args, 0), *func = sol_list_get_index(state, args, 1);
	sol_object_t *list = sol_list_copy(state, olist);
	sol_object_t *fargs = sol_new_list(state), *item;
	int idx = 0, len = sol_list_len(state, list);
	sol_obj_free(olist);
	sol_list_insert(state, fargs, 0, func);
	while(idx < len) {
		item = sol_list_get_index(state, list, idx);
		sol_list_insert(state, fargs, 1, item);
		sol_obj_free(item);
		item = CALL_METHOD(state, func, call, fargs);
		if(sol_has_error(state)) {
			return list;
		}
		sol_list_remove(state, fargs, 1);
		sol_list_set_index(state, list, idx, item);
		sol_obj_free(item);
		idx++;
	}
	sol_obj_free(fargs);
	sol_obj_free(func);
	return list;
}

sol_object_t *sol_f_list_filter(sol_state_t *state, sol_object_t *args) {
	sol_object_t *olist = sol_list_get_index(state, args, 0), *func = sol_list_get_index(state, args, 1);
	sol_object_t *list = sol_list_copy(state, olist);
	sol_object_t *fargs = sol_new_list(state), *item, *ival;
	int idx = 0, len = sol_list_len(state, list);
	sol_obj_free(olist);
	sol_list_insert(state, fargs, 0, func);
	while(idx < len) {
		item = sol_list_get_index(state, list, idx);
		sol_list_insert(state, fargs, 1, item);
		sol_obj_free(item);
		item = CALL_METHOD(state, func, call, fargs);
		if(sol_has_error(state)) {
			return list;
		}
		ival = sol_cast_int(state, item);
		if(ival->ival) {
			idx++;
		} else {
			sol_list_remove(state, list, idx);
			len--;
		}
		sol_obj_free(item);
		sol_obj_free(ival);
	}
	sol_obj_free(fargs);
	sol_obj_free(func);
	return list;
}

sol_object_t *sol_f_list_reduce(sol_state_t *state, sol_object_t *args) {
	sol_object_t *list = sol_list_get_index(state, args, 0), *func = sol_list_get_index(state, args, 1);
	sol_object_t *val = sol_list_get_index(state, args, 2), *newval;
	sol_object_t *fargs = sol_new_list(state), *item;
	int idx = 0, len = sol_list_len(state, list);
	sol_list_insert(state, fargs, 0, func);
	sol_list_insert(state, fargs, 1, state->None);
	sol_list_insert(state, fargs, 2, state->None);
	while(idx < len) {
		item = sol_list_get_index(state, list, idx);
		sol_list_set_index(state, fargs, 1, val);
		sol_list_set_index(state, fargs, 2, item);
		sol_obj_free(item);
		newval = CALL_METHOD(state, func, call, fargs);
		sol_obj_free(val);
		val = newval;
		idx++;
	}
	sol_obj_free(list);
	sol_obj_free(func);
	sol_obj_free(fargs);
	return val;
}

sol_object_t *sol_f_map_add(sol_state_t *state, sol_object_t *args) {
	sol_object_t *a = sol_list_get_index(state, args, 0), *b = sol_list_get_index(state, args, 1), *map;
	if(!sol_is_map(b)) {
		sol_obj_free(a);
		sol_obj_free(b);
		return sol_set_error_string(state, "Adding map to non-map");
	}
	map = sol_map_copy(state, a);
	sol_map_merge(state, map, b);
	sol_obj_free(a);
	sol_obj_free(b);
	return map;
}

sol_object_t *sol_f_map_index(sol_state_t *state, sol_object_t *args) {
	sol_object_t *map = sol_list_get_index(state, args, 0), *b = sol_list_get_index(state, args, 1);
	sol_object_t *indexf = sol_map_get_name(state, map, "__index");
	sol_object_t *res = NULL, *newls;
	res = sol_map_get(state, map, b);
	if(sol_is_none(state, res)) {
		if(!sol_is_none(state, indexf)) {
			sol_obj_free(res);
			if(indexf->ops->call && (sol_is_func(indexf) || sol_is_cfunc(indexf)) && indexf->ops->call != sol_f_not_impl) {
				newls = sol_new_list(state);
				sol_list_insert(state, newls, 0, indexf);
				sol_list_append(state, newls, args);
				res = CALL_METHOD(state, indexf, call, newls);
				sol_obj_free(newls);
			} else if(indexf->ops->index && indexf->ops->index != sol_f_not_impl) {
				newls = sol_new_list(state);
				sol_list_insert(state, newls, 0, indexf);
				sol_list_insert(state, newls, 1, b);
				res = CALL_METHOD(state, indexf, index, newls);
				sol_obj_free(newls);
			}
		}
	}
	sol_obj_free(indexf);
	sol_obj_free(map);
	sol_obj_free(b);
	return res;
}

sol_object_t *sol_f_map_setindex(sol_state_t *state, sol_object_t *args) {
	sol_object_t *map = sol_list_get_index(state, args, 0), *b = sol_list_get_index(state, args, 1);
	sol_object_t *val = sol_list_get_index(state, args, 2);
	sol_object_t *setindexf = sol_map_get_name(state, map, "__setindex"), *newls;
	if(!sol_is_none(state, setindexf)) {
		if(setindexf->ops->call && (sol_is_func(setindexf) || sol_is_cfunc(setindexf)) && setindexf->ops->call != sol_f_not_impl) {
			newls = sol_new_list(state);
			sol_list_insert(state, newls, 0, setindexf);
			sol_list_append(state, newls, args);
			sol_obj_free(CALL_METHOD(state, setindexf, call, newls));
			sol_obj_free(newls);
			return sol_incref(state->None);
		} else if(setindexf->ops->setindex && setindexf->ops->setindex != sol_f_not_impl) {
			newls = sol_new_list(state);
			sol_list_insert(state, newls, 0, setindexf);
			sol_list_insert(state, newls, 1, b);
			sol_list_insert(state, newls, 2, val);
			sol_obj_free(CALL_METHOD(state, setindexf, index, newls));
			sol_obj_free(newls);
			return sol_incref(state->None);
		}
	}
	sol_obj_free(setindexf);
	sol_map_set(state, map, b, val);
	sol_obj_free(map);
	sol_obj_free(b);
	sol_obj_free(val);
	return sol_incref(state->None);
}

sol_object_t *sol_f_map_call(sol_state_t *state, sol_object_t *args) {
	sol_object_t *map = sol_list_get_index(state, args, 0), *fargs = sol_list_sublist(state, args, 1);
	sol_object_t *callf = sol_map_get_name(state, map, "__call"), *res = NULL;
	if(!sol_is_none(state, callf)) {
		if(callf->ops->call) {
			sol_list_insert(state, fargs, 0, callf);
			sol_list_insert(state, fargs, 1, map);
			res = CALL_METHOD(state,  callf, call, fargs);
		}
	}
	sol_obj_free(map);
	sol_obj_free(fargs);
	sol_obj_free(callf);
	if(res) {
		return res;
	}
	return sol_set_error_string(state, "Call map without call method");
}

sol_object_t *sol_f_map_len(sol_state_t *state, sol_object_t *args) {
	sol_object_t *map = sol_list_get_index(state, args, 0);
	sol_object_t *res = sol_new_int(state, sol_map_len(state, map));
	sol_obj_free(map);
	return res;
}

sol_object_t *sol_f_map_iter(sol_state_t *state, sol_object_t *args) {
	return sol_new_cfunc(state, sol_f_iter_map, "iter.map");
}

sol_object_t *sol_f_map_tostring(sol_state_t *state, sol_object_t *args) {
	sol_object_t *map = sol_list_get_index(state, args, 0), *res;
	sol_object_t *tostrf = sol_map_get_name(state, map, "__tostring"), *fargs;
	if(!sol_is_none(state, tostrf) && tostrf->ops->call) {
		fargs = sol_new_list(state);
		sol_list_insert(state, fargs, 0, tostrf);
		sol_list_insert(state, fargs, 1, map);
		res = CALL_METHOD(state, tostrf, call, fargs);
		sol_obj_free(fargs);
	} else {
		res = sol_cast_repr(state, map);
	}
	sol_obj_free(tostrf);
	sol_obj_free(map);
	return res;
}

sol_object_t *sol_f_map_repr(sol_state_t *state, sol_object_t *args) {
	sol_object_t *cur = sol_new_string(state, "{"), *next, *str, *obj = sol_list_get_index(state, args, 0), *item, *reprf = sol_map_get_name(state, obj, "__repr"), *fargs;
	dsl_seq_iter *iter;
	char s[64];
	if(!sol_is_none(state, reprf) && reprf->ops->call) {
		sol_obj_free(cur);
		fargs = sol_new_list(state);
		sol_list_insert(state, fargs, 0, reprf);
		sol_list_insert(state, fargs, 1, obj);
		cur = CALL_METHOD(state, reprf, call, fargs);
		sol_obj_free(fargs);
		sol_obj_free(obj);
		sol_obj_free(reprf);
		return cur;
	}
	iter = dsl_new_seq_iter(obj->seq);
	while(!dsl_seq_iter_is_invalid(iter)) {
		item = AS_OBJ(dsl_seq_iter_at(iter));
		if(test_seen(item)) {
			snprintf(s, 64, "... (%p)", item);
			next = sol_string_concat_cstr(state, cur, s);
		} else {
			str = sol_cast_repr(state, item);
			next = sol_string_concat(state, cur, str);
			sol_obj_free(str);
		}
		sol_obj_free(cur);
		cur = next;
		if(!dsl_seq_iter_at_end(iter)) {
			next = sol_string_concat_cstr(state, cur, ", ");
			sol_obj_free(cur);
			cur = next;
		}
		dsl_seq_iter_next(iter);
	}
	next = sol_string_concat_cstr(state, cur, "}");
	sol_obj_free(cur);
	dsl_free_seq_iter(iter);
	sol_obj_free(obj);
	return next;
}

sol_object_t *sol_f_mcell_tostring(sol_state_t *state, sol_object_t *args) {
	sol_object_t *mcell = sol_list_get_index(state, args, 0), *cur = sol_new_string(state, "["), *next, *str;
	char s[64];
	if(test_seen(mcell->key)) {
		snprintf(s, 64, "... (%p)", mcell->key);
		next = sol_string_concat_cstr(state, cur, s);
	} else {
		str = sol_cast_repr(state, mcell->key);
		next = sol_string_concat(state, cur, str);
		sol_obj_free(str);
	}
	sol_obj_free(cur);
	cur = next;
	next = sol_string_concat_cstr(state, cur, "] = ");
	sol_obj_free(cur);
	cur = next;
	if(test_seen(mcell->val)) {
		snprintf(s, 64, "... (%p)", mcell->val);
		next = sol_string_concat_cstr(state, cur, s);
	} else {
		str = sol_cast_repr(state, mcell->val);
		next = sol_string_concat(state, cur, str);
		sol_obj_free(str);
	}
	sol_obj_free(cur);
	sol_obj_free(mcell);
	return next;
}

sol_object_t *sol_f_func_index(sol_state_t *state, sol_object_t *args) {
	sol_object_t *func = sol_list_get_index(state, args, 0), *key = sol_list_get_index(state, args, 1), *res;
	identlist_node *curi;
	int i = 0;
	if(!sol_is_name(key)) {
		res = sol_map_get(state, func->udata, key);
	} else {
		if(sol_name_eq(state, key, "name")) {
			if(func->fname) {
				res = sol_new_string(state, func->fname);
			} else {
				res = sol_incref(state->None);
			}
		} else if(sol_name_eq(state, key, "closure")) {
			res = sol_incref(func->closure);
		} else if(sol_name_eq(state, key, "udata")) {
			res = sol_incref(func->udata);
		} else if(sol_name_eq(state, key, "stmt")) {
			res = sol_new_stmtnode(state, st_copy((stmt_node *) func->func));
		} else if(sol_name_eq(state, key, "args")) {
			res = sol_new_list(state);
			curi = func->args;
			while(curi) {
				sol_list_insert(state, res, i++, sol_new_string(state, curi->ident));
				curi = curi->next;
			}
		} else if(sol_name_eq(state, key, "rest")) {
			if(func->rest) {
				res = sol_new_string(state, func->rest);
			} else {
				res = sol_incref(state->None);
			}
		} else if(sol_name_eq(state, key, "annos")) {
			res = sol_incref(func->annos);
		} else {
			res = sol_map_get(state, func->udata, key);
		}
	}
	sol_obj_free(func);
	sol_obj_free(key);
	return res;
}

sol_object_t *sol_f_func_setindex(sol_state_t *state, sol_object_t *args) {
	sol_object_t *func = sol_list_get_index(state, args, 0), *key = sol_list_get_index(state, args, 1), *val = sol_list_get_index(state, args, 2), *temp, *str;
	size_t i, len;
	identlist_node *cur, *prev;
	if(sol_name_eq(state, key, "name") && sol_is_name(val)) {
		free(func->fname);
		if(sol_is_string(val)) {
			func->fname = strdup(val->str);
		} else {
			func->fname = sol_buffer_strdup(val);
		}
	} else if(sol_name_eq(state, key, "closure") && sol_is_map(val)) {
		temp = func->closure;
		func->closure = sol_incref(val);
		sol_obj_free(temp);
	} else if(sol_name_eq(state, key, "udata") && sol_is_map(val)) {
		temp = func->udata;
		func->udata = sol_incref(val);
		sol_obj_free(temp);
	} else if(sol_name_eq(state, key, "stmt") && sol_is_aststmt(val)) {
		st_free(func->func);
		func->func = st_copy(val->node);
	} else if(sol_name_eq(state, key, "args") && sol_is_list(val)) {
		idl_free(func->args);
		func->args = NEW(identlist_node);
		cur = func->args;
		prev = cur;
		len = sol_list_len(state, val);
		for(i = 0; i < len; i++ ) {
			temp = sol_list_get_index(state, val, i);
			str = sol_cast_string(state, temp);
			cur->ident = strdup(str->str);
			sol_obj_free(temp);
			sol_obj_free(str);
			prev = cur;
			cur->next = NEW(identlist_node);
			cur = cur->next;
		} 
		prev->next = NULL;
		if(cur == func->args) func->args = NULL;
		free(cur);
	} else if(sol_name_eq(state, key, "rest") && sol_is_name(val)) {
		free(func->rest);
		if(sol_is_string(val)) {
			func->rest = strdup(val->str);
		} else {
			func->rest = sol_buffer_strdup(val);
		}
	} else if(sol_name_eq(state, key, "annos") && sol_is_map(val)) {
		sol_obj_free(func->annos);
		func->annos = sol_incref(val);
	} else {
		sol_map_set(state, func->udata, key, val);
	}
	sol_obj_free(func);
	sol_obj_free(key);
	sol_obj_free(val);
	return sol_incref(state->None);
}

sol_object_t *sol_f_func_tostring(sol_state_t *state, sol_object_t *args) {
	sol_object_t *func = sol_list_get_index(state, args, 0), *ret;
	char *s = malloc(256 * sizeof(char));
	if(func->fname) {
		snprintf(s, 256, "<Function %s>", func->fname);
	} else {
		snprintf(s, 256, "<Function>");
	}
	ret = sol_new_string(state, s);
	free(s);
	sol_obj_free(func);
	return ret;
}

sol_object_t *sol_f_cfunc_call(sol_state_t *state, sol_object_t *args) {
	sol_object_t *func = sol_list_get_index(state, args, 0), *fargs = sol_list_sublist(state, args, 1);
	sol_object_t *res = NULL, *tmp = NULL;
	sol_list_insert(state, state->fnstack, 0, func);
	res = func->cfunc(state, fargs);
	tmp = sol_list_remove(state, state->fnstack, 0);
	if(tmp != func) {
		printf("ERROR: Function stack imbalance\n");
	}
	sol_obj_free(func);
	sol_obj_free(fargs);
	return res;
}

sol_object_t *sol_f_cfunc_tostring(sol_state_t *state, sol_object_t *args) {
	sol_object_t *cfunc = sol_list_get_index(state, args, 0), *ret;
	char *s = malloc(256 * sizeof(char));
	if(cfunc->cfname) {
		snprintf(s, 256, "<CFunction %s>", cfunc->cfname);
	} else {
		snprintf(s, 256, "<CFunction>");
	}
	ret = sol_new_string(state, s);
	free(s);
	sol_obj_free(cfunc);
	return ret;
}

sol_object_t *sol_f_astnode_call(sol_state_t *state, sol_object_t *args) {
	sol_object_t *obj = sol_list_get_index(state, args, 0), *env = NULL, *res;
	stmt_node *stmt = (stmt_node *) obj->node;
	expr_node *expr = (expr_node *) obj->node;
	sol_obj_free(obj);
	if(sol_list_len(state, args) > 1) {
		env = sol_list_get_index(state, args, 1);
		sol_state_push_scope(state, env);
	}
	if(sol_is_aststmt(obj)) {
		sol_exec(state, stmt);
		res = sol_incref(state->None);
	} else {
		res = sol_eval(state, expr);
	}
	if(env) {
		sol_state_pop_scope(state);
		sol_obj_free(env);
	}
	return res;
}

sol_object_t *sol_f_astnode_index(sol_state_t *state, sol_object_t *args) {
	sol_object_t *obj = sol_list_get_index(state, args, 0), *key = sol_list_get_index(state, args, 1), *str = sol_cast_string(state, key), *res = NULL, *pair;
	stmt_node *stmt = (stmt_node *) obj->node;
	stmtlist_node *curs;
	expr_node *expr = (expr_node *) obj->node;
	exprlist_node *cure;
	assoclist_node *cura;
	identlist_node *curi;
	int i = 0;
	if(!stmt) {
		sol_obj_free(obj);
		sol_obj_free(key);
		sol_obj_free(str);
		return sol_set_error_string(state, "Access NULL AST node");
	}
	if(sol_is_aststmt(obj)) {
		if(sol_string_eq(state, str, "kind")) {
			res = sol_new_int(state, -1);
		} else if(sol_string_eq(state, str, "type")) {
			res = sol_new_int(state, stmt->type);
		} else if(sol_string_eq(state, str, "loc")) {
			res = sol_new_map(state);
			sol_map_set_name(state, res, "line", sol_new_int(state, stmt->loc.line));
			sol_map_set_name(state, res, "col", sol_new_int(state, stmt->loc.col));
		} else {
			switch(stmt->type) {
				case ST_EXPR:
					if(sol_string_eq(state, str, "expr")) {
						res = sol_new_exprnode(state, ex_copy(stmt->expr));
					}
					break;

				case ST_LIST:
					if(sol_string_eq(state, str, "stmtlist")) {
						res = sol_new_list(state);
						curs = stmt->stmtlist;
						while(curs) {
							sol_list_insert(state, res, i++, sol_new_stmtnode(state, st_copy(curs->stmt)));
							curs = curs->next;
						}
					}
					break;

				case ST_RET:
					if(sol_string_eq(state, str, "ret")) {
						res = sol_new_exprnode(state, ex_copy(stmt->ret->ret));
					}
					break;

				case ST_CONT:
					if(sol_string_eq(state, str, "val")) {
						res = sol_new_exprnode(state, ex_copy(stmt->cont->val));
					}
					break;

				case ST_BREAK:
					if(sol_string_eq(state, str, "val")) {
						res = sol_new_exprnode(state, ex_copy(stmt->brk->val));
					}
					break;
			}
		}
	} else {
		if(sol_string_eq(state, str, "kind")) {
			res = sol_new_int(state, -2);
		} else if(sol_string_eq(state, str, "type")) {
			res = sol_new_int(state, expr->type);
		} else if(sol_string_eq(state, str, "loc")) {
			res = sol_new_map(state);
			sol_map_set_name(state, res, "line", sol_new_int(state, expr->loc.line));
			sol_map_set_name(state, res, "col", sol_new_int(state, expr->loc.col));
		} else {
			switch(expr->type) {
				case EX_LIT:
					if(sol_string_eq(state, str, "littype")) {
						res = sol_new_int(state, expr->lit->type);
					} else if(sol_string_eq(state, str, "ival")) {
						res = sol_new_int(state, expr->lit->ival);
					} else if(sol_string_eq(state, str, "fval")) {
						res = sol_new_float(state, expr->lit->fval);
					} else if(sol_string_eq(state, str, "str")) {
						res = sol_new_string(state, expr->lit->str);
					}
					break;

				case EX_LISTGEN:
					if(sol_string_eq(state, str, "list")) {
						res = sol_new_list(state);
						cure = expr->listgen->list;
						while(cure) {
							sol_list_insert(state, res, i++, sol_new_exprnode(state, ex_copy(cure->expr)));
							cure = cure->next;
						}
					}
					break;

				case EX_MAPGEN:
					if(sol_string_eq(state, str, "map")) {
						res = sol_new_list(state);
						cura = expr->mapgen->map;
						while(cura) {
							pair = sol_new_list(state);
							sol_list_insert(state, pair, 0, sol_new_exprnode(state, ex_copy(cura->item->key)));
							sol_list_insert(state, pair, 1, sol_new_exprnode(state, ex_copy(cura->item->value)));
							sol_list_insert(state, res, i++, pair);
							sol_obj_free(pair);
						}
					}
					break;

				case EX_BINOP:
					if(sol_string_eq(state, str, "binoptype")) {
						res = sol_new_int(state, expr->binop->type);
					} else if(sol_string_eq(state, str, "left")) {
						res = sol_new_exprnode(state, ex_copy(expr->binop->left));
					} else if(sol_string_eq(state, str, "right")) {
						res = sol_new_exprnode(state, ex_copy(expr->binop->right));
					}
					break;

				case EX_UNOP:
					if(sol_string_eq(state, str, "unoptype")) {
						res = sol_new_int(state, expr->unop->type);
					} else if(sol_string_eq(state, str, "expr")) {
						res = sol_new_exprnode(state, ex_copy(expr->unop->expr));
					}
					break;

				case EX_INDEX:
					if(sol_string_eq(state, str, "expr")) {
						res = sol_new_exprnode(state, ex_copy(expr->index->expr));
					} else if(sol_string_eq(state, str, "index")) {
						res = sol_new_exprnode(state, ex_copy(expr->index->index));
					}
					break;

				case EX_SETINDEX:
					if(sol_string_eq(state, str, "expr")) {
						res = sol_new_exprnode(state, ex_copy(expr->setindex->expr));
					} else if(sol_string_eq(state, str, "index")) {
						res = sol_new_exprnode(state, ex_copy(expr->setindex->index));
					} else if(sol_string_eq(state, str, "value")) {
						res = sol_new_exprnode(state, ex_copy(expr->setindex->value));
					}
					break;

				case EX_ASSIGN:
					if(sol_string_eq(state, str, "ident")) {
						res = sol_new_string(state, expr->assign->ident);
					} else if(sol_string_eq(state, str, "value")) {
						res = sol_new_exprnode(state, ex_copy(expr->assign->value));
					}
					break;

				case EX_REF:
					if(sol_string_eq(state, str, "ident")) {
						res = sol_new_string(state, expr->ref->ident);
					}
					break;

				case EX_CALL:
					if(sol_string_eq(state, str, "expr")) {
						res = sol_new_exprnode(state, ex_copy(expr->call->expr));
					} else if(sol_string_eq(state, str, "args")) {
						res = sol_new_list(state);
						cure = expr->call->args;
						while(cure) {
							sol_list_insert(state, res, i++, sol_new_exprnode(state, ex_copy(cure->expr)));
							cure = cure->next;
						}
					}
					break;

				case EX_FUNCDECL:
					if(sol_string_eq(state, str, "name")) {
						res = sol_new_string(state, (expr->funcdecl->name ? expr->funcdecl->name : ""));
					} else if(sol_string_eq(state, str, "args")) {
						res = sol_new_list(state);
						curi = expr->funcdecl->params ? expr->funcdecl->params->args : NULL;
						while(curi) {
							sol_list_insert(state, res, i++, sol_new_string(state, curi->ident));
							curi = curi->next;
						}
					} else if(sol_string_eq(state, str, "body")) {
						res = sol_new_stmtnode(state, st_copy(expr->funcdecl->body));
					}
					break;

				case EX_IFELSE:
					if(sol_string_eq(state, str, "cond")) {
						res = sol_new_exprnode(state, ex_copy(expr->ifelse->cond));
					} else if(sol_string_eq(state, str, "iftrue")) {
						res = sol_new_stmtnode(state, st_copy(expr->ifelse->iftrue));
					} else if(sol_string_eq(state, str, "iffalse")) {
						res = sol_new_stmtnode(state, st_copy(expr->ifelse->iffalse));
					}
					break;

				case EX_LOOP:
					if(sol_string_eq(state, str, "cond")) {
						res = sol_new_exprnode(state, ex_copy(expr->loop->cond));
					} else if(sol_string_eq(state, str, "loop")) {
						res = sol_new_stmtnode(state, st_copy(expr->loop->loop));
					}
					break;

				case EX_ITER:
					if(sol_string_eq(state, str, "var")) {
						res = sol_new_string(state, expr->iter->var);
					} else if(sol_string_eq(state, str, "iter")) {
						res = sol_new_exprnode(state, ex_copy(expr->iter->iter));
					} else if(sol_string_eq(state, str, "loop")) {
						res = sol_new_stmtnode(state, st_copy(expr->iter->loop));
					}
					break;
			}
		}
	}
	sol_obj_free(obj);
	sol_obj_free(key);
	sol_obj_free(str);
	if(!res) {
		res = sol_incref(state->None);
	}
	return res;
}

sol_object_t *sol_f_astnode_setindex(sol_state_t *state, sol_object_t *args) {
	sol_object_t *obj = sol_list_get_index(state, args, 0), *key = sol_list_get_index(state, args, 1), *str = sol_cast_string(state, key), *val = sol_list_get_index(state, args, 2), *pair;
	sol_object_t *ival, *fval, *sval;
	stmt_node *stmt = (stmt_node *) obj->node;
	stmtlist_node *curs, *prevs;
	expr_node *expr = (expr_node *) obj->node;
	exprlist_node *cure, *preve = NULL;
	assoclist_node *cura, *preva = NULL;
	identlist_node *curi, *previ = NULL;
	int i = 0, len;
	if(!stmt) {
		sol_obj_free(obj);
		sol_obj_free(key);
		sol_obj_free(str);
		sol_obj_free(val);
		return sol_set_error_string(state, "Access NULL AST node");
	}
	if(sol_is_aststmt(obj)) {
		if(sol_string_eq(state, str, "type")) {
			ival = sol_cast_int(state, val);
			stmt->type = ival->ival;
			sol_obj_free(ival);
		} else if(sol_string_eq(state, str, "loc") && sol_is_map(val)) {
			pair = sol_map_get_name(state, val, "line");
			ival = sol_cast_int(state, pair);
			stmt->loc.line = ival->ival;
			sol_obj_free(ival);
			sol_obj_free(pair);
			pair = sol_map_get_name(state, val, "col");
			ival = sol_cast_int(state, pair);
			stmt->loc.col = ival->ival;
			sol_obj_free(ival);
			sol_obj_free(pair);
		} else {
			switch(stmt->type) {
				case ST_EXPR:
					if(sol_string_eq(state, str, "expr") && sol_is_astexpr(val)) {
						ex_free(stmt->expr);
						stmt->expr = ex_copy(val->node);
					}
					break;

				case ST_LIST:
					if(sol_string_eq(state, str, "stmtlist") && sol_is_list(val)) {
						stl_free(stmt->stmtlist);
						len = sol_list_len(state, val);
						if(len > 0) {
							curs = malloc(sizeof(stmtlist_node));
							stmt->stmtlist = curs;
							for(i = 0; i < len; i++) {
								if(sol_is_aststmt(sol_list_get_index(state, val, i))) {
									curs->stmt = st_copy(sol_list_get_index(state, val, i)->node);
									prevs = curs;
									curs = malloc(sizeof(stmtlist_node));
									prevs->next = curs;
								}
							}
							if(stmt->stmtlist == curs) {
								stmt->stmtlist = NULL;
							}
							free(curs);
							if(prevs) {
								prevs->next = NULL;
							}
						} else {
							stmt->stmtlist = NULL;
						}
					}
					break;

				case ST_RET:
					if(sol_string_eq(state, str, "ret") && sol_is_astexpr(val)) {
						ex_free(stmt->ret->ret);
						stmt->ret->ret = ex_copy(val->node);
					}
					break;

				case ST_CONT:
					if(sol_string_eq(state, str, "val") && sol_is_astexpr(val)) {
						ex_free(stmt->cont->val);
						stmt->cont->val = ex_copy(val->node);
					}
					break;

				case ST_BREAK:
					if(sol_string_eq(state, str, "val") && sol_is_astexpr(val)) {
						ex_free(stmt->brk->val);
						stmt->brk->val = ex_copy(val->node);
					}
					break;
			}
		}
	} else {
		if(sol_string_eq(state, str, "type")) {
			ival = sol_cast_int(state, val);
			expr->type = ival->ival;
			sol_obj_free(ival);
		} else if(sol_string_eq(state, str, "loc") && sol_is_map(val)) {
			pair = sol_map_get_name(state, val, "line");
			ival = sol_cast_int(state, pair);
			expr->loc.line = ival->ival;
			sol_obj_free(ival);
			sol_obj_free(pair);
			pair = sol_map_get_name(state, val, "col");
			ival = sol_cast_int(state, pair);
			expr->loc.col = ival->ival;
			sol_obj_free(ival);
			sol_obj_free(pair);
		} else {
			switch(expr->type) {
				case EX_LIT:
					if(sol_string_eq(state, str, "littype")) {
						ival = sol_cast_int(state, val);
						expr->lit->type = ival->ival;
						sol_obj_free(ival);
					} else if(sol_string_eq(state, str, "ival")) {
						ival = sol_cast_int(state, val);
						expr->lit->ival = ival->ival;
						sol_obj_free(ival);
					} else if(sol_string_eq(state, str, "fval")) {
						fval = sol_cast_float(state, val);
						expr->lit->fval = fval->fval;
						sol_obj_free(fval);
					} else if(sol_string_eq(state, str, "str")) {
						sval = sol_cast_string(state, val);
						expr->lit->str = strdup(sval->str);
						sol_obj_free(sval);
					}
					break;

				case EX_LISTGEN:
					if(sol_string_eq(state, str, "list") && sol_is_list(val)) {
						exl_free(expr->listgen->list);
						len = sol_list_len(state, val);
						if(len > 0) {
							cure = malloc(sizeof(exprlist_node));
							expr->listgen->list = cure;
							for(i = 0; i < len; i++) {
								if(sol_is_astexpr(sol_list_get_index(state, val, i))) {
									cure->expr = sol_list_get_index(state, val, i)->node;
									preve = cure;
									cure = malloc(sizeof(exprlist_node));
									preve->next = cure;
								}
							}
							if(expr->listgen->list == cure) {
								expr->listgen->list = NULL;
							}
							free(cure);
							if(preve) {
								preve->next = NULL;
							}
						} else {
							expr->listgen->list = NULL;
						}
					}
					break;

				case EX_MAPGEN:
					if(sol_string_eq(state, str, "map") && sol_is_list(val)) {
						asl_free(expr->mapgen->map);
						len = sol_list_len(state, val);
						if(len > 0) {
							cura = malloc(sizeof(assoclist_node));
							expr->mapgen->map = cura;
							for(i = 0; i < len; i++) {
								if(sol_is_list(sol_list_get_index(state, val, i))) {
									pair = sol_list_get_index(state, val, i);
									if(sol_list_len(state, pair) >= 2 && sol_is_astexpr(sol_list_get_index(state, pair, 0)) && sol_is_astexpr(sol_list_get_index(state, pair, 1))) {
										cura->item = malloc(sizeof(associtem_node));
										cura->item->key = sol_list_get_index(state, pair, 0)->node;
										cura->item->value = sol_list_get_index(state, pair, 1)->node;
										preva = cura;
										cura = malloc(sizeof(assoclist_node));
										preva->next = cura;
									}
								}
							}
							if(expr->mapgen->map == cura) {
								expr->mapgen->map = NULL;
							}
							free(cura);
							if(preva) {
								preva->next = NULL;
							}
						} else {
							expr->mapgen->map = NULL;
						}
					}
					break;

				case EX_BINOP:
					if(sol_string_eq(state, str, "binoptype")) {
						ival = sol_cast_int(state, val);
						expr->binop->type = ival->ival;
						sol_obj_free(ival);
					} else if(sol_string_eq(state, str, "left") && sol_is_astexpr(val)) {
						ex_free(expr->binop->left);
						expr->binop->left = ex_copy(val->node);
					} else if(sol_string_eq(state, str, "right") && sol_is_astexpr(val)) {
						ex_free(expr->binop->right);
						expr->binop->right = ex_copy(val->node);
					}
					break;

				case EX_UNOP:
					if(sol_string_eq(state, str, "unoptype")) {
						ival = sol_cast_int(state, val);
						expr->unop->type = ival->ival;
						sol_obj_free(ival);
					} else if(sol_string_eq(state, str, "expr") && sol_is_astexpr(val)) {
						ex_free(expr->unop->expr);
						expr->unop->expr = ex_copy(val->node);
					}
					break;

				case EX_INDEX:
					if(sol_string_eq(state, str, "expr") && sol_is_astexpr(val)) {
						ex_free(expr->index->expr);
						expr->index->expr = ex_copy(val->node);
					} else if(sol_string_eq(state, str, "index") && sol_is_astexpr(val)) {
						ex_free(expr->index->index);
						expr->index->index = ex_copy(val->node);
					}
					break;

				case EX_SETINDEX:
					if(sol_string_eq(state, str, "expr") && sol_is_astexpr(val)) {
						ex_free(expr->setindex->expr);
						expr->setindex->expr = ex_copy(val->node);
					} else if(sol_string_eq(state, str, "index") && sol_is_astexpr(val)) {
						ex_free(expr->setindex->index);
						expr->setindex->index = ex_copy(val->node);
					} else if(sol_string_eq(state, str, "value") && sol_is_astexpr(val)) {
						ex_free(expr->setindex->value);
						expr->setindex->value = ex_copy(val->node);
					}
					break;

				case EX_ASSIGN:
					if(sol_string_eq(state, str, "ident")) {
						sval = sol_cast_string(state, val);
						expr->assign->ident = strdup(sval->str);
						sol_obj_free(sval);
					} else if(sol_string_eq(state, str, "value") && sol_is_astexpr(val)) {
						ex_free(expr->assign->value);
						expr->assign->value = ex_copy(val->node);
					}
					break;

				case EX_REF:
					if(sol_string_eq(state, str, "ident")) {
						sval = sol_cast_string(state, val);
						expr->ref->ident = strdup(sval->str);
						sol_obj_free(sval);
					}
					break;

				case EX_CALL:
					if(sol_string_eq(state, str, "expr") && sol_is_astexpr(val)) {
						ex_free(expr->call->expr);
						expr->call->expr = ex_copy(val->node);
					} else if(sol_string_eq(state, str, "args") && sol_is_list(val)) {
						exl_free(expr->call->args);
						len = sol_list_len(state, val);
						if(len > 0) {
							cure = malloc(sizeof(exprlist_node));
							expr->call->args = cure;
							for(i = 0; i < len; i++) {
								if(sol_is_astexpr(sol_list_get_index(state, val, i))) {
									cure->expr = sol_list_get_index(state, val, i)->node;
									preve = cure;
									cure = malloc(sizeof(exprlist_node));
									preve->next = cure;
								}
							}
							if(expr->call->args == cure) {
								expr->call->args = NULL;
							}
							free(cure);
							if(preve) {
								preve->next = NULL;
							}
						} else {
							expr->call->args = NULL;
						}
					}
					break;

				case EX_FUNCDECL:
					if(sol_string_eq(state, str, "name")) {
						sval = sol_cast_string(state, val);
						expr->funcdecl->name = strdup(sval->str);
						sol_obj_free(sval);
					} else if(sol_string_eq(state, str, "args") && sol_is_list(val)) {
						if(!expr->funcdecl->params) {
							expr->funcdecl->params = malloc(sizeof(paramlist_node));
							expr->funcdecl->params->args = NULL;
							expr->funcdecl->params->clkeys = NULL;
							expr->funcdecl->params->clvalues = NULL;
							expr->funcdecl->params->rest = NULL;
						}
						idl_free(expr->funcdecl->params->args);
						len = sol_list_len(state, val);
						if(len > 0) {
							curi = malloc(sizeof(identlist_node));
							expr->funcdecl->params->args = curi;
							for(i = 0; i < len; i++) {
								sval = sol_cast_string(state, sol_list_get_index(state, val, i));
								curi->ident = strdup(sval->str);
								sol_obj_free(sval);
								previ = curi;
								curi = malloc(sizeof(identlist_node));
								previ->next = curi;
							}
							if(expr->funcdecl->params->args == curi) {
								expr->funcdecl->params->args = NULL;
							}
							free(curi);
							if(previ) {
								previ->next = NULL;
							}
						} else {
							expr->funcdecl->params->args = NULL;
						}
					} else if(sol_string_eq(state, str, "body") && sol_is_aststmt(val)) {
						st_free(expr->funcdecl->body);
						expr->funcdecl->body = st_copy(val->node);
					}
					break;

				case EX_IFELSE:
					if(sol_string_eq(state, str, "cond") && sol_is_astexpr(val)) {
						ex_free(expr->ifelse->cond);
						expr->ifelse->cond = ex_copy(val->node);
					} else if(sol_string_eq(state, str, "iftrue") && sol_is_aststmt(val)) {
						st_free(expr->ifelse->iftrue);
						expr->ifelse->iftrue = st_copy(val->node);
					} else if(sol_string_eq(state, str, "iffalse") && sol_is_aststmt(val)) {
						st_free(expr->ifelse->iffalse);
						expr->ifelse->iffalse = st_copy(val->node);
					}
					break;

				case EX_LOOP:
					if(sol_string_eq(state, str, "cond") && sol_is_astexpr(val)) {
						ex_free(expr->loop->cond);
						expr->loop->cond = ex_copy(val->node);
					} else if(sol_string_eq(state, str, "loop") && sol_is_aststmt(val)) {
						st_free(expr->loop->loop);
						expr->loop->loop = st_copy(val->node);
					}
					break;

				case EX_ITER:
					if(sol_string_eq(state, str, "var")) {
						sval = sol_cast_string(state, val);
						expr->iter->var = strdup(sval->str);
						sol_obj_free(sval);
					} else if(sol_string_eq(state, str, "iter") && sol_is_astexpr(val)) {
						ex_free(expr->iter->iter);
						expr->iter->iter = ex_copy(val->node);
					} else if(sol_string_eq(state, str, "loop") && sol_is_aststmt(val)) {
						st_free(expr->iter->loop);
						expr->iter->loop = st_copy(val->node);
					}
					break;
			}
		}
	}
	sol_obj_free(obj);
	sol_obj_free(key);
	sol_obj_free(str);
	return val;
}

static char *sol_StmtNames[] = {"EXPR", "LIST", "RET", "CONT", "BREAK"};
static char *sol_ExprNames[] = {"LIT", "LISTGEN", "MAPGEN", "BINOP", "UNOP", "INDEX", "SETINDEX", "ASSIGN", "REF", "CALL", "FUNCDECL", "IFELSE", "LOOP", "ITER"};

sol_object_t *sol_f_astnode_tostring(sol_state_t *state, sol_object_t *args) {
	sol_object_t *obj = sol_list_get_index(state, args, 0), *res;
	char s[64];
	if(sol_is_aststmt(obj)) {
		if(!obj->node) {
			snprintf(s, 64, "<NULL Stmt>");
		} else {
			stmt_t type = ((stmt_node *) obj->node)->type;
			if(type < sizeof(sol_StmtNames)/sizeof(char *)) {
				snprintf(s, 64, "<Stmt[%s]>", sol_StmtNames[type]);
			} else {
				snprintf(s, 64, "<Invalid Stmt>");
			}
		}
	} else {
		if(!obj->node) {
			snprintf(s, 64, "<NULL Expr>");
		} else {
			expr_t type = ((stmt_node *) obj->node)->type;
			if(type < sizeof(sol_ExprNames)/sizeof(char *)) {
				snprintf(s, 64, "<Expr[%s]>", sol_ExprNames[type]);
			} else {
				snprintf(s, 64, "<Invalid Expr>");
			}
		}
	}
	sol_obj_free(obj);
	return sol_new_string(state, s);
}

sol_object_t *sol_f_buffer_index(sol_state_t *state, sol_object_t *args) {
	sol_object_t *a = sol_list_get_index(state, args, 0);
	sol_object_t *key = sol_list_get_index(state, args, 1), *funcs = sol_get_methods_name(state, "buffer");
	sol_object_t *res;
	if(sol_is_name(key)) {
		res = sol_map_get(state, funcs, key);
	} else if(sol_is_int(key)) {
		res = sol_new_buffer(state, ((char *) a->buffer) + key->ival, (a->sz < 0) ? a->sz : (a->sz - key->ival), OWN_NONE, NULL, NULL);
	} else {
		res = sol_f_not_impl(state, args);
	}
	sol_obj_free(a);
	sol_obj_free(key);
	sol_obj_free(funcs);
	return res;
}

sol_object_t *sol_f_buffer_add(sol_state_t *state, sol_object_t *args) {
	sol_object_t *a = sol_list_get_index(state, args, 0), *b = sol_list_get_index(state, args, 1), *bb = sol_cast_buffer(state, b);
	sol_object_t *res = sol_buffer_concat(state, a, bb);
	sol_obj_free(a);
	sol_obj_free(b);
	if(sol_has_error(state)) {
		sol_obj_free(res);
		return sol_incref(state->None);
	}
	return res;
}

sol_object_t *sol_f_buffer_mul(sol_state_t *state, sol_object_t *args) {
	sol_object_t *a = sol_list_get_index(state, args, 0), *b = sol_list_get_index(state, args, 1), *bint = sol_cast_int(state, b);
	long ival, i;
	size_t sz;
	char *buf;
	sol_obj_free(b);
	if(sol_has_error(state)) {
		sol_obj_free(a);
		sol_obj_free(bint);
		return sol_incref(state->None);
	}
	ival = bint->ival;
	sol_obj_free(bint);
	if(a->sz < 0) {
		sol_obj_free(a);
		return sol_set_error_string(state, "Multiply unsized buffer");
	}
	if(ival < 0) {
		ival = 0;
	}
	sz = a->sz * ival;
	buf = malloc(sz * sizeof(char));
	for(i = 0; i < ival; i++) {
		memcpy(buf + (i * a->sz), a->buffer, a->sz);
	}
	sol_obj_free(a);
	return sol_new_buffer(state, buf, sz, OWN_FREE, NULL, NULL);
}

sol_object_t *sol_f_buffer_cmp(sol_state_t *state, sol_object_t *args) {
	sol_object_t *a = sol_list_get_index(state, args, 0), *b = sol_list_get_index(state, args, 1);
	sol_object_t *bb, *res;
	if(sol_is_string(b)) {
		bb = sol_cast_buffer(state, b);
		sol_obj_free(b);
		b = bb;
	}
	if(sol_is_buffer(b)) {
		ssize_t len = a->sz;
		if(a->sz >= 0 && b->sz >= 0 && a->sz != b->sz) {
			res = sol_new_int(state, 1);
		} else {
			if(len < 0) {
				len = b->sz;
			}
			if(a->sz >= 0 && len > a->sz) len = a->sz;
			if(b->sz >= 0 && len > b->sz) len = b->sz;
			if(len < 0) {
				res = sol_new_int(state, 1);
			} else {
				res = sol_new_int(state, memcmp(a->buffer, b->buffer, len));
			}
		}
	} else {
		res = sol_new_int(state, 1);
	}
	sol_obj_free(a);
	sol_obj_free(b);
	return res;
}

sol_object_t *sol_f_buffer_len(sol_state_t *state, sol_object_t *args) {
	sol_object_t *a = sol_list_get_index(state, args, 0);
	sol_object_t *res = sol_new_int(state, a->sz);
	sol_obj_free(a);
	return res;
}

sol_object_t *sol_f_buffer_iter(sol_state_t *state, sol_object_t *args) {
	return sol_new_cfunc(state, sol_f_iter_buffer, "iter.buffer");
}

sol_object_t *sol_f_buffer_tostring(sol_state_t *state, sol_object_t *args) {
	sol_object_t *buf = sol_list_get_index(state, args, 0), *res;
	char *b;
	/*
	char s[64];
	if(buf->sz == -1) {
		snprintf(s, 64, "<Buffer @%p>", buf->buffer);
	} else {
		snprintf(s, 64, "<Buffer @%p size %ld>", buf->buffer, buf->sz);
	}
	*/
	if(buf->sz < 0) {
		res = sol_new_string(state, "<UNSIZED_BUFFER>");
	} else {
		b = malloc(buf->sz + 1);
		strncpy(b, buf->buffer, buf->sz);
		b[buf->sz] = '\0';
		res = sol_new_string(state, b);
		free(b);
	}
	sol_obj_free(buf);
	return res;
}

sol_object_t *sol_f_buffer_repr(sol_state_t *state, sol_object_t *args) {
	sol_object_t *obj = sol_list_get_index(state, args, 0), *cur = sol_new_buffer(state, "'", 1, OWN_NONE, NULL, NULL), *next = sol_buffer_concat(state, cur, obj);
	sol_object_t *termin = sol_new_buffer(state, "'", 2, OWN_NONE, NULL, NULL);
	sol_obj_free(cur);
	cur = next;
	next = sol_buffer_concat(state, cur, termin);
	sol_obj_free(cur);
	return next;
}

sol_object_t *sol_f_buffer_toint(sol_state_t *state, sol_object_t *args) {
	sol_object_t *a = sol_list_get_index(state, args, 0);
	sol_object_t *res = sol_new_int(state, a->buffer ? atoi(a->buffer) : 0);
	sol_obj_free(a);
	return res;
}

sol_object_t *sol_f_buffer_tofloat(sol_state_t *state, sol_object_t *args) {
	sol_object_t *a = sol_list_get_index(state, args, 0);
	sol_object_t *res = sol_new_float(state, a->buffer ? atof(a->buffer) : 0.0);
	sol_obj_free(a);
	return res;
}

sol_object_t *sol_f_buffer_tobuffer(sol_state_t *state, sol_object_t *args) {
	return sol_list_get_index(state, args, 0);
}

sol_object_t *sol_f_buffer_new(sol_state_t *state, sol_object_t *args) {
	sol_object_t *sz = sol_list_get_index(state, args, 0), *isz = sol_cast_int(state, sz);
	size_t bufsz = isz->ival;
	void *buf = malloc(bufsz);
	sol_obj_free(sz);
	sol_obj_free(isz);
	if(buf) {
		return sol_new_buffer(state, buf, bufsz, OWN_FREE, NULL, NULL);
	}
	return sol_incref(state->None);
}

sol_object_t *sol_f_buffer_get(sol_state_t *state, sol_object_t *args) {
	sol_object_t *buf = sol_list_get_index(state, args, 0), *tp = sol_list_get_index(state, args, 1), *off = sol_list_get_index(state, args, 2);
	sol_object_t *itp = sol_cast_int(state, tp), *ioff, *res = NULL;
	sol_buftype_t buftp = itp->ival;
	char *data, cbuf[2] = {0, 0};
	sol_obj_free(tp);
	sol_obj_free(itp);
	if(!sol_is_none(state, off)) {
		ioff = sol_cast_int(state, off);
	} else {
		ioff = sol_new_int(state, 0);
	}
	sol_obj_free(off);
	if(buf->sz != -1 && (ioff->ival >= buf->sz || ioff->ival < 0)) {
		sol_obj_free(buf);
		sol_obj_free(ioff);
		return sol_incref(state->None);
	}
	data = ((char *) buf->buffer) + ioff->ival;
	sol_obj_free(buf);
	sol_obj_free(ioff);
	switch(buftp) {
		case BUF_UINT8:
			res = sol_new_int(state, *((uint8_t *) data));
			break;

		case BUF_UINT16:
			res = sol_new_int(state, *((uint16_t *) data));
			break;

		case BUF_UINT32:
			res = sol_new_int(state, *((uint32_t *) data));
			break;

		case BUF_UINT64:
			res = sol_new_int(state, *((uint64_t *) data));
			break;

		case BUF_INT8:
			res = sol_new_int(state, *((int8_t *) data));
			break;

		case BUF_INT16:
			res = sol_new_int(state, *((int16_t *) data));
			break;

		case BUF_INT32:
			res = sol_new_int(state, *((int32_t *) data));
			break;

		case BUF_INT64:
			res = sol_new_int(state, *((int64_t *) data));
			break;

		case BUF_CHAR:
			cbuf[0] = *((char *) data);
			res = sol_new_string(state, cbuf);
			break;

		case BUF_BYTE:
			res = sol_new_int(state, *((unsigned char *) data));
			break;

		case BUF_INT:
			res = sol_new_int(state, *((int *) data));
			break;

		case BUF_UINT:
			res = sol_new_int(state, *((unsigned int *) data));
			break;

		case BUF_LONG:
			res = sol_new_int(state, *((long *) data));
			break;

		case BUF_ULONG:
			res = sol_new_int(state, *((unsigned long *) data));
			break;

		case BUF_FLOAT:
			res = sol_new_float(state, *((float *) data));
			break;

		case BUF_DOUBLE:
			res = sol_new_float(state, *((double *) data));
			break;

		case BUF_CSTR:
			res = sol_new_string(state, (char *) data);
			break;

		case BUF_PTR:
			res = sol_new_buffer(state, ((void *) * ((unsigned long *) data)), -1, OWN_NONE, NULL, NULL);
			break;
	}
	if(!res) {
		res = sol_incref(state->None);
	}
	return res;
}

sol_object_t *sol_f_buffer_set(sol_state_t *state, sol_object_t *args) {
	sol_object_t *buf = sol_list_get_index(state, args, 0), *tp = sol_list_get_index(state, args, 1), *val = sol_list_get_index(state, args, 2), *off = sol_list_get_index(state, args, 3);
	sol_object_t *itp = sol_cast_int(state, tp), *ioff, *ival = NULL, *fval = NULL, *sval = NULL;
	sol_buftype_t buftp = itp->ival;
	char *data;
	sol_obj_free(tp);
	sol_obj_free(itp);
	if(!sol_is_none(state, off)) {
		ioff = sol_cast_int(state, off);
	} else {
		ioff = sol_new_int(state, 0);
	}
	sol_obj_free(off);
	if(buf->sz != -1 && (ioff->ival >= buf->sz || ioff->ival < 0)) {
		sol_obj_free(buf);
		sol_obj_free(ioff);
		return sol_incref(state->None);
	}
	data = ((char *) buf->buffer) + ioff->ival;
	sol_obj_free(buf);
	sol_obj_free(ioff);
	switch(buftp) {
		case BUF_UINT8:
			ival = sol_cast_int(state, val);
			*((uint8_t *) data) = ival->ival;
			break;

		case BUF_UINT16:
			ival = sol_cast_int(state, val);
			*((uint16_t *) data) = ival->ival;
			break;

		case BUF_UINT32:
			ival = sol_cast_int(state, val);
			*((uint32_t *) data) = ival->ival;
			break;

		case BUF_UINT64:
			ival = sol_cast_int(state, val);
			*((uint64_t *) data) = ival->ival;
			break;

		case BUF_INT8:
			ival = sol_cast_int(state, val);
			*((int8_t *) data) = ival->ival;
			break;

		case BUF_INT16:
			ival = sol_cast_int(state, val);
			*((int16_t *) data) = ival->ival;
			break;

		case BUF_INT32:
			ival = sol_cast_int(state, val);
			*((int32_t *) data) = ival->ival;
			break;

		case BUF_INT64:
			ival = sol_cast_int(state, val);
			*((int64_t *) data) = ival->ival;
			break;

		case BUF_CHAR:
			sval = sol_cast_string(state, val);
			*((char *) data) = sval->str[0];
			break;

		case BUF_BYTE:
			ival = sol_cast_int(state, val);
			*((unsigned char *) data) = ival->ival;
			break;

		case BUF_INT:
			ival = sol_cast_int(state, val);
			*((int *) data) = ival->ival;
			break;

		case BUF_UINT:
			ival = sol_cast_int(state, val);
			*((unsigned int *) data) = ival->ival;
			break;

		case BUF_LONG:
			ival = sol_cast_int(state, val);
			*((long *) data) = ival->ival;
			break;

		case BUF_ULONG:
			ival = sol_cast_int(state, val);
			*((unsigned long *) data) = ival->ival;
			break;

		case BUF_FLOAT:
			fval = sol_cast_float(state, val);
			*((float *) data) = fval->fval;
			break;

		case BUF_DOUBLE:
			fval = sol_cast_float(state, val);
			*((double *) data) = fval->fval;
			break;

		case BUF_CSTR:
			sval = sol_cast_string(state, val);
			strcpy((char *) data, sval->str);
			break;

		case BUF_PTR:
			if(sol_is_buffer(val)) {
				*((unsigned long *) data) = ((unsigned long) val->buffer);
				if(val->own == OWN_CALLF) {
					val->movef(val->buffer, val->sz);
				}
			} else {
				ival = sol_cast_int(state, val);
				*((unsigned long *) data) = ival->ival;
			}
			break;
	}
	if(ival) {
		sol_obj_free(ival);
	}
	if(fval) {
		sol_obj_free(fval);
	}
	if(sval) {
		sol_obj_free(sval);
	}
	return sol_incref(state->None);
}

sol_object_t *sol_f_buffer_address(sol_state_t *state, sol_object_t *args) {
	sol_object_t *buf = sol_list_get_index(state, args, 0);
	sol_object_t *res = sol_new_int(state, (unsigned long) buf->buffer);
	sol_obj_free(buf);
	return res;
}

sol_object_t *sol_f_buffer_size(sol_state_t *state, sol_object_t *args) {
	sol_object_t *buf = sol_list_get_index(state, args, 0);
	sol_object_t *res = sol_new_int(state, (long) buf->sz);
	sol_obj_free(buf);
	return res;
}

sol_object_t *sol_f_buffer_sub(sol_state_t *state, sol_object_t *args) {
	sol_object_t *buf = sol_list_get_index(state, args, 0);
	sol_object_t *low = sol_list_get_index(state, args, 1), *high = sol_list_get_index(state, args, 2);
	sol_object_t *ilow, *ihigh;
	long l, h;
	char *b;
	if(sol_is_none(state, low)) {
		ilow = sol_new_int(state, 0);
	} else {
		ilow = sol_cast_int(state, low);
	}
	if(sol_is_none(state, high)) {
		ihigh = sol_new_int(state, buf->sz);
	} else {
		ihigh = sol_cast_int(state, high);
	}
	l = ilow->ival;
	h = ihigh->ival;
	sol_obj_free(low);
	sol_obj_free(high);
	sol_obj_free(ilow);
	sol_obj_free(ihigh);
	if(l < 0) {
		l += buf->sz;
		if(l < 0) {
			l = 0;
		}
	}
	if(l > buf->sz) {
		l = buf->sz;
	}
	if(h < 0) {
		h += buf->sz;
		if(h < 0) {
			h = 0;
		}
	}
	if(h > buf->sz) {
		h = buf->sz;
	}
	if(l >= h) {
		sol_obj_free(buf);
		return sol_new_buffer(state, NULL, 0, OWN_NONE, NULL, NULL);
	}
	b = malloc(sizeof(char) * (h - l));
	memcpy(b, ((char *) buf->buffer) + l, h - l);
	sol_obj_free(buf);
	return sol_new_buffer(state, b, h - l, OWN_FREE, NULL, NULL);
}

sol_object_t *sol_f_buffer_split(sol_state_t *state, sol_object_t *args) {
	sol_object_t *buf = sol_list_get_index(state, args, 0);
	char *b;
	sol_object_t *str, *res, *ls;
	if(buf->sz < 0) {
		sol_obj_free(buf);
		return sol_set_error_string(state, "split unsized buffer");
	}
	b = malloc(sizeof(char) * (buf->sz + 1));
	memcpy(b, buf->buffer, buf->sz);
	b[buf->sz] = '\0';
	str = sol_new_string(state, b);
	free(b);
	ls = sol_new_list(state);
	sol_list_insert(state, ls, 0, str);
	sol_obj_free(str);
	str = sol_list_get_index(state, args, 1);
	sol_list_insert(state, ls, 1, str);
	sol_obj_free(str);
	res = sol_f_str_split(state, ls);
	sol_obj_free(ls);
	return res;
}

sol_object_t *sol_f_buffer_find(sol_state_t *state, sol_object_t *args) {
	sol_object_t *buf = sol_list_get_index(state, args, 0), *subbuf = sol_list_get_index(state, args, 1), *bsubbuf = sol_cast_buffer(state, subbuf);
	sol_object_t *res;
	char *ptr;
	sol_obj_free(subbuf);
	if(buf->sz < 0 || bsubbuf->sz < 0) {
		sol_obj_free(buf);
		sol_obj_free(bsubbuf);
		return sol_set_error_string(state, "find with unsized buffer");
	}
	ptr = memmem(buf->buffer, buf->sz, bsubbuf->buffer, bsubbuf->sz);
	res = sol_new_int(state, ptr ? (ptr - ((char *) buf->buffer)) : -1);
	sol_obj_free(buf);
	sol_obj_free(bsubbuf);
	return res;
}

sol_object_t *sol_f_buffer_fromstring(sol_state_t *state, sol_object_t *args) {
	sol_object_t *val = sol_list_get_index(state, args, 0), *sval = sol_cast_string(state, val);
	size_t sz = strlen(sval->str) + 1;
	sol_object_t *buf = sol_new_buffer(state, malloc(sz), sz, OWN_FREE, NULL, NULL);
	strcpy(buf->buffer, sval->str);
	sol_obj_free(val);
	sol_obj_free(sval);
	return buf;
}

sol_object_t *sol_f_buffer_fromobject(sol_state_t *state, sol_object_t *args) {
	sol_object_t *obj = sol_list_get_index(state, args, 0);
	sol_object_t *buf = sol_new_buffer(state, obj, sizeof(sol_object_t), OWN_CALLF, (sol_freefunc_t) state->obfuncs.destr, (sol_movefunc_t) state->obfuncs.copy);
	//Keep ref to obj so buf remains alive
	return buf;
}

sol_object_t *sol_f_buffer_fromaddress(sol_state_t *state, sol_object_t *args) {
	sol_object_t *addr = sol_list_get_index(state, args, 0), *sz = sol_list_get_index(state, args, 1);
	sol_object_t *iaddr = sol_cast_int(state, addr), *isz = sol_cast_int(state, sz);
	sol_object_t *buf = sol_new_buffer(state, (void *) iaddr->ival, (ssize_t) isz->ival, OWN_NONE, NULL, NULL);
	sol_obj_free(addr);
	sol_obj_free(sz);
	sol_obj_free(iaddr);
	sol_obj_free(isz);
	return buf;
}

sol_object_t *sol_f_dylib_index(sol_state_t *state, sol_object_t *args) {
	sol_object_t *dylib = sol_list_get_index(state, args, 0), *key = sol_list_get_index(state, args, 1), *skey = sol_cast_string(state, key);
	void *handle = dlsym(dylib->dlhandle, skey->str);
	sol_obj_free(dylib);
	sol_obj_free(key);
	sol_obj_free(skey);
	if(handle) {
		return sol_new_dysym(state, handle, NULL, BUF_NONE);
	} else {
		return sol_incref(state->None);
	}
}

sol_object_t *sol_f_dylib_tostring(sol_state_t *state, sol_object_t *args) {
	return sol_new_string(state, "<DyLib>");
}

sol_object_t *sol_f_dysym_call(sol_state_t *state, sol_object_t *args) {
	//TODO
	return sol_incref(state->None);
}

sol_object_t *sol_f_dysym_index(sol_state_t *state, sol_object_t *args) {
	//TODO
	return sol_incref(state->None);
}

sol_object_t *sol_f_dysym_setindex(sol_state_t *state, sol_object_t *args) {
	//TODO
	return sol_incref(state->None);
}

sol_object_t *sol_f_dysym_tostring(sol_state_t *state, sol_object_t *args) {
	return sol_new_string(state, "<DySym>");
}

sol_object_t *sol_f_stream_blsh(sol_state_t *state, sol_object_t *args) {
	return sol_f_stream_write(state, args);
}

sol_object_t *sol_f_stream_brsh(sol_state_t *state, sol_object_t *args) {
	return sol_f_stream_read_buffer(state, args);
}

sol_object_t *sol_f_stream_index(sol_state_t *state, sol_object_t *args) {
	sol_object_t *key = sol_list_get_index(state, args, 1), *funcs = sol_get_methods_name(state, "stream");
	sol_object_t *res = sol_map_get(state, funcs, key);
	sol_obj_free(key);
	sol_obj_free(funcs);
	return res;
}

sol_object_t *sol_f_stream_tostring(sol_state_t *state, sol_object_t *args) {
	return sol_new_string(state, "<Stream>");
}

sol_object_t *sol_f_stream_write(sol_state_t *state, sol_object_t *args) {
	sol_object_t *stream = sol_list_get_index(state, args, 0), *obj = sol_list_get_index(state, args, 1), *str;
	size_t sz;
	if(sol_is_buffer(obj)) {
		sz = sol_stream_fwrite(state, stream, obj->buffer, sizeof(char), obj->sz);
	} else {
		str = sol_cast_string(state, obj);
		sz = sol_stream_printf(state, stream, "%s", str->str);
		sol_obj_free(str);
	}
	sol_obj_free(obj);
	sol_obj_free(stream);
	return sol_new_int(state, sz);
}

sol_object_t *sol_f_stream_read_buffer(sol_state_t *state, sol_object_t *args) {
	sol_object_t *stream = sol_list_get_index(state, args, 0), *amt = sol_list_get_index(state, args, 1), *iamt, *res;
	char *s = NULL, *p;
	size_t count = 0, max = 0, pos, end;
	if(sol_is_name(amt)) {
		if(sol_name_eq(state, amt, "ALL")) {
			pos = sol_stream_ftell(state, stream);
			sol_stream_fseek(state, stream, 0, SEEK_END);
			end = sol_stream_ftell(state, stream);
			sol_stream_fseek(state, stream, pos, SEEK_SET);
			//printf("IO: Reading %ld bytes starting at %ld\n", end-pos, pos);
			s = malloc((end - pos + 1) * sizeof(char));
			if(sol_stream_fread(state, stream, s, sizeof(char), end - pos) < (end - pos)) {
				free(s);
				sol_obj_free(stream);
				sol_obj_free(amt);
				return sol_set_error_string(state, "IO read error");
			}
			count = end - pos;
		} else if(sol_name_eq(state, amt, "LINE")) {
			s = malloc(STDIO_CHUNK_SIZE * sizeof(char));
			sol_stream_fgets(state, stream, s, STDIO_CHUNK_SIZE);
			count = strlen(s);
		}
	} else {
		iamt = sol_cast_int(state, amt);
		s = malloc((iamt->ival + 1) * sizeof(char));
		count = sol_stream_fread(state, stream, s, sizeof(char), iamt->ival);
		sol_obj_free(iamt);
	}
	if(s) {
		//printf("IO: Read result: %s\n", s);
		res = sol_new_buffer(state, s, count, OWN_FREE, NULL, NULL);
	} else {
		//printf("IO: No read result!\n");
		res = sol_incref(state->None);
	}
	sol_obj_free(amt);
	sol_obj_free(stream);
	return res;
}

/*
sol_object_t *sol_f_stream_read(sol_state_t *state, sol_object_t *args) {
	sol_object_t *buf = sol_f_stream_read_buffer(state, args);
	sol_object_t *str = sol_new_string(state, buf->buffer);
	sol_obj_free(buf);
	return str;
}
*/

sol_object_t *sol_f_stream_seek(sol_state_t *state, sol_object_t *args) {
	sol_object_t *stream = sol_list_get_index(state, args, 0), *offset = sol_list_get_index(state, args, 1), *whence = sol_list_get_index(state, args, 2);
	sol_object_t *ioffset = sol_cast_int(state, offset), *iwhence = sol_cast_int(state, whence);
	sol_object_t *res = sol_new_int(state, sol_stream_fseek(state, stream, ioffset->ival, iwhence->ival));
	sol_obj_free(stream);
	sol_obj_free(offset);
	sol_obj_free(whence);
	sol_obj_free(ioffset);
	sol_obj_free(iwhence);
	return res;
}

sol_object_t *sol_f_stream_tell(sol_state_t *state, sol_object_t *args) {
	sol_object_t *stream = sol_list_get_index(state, args, 0), *res = sol_new_int(state, sol_stream_ftell(state, stream));
	sol_obj_free(stream);
	return res;
}

sol_object_t *sol_f_stream_flush(sol_state_t *state, sol_object_t *args) {
	sol_object_t *stream = sol_list_get_index(state, args, 0), *res = sol_new_int(state, sol_stream_fflush(state, stream));
	sol_obj_free(stream);
	return res;
}

sol_object_t *sol_f_stream_eof(sol_state_t *state, sol_object_t *args) {
	sol_object_t *stream = sol_list_get_index(state, args, 0), *res = sol_new_int(state, sol_stream_feof(state, stream));
	sol_obj_free(stream);
	return res;
}

sol_object_t *sol_f_stream_ioctl(sol_state_t *state, sol_object_t *args) {
	sol_object_t *stream = sol_list_get_index(state, args, 0), *buf = sol_list_get_index(state, args, 2);
	sol_object_t *req = sol_list_get_index(state, args, 1), *ireq = sol_cast_int(state, req);
	sol_object_t *res = sol_new_int(state, ioctl(fileno(stream->stream), (unsigned long) ireq->ival, buf->buffer));
	sol_obj_free(stream);
	sol_obj_free(buf);
	sol_obj_free(req);
	sol_obj_free(ireq);
	return res;
}

static char *sol_FileModes[] = {
	NULL,
	"r",
	"w",
	"r+",
	NULL,
	NULL,
	"a",
	"a+",
	NULL,
	NULL,
	"w",
	"w+",
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	"rb",
	"wb",
	"r+b",
	NULL,
	NULL,
	"ab",
	"a+b",
	NULL,
	NULL,
	"wb",
	"w+b",
	NULL,
	NULL,
	NULL,
	NULL
};

sol_object_t *sol_f_stream_open(sol_state_t *state, sol_object_t *args) {
	sol_object_t *fn = sol_list_get_index(state, args, 0), *mode = sol_list_get_index(state, args, 1);
	sol_object_t *sfn = sol_cast_string(state, fn), *imode = sol_cast_int(state, mode);
	sol_modes_t m = imode->ival;
	char *smode = sol_FileModes[(m >= 0 && m < (sizeof(sol_FileModes) / sizeof(char *))) ? m : 0];
	FILE *f;
	sol_obj_free(mode);
	sol_obj_free(imode);
	if(!smode) {
		sol_obj_free(fn);
		sol_obj_free(sfn);
		return sol_set_error_string(state, "Bad file open mode");
	}
	f = fopen(sfn->str, smode);
	sol_obj_free(sfn);
	sol_obj_free(fn);
	if(!f) {
		return sol_set_error_string(state, "File open failed");
	}
	return sol_new_stream(state, f, m);
}
