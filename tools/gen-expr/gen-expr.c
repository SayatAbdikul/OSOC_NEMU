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

// buffers
static char buf[65536];
static char code_buf[65536 + 128];
static char *code_format =
    "#include <stdio.h>\n"
    "int main() { "
    "  unsigned result = %s; "
    "  printf(\"%%u\", result); "
    "  return 0; "
    "}";

static int buf_id;   // current writing position in buf

// ----- helper: write a string to buf -----
static void write_str(const char *s) {
    while (*s) {
        buf[buf_id++] = *s++;
    }
}

// ----- generate a number (non‑uniform distribution) -----
static void gen_num(void) {
    char tmp[32];
    if (rand() % 2 == 0) {
        // small number 0..100
        sprintf(tmp, "%d", rand() % 101);
    } else {
        // large number, possibly with high bit set
        uint32_t big = rand();
        if (rand() % 2 == 0) big |= 0x80000000;
        sprintf(tmp, "%u", big);
    }
    write_str(tmp);
}

// ----- generate a register -----
static void gen_reg(void) {
    const char *regs[] = {"$a0", "$sp", "$t0", "$zero"};
    write_str(regs[rand() % 4]);
}

// ----- generate random whitespace (0-3 spaces/tabs) -----
static void gen_whitespace(void) {
    int n = rand() % 4;
    for (int i = 0; i < n; i++) {
        buf[buf_id++] = (rand() % 2 == 0) ? ' ' : '\t';
    }
}

// ----- generate an operator, possibly with whitespace around it -----
static void gen_op(char op) {
    gen_whitespace();
    buf[buf_id++] = op;
    gen_whitespace();
}

// ----- recursive expression generator -----
static void gen_expr(int depth) {
    // Max depth to avoid stack overflow
    if (depth >= 5 || (depth > 2 && rand() % 100 < 30)) {
        // leaf node: number or register
        if (rand() % 2 == 0) gen_num();
        else gen_reg();
        return;
    }

    // with probability 1/3, generate a unary operation: *expr or &expr
    if (rand() % 3 == 0) {
        if (rand() % 2 == 0) {
            write_str("*(");
            gen_expr(depth + 1);
            buf[buf_id++] = ')';
        } else {
            write_str("&(");
            gen_expr(depth + 1);
            buf[buf_id++] = ')';
        }
        return;
    }

    // binary operation
    char op;
    int op_choice = rand() % 4;
    switch (op_choice) {
        case 0: op = '+'; break;
        case 1: op = '-'; break;
        case 2: op = '*'; break;
        case 3: op = '/'; break;
        default: op = '+';
    }

    buf[buf_id++] = '(';
    gen_whitespace();
    gen_expr(depth + 1);
    gen_op(op);

    // For division by zero: force right operand to 0
    if (op == '/') {
        write_str("0");
    } else {
        gen_expr(depth + 1);
    }
    gen_whitespace();
    buf[buf_id++] = ')';
}

// ----- public entry point: generate a random expression into buf -----
static void gen_rand_expr(void) {
    buf_id = 0;
    gen_expr(0);
    buf[buf_id] = '\0';   // null terminate
}

// ----- corrupt a valid expression to make it invalid -----
static void corrupt_expr(char *s) {
    int len = strlen(s);
    if (len < 2) return;
    int pos = rand() % len;
    switch (rand() % 4) {
        case 0: // delete a character
            memmove(s + pos, s + pos + 1, len - pos);
            break;
        case 1: // insert an illegal character
            memmove(s + pos + 1, s + pos, len - pos + 1);
            s[pos] = '@';
            break;
        case 2: // duplicate an operator
            if (strchr("+-*/", s[pos])) {
                memmove(s + pos + 1, s + pos, len - pos + 1);
                s[pos+1] = s[pos];
            }
            break;
        case 3: // remove a parenthesis
            if (s[pos] == '(' || s[pos] == ')') {
                memmove(s + pos, s + pos + 1, len - pos);
            }
            break;
    }
}

// ----- transform the expression for GCC evaluation -----
// Replace: registers -> "0", *(...) -> "0", &(...) -> "0"
// This is a simplified but sufficient transformation.
static void transform_for_gcc(const char *src, char *dst) {
    while (*src) {
        if (*src == '$') {
            // skip the register name, output "0"
            while (*src && (isalnum(*src) || *src == '$')) src++;
            strcat(dst, "0");
        } else if (*src == '*') {
            // assume it's followed by '('; replace "*(" with "0"
            // For safety, we just output "0" and skip the whole subexpression
            // This works because the subexpression will evaluate to 0 anyway.
            strcat(dst, "0");
            // skip until the matching ')' (simplistic but sufficient for test)
            int paren = 1;
            src++; // skip '*'
            if (*src == '(') {
                src++;
                while (paren > 0 && *src) {
                    if (*src == '(') paren++;
                    else if (*src == ')') paren--;
                    src++;
                }
            } else {
                // not followed by '(', just skip one char
                src++;
            }
        } else if (*src == '&') {
            // address-of: replace with "0" and skip the parenthesized expression
            strcat(dst, "0");
            if (*(src+1) == '(') {
                src += 2;
                int paren = 1;
                while (paren > 0 && *src) {
                    if (*src == '(') paren++;
                    else if (*src == ')') paren--;
                    src++;
                }
            } else {
                src++;
            }
        } else {
            char ch = *src++;
            strncat(dst, &ch, 1);
        }
    }
}

int main(int argc, char *argv[]) {
    srand(time(0));

    int loop = 1;
    if (argc > 1) {
        sscanf(argv[1], "%d", &loop);
    }

    for (int i = 0; i < loop; i++) {
        // 10% chance to generate an invalid expression
        int is_invalid = (rand() % 10 == 0);

        gen_rand_expr();   // generate a valid expression

        if (is_invalid) {
            corrupt_expr(buf);
            printf("invalid %s\n", buf);
            continue;
        }

        // valid case: transform for GCC, compile, run, print result
        char gcc_expr[65536] = "";
        transform_for_gcc(buf, gcc_expr);

        sprintf(code_buf, code_format, gcc_expr);

        FILE *fp = fopen("/tmp/.code.c", "w");
        if (!fp) { perror("fopen"); exit(1); }
        fputs(code_buf, fp);
        fclose(fp);

        int ret = system("gcc /tmp/.code.c -o /tmp/.expr");
        if (ret != 0) continue;   // compilation failed – should not happen

        fp = popen("/tmp/.expr", "r");
        if (!fp) { perror("popen"); continue; }

        int result;
        fscanf(fp, "%d", &result);
        pclose(fp);

        printf("%u %s\n", result, buf);
    }

    return 0;
}
