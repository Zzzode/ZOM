# SPEC

## Lexical Grammar

### Identifiers

```bnf
IdentifierName ::
    IdentifierStart
    IdentifierName IdentifierPart

IdentifierStart ::
    IdentifierStartChar
    \ UnicodeEscapeSequence

IdentifierPart ::
    IdentifierPartChar
    \ UnicodeEscapeSequence

IdentifierStartChar ::
    UnicodeIDStart
    $
    _

IdentifierPartChar ::
    UnicodeIDContinue
    $
    <ZWNJ>
    <ZWJ>

AsciiLetter :: one of
    a b c d e f g h i j k l m n o p q r s t u v w x y z A B C D E F G H I J K L M N O P Q R S T U V W X Y Z

UnicodeIDStart ::
    any Unicode code point with the Unicode property “ID_Start”

UnicodeIDContinue ::
    any Unicode code point with the Unicode property “ID_Continue”
```
