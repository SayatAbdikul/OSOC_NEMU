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
static char code_buf[65536 + 128];

static char *code_format =
    "#include <stdio.h>\n"
    "int main() { "
    "  unsigned result = %s; "
    "  printf(\"%%u\", result); "
    "  return 0; "
    "}";

static int buf_id;

static uint32_t choose(uint32_t n) {
    return rand() % n;
}

static void gen(char c) {
    assert(buf_id < sizeof(buf) - 1);
    buf[buf_id++] = c;
}

static void gen_str(const char *s) {
    while (*s) {
        gen(*s++);
    }
}

static void gen_spaces(void) {
    int n = choose(4);   // 0..3 spaces
    for (int i = 0; i < n; i++) {
        gen(' ');
    }
}

static void gen_num(void) {
    char tmp[32];

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
    gen_str(tmp);
}

static void gen_rand_op(void) {
    char ops[] = {'+', '-', '*', '/'};
    gen_spaces();
    gen(ops[choose(4)]);
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
            gen('(');
            gen_rand_expr_rec(depth + 1);
            gen(')');
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
    gen_rand_expr_rec(0);
    buf[buf_id] = '\0';
}

int main(int argc, char *argv[]) {
    srand(time(0));

    int loop = 1;
    if (argc > 1) {
        sscanf(argv[1], "%d", &loop);
    }

    for (int i = 0; i < loop; i++) {
        gen_rand_expr();

        int n = snprintf(code_buf, sizeof(code_buf), code_format, buf);
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

        int ret = system("gcc -w /tmp/.code.c -o /tmp/.expr");
        if (ret != 0) {
            i--;
            continue;
        }

        fp = popen("/tmp/.expr", "r");
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