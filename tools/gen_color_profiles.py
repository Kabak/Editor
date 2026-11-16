#!/usr/bin/env python3
"""Generates ColorProfiles/*.prf (the editor's color profile format).
Usage (from the project root): python3 tools/gen_color_profiles.py

Colors are the Solarized accents, which stay readable on both light and dark
backgrounds. Phrase rules are case-insensitive *substring* matches (no word
boundaries), so keywords are written with a trailing space or bracket where
that avoids matching inside longer words."""
import os, re

GREEN, YELLOW, BLUE, MAGENTA, ORANGE, CYAN, VIOLET, RED, GRAY = (
    "#859900", "#b58900", "#268bd2", "#d33682", "#cb4b16", "#2aa198", "#6c71c4", "#dc322f", "#839496")
# Role -> color: keywords, types, storage/modifiers, literals, preprocessor/imports, library calls, special.
KEY, TYPE, MOD, LIT, PRE, FUN, SPEC = GREEN, YELLOW, BLUE, MAGENTA, ORANGE, CYAN, VIOLET

def rules(*groups):
    """groups: (color, [phrases]) -> list of (text, color); a later duplicate replaces an earlier one."""
    out = {}
    # Library calls go last: a call such as "printf(" must win over a shorter keyword inside it.
    for color, words in sorted(groups, key=lambda g: g[0] == FUN):
        for w in words:
            out.pop(w.lower(), None)          # phrase rules ignore case, so "Boolean " and "boolean " are one rule
            out[w.lower()] = (w, color)
    return list(out.values())

def quote(text):
    assert "\n" not in text and "\\" not in text and '"' not in text, text
    if text != text.strip() or re.search(r'[,;=#@\[\]]', text):
        return '"' + text + '"'
    return text

def write(name, blocks, phrases):
    lines = []
    for section, items in (("blocks", blocks), ("phrases", phrases)):
        lines.append("[%s]" % section)
        lines.append("size=%d" % len(items))
        for i, (text, color) in enumerate(items, 1):
            assert text.strip() != "" and re.fullmatch(r"#[0-9a-f]{6}", color), (name, text, color)
            lines.append("%d\\text=%s" % (i, quote(text)))
            lines.append("%d\\color=%s" % (i, color))
        lines.append("")
    os.makedirs("ColorProfiles", exist_ok=True)
    with open("ColorProfiles/%s.prf" % name, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines))

C_TYPES = ["int ", "char ", "long ", "short ", "float ", "double ", "void ", "unsigned", "signed ", "size_t", "ssize_t",
           "uint8_t", "uint16_t", "uint32_t", "uint64_t", "int8_t", "int16_t", "int32_t", "int64_t", "bool ", "wchar_t",
           "struct ", "enum ", "union ", "typedef ", "FILE "]
C_KEYS = ["if ", "if(", "else", "for ", "for(", "while", "do ", "do{", "switch", "case ", "default:", "break", "continue",
          "return", "goto "]
C_MODS = ["static ", "extern ", "const ", "volatile ", "register ", "inline ", "sizeof"]
C_LITS = ["NULL", "true", "false"]
C_PRE = ["#include", "#define", "#undef", "#ifdef", "#ifndef", "#if ", "#elif", "#else", "#endif", "#pragma", "#error", "defined("]
C_FUN = ["printf(", "scanf(", "sprintf(", "snprintf(", "fprintf(", "puts(", "malloc(", "calloc(", "realloc(", "free(",
         "memcpy(", "memset(", "memmove(", "strlen(", "strcmp(", "strcpy(", "strcat(", "fopen(", "fclose(", "fread(",
         "fwrite(", "exit(", "assert(", "main("]
c = rules((FUN, C_FUN), (TYPE, C_TYPES), (MOD, C_MODS), (KEY, C_KEYS), (LIT, C_LITS), (PRE, C_PRE))
write("C", [("#include", PRE), ("#define", PRE)], c)

CPP_TYPES = C_TYPES + ["class ", "template", "typename", "string ", "vector<", "map<", "set<", "unordered_map<", "array<",
                       "unique_ptr", "shared_ptr", "optional<", "auto ", "decltype", "nullptr_t", "char16_t", "char32_t"]
