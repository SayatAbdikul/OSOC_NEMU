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
#include <memory/vaddr.h>
#include <string.h>

/* We use the POSIX regex functions to process regular expressions.
 * Type 'man regex' for more information about POSIX regex functions.
 */
#include <regex.h>

enum {
  TK_NOTYPE = 256,
  TK_EQ,
  TK_NUM,
  TK_REG,
  TK_NEG,
  TK_POS,
  TK_DEREF,
};

static struct rule {
  const char *regex;
  int token_type;
} rules[] = {
  {" +",                 TK_NOTYPE},
  {"==",                 TK_EQ},
  {"0[xX][0-9a-fA-F]+[uU]?", TK_NUM},
  {"[0-9]+[uU]?",            TK_NUM},
  {"\\$[a-zA-Z0-9]+",    TK_REG},
  {"\\+",                '+'},
  {"-",                  '-'},
  {"\\*",                '*'},
  {"/",                  '/'},
  {"\\(",                '('},
  {"\\)",                ')'},
};

#define NR_REGEX ARRLEN(rules)
#define MAX_TOKENS 128
#define MAX_TOKEN_LEN 64

static regex_t re[NR_REGEX] = {};

/* Rules are used for many times.
 * Therefore we compile them only once before any usage.
 */
void init_regex() {
  int i;
  char error_msg[128];
  int ret;

  for (i = 0; i < NR_REGEX; i ++) {
    ret = regcomp(&re[i], rules[i].regex, REG_EXTENDED);
    if (ret != 0) {
      regerror(ret, &re[i], error_msg, 128);
      panic("regex compilation failed: %s\n%s", error_msg, rules[i].regex);
    }
  }
}

typedef struct token {
  int type;
  char str[MAX_TOKEN_LEN];
} Token;

static Token tokens[MAX_TOKENS] __attribute__((used)) = {};
static int nr_token __attribute__((used))  = 0;

static bool add_token(int type, const char *str, int len) {
  if (nr_token >= MAX_TOKENS) {
    printf("too many tokens\n");
    return false;
  }

  tokens[nr_token].type = type;
  tokens[nr_token].str[0] = '\0';

  if (str != NULL) {
    if (len >= MAX_TOKEN_LEN) {
      printf("token is too long: %.*s\n", len, str);
      return false;
    }
    memcpy(tokens[nr_token].str, str, len);
    tokens[nr_token].str[len] = '\0';
  }

  nr_token++;
  return true;
}

static bool is_operand(int type) {
  return type == TK_NUM || type == TK_REG || type == ')';
}

static void mark_unary_ops() {
  for (int i = 0; i < nr_token; i++) {
    bool unary = (i == 0 || !is_operand(tokens[i - 1].type));
    if (!unary) {
      continue;
    }

    if (tokens[i].type == '-') {
      tokens[i].type = TK_NEG;
    } else if (tokens[i].type == '+') {
      tokens[i].type = TK_POS;
    } else if (tokens[i].type == '*') {
      tokens[i].type = TK_DEREF;
    }
  }
}

static bool make_token(char *e) {
  int position = 0;
  int i;
  regmatch_t pmatch;

  nr_token = 0;

  while (e[position] != '\0') {
    /* Try all rules one by one. */
    for (i = 0; i < NR_REGEX; i ++) {
      if (regexec(&re[i], e + position, 1, &pmatch, 0) == 0 && pmatch.rm_so == 0) {
        char *substr_start = e + position;
        int substr_len = (int)pmatch.rm_eo;

        position += substr_len;

        switch (rules[i].token_type) {
          case TK_NOTYPE:
            break;
          case TK_NUM:
          case TK_REG:
            if (!add_token(rules[i].token_type, substr_start, substr_len)) {
              return false;
            }
            break;
          case '+': case '-': case '*': case '/': case '(': case ')':
          case TK_EQ:
            if (!add_token(rules[i].token_type, NULL, 0)) {
              return false;
            }
            break;
          default: TODO();
        }

        break;
      }
    }

    if (i == NR_REGEX) {
      printf("no match at position %d\n%s\n%*.s^\n", position, e, position, "");
      return false;
    }
  }

  mark_unary_ops();
  return true;
}

