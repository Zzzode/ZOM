/*
 * ZomLexer.g4 — ZOM 编程语言词法规范（可执行的 EBNF）
 *
 * ⚠️ 真理来源（严格对齐，不要臆测）：
 *   1. docs/design/syntax-ebnf.md   (最高优先级，§3 Lexical Conventions)
 *   2. docs/spec/chapters/17-grammar-reference.md
 *   3. docs/spec/chapters/02-lexical-structure.md
 *
 * 🔗 与本项目实现的关系：
 *   本文件是"官方的、可运行的语法参考表达"；products/zomlang/compiler/lexer/
 *   下的手写 C++ lexer 需要与其对齐。出现冲突以本文件 + 上述 spec 为准。
 *
 * 🚀 使用：
 *   antlr4 ZomLexer.g4 ZomParser.g4 -visitor
 *   javac -cp $(antlr4 -cp) Zom*.java
 *   echo 'fun id() -> i32 { return 42; }' | grun Zom tokens
 *
 * ANTLR 版本要求：4.13+（支持 \p{ID_Start} / \p{ID_Continue} Unicode 属性转义）
 */
lexer grammar ZomLexer;

// ANTLR 建议：lexer 规则按最长匹配优先，关键字必须在 Identifier 之前声明
channels {
    WHITESPACE,
    COMMENTS
}

// CHAR_LITERAL 是虚拟 token。真正的词法匹配在下面的 SINGLE_STRING_LITERAL
// 规则 action 中通过 setType(CHAR_LITERAL) 动态分派（内容恰好 1 个 Unicode
// scalar 时）。通过 tokens {} 块向 ANTLR 注册该 token 名，避免写占位 lexer
// 规则产生 token 重叠 warning。
tokens {
    CHAR_LITERAL
}

options {
    // 保持默认 SLL(*)，不主动降 ALL，除非产生 SLL 决策冲突
}

// Java helper for CHAR_LITERAL semantic predicate.
// （ANTLR4 的 lexer action 可以访问 token 文本；我们内联一个计数函数）
@lexer::members {
    /**
     * Count Unicode scalar values in a string. Escapes like '\n' count as 1;
     * unescaped multi-byte UTF-16 surrogate pairs count as 1.
     * For CHAR_LITERAL check we only care about the logical count:
     *   - each backslash-X escape counts as 1
     *   - backslash-x HH counts as 1
     *   - backslash-u HHHH / backslash-u{H+} counts as 1
     *   - a raw (not-escaped) pair of surrogate chars counts as 1 (or 2 if unpaired)
     * Fast path: iterate char-by-char, group escapes into single scalars.
     */
    static int countUnicodeScalars(String s) {
        int n = 0;
        for (int i = 0; i < s.length();) {
            char c = s.charAt(i);
            if (c == '\\') {
                // Any escape: backslash-x HH, backslash-u HHHH, backslash-u{H+}, newline, tab, etc.
                n++;
                i++;
                if (i >= s.length()) break;
                char e = s.charAt(i++);
                if (e == 'x') { i += 2; }
                else if (e == 'u') {
                    if (i < s.length() && s.charAt(i) == '{') {
                        while (i < s.length() && s.charAt(i) != '}') i++;
                        i++; // skip '}'
                    } else {
                        i += 4;
                    }
                } // else single-char escape
            } else if (Character.isHighSurrogate(c) && i + 1 < s.length()
                       && Character.isLowSurrogate(s.charAt(i+1))) {
                n++;
                i += 2;
            } else {
                n++;
                i++;
            }
        }
        return n;
    }

    /** Validate all backslash-u{...} escape sub-sequences in the last-matched token text. */
    static void validateUnicodeEscapes(String text) {
        int i = 0;
        while ((i = text.indexOf("\\u{", i)) != -1) {
            int end = text.indexOf('}', i);
            if (end == -1) return;
            String hex = text.substring(i + 3, end);
            try {
                int cp = Integer.parseInt(hex, 16);
                if (cp > 0x10FFFF) {
                    throw new org.antlr.v4.runtime.misc.ParseCancellationException(
                        "unicode escape exceeds U+10FFFF: \\" + text.substring(i, end + 1));
                }
            } catch (NumberFormatException e) {
                throw new org.antlr.v4.runtime.misc.ParseCancellationException(
                    "invalid unicode escape: \\" + text.substring(i, end + 1));
            }
            i = end + 1;
        }
    }
}