CPP_KEYS = C_KEYS + ["try ", "try{", "catch", "throw ", "namespace ", "using ", "new ", "delete ", "co_await", "co_return", "co_yield"]
CPP_MODS = C_MODS + ["constexpr", "consteval", "constinit", "noexcept", "virtual ", "override", "final", "explicit",
                     "mutable ", "operator", "public:", "private:", "protected:", "friend ", "static_cast", "dynamic_cast",
                     "reinterpret_cast", "const_cast", "concept ", "requires "]
CPP_LITS = ["NULL", "nullptr", "true", "false", "this->", "this."]
CPP_FUN = ["std::", "cout", "cin", "cerr", "endl", "make_unique", "make_shared", "move(", "forward<", "begin(", "end(",
           "push_back(", "emplace_back(", "size()", "empty()", "main("]
cpp = rules((FUN, CPP_FUN), (TYPE, CPP_TYPES), (MOD, CPP_MODS), (KEY, CPP_KEYS), (LIT, CPP_LITS), (PRE, C_PRE + ["#pragma once"]))
write("Cpp", [("#include", PRE), ("#define", PRE)], cpp)
write("H", [("#include", PRE), ("#define", PRE), ("#pragma", PRE)], cpp)   # headers: C and C++ declarations

py_keys = ["def ", "class ", "return", "if ", "elif ", "else:", "else ", "for ", "while ", "break", "continue", "try:",
           "except", "finally:", "raise ", "with ", "yield", "lambda", "assert ", "global ", "nonlocal ", "async ", "await ",
           "import ", "from ", " in ", " is ", " not ", " and ", " or ", " as ", "match ", "case ", "del "]
py_lits = ["True", "False", "None", "self.", "self)", "cls.", "__init__", "__name__", "__main__", "__str__", "__repr__", "__file__"]
py_fun = ["print(", "len(", "range(", "open(", "str(", "int(", "float(", "bool(", "list(", "dict(", "set(", "tuple(",
          "enumerate(", "zip(", "map(", "filter(", "sorted(", "sum(", "min(", "max(", "abs(", "isinstance(", "super(",
          "input(", "type(", "getattr(", "setattr(", "hasattr("]
py_dec = ["@staticmethod", "@classmethod", "@property", "@dataclass", "@abstractmethod", "@lru_cache"]
write("Python", [("import ", PRE), ("from ", PRE)],
      rules((FUN, py_fun), (KEY, py_keys), (LIT, py_lits), (SPEC, py_dec)))

write("JSON", [], rules((LIT, ["true", "false", "null"]), (FUN, ["http://", "https://"])))

php_keys = ["function ", "return", "if ", "if(", "elseif", "else", "foreach", "for ", "for(", "while", "do ", "switch", "case ",
            "break", "continue", "try ", "try{", "catch", "finally", "throw ", "new ", "echo ", "print ", "namespace ", "use ",
            "require", "include", "match", "yield", "fn ", "as ", "instanceof "]
php_mods = ["public ", "private ", "protected ", "static ", "final ", "abstract ", "class ", "interface ", "trait ", "enum ",
            "extends ", "implements ", "readonly ", "const ", "global ", "var ", "declare("]
php_lits = ["null", "true", "false", "$this", "self::", "parent::", "static::", "__construct", "__DIR__", "__FILE__"]
php_fun = ["isset(", "empty(", "unset(", "array(", "count(", "strlen(", "str_replace(", "substr(", "explode(", "implode(",
           "in_array(", "array_map(", "array_filter(", "print_r(", "var_dump(", "json_encode(", "json_decode(", "sprintf(",
           "$_GET", "$_POST", "$_SESSION", "$_SERVER", "$_COOKIE", "$_FILES", "$_REQUEST", "$GLOBALS"]
write("PHP", [], rules((FUN, php_fun), (KEY, php_keys), (MOD, php_mods), (LIT, php_lits), (PRE, ["<?php", "<?=", "?>"])))

