# Lexical Structure

The lexical structure of Zom defines how sequences of Unicode characters are translated into tokens that form the basic building blocks of the language's syntax.

## Source Files

Zom source code is written in UTF-8 encoded text files with the `.zom` extension. Source files are organized into modules, with each file representing a single module.

## Unicode Support

Zom fully supports Unicode and includes special handling for format-control characters:

- **ZWNJ** (`\u200C`): Zero Width Non-Joiner
- **ZWJ** (`\u200D`): Zero Width Joiner
- **ZWNBSP** (`\uFEFF`): Zero Width No-Break Space (Byte Order Mark)

## Whitespace and Line Terminators

### Whitespace Characters

- **TAB** (`\u0009`): Character Tabulation
- **VT** (`\u000B`): Line Tabulation
- **FF** (`\u000C`): Form Feed
- **Space** (`\u0020`): Regular space character

### Line Terminators

- **LF** (`\u000A`): Line Feed
- **CR** (`\u000D`): Carriage Return
- **LS** (`\u2028`): Line Separator
- **PS** (`\u2029`): Paragraph Separator
- **CRLF** (`\u000D\u000A`): Carriage Return + Line Feed

## Comments

Zom supports two types of comments that are treated as whitespace:

### Single-Line Comments

Single-line comments start with `//` and continue to the end of the line:

```zom
// This is a single-line comment
let x = 42; // Comment at end of line
```

### Multi-Line Comments

Multi-line comments start with `/*` and end with `*/`. They can span multiple lines but cannot be nested:

```zom
/*
 * This is a multi-line comment
 * that spans several lines
 */
let y = 10;

/* Inline comment */ let z = 20;
```

## Identifiers

Identifiers name variables, functions, types, modules, and other program entities.

### Identifier Grammar

```bnf
IdentifierName ::= IdentifierStart IdentifierPart*

IdentifierStart ::= IdentifierStartChar | '\\' UnicodeEscapeSequence

IdentifierPart ::= IdentifierPartChar | '\\' UnicodeEscapeSequence

IdentifierStartChar ::= UnicodeIDStart | '$' | '_'

IdentifierPartChar ::= UnicodeIDContinue | '$' | ZWNJ | ZWJ
```

### Valid Identifier Examples

```zom
// Basic identifiers
myVariable
_private
$temp
UserRecord

// Unicode identifiers
π
α_beta
变量名
идентификатор

// With escape sequences
\u0041pple  // Same as "Apple"
```

### Identifier Rules

1. Must start with a letter, underscore, or dollar sign
2. Subsequent characters can include digits and Unicode continuation characters
3. Case-sensitive (`myVar` ≠ `MyVar`)
4. Cannot be reserved keywords
5. Unicode escape sequences are allowed

## Keywords

The following identifiers are reserved as keywords and cannot be used as regular identifiers:

### Declaration Keywords

```
class       struct      interface   enum        error
fun         let         const       var         alias
type        module      namespace   package     constructor
init        deinit      get         set         accessor
declare
```

### Control Flow Keywords

```
if          else        match       when        default     case
for         while       do          break       continue
return      throw       try         catch       finally
switch      debugger
```

### Type Keywords

```
i8          i32         i64         u8          u16
u32         u64         f32         f64         bool
str         null        unit        any         never
object      symbol      bigint      undefined   void
```

### Modifier Keywords

```
public      private     protected   static      abstract
readonly    mutable     async       await       override
immediate   intrinsic   global      unique      out
```

### Operator Keywords

```
as          is          in          of          typeof
keyof       infer       satisfies   asserts     assert
instanceof  new         delete      this        super
raises      implements  extends
```

### Advanced Keywords

```
import      export      from        using       require
with        yield
```

## Literals

Literals represent constant values in source code.

### Null Literal

The `null` literal represents the absence of a value:

```zom
let optional: i32? = null;
let result = someFunction() ?? null;
```

### Boolean Literals

```zom
let isTrue: bool = true;
let isFalse: bool = false;
```

### Numeric Literals

#### Integer Literals

**Decimal Integers:**

```zom
0           // Zero
42          // Positive integer
1_000_000   // With separators for readability
```