static bool token2val(int i, word_t *val) {
  char *str = tokens[i].str;
  char *end = NULL;
  unsigned long long result;

  if (str[0] == '0' && (str[1] == 'x' || str[1] == 'X')) {
    result = strtoull(str, &end, 16);
  } else {
    result = strtoull(str, &end, 10);
  }

  if (*end == 'u' || *end == 'U') {
    end++;
  }

  if (end == str || *end != '\0') {
    return false;
  }

  *val = (word_t)result;
  return true;
}

static bool reg2val(int i, word_t *val) {
  bool success = false;
  *val = isa_reg_str2val(tokens[i].str, &success);
  if (!success && strcmp(tokens[i].str, "/bin/zsh") == 0) {
    *val = 0;
    return true;
  }
  return success;
}

static int precedence(int op) {
  switch (op) {
    case TK_EQ: return 1;
    case '+': case '-': return 2;
    case '*': case '/': return 3;
    default: return 0;
  }
}

static bool check_parentheses(int p, int q) {
  if (tokens[p].type != '(' || tokens[q].type != ')') {
    return false;
  }

  int level = 0;
  for (int i = p; i <= q; i++) {
    if (tokens[i].type == '(') {
      level++;
    } else if (tokens[i].type == ')') {
      level--;
      if (level < 0) {
        return false;
      }
    }

    if (level == 0 && i < q) {
      return false;
    }
  }

  return level == 0;
}

static bool is_binary_op(int type) {
  return type == TK_EQ || type == '+' || type == '-' || type == '*' || type == '/';
}

static int find_main_op(int p, int q, bool *success) {
  int op = -1;
  int min_prec = 100;
  int level = 0;

  for (int i = p; i <= q; i++) {
    int type = tokens[i].type;

    if (type == '(') {
      level++;
      continue;
    }

    if (type == ')') {
      level--;
      if (level < 0) {
        *success = false;
        return -1;
      }
      continue;
    }

    if (level == 0 && is_binary_op(type)) {
      int prec = precedence(type);
      if (prec <= min_prec) {
        min_prec = prec;
        op = i;
      }
    }
  }

  if (level != 0) {
    *success = false;
    return -1;
  }

  return op;
}

// Recursive evaluation of tokens from index p to q (inclusive)
static word_t eval(int p, int q, bool *success) {
  if (p > q) {
    *success = false;
    return 0;
  }

  while (p < q && check_parentheses(p, q)) {
    p++;
    q--;
  }

  if (p == q) {
    word_t val = 0;

    if (tokens[p].type == TK_NUM) {
      *success = token2val(p, &val);
      return *success ? val : 0;
    }
    if (tokens[p].type == TK_REG) {
      *success = reg2val(p, &val);
      return *success ? val : 0;
    }

    *success = false;
    return 0;
  }

  int op = find_main_op(p, q, success);
  if (!*success) {
    return 0;
  }

  if (op == -1) {
    word_t val;

    switch (tokens[p].type) {
      case TK_NEG:
        val = eval(p + 1, q, success);
        return *success ? -val : 0;
      case TK_POS:
        return eval(p + 1, q, success);
      case TK_DEREF:
        val = eval(p + 1, q, success);
        return *success ? vaddr_read(val, sizeof(word_t)) : 0;
      default:
        *success = false;
        return 0;
    }
  }

  if (op == p || op == q) {
    *success = false;
    return 0;
  }

  word_t left_val = eval(p, op - 1, success);
  if (!*success) {
    return 0;
  }

  word_t right_val = eval(op + 1, q, success);
  if (!*success) {
    return 0;
  }

  switch (tokens[op].type) {
    case TK_EQ: return left_val == right_val;
    case '+': return left_val + right_val;
    case '-': return left_val - right_val;
    case '*': return left_val * right_val;
    case '/':
      if (right_val == 0) {
        *success = false;
        return 0;
      }
      return left_val / right_val;
    default:
      *success = false;
      return 0;
  }
}

word_t expr(char *e, bool *success) {
  if (success == NULL) {
    return 0;
  }
  *success = true;

  if (e == NULL) {
    *success = false;
    return 0;
  }

  if (!make_token(e)) {
    *success = false;
    return 0;
  }

  if (nr_token == 0) {
    *success = false;
    return 0;
  }

  return eval(0, nr_token - 1, success);
}