java_types = ["void ", "int ", "long ", "double ", "float ", "boolean ", "char ", "byte ", "short ", "String ", "Object ",
              "Integer ", "Long ", "Double ", "Boolean ", "List<", "Map<", "Set<", "ArrayList", "HashMap", "HashSet",
              "Optional<", "Stream<", "record ", "var "]
java_keys = ["if ", "if(", "else", "for ", "for(", "while", "do ", "switch", "case ", "default:", "break", "continue", "return",
             "try ", "try{", "catch", "finally", "throw ", "throws ", "new ", "instanceof ", "yield "]
java_mods = ["public ", "private ", "protected ", "static ", "final ", "abstract ", "class ", "interface ", "enum ",
             "extends ", "implements ", "synchronized", "volatile ", "transient ", "native ", "sealed ", "permits "]
java_lits = ["null", "true", "false", "this.", "super.", "super("]
java_fun = ["System.out", "System.err", "System.in", "println(", "printf(", "equals(", "hashCode(", "toString(", "main("]
java_ann = ["@Override", "@Deprecated", "@FunctionalInterface", "@SuppressWarnings", "@Test", "@Nullable", "@NotNull"]
write("Java", [("import ", PRE), ("package ", PRE)],
      rules((FUN, java_fun), (TYPE, java_types), (KEY, java_keys), (MOD, java_mods), (LIT, java_lits), (SPEC, java_ann),
            (PRE, ["import ", "package "])))

# Microchip PIC assembler (MPASM): the language is case-insensitive, and so are phrase rules.
asm_ops = ["movlw", "movwf", "movf", "addlw", "addwf", "addwfc", "sublw", "subwf", "subwfb", "andlw", "andwf", "iorlw", "iorwf",
           "xorlw", "xorwf", "incf", "incfsz", "infsnz", "decf", "decfsz", "dcfsnz", "clrf", "clrw", "comf", "rlf", "rrf",
           "rlcf", "rrcf", "rlncf", "rrncf", "swapf", "btfsc", "btfss", "btg", "bsf", "bcf", "goto", "call", "return",
           "retlw", "retfie", "nop", "sleep", "clrwdt", "bra", "bnz", "bz", "bnc", "bc", "movlb", "tstfsz", "mullw", "mulwf"]
asm_dir = ["org ", "end", "equ ", "list ", "include ", "processor", "radix", "__config", "cblock", "endc", "udata", "idata",
           "res ", "db ", "dw ", "banksel", "pagesel", "macro", "endm", "local ", "variable ", "constant ", "errorlevel",
           "#include", "#define", "if ", "else", "endif", "while ", "endw", "ifdef ", "ifndef ", "code", "global ", "extern "]
asm_regs = ["STATUS", "INTCON", "OPTION_REG", "PORTA", "PORTB", "PORTC", "PORTD", "PORTE", "TRISA", "TRISB", "TRISC", "TRISD",
            "TRISE", "LATA", "LATB", "LATC", "LATD", "LATE", "TMR0", "TMR1H", "TMR1L", "TMR2", "T1CON", "T2CON", "PR2",
            "ADCON0", "ADCON1", "ADRESH", "ADRESL", "EEDATA", "EEADR", "EECON1", "EECON2", "PCL", "PCLATH", "FSR", "INDF",
            "PIR1", "PIE1", "CCP1CON", "CCPR1L", "SSPCON", "SSPBUF", "TXREG", "RCREG", "TXSTA", "RCSTA", "SPBRG", "WREG"]
asm_bits = ["GIE", "PEIE", "T0IE", "T0IF", "INTE", "INTF", "RBIE", "RBIF", "RP0", "RP1", "IRP"]
write("PicASM", [("#include", PRE), ("#define", PRE)],
      rules((SPEC, asm_regs), (LIT, asm_bits), (KEY, asm_ops), (MOD, asm_dir), (PRE, ["#include", "#define", "__config"])))

rust_keys = ["fn ", "let ", "if ", "else", "match ", "loop", "while ", "for ", " in ", "return", "break", "continue", "async ",
             "await", "move ", "unsafe ", "where ", " as ", "yield "]
