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

#include <isa.h>
#include <cpu/cpu.h>
#include <readline/readline.h>
#include <readline/history.h>
#include "sdb.h"
#include <utils.h>
#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <stdlib.h>
static int is_batch_mode = false;
static const char *expr_test_file = NULL;

void init_regex();
void init_wp_pool();
void isa_reg_display(void);
word_t vaddr_read(vaddr_t addr, int len);
word_t expr(char *e, bool *success);
/* We use the `readline' library to provide more flexibility to read from stdin. */
static char* rl_gets() {
  static char *line_read = NULL;

  if (line_read) {
    free(line_read);
    line_read = NULL;
  }

  line_read = readline("(nemu) ");

  if (line_read && *line_read) {
    add_history(line_read);
  }

  return line_read;
}

static int cmd_c(char *args) {
  cpu_exec(-1);
  return 0;
}

static int cmd_si(char *args) {
    int inst_n = 1;
    if (args[0] != '\0') {
        inst_n = atoi(args);
        if (inst_n == 0) inst_n = 1;   // atoi returns 0 if it's invalid/empty
    }
    cpu_exec(inst_n);
    return 0;
}
static int cmd_info(char *args) {
  if(args[0] == 'r'){
      isa_reg_display();
    } else
      return 1;
  return 0;
  }

static int cmd_q(char *args) {
  nemu_state.state = NEMU_QUIT;
  return -1;
}

static int cmd_scan(char *args) {
  int n;
  word_t addr;
  if (sscanf(args, "%d %x", &n, &addr) != 2) {
      printf("usage: x N EXPR\n");
      return 0;
  }
  for (int i = 0; i < n; i++) {
      word_t val = vaddr_read(addr + i * sizeof(word_t), sizeof(word_t));
      printf("0x%08x\n", val);
  }
  return 0;
}

static int cmd_expr(char *args){
  bool success = false;
    word_t result = expr(args, &success);
    
    if (success) {
        // Print as hexadecimal (common in debuggers) and optionally decimal
        printf("0x%x\n", result);
        // printf("%u\n", result);  // decimal version if preferred
    } else {
        printf("Invalid expression\n");
    }
    return 0; 
}

static int cmd_help(char *args);

static struct {
  const char *name;
  const char *description;
  int (*handler) (char *);
} cmd_table [] = {
  { "help", "Display information about all supported commands", cmd_help },
  { "c", "Continue the execution of the program", cmd_c },
  { "q", "Exit NEMU", cmd_q },
  { "si", "Single-step execution", cmd_si},
  { "info", "Print program status", cmd_info},
  { "x", "Scan memory", cmd_scan},
  { "p", "Expression evaluation", cmd_expr}
  /* TODO: Add more commands */

};

#define NR_CMD ARRLEN(cmd_table)

static int cmd_help(char *args) {
  /* extract the first argument */
  char *arg = strtok(NULL, " ");
  int i;

  if (arg == NULL) {
    /* no argument given */
    for (i = 0; i < NR_CMD; i ++) {
      printf("%s - %s\n", cmd_table[i].name, cmd_table[i].description);
    }
  }
  else {
    for (i = 0; i < NR_CMD; i ++) {
      if (strcmp(arg, cmd_table[i].name) == 0) {
        printf("%s - %s\n", cmd_table[i].name, cmd_table[i].description);
        return 0;
      }
    }
    printf("Unknown command '%s'\n", arg);
  }
  return 0;
}

void sdb_set_batch_mode() {
  is_batch_mode = true;
}

void sdb_set_expr_test_file(const char *file) {
  expr_test_file = file;
}

static char *trim_left(char *s) {
  while (isspace((unsigned char)*s)) {
    s++;
  }
  return s;
}

static void trim_right(char *s) {
  size_t len = strlen(s);
  while (len > 0 && isspace((unsigned char)s[len - 1])) {
    s[--len] = '\0';
  }
}

static bool run_expr_tests(const char *file) {
  FILE *fp = fopen(file, "r");
  if (fp == NULL) {
    perror(file);
    return false;
  }

  static char line[131072];
  int line_no = 0;
  int total = 0;
  int passed = 0;
  int failed = 0;

  while (fgets(line, sizeof(line), fp) != NULL) {
    line_no++;

    char *p = trim_left(line);
    if (*p == '\0' || *p == '#') {
      continue;
    }

    total++;

    errno = 0;
    char *end = NULL;
    uint64_t expected_raw = strtoull(p, &end, 10);
    if (p == end || errno != 0) {
      printf("expr-test:%d: malformed expected value\n", line_no);
      failed++;
      continue;
    }

    char *expr_text = trim_left(end);
    trim_right(expr_text);
    if (*expr_text == '\0') {
      printf("expr-test:%d: missing expression\n", line_no);
      failed++;
      continue;
    }

    bool success = false;
    word_t actual = expr(expr_text, &success);
    word_t expected = (word_t)expected_raw;

    if (!success || actual != expected) {
      printf("expr-test:%d: %s\n", line_no, expr_text);
      if (!success) {
        printf("  evaluator reported an invalid expression\n");
      } else {
        printf("  expected " FMT_WORD ", got " FMT_WORD "\n", expected, actual);
      }
      failed++;
      continue;
    }

    passed++;
  }

  fclose(fp);

  printf("expr-test: %d/%d passed", passed, total);
  if (failed > 0) {
    printf(", %d failed", failed);
  }
  printf("\n");

  return total > 0 && failed == 0;
}

void sdb_mainloop() {
  if (expr_test_file != NULL) {
    bool ok = run_expr_tests(expr_test_file);
    nemu_state.state = ok ? NEMU_QUIT : NEMU_ABORT;
    return;
  }

  if (is_batch_mode) {
    cmd_c(NULL);
    return;
  }

  for (char *str; (str = rl_gets()) != NULL; ) {
    char *str_end = str + strlen(str);

    /* extract the first token as the command */
    char *cmd = strtok(str, " ");
    if (cmd == NULL) { continue; }

    /* treat the remaining string as the arguments,
     * which may need further parsing
     */
    char *args = cmd + strlen(cmd) + 1;
    if (args >= str_end) {
      args = NULL;
    }

#ifdef CONFIG_DEVICE
    extern void sdl_clear_event_queue();
    sdl_clear_event_queue();
#endif

    int i;
    for (i = 0; i < NR_CMD; i ++) {
      if (strcmp(cmd, cmd_table[i].name) == 0) {
        if (cmd_table[i].handler(args) < 0) { return; }
        break;
      }
    }

    if (i == NR_CMD) { printf("Unknown command '%s'\n", cmd); }
  }
}

void init_sdb() {
  /* Compile the regular expressions. */
  init_regex();

  /* Initialize the watchpoint pool. */
  init_wp_pool();
}
