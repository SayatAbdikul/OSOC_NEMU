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
#include <string.h>

/* We use the POSIX regex functions to process regular expressions.
 * Type 'man regex' for more information about POSIX regex functions.
 */
#include <regex.h>

enum {
  TK_NOTYPE = 256, TK_EQ,
  TK_NUM,
  /* TODO: Add more token types */

};

static struct rule {
  const char *regex;
  int token_type;
} rules[] = {

  /* TODO: Add more rules.
   * Pay attention to the precedence level of different rules.
   */
  // Hex number (0x...)
  {"0x[0-9a-f]+", TK_NUM},
  // Decimal number
  {"[0-9]+",       TK_NUM},
  // Operators and parentheses
  {"\\*",          '*'},
  {"/",            '/'},
  {"\\(",          '('},
  {"\\)",          ')'},
  {"-",            '-'},   // subtraction
  {" +", TK_NOTYPE},    // spaces
  {"\\+", '+'},         // plus
  {"==", TK_EQ},        // equal
};

#define NR_REGEX ARRLEN(rules)

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
  char str[32];
} Token;

static Token tokens[32] __attribute__((used)) = {};
static int nr_token __attribute__((used))  = 0;

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
        int substr_len = pmatch.rm_eo;

        Log("match rules[%d] = \"%s\" at position %d with len %d: %.*s",
            i, rules[i].regex, position, substr_len, substr_len, substr_start);

        position += substr_len;

        /* TODO: Now a new token is recognized with rules[i]. Add codes
         * to record the token in the array `tokens'. For certain types
         * of tokens, some extra actions should be performed.
         */

        switch (rules[i].token_type) {
          case TK_NUM:
            tokens[nr_token].type = TK_NUM;
            // Copy the matched substring into tokens[nr_token].str
            strncpy(tokens[nr_token].str, substr_start, substr_len);
            tokens[nr_token].str[substr_len] = '\0';
            nr_token++;
            break;
          case '+': case '-': case '*': case '/': case '(': case ')':
            tokens[nr_token].type = rules[i].token_type;  // e.g., '+'
            nr_token++;
            break;
          case TK_EQ:           // keep for future
            tokens[nr_token].type = TK_EQ;
            nr_token++;
            break;
          case TK_NOTYPE:       // spaces – do nothing
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

  return true;
}

static word_t token2val(int i) {
  char *str = tokens[i].str;
  if (str[0] == '0' && str[1] == 'x') {
    return strtol(str, NULL, 16);
  } else {
    return atoi(str);
  }
}

// Return precedence of operator (higher value = higher precedence)
static int precedence(int op) {
  switch (op) {
    case '*': case '/': return 2;
    case '+': case '-': return 1;
    default: return 0;
  }
}


// Recursive evaluation of tokens from index p to q (inclusive)
static word_t eval(int p, int q) {
    if (p > q) {
        assert(0 && "Empty token range");
    }

    // Single token -> must be a number
    if (p == q && tokens[p].type == TK_NUM) {
        return token2val(p);
    }

    // Remove outer parentheses if they wrap the whole expression
    if (tokens[p].type == '(' && tokens[q].type == ')') {
        // Check that the parentheses are balanced and enclose everything
        int paren = 0, i;
        for (i = p; i <= q; i++) {
            if (tokens[i].type == '(') paren++;
            if (tokens[i].type == ')') paren--;
            if (paren == 0 && i < q) break; // not outermost
        }
        if (i == q) {
            return eval(p + 1, q - 1);
        }
    }

    // Find the main operator (lowest precedence, not inside parentheses)
    int op = -1;
    int min_prec = 100;
    int paren_level = 0;
    for (int i = p; i <= q; i++) {
        if (tokens[i].type == '(') {
            paren_level++;
        } else if (tokens[i].type == ')') {
            paren_level--;
        } else if (paren_level == 0) {
            int prec = precedence(tokens[i].type);
            if (prec > 0) {
                // Choose the rightmost operator among the lowest precedence
                if (prec < min_prec) {
                    min_prec = prec;
                    op = i;
                }
            }
        }
    }

    // If no binary operator found, check for unary minus/plus at the start
    if (op == -1) {
        // The expression could be something like "-5" or "+3"
        if (p <= q && (tokens[p].type == '-' || tokens[p].type == '+')) {
            word_t val = eval(p + 1, q);
            return (tokens[p].type == '-') ? -val : val;
        }
        assert(0 && "No operator found and not a unary expression");
    }

    // Handle unary minus/plus when the operator is at the very beginning of the range
    // This prevents evaluation of an empty left side.
    if (op == p && (tokens[op].type == '-' || tokens[op].type == '+')) {
        word_t val = eval(op + 1, q);
        return (tokens[op].type == '-') ? -val : val;
    }

    // Binary operator
    word_t left_val = eval(p, op - 1);
    word_t right_val = eval(op + 1, q);

    switch (tokens[op].type) {
        case '+': return left_val + right_val;
        case '-': return left_val - right_val;
        case '*': return left_val * right_val;
        case '/': return left_val / right_val;   // integer division truncates toward zero
        default:  assert(0 && "Unknown operator");
    }
}

word_t expr(char *e, bool *success) {
  if (!make_token(e)) {
    *success = false;
    return 0;
  }

  
// Evaluate the token list
  if (nr_token == 0) {
    *success = false;
    return 0;
  }
  
  word_t result = eval(0, nr_token - 1);
  *success = true;
  return result;
}