// ============================================================================
// §3.2  Format-control Characters（格式控制字符）
//      仅 ZWNJ/ZWJ 允许出现在标识符内部；ZWNBSP 在除文件开头外被视为空白
//      （见 §3.3 Whitespace）
// ============================================================================
// 作为独立 token 产生，以便在 IdentifierPart 分支中使用；非标识符位置出现
// 会被 parser 拒绝——不是词法错误而是语法错误。
ZWNJ   : '\u200C';
ZWJ    : '\u200D';

// ============================================================================
// §3.3  Whitespace + Line Terminators
//
//   Whitespace: U+0009 | U+000B | U+000C | U+0020 | U+00A0 | U+1680
//             | U+2000..U+200A | U+202F | U+205F | U+3000 | ZWNBSP
//   LineTerminator: LF | CR | LS | PS
//   LineTerminatorSeq = LF | CRLF | CR | LS | PS
//
//   统一合并进 WS 通道（独立 WHITESPACE channel，不进默认 token stream）
// ============================================================================
WS
    : (
          [\u0009\u000B\u000C\u0020\u00A0\u1680\u202F\u205F\u3000\uFEFF]
        // Unicode general category Zs 中 2000..200A
        | ' '..' '
        // LineTerminator
        | [\r\n\u2028\u2029]
      )+
      -> channel(WHITESPACE)
    ;