rust_mods = ["mut ", "pub ", "pub(", "struct ", "enum ", "impl ", "trait ", "mod ", "use ", "const ", "static ", "type ", "dyn ",
             "ref ", "extern ", "crate::", "super::", "self::", "Self"]
rust_types = ["i8", "i16", "i32", "i64", "i128", "isize", "u8", "u16", "u32", "u64", "u128", "usize", "f32", "f64", "bool",
              "&str", "String", "Vec<", "Option<", "Result<", "Box<", "Rc<", "Arc<", "HashMap<", "HashSet<", "RefCell<", "Cow<"]
rust_lits = ["Some(", "None", "Ok(", "Err(", "true", "false", "self"]
rust_fun = ["println!", "print!", "eprintln!", "eprint!", "format!", "vec!", "panic!", "assert!", "assert_eq!", "assert_ne!",
            "todo!", "unimplemented!", "write!", "writeln!", "matches!", "unwrap()", "expect(", "clone()", ".iter()", "collect"]
rust_attr = ["#[derive", "#[cfg", "#[test", "#[allow", "#![", "#["]
write("Rust", [("use ", PRE)],
      rules((FUN, rust_fun), (TYPE, rust_types), (KEY, rust_keys), (MOD, rust_mods), (LIT, rust_lits), (SPEC, rust_attr), (PRE, ["use "])))

go_keys = ["if ", "else", "for ", "range ", "switch ", "case ", "default:", "select ", "break", "continue", "fallthrough", "goto ",
           "return", "defer ", "go func", "chan ", "<-"]
go_mods = ["func ", "package ", "import ", "var ", "const ", "type ", "struct ", "interface ", "map[", "iota"]
go_types = ["int ", "int8", "int16", "int32", "int64", "uint ", "uint8", "uint16", "uint32", "uint64", "uintptr", "float32",
            "float64", "complex128", "string ", "bool ", "byte ", "rune ", "error", "any "]
go_lits = ["nil", "true", "false"]
go_fun = ["make(", "len(", "cap(", "append(", "copy(", "delete(", "new(", "panic(", "recover(", "close(", "fmt.", "Println",
          "Printf", "Sprintf", "Errorf", "errors.", "strings.", "strconv.", "context.", "sync.", "time.", "os.", "http.", "main("]
write("Go", [("import ", PRE), ("package ", PRE)],
      rules((FUN, go_fun), (TYPE, go_types), (KEY, go_keys), (MOD, go_mods), (LIT, go_lits), (PRE, ["import ", "package "])))

# Free Pascal / Delphi (case-insensitive language).
pas_keys = ["begin", "end;", "end.", "end else", "if ", " then", "else", "case ", " of ", "for ", " to ", "downto", " do ", "while ",
            "repeat", "until", "with ", "try ", "finally", "except", "raise ", "exit", "break", "continue", "goto ", "label "]
pas_mods = ["program ", "unit ", "uses ", "interface", "implementation", "initialization", "finalization", "procedure ",
            "function ", "constructor ", "destructor ", "var ", "const ", "type ", "record", "class ", "object ",
            "property ", "private", "public", "protected", "published", "override", "virtual", "inherited", "overload",
            "forward", "external", "packed ", "inline"]
pas_types = ["integer", "longint", "shortint", "smallint", "byte", "word", "cardinal", "int64", "qword", "real", "single",
             "double", "extended", "boolean", "char", "string", "pointer", "array ", "set of", "file of", "tobject"]
pas_lits = ["nil", "true", "false", " and ", " or ", " not ", " xor ", " div ", " mod ", " shl ", " shr ", " in ", " is ", " as ", "self"]
pas_fun = ["writeln(", "write(", "readln(", "read(", "inc(", "dec(", "length(", "setlength(", "assigned(", "format(",
           "inttostr(", "strtoint(", "copy(", "pos(", "freeandnil(", "assign(", "reset(", "rewrite(", "close("]