**Binary Integers:**

```zom
0b1010      // Binary: 10 in decimal
0B11110000  // Binary: 240 in decimal
0b1111_0000_1111_0000  // With separators
```

**Octal Integers:**

```zom
0o755       // Octal: 493 in decimal
0O1234      // Octal: 668 in decimal
```

**Hexadecimal Integers:**

```zom
0xFF        // Hex: 255 in decimal
0x1A2B      // Hex: 6699 in decimal
0xDEAD_BEEF // With separators
```

#### Floating-Point Literals

```zom
3.14159     // Standard decimal notation
.5          // Leading decimal point
2.5e10      // Scientific notation: 2.5 × 10^10
1.0E-9      // Scientific notation: 1.0 × 10^-9
6.022e+23   // Avogadro's number
```

### String Literals

String literals can use either double quotes or single quotes:

```zom
"Hello, World!"     // Double-quoted string
'Single quoted'     // Single-quoted string
"Mixed 'quotes'"    // Mixing quote types
'Escaped \"quotes\"' // Escaped quotes
```

#### Escape Sequences

| Sequence | Meaning |
|----------|----------|
| `\'` | Single quote |
| `\"` | Double quote |
| `\\` | Backslash |
| `\b` | Backspace |
| `\f` | Form feed |
| `\n` | Line feed (newline) |
| `\r` | Carriage return |
| `\t` | Horizontal tab |
| `\v` | Vertical tab |
| `\xHH` | Hexadecimal byte (e.g., `\x41` = 'A') |
| `\uHHHH` | Unicode code point (e.g., `\u03C0` = 'π') |
| `\u{HHHHHH}` | Extended Unicode (e.g., `\u{1F600}` = '😀') |

#### String Examples

```zom
"Line 1\nLine 2"              // Multi-line via escape
"Unicode: \u03C0 ≈ 3.14"      // Unicode characters
"Emoji: \u{1F44D}"            // Extended Unicode
"Path: C:\\Users\\Name"       // Windows path
"Quote: \"Hello\""             // Embedded quotes
```

### Character Literals

Character literals represent single Unicode characters:

```zom
'a'         // ASCII character
'π'         // Unicode character
'\n'        // Escaped newline
'\u03C0'    // Unicode escape
'\u{1F600}' // Extended Unicode emoji
```

## Punctuators and Operators

### Delimiters

```
( )         Parentheses
[ ]         Square brackets
{ }         Curly braces
```

### Punctuation

```
.           Period (member access)
,           Comma (separator)
;           Semicolon (statement terminator)
:           Colon (type annotation, label)
?           Question mark (optional, conditional)
...         Ellipsis (spread, rest)
```

### Arithmetic Operators

```
+           Addition
-           Subtraction
*           Multiplication
/           Division
%           Modulo
**          Exponentiation
++          Increment
--          Decrement
```

### Comparison Operators

```
==          Equality
!=          Inequality
===         Strict equality
!==         Strict inequality
<           Less than
>           Greater than
<=          Less than or equal
>=          Greater than or equal
```

### Logical Operators

```
&&          Logical AND
||          Logical OR
!           Logical NOT
```

### Bitwise Operators

```
&           Bitwise AND
|           Bitwise OR
^           Bitwise XOR
~           Bitwise NOT
<<          Left shift
>>          Right shift
>>>         Unsigned right shift
```

### Assignment Operators

```
=           Simple assignment
+=          Addition assignment
-=          Subtraction assignment
*=          Multiplication assignment
/=          Division assignment
%=          Modulo assignment
**=         Exponentiation assignment
&=          Bitwise AND assignment
|=          Bitwise OR assignment
^=          Bitwise XOR assignment
<<=         Left shift assignment
>>=         Right shift assignment
>>>=        Unsigned right shift assignment
&&=         Logical AND assignment
||=         Logical OR assignment
??=         Null coalescing assignment
```

### Special Operators

```
?.          Optional chaining
??          Null coalescing
?!          Error propagation
!!          Force unwrap
?:          Error default
->          Arrow (function return type)
=>          Rocket (lambda, match arms)
```