// ============================================================================
// §3.4  Comments
//
//   SingleLineComment  ::= '//' (~ LineTerminator)*
//   MultiLineComment   ::= '/*' (MultiLineCommentChar | MultiLineComment)* '*/'
//
//   MultiLineCommentChar = ~( '*' | '/' ) | '*' ~'/' | '/' ~'*'
//
//   注：MultiLineComment 不嵌套（原文长注释明确标注 NOT nestable）。
//   非递归实现：使用非贪婪 '/*' .*? '*/'（在非嵌套情形下等价）。
// ============================================================================
SINGLE_LINE_COMMENT
    : '//' ~['\r\n\u2028\u2029]*
      -> channel(COMMENTS)
    ;

MULTI_LINE_COMMENT
    : '/*' .*? '*/'
      -> channel(COMMENTS)
    ;

// ============================================================================
// §3.6.2  Internal fragments（数字 / 十六进制 / 转义的内部组成）
//        全部加 fragment 前缀，避免 leak 到默认 token stream。
// ============================================================================
fragment NUM_SEP            : '_';
fragment DECIMAL_DIGIT      : [0-9];
fragment NON_ZERO_DIGIT     : [1-9];
fragment BINARY_DIGIT       : [01];
fragment OCTAL_DIGIT        : [0-7];
fragment HEX_DIGIT          : [0-9a-fA-F];
fragment EXPONENT_INDICATOR : [eE];
fragment SIGN               : '+' | '-';

fragment DECIMAL_DIGITS         : DECIMAL_DIGIT (NUM_SEP* DECIMAL_DIGIT)* NUM_SEP*;
fragment NON_ZERO_DECIMAL_DIGITS: NON_ZERO_DIGIT (NUM_SEP* DECIMAL_DIGIT)* NUM_SEP*;
fragment BINARY_DIGITS          : BINARY_DIGIT (NUM_SEP* BINARY_DIGIT)* NUM_SEP*;
fragment OCTAL_DIGITS           : OCTAL_DIGIT  (NUM_SEP* OCTAL_DIGIT)* NUM_SEP*;
fragment HEX_DIGITS             : HEX_DIGIT    (NUM_SEP* HEX_DIGIT)* NUM_SEP*;

fragment HEX_4_DIGITS       : HEX_DIGIT HEX_DIGIT HEX_DIGIT HEX_DIGIT;
fragment HEX_ESCAPE         : 'x' HEX_DIGIT HEX_DIGIT;
fragment UNICODE_ESCAPE
    : 'u' HEX_4_DIGITS
    | 'u{' HEX_DIGITS '}'
    ;

// ----------------------------------------------------------------------------
// §3.6.2  Numeric Literals（lexer 层完整产出一个 token，parser 不再拼装）
// ----------------------------------------------------------------------------
BIGINT_LITERAL
    : // §3.6.2 BigIntLiteral  ::= DecimalDigits 'n'
      // 数字分隔符不能出现在最前或最后（lexer 由 DecimalDigits 保证末尾是数字，
      // 再加上最后的 'n'——分隔符永远不会在末尾）。
      ( '0' | NON_ZERO_DECIMAL_DIGITS ) 'n'
    ;

DECIMAL_LITERAL
    : // §3.6.2 DecimalLiteral（3 种互斥形式）
      // 形式 A:  . DecimalDigits ExponentPart?
      // 形式 B:  DecimalIntegerLiteral '.' DecimalDigits? ExponentPart?
      // 形式 C:  DecimalIntegerLiteral ExponentPart?
      //
      // DecimalIntegerLiteral = '0' | NON_ZERO_DECIMAL_DIGITS
      // （注意 0 是独立情形，不能出现"前导非 0 数字之后紧跟 0 开头的无分隔分支"——
      //  ANTLR 会按最长匹配处理，本规则的顺序表达了 C/B/A 的尝试顺序）
      (   '0'
            ( '.' DECIMAL_DIGITS? (EXPONENT_INDICATOR SIGN? DECIMAL_DIGITS)? )?
            ( EXPONENT_INDICATOR SIGN? DECIMAL_DIGITS )?
        | NON_ZERO_DECIMAL_DIGITS
            ( '.' DECIMAL_DIGITS? (EXPONENT_INDICATOR SIGN? DECIMAL_DIGITS)? )?
            ( EXPONENT_INDICATOR SIGN? DECIMAL_DIGITS )?
        | '.' DECIMAL_DIGITS (EXPONENT_INDICATOR SIGN? DECIMAL_DIGITS)?
      )
      {
        // §3.6.2 NUM_SEP MUST NOT appear first or last
        // （本规则在结构上已避免 _ 在前，但显式防御非法输入）
        String t = getText();
        if (t.startsWith("_")) {
            throw new org.antlr.v4.runtime.misc.ParseCancellationException(
                "numeric literal must not start with separator: " + t);
        }
      }
    ;

BINARY_LITERAL
    : '0' [bB] BINARY_DIGITS
    ;

OCTAL_LITERAL
    : '0' [oO] OCTAL_DIGITS
    ;

HEX_LITERAL
    : '0' [xX] HEX_DIGITS
    ;

// ----------------------------------------------------------------------------
// §3.6.3 / §3.6.4  String and Character Literals（lexer 层完整 token 化）
//
//   §3.6.3  StringLiteral  双引号 / 单引号 — 内容允许 LineTerminator（多行字符串）
//   §3.6.4  CharacterLiteral   MUST contain exactly one Unicode scalar value
// ----------------------------------------------------------------------------
fragment CHARACTER_ESCAPE
    : '\\' (["\\bfnrtv0] | '\'')
    ;

fragment STRING_ESCAPE_SEQ
    : CHARACTER_ESCAPE
    | '\\' HEX_ESCAPE
    | '\\' UNICODE_ESCAPE
    | '\\' '\n'  | '\\' '\r'     // LineContinuation
    | '\\' '\u2028'  | '\\' '\u2029'
    ;

DOUBLE_STRING_LITERAL
    : '"'
      (
          ~["\\\r\n\u2028\u2029]
        | STRING_ESCAPE_SEQ
      )*
      '"'
      {
          validateUnicodeEscapes(getText());
          // A3-REJECT: 字符串字面量禁止未转义行终止符（裸 \n \r LS PS）
          String t = getText();
          for (int i = 0; i < t.length(); i++) {
              char c = t.charAt(i);
              if (c == '\r' || c == '\n' || c == '\u2028' || c == '\u2029') {
                  throw new org.antlr.v4.runtime.misc.ParseCancellationException(
                      "string literal must not contain unescaped line terminator");
              }
          }
      }
    ;

// §3.6.3 / §3.6.4  Single-quoted literals (char or short string)
//
//   合并为一条 lexer 规则，用 action 按 Unicode scalar 数动态分派 token 类型：
//     scalars == 0 → SINGLE_STRING_LITERAL（空串 ''）
//     scalars == 1 → CHAR_LITERAL
//     scalars >= 2 → SINGLE_STRING_LITERAL
//
//   这样避免了原来 CHAR_LITERAL 在 SINGLE_STRING_LITERAL 之后声明，
//   导致 'ab' 之类多字符文本被误匹配为合法字符串、永远到不了 CHAR 的
//   "exactly 1 scalar" 谓词的歧义（ANTLR 选第一条能匹配的规则）。
SINGLE_STRING_LITERAL
    : '\''
      (
          ~['\\\r\n  ]
        | STRING_ESCAPE_SEQ
      )*
      '\''
      {
          validateUnicodeEscapes(getText());
          String raw = getText();
          // A3-REJECT: 禁止未转义行终止（对称双引号）
          for (int i = 0; i < raw.length(); i++) {
              char c = raw.charAt(i);
              if (c == '\r' || c == '\n' || c == '\u2028' || c == '\u2029') {
                  throw new org.antlr.v4.runtime.misc.ParseCancellationException(
                      "single-quoted literal must not contain unescaped line terminator");
              }
          }
          String content = raw.substring(1, raw.length() - 1);
          int scalars = countUnicodeScalars(content);
          if (scalars == 1) {
              setType(CHAR_LITERAL);
          } else if (scalars == 0) {
              setType(SINGLE_STRING_LITERAL);
          } else {
              // A1-REJECT: 单引号字面量必须恰好 1 个 Unicode scalar
              throw new org.antlr.v4.runtime.misc.ParseCancellationException(
                  "single-quoted literal must contain exactly one Unicode scalar value, got "
                  + scalars + ": " + raw);
          }
      }
    ;

// 占位 token 类型声明（见顶部 tokens { CHAR_LITERAL } 块）:
// CHAR_LITERAL 永远不会被 lexer 直接匹配。上面的 SINGLE_STRING_LITERAL 规则
// 在检测到内容正好 1 个 Unicode scalar 时会 setType(CHAR_LITERAL)，
// 从而产出此 token。parser 层的 literal 规则可以直接引用 CHAR_LITERAL。


// ============================================================================
// §3.6.5  Template Literals
//
//   NoSubTemplate  =  ` (TemplateChar | TemplateEscape | '$' ~'{')* `
//   TemplateHead    =  ` ...  ${
//   TemplateMiddle  =  } ...  ${
//   TemplateTail    =  } ...  `
//
//   TemplateChar    = ~ [`\$]
//   TemplateEscape  =  \ SourceCharacter
// ============================================================================
TEMPLATE_ESCAPE : '\\' . ;  // 辅助 fragment

NO_SUBSTITUTION_TEMPLATE_LITERAL
    : '`' ( ~[`\\$] | '\\' . | '$' ~[{] )* '`'
    ;

TEMPLATE_HEAD
    : '`' ( ~[`\\$] | '\\' . | '$' ~[{] )* '${'
    ;

TEMPLATE_MIDDLE
    : '}' ( ~[`\\$] | '\\' . | '$' ~[{] )* '${'
    ;

TEMPLATE_TAIL
    : '}' ( ~[`\\] | '\\' . )* '`'
    ;

// ============================================================================
// §3.7  Operators / Punctuators（严格按 Punctuator EBNF 顺序——最长匹配优先）
// ============================================================================
// -- 复合 attribute 操作符 ---------------------------------------------------
//    §3.7 长注释： #[ 是 compound token with no intervening whitespace
HASH_LBRACK : '#[';

// -- 多字符运算符（按长度从长到短，保证最长匹配） ----------------------------
ELLIPSIS       : '...';
DOTDOTLT       : '..<' { /* §3.7 未出现，语法-ebnf 未定义，作保留词法，
                           出现时 parser 报 ReservedSyntax。若有更短规则
                           冲突则撤回。ANTLR 先试最长的 ELLIPSIS (..? 的 . 冲突)。*/ };
DOTDOT         : '..'  { /* 同上 */ };
COLONCOLON     : '::';

LSHIFT_ASSIGN  : '<<=';
RSHIFT_ASSIGN  : '>>=';
URSHIFT_ASSIGN : '>>>=';
AND_ASSIGN     : '&&=';
OR_ASSIGN      : '||=';
NULL_COALESCE_ASSIGN : '??=';
POW_ASSIGN     : '**=';
MUL_ASSIGN     : '*=';
DIV_ASSIGN     : '/=';
MOD_ASSIGN     : '%=';
PLUS_ASSIGN    : '+=';
MINUS_ASSIGN   : '-=';
BIT_AND_ASSIGN : '&=';
BIT_OR_ASSIGN  : '|=';
BIT_XOR_ASSIGN : '^=';

URSHIFT        : '>>>';
LSHIFT         : '<<';
RSHIFT         : '>>';
STRICT_EQ      : '===';
STRICT_NEQ     : '!==';
EQ             : '==';
NEQ            : '!=';
LTE            : '<=';
GTE            : '>=';
PLUSPLUS       : '++';
MINUSMINUS     : '--';
POW            : '**';
AND            : '&&';
OR             : '||';
NULL_COALESCE  : '??';
ERROR_DEFAULT  : '?:';   // §3.7 注：lexer 层面 ? 和 : 紧邻产生单个 ?: token
OPTIONAL_CHAIN : '?.';
ERROR_PROPAGATE: '?!';   // §4.6 PostfixExpr，precedence 3
FORCE_UNWRAP   : '!!';   // §4.6 PostfixExpr，precedence 3
BIT_AND        : '&';
BIT_OR         : '|';
BIT_XOR        : '^';
NOT            : '!';
BIT_NOT        : '~';
PLUS           : '+';
MINUS          : '-';
MUL            : '*';
DIV            : '/';
MOD            : '%';
LT             : '<';
GT             : '>';
ASSIGN         : '=';
QUESTION       : '?';
COLON          : ':';
SEMICOLON      : ';';
COMMA          : ',';
PERIOD         : '.';
LPAREN         : '(';
RPAREN         : ')';
LBRACK         : '[';
RBRACK         : ']';
LBRACE         : '{';
RBRACE         : '}';
ROCKET         : '=>';
ARROW          : '->';
AT             : '@';   // §3.7 表中列作 Punctuator

// ============================================================================
// §6.1  Implemented Keywords（按 groups 分类，完整列表）
//
// 规则：放在 Identifier 之前，ANTLR 最长匹配会自动在标识符候选中
//       先命中这些 keyword。
// ============================================================================
// -- Declaration -------------------------------------------------------------
CLASS    : 'class';
STRUCT   : 'struct';
INTERFACE: 'interface';
ENUM     : 'enum';
ERROR    : 'error';
FUN      : 'fun';
MUT      : 'mut';
LET      : 'let';
CONST    : 'const';
ALIAS    : 'alias';
INIT     : 'init';
DEINIT   : 'deinit';
GET      : 'get';
SET      : 'set';

// -- Control Flow ------------------------------------------------------------
IF       : 'if';
ELSE     : 'else';
MATCH    : 'match';
WHEN     : 'when';
DEFAULT  : 'default';
FOR      : 'for';
WHILE    : 'while';
DO       : 'do';
BREAK    : 'break';
CONTINUE : 'continue';
RETURN   : 'return';
DEBUGGER : 'debugger';
IN       : 'in';
OUT      : 'out';

// -- Type --------------------------------------------------------------------
I8   : 'i8';
I16  : 'i16';
I32  : 'i32';
I64  : 'i64';
U8   : 'u8';
U16  : 'u16';
U32  : 'u32';
U64  : 'u64';
F32  : 'f32';
F64  : 'f64';
BOOL : 'bool';
STR  : 'str';
CHAR : 'char';   // §6.1 Type + §3.6.4 char literal 对应宿主类型
NULL : 'null';
UNIT : 'unit';
NEVER: 'never';
ANY  : 'any';

// -- Modifier ----------------------------------------------------------------
PUBLIC    : 'public';
PRIVATE   : 'private';
PROTECTED : 'protected';
STATIC    : 'static';
READONLY  : 'readonly';
MUTATING  : 'mutating';
OVERRIDE  : 'override';
ABSTRACT  : 'abstract';

// -- Operator / operation ----------------------------------------------------
AS       : 'as';
IS       : 'is';
TYPEOF   : 'typeof';
NEW      : 'new';
THIS     : 'this';
SUPER    : 'super';
EXTENDS  : 'extends';
RAISES   : 'raises';

// -- Module ------------------------------------------------------------------
MODULE : 'module';
IMPORT : 'import';
EXPORT : 'export';
// AS (已在 Operator 组声明)

// -- Concurrency -------------------------------------------------------------
SUSPEND : 'suspend';
SPAWN   : 'spawn';

// -- Marker Type Names（§6.3 Marker 软关键字，首字母大写）----------------------
//   注：虽然语法层面 Identifier 形态，但在 lexer 层作硬关键字可以：
//   a. 避免 parser 层频繁用谓词判断文本
//   b. 用户自定义同名类型会 shadow（lint ZOM6001）——语义层做最终检查
//   为避免与用户普通标识符冲突（首字母大写也合法），以下四项**不作为**硬 keyword，
//   保持为普通 Identifier；parser 层用字符串匹配处理。
//   （因此此处不声明它们）

// ============================================================================
// §6.2  Reserved Keywords（当前 reject，带各自的 ZOM 诊断码）
//
// 语法层产生真实 token（便于 parser 匹配到准确位置报出 ZOM500x）；
// 若 parser 中匹配到则抛出语义谓词。
// ============================================================================
THROW      : 'throw';       // ZOM5001
TRY        : 'try';         // ZOM5001
CATCH      : 'catch';       // ZOM5001
FINALLY    : 'finally';     // ZOM5001
ASYNC      : 'async';       // ZOM5002
AWAIT      : 'await';       // ZOM5002
VAR        : 'var';         // ZOM5003
ACTOR      : 'actor';       // ZOM5004
CHANNEL    : 'channel';     // ZOM5004
YIELD      : 'yield';       // ZOM5005
GENERATOR  : 'generator';   // ZOM5005
NAMESPACE  : 'namespace';   // ZOM5006
PACKAGE    : 'package';     // ZOM5006
TYPE       : 'type';        // ZOM5007（ObjectType 内 associated type 合法）
DELETE     : 'delete';      // ZOM5008
INSTANCEOF : 'instanceof';  // ZOM5008
OF         : 'of';          // ZOM5008
WITH       : 'with';        // ZOM5008

// ============================================================================
// Literal-like hard keywords（在 IDENTIFIER 前声明，避免被标识符吞掉）
//   TRUE / FALSE 是布尔字面量硬关键字
//   UNDERSCORE 是 _ 字面量，用作 wildcard pattern 与某些标识符起始位的区分
//   NOTE: 'implements' is NOT a keyword (per Ch.06 / 17-gr truth). Interface
//   implementations use standalone 'impl Interface for Type { }' form.
//   'implements' remains a plain IDENTIFIER so users get ordinary parser
//   'mismatched input' diagnostics instead of cryptic tokenizer errors.
//   MUT is declared in Declaration keywords section (Ch.06).
TRUE       : 'true';
FALSE      : 'false';
UNDERSCORE : '_';
IMPLEMENTS : 'implements';

// ============================================================================
// §3.5 + §3.8  Identifier（放在最后；keyword 规则先匹配）
//
//   IdentifierName   ::= IdentifierStart IdentifierPart*
//   IdentifierStart  ::= \p{ID_Start} | '$' | '_' | '\' UnicodeEscapeSeq
//   IdentifierPart   ::= \p{ID_Continue} | '$' | ZWNJ | ZWJ | '\' UnicodeEscapeSeq
//   Identifier       ::= IdentifierName （语义层进一步拒绝 ReservedWord）
//
//   Identifier 作为一条 lexer 规则；parser 会在需要时再对其做 "not Reserved"
//   的语义谓词检查。
// ============================================================================
IDENTIFIER
    : (
          // IdentifierStart
          [\p{ID_Start}$_]
        | '\\' ( HEX_ESCAPE | UNICODE_ESCAPE )
      )
      (
          // IdentifierPart
          [\p{ID_Continue}$]
        | '\u200C' | '\u200D'     // ZWNJ / ZWJ
        | '\\' ( HEX_ESCAPE | UNICODE_ESCAPE )
      )*
      {
          validateUnicodeEscapes(getText());
          // A2-REJECT: `_` + \u81F3\u5C11 1 \u4F4D\u6570\u5B57 + \u5176\u5B83\u6570\u5B57/\u4E0B\u5212\u7EBF/n \u7ED3\u5C3E\uFF08\u5982 _123\u3001_0\u3001_9n\u3001
          // _1_000\uFF09\u2014\u2014 \u8FD9\u662F"\u975E\u6CD5\u6570\u5B57\u5B57\u9762\u91CF\u4F2A\u88C5\u6210\u6807\u8BC6\u7B26"\uFF0CDECIMAL_LITERAL \u6839\u672C
          // \u5339\u914D\u4E0D\u5230 _ \u5F00\u5934\uFF0C\u5FC5\u987B\u515C\u5E95\u62D2\u7EDD\u3002
          // \u5141\u8BB8\u7EAF\u4E0B\u5212\u7EBF `_` / `__` / `___...`\uFF08\u533F\u540D\u7ED1\u5B9A\u3001\u5FFD\u7565\u6807\u8BB0\uFF09\uFF0C\u4E5F\u5141\u8BB8 `_foo`
          // \uFF08\u666E\u901A\u6807\u8BC6\u7B26\uFF09\u3002\u53EA\u62D2\u7EDD\u300C\u9996\u5B57\u7B26\u540E\u51FA\u73B0\u8FC7\u6570\u5B57\uFF0C\u4E14\u5168\u7A0B\u6570\u5B57/_/\u672B\u5C3E n\u300D\u3002
          String t = getText();
          if (t.length() >= 2 && t.charAt(0) == '_') {
              boolean seenDigit = false;
              boolean validNumeric = true;
              for (int i = 1; i < t.length(); i++) {
                  char c = t.charAt(i);
                  if (c >= '0' && c <= '9') {
                      seenDigit = true;
                  } else if (c == '_') {
                      // ok, numeric separator placeholder
                  } else if (c == 'n' && i == t.length() - 1) {
                      // ok, bigint suffix
                  } else {
                      validNumeric = false;
                      break;
                  }
              }
              if (seenDigit && validNumeric) {
                  throw new org.antlr.v4.runtime.misc.ParseCancellationException(
                      "identifier must not start with '_' followed by digits (numeric literal with leading separator): '" + t + "'");
              }
          }
      }
    ;

// ============================================================================
// §6.3  Contextual / Soft Keywords（不声明为独立 token；parser 层在
//      对应位置通过 identifier.getText().equals("xxx") 语义谓词识别）：
//
//   use           CaptureClause    `use [...]`
//   detached      SpawnModifier
//   blocking      SpawnModifier
//   priority      SpawnModifier   `priority( high | low )`
//   high / low    SpawnModifier   priority arg
//   until         SuspendEventSelector  `suspend until <expr>`
//
//  它们保持普通 IDENTIFIER 形态，避免影响用户标识符命名空间。
// ============================================================================