write("Pascal", [], rules((FUN, pas_fun), (TYPE, pas_types), (KEY, pas_keys), (MOD, pas_mods), (LIT, pas_lits), (PRE, ["{$"])))

css_props = ["color:", "background:", "background-", "border:", "border-", "margin:", "margin-", "padding:", "padding-",
             "display:", "position:", "width:", "height:", "min-", "max-", "font:", "font-", "text-", "line-height:",
             "letter-spacing:", "align-", "justify-", "flex", "grid", "gap:", "overflow", "opacity:", "transition", "transform",
             "animation", "z-index:", "cursor:", "box-shadow:", "box-sizing:", "content:", "float:", "clear:", "top:", "left:",
             "right:", "bottom:", "outline", "visibility:", "white-space:", "list-style", "vertical-align:", "object-fit:"]
css_vals = ["none", "auto", "inherit", "initial", "block", "inline", "absolute", "relative", "fixed", "sticky", "solid", "dashed",
            "bold", "normal", "center", "hidden", "transparent", "pointer", "wrap", "nowrap", "space-between", "uppercase"]
css_at = ["@media", "@import", "@font-face", "@keyframes", "@charset", "@supports", "@layer", "@container", "@property"]
css_pseudo = [":hover", ":active", ":focus", ":visited", ":first-child", ":last-child", ":nth-child", ":not(", ":root", "::before",
              "::after", ":before", ":after", "::selection", "::placeholder"]
css_fun = ["var(", "calc(", "rgb(", "rgba(", "hsl(", "hsla(", "url(", "min(", "max(", "clamp(", "linear-gradient(",
           "radial-gradient(", "translate(", "rotate(", "scale("]
write("CSS", [], rules((LIT, css_vals), (BLUE, css_props), (FUN, css_fun), (SPEC, css_pseudo), (PRE, css_at), (RED, ["!important"])))

html_struct = ["<html", "</html>", "<head", "</head>", "<body", "</body>", "<header", "</header>", "<footer", "</footer>", "<nav",
               "</nav>", "<main", "</main>", "<section", "</section>", "<article", "</article>", "<aside", "</aside>", "<div",
               "</div>", "<span", "</span>"]
html_text = ["<p>", "<p ", "</p>", "<a ", "</a>", "<h1", "</h1>", "<h2", "</h2>", "<h3", "</h3>", "<h4", "</h4>", "<h5", "</h5>",
             "<h6", "</h6>", "<ul", "</ul>", "<ol", "</ol>", "<li", "</li>", "<br", "<hr", "<strong", "</strong>", "<em>", "</em>",
             "<b>", "</b>", "<i>", "</i>", "<pre", "</pre>", "<code", "</code>", "<blockquote", "<table", "</table>", "<tr", "</tr>",
             "<td", "</td>", "<th", "</th>", "<thead", "<tbody"]
html_forms = ["<form", "</form>", "<input", "<button", "</button>", "<select", "</select>", "<option", "<textarea", "</textarea>",
              "<label", "</label>", "<fieldset", "<legend"]
html_media = ["<img", "<svg", "</svg>", "<canvas", "<video", "<audio", "<source", "<iframe", "<picture", "<figure"]
html_meta = ["<script", "</script>", "<style", "</style>", "<link", "<meta", "<title", "</title>", "<!DOCTYPE", "<template", "<noscript"]
html_attrs = [" class=", " id=", " href=", " src=", " alt=", " style=", " type=", " name=", " value=", " rel=", " lang=", " charset=",
              " content=", " width=", " height=", " title=", " action=", " method=", " placeholder=", " target=", " onclick=",
              " for=", " data-", " aria-", " role=", " disabled", " checked", " required", " selected", " async", " defer"]
html_ent = ["&nbsp;", "&amp;", "&lt;", "&gt;", "&quot;", "&copy;", "&laquo;", "&raquo;", "&mdash;"]
write("HTML", [], rules((BLUE, html_struct), (KEY, html_text), (SPEC, html_forms), (FUN, html_media), (PRE, html_meta),
                        (YELLOW, html_attrs), (MAGENTA, html_ent), (GRAY, ["<!--", "-->"])))
