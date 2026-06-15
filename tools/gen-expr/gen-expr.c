/***************************************************************************************
* Copyright (c) 2014-2022 Zihao Yu, Nanjing University
*
* NEMU is licensed under Mulan PSL v2.
* You can use this software according to the terms and conditions of the Mulan PSL v2.
* You may obtain a copy of Mulan PSL v2 at:
*          http://license.coscl.org.cn/MulanPSL2
*
* THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
* EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
* MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
*
* See the Mulan PSL v2 for more details.
***************************************************************************************/
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <assert.h>
#include <string.h>
#include <stdbool.h>
#include <sys/wait.h>

static char buf[65536];
static char code_expr_buf[262144];
static char code_buf[262144 + 512];

static char *code_format =
    "#include <stdio.h>\n"
    "static volatile unsigned nemu_expr_sink; "
    "static unsigned nemu_expr_id(unsigned x) { "
    "  nemu_expr_sink = x; "
    "  return nemu_expr_sink; "
    "} "
    "int main() { "
    "  unsigned result = %s; "
    "  printf(\"%%u\", result); "
    "  return 0; "
    "}";

static size_t buf_id;
static size_t code_expr_id;

static uint32_t choose(uint32_t n) {
    return rand() % n;
}

static void gen_to(char *dst, size_t size, size_t *id, char c) {
    assert(*id < size - 1);
    dst[(*id)++] = c;
}

static void gen(char c) {
    gen_to(buf, sizeof(buf), &buf_id, c);
}

static void gen_code(char c) {
    gen_to(code_expr_buf, sizeof(code_expr_buf), &code_expr_id, c);
}

static void gen_both(char c) {
    gen(c);
    gen_code(c);
}

static void gen_to_str(char *dst, size_t size, size_t *id, const char *s) {
    while (*s) {
        gen_to(dst, size, id, *s++);
    }
}

static void gen_str(const char *s) {
    gen_to_str(buf, sizeof(buf), &buf_id, s);
}

static void gen_code_str(const char *s) {
    gen_to_str(code_expr_buf, sizeof(code_expr_buf), &code_expr_id, s);
}

static void gen_spaces(void) {
    int n = choose(4);   // 0..3 spaces
    for (int i = 0; i < n; i++) {
        gen_both(' ');
    }
}

static void gen_num(void) {
    char tmp[32];
    char code_tmp[64];

    uint32_t val;
    if (choose(2) == 0) {
        val = choose(100);
    } else {
        val = ((uint32_t)rand() << 16) ^ rand();
    }

    /*
     * Add 'u' suffix to force unsigned arithmetic in GCC.
     * Example: 123u, 4000000000u
     */
    snprintf(tmp, sizeof(tmp), "%uu", val);
    snprintf(code_tmp, sizeof(code_tmp), "nemu_expr_id(%uu)", val);
    gen_str(tmp);
    gen_code_str(code_tmp);
}

static void gen_rand_op(void) {
    char ops[] = {'+', '-', '*', '/'};
    gen_spaces();
    gen_both(ops[choose(4)]);
    gen_spaces();
}

static void gen_rand_expr_rec(int depth) {
    /*
     * Limit depth to avoid too long expressions and stack overflow.
     * Also stop earlier sometimes to keep expressions varied.
     */
    if (depth > 6 || (depth > 2 && choose(100) < 30)) {
        gen_spaces();
        gen_num();
        gen_spaces();
        return;
    }

    switch (choose(3)) {
        case 0:
            gen_spaces();
            gen_num();
            gen_spaces();
            break;

        case 1:
            gen_spaces();
            gen_both('(');
            gen_rand_expr_rec(depth + 1);
            gen_both(')');
            gen_spaces();
            break;

        default:
            gen_spaces();
            gen_rand_expr_rec(depth + 1);
            gen_rand_op();
            gen_rand_expr_rec(depth + 1);
            gen_spaces();
            break;
    }
}

static void gen_rand_expr(void) {
    buf_id = 0;
    code_expr_id = 0;
    gen_rand_expr_rec(0);
    buf[buf_id] = '\0';
    code_expr_buf[code_expr_id] = '\0';
}

int main(int argc, char *argv[]) {
    srand(time(0));

    int loop = 1;
    if (argc > 1) {
        sscanf(argv[1], "%d", &loop);
    }

    for (int i = 0; i < loop; i++) {
        gen_rand_expr();

        int n = snprintf(code_buf, sizeof(code_buf), code_format, code_expr_buf);
        if (n < 0 || n >= sizeof(code_buf)) {
            i--;
            continue;
        }

        FILE *fp = fopen("/tmp/.code.c", "w");
        if (fp == NULL) {
            perror("fopen");
            exit(1);
        }

        fputs(code_buf, fp);
        fclose(fp);

        int ret = system("gcc -w -fsanitize=undefined -fno-sanitize-recover=undefined /tmp/.code.c -o /tmp/.expr");
        if (ret != 0) {
            i--;
            continue;
        }

        fp = popen("/tmp/.expr 2>/dev/null", "r");
        if (fp == NULL) {
            perror("popen");
            i--;
            continue;
        }

        unsigned result;
        int scan_ret = fscanf(fp, "%u", &result);
        int status = pclose(fp);

        /*
         * If the expression has division by zero, the generated program
         * may crash and produce no valid output. Skip it and retry.
         */
        if (scan_ret != 1 || status != 0) {
            i--;
            continue;
        }

        printf("%u %s\n", result, buf);
    }

    return 0;
}
