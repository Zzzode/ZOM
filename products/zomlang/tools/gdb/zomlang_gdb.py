import gdb
import gdb.printing


def _escape_text(text):
    return text.replace("\\", "\\\\").replace("\n", "\\n").replace("\r", "\\r").replace("\t", "\\t")


def _type_name(value):
    return str(value.type.strip_typedefs())


def _field(value, name):
    try:
        return value[name]
    except gdb.error:
        return None


def _dereference(value):
    try:
        if value.type.strip_typedefs().code == gdb.TYPE_CODE_PTR:
            if int(value) == 0:
                return None
            return value.dereference()
    except gdb.error:
        return None
    return value


def _node_value(value):
    type_name = _type_name(value)
    if type_name.startswith("zc::Own<"):
        value = _field(value, "ptr")
        if value is None:
            return None
    return _dereference(value)


def _kind_name(node):
    kind = _field(node, "kind")
    if kind is None:
        return ""
    text = str(kind)
    return text.rsplit("::", 1)[-1]


def _range_text(node, limit=256):
    source_range = _field(node, "range")
    if source_range is None:
        return ""
    start = _field(source_range, "start")
    end = _field(source_range, "end")
    if start is None or end is None:
        return ""
    start_ptr = _field(start, "ptr")
    end_ptr = _field(end, "ptr")
    if start_ptr is None or end_ptr is None:
        return ""
    try:
        start_address = int(start_ptr)
        end_address = int(end_ptr)
        if end_address <= start_address:
            return ""
        size = min(end_address - start_address, limit)
        data = bytes(gdb.selected_inferior().read_memory(start_address, size))
    except gdb.error:
        return ""
    text = _escape_text(data.decode("utf-8", errors="replace").rstrip("\0"))
    return text + ("..." if end_address - start_address > limit else "")


def _summary(value):
    node = _node_value(value)
    if node is None:
        return ""
    kind = _kind_name(node)
    text = _range_text(node)
    if not kind:
        return text
    return kind if not text else f'{kind} "{text}"'


class _NodePrinter:
    def __init__(self, value):
        self.value = value

    def to_string(self):
        return _summary(self.value)


class _ZomKind(gdb.Command):
    "Print the SyntaxKind for an AST node expression."

    def __init__(self):
        super().__init__("zomkind", gdb.COMMAND_DATA)

    def invoke(self, argument, from_tty):
        expression = argument.strip() or "expression"
        try:
            node = _node_value(gdb.parse_and_eval(expression))
        except gdb.error as error:
            raise gdb.GdbError(str(error))
        if node is None:
            raise gdb.GdbError("expected a non-null AST node")
        gdb.write(_kind_name(node) + "\n")


class _ZomInfo(gdb.Command):
    "Print the SyntaxKind and source text for an AST node expression."

    def __init__(self):
        super().__init__("zominfo", gdb.COMMAND_DATA)

    def invoke(self, argument, from_tty):
        expression = argument.strip() or "expression"
        try:
            value = gdb.parse_and_eval(expression)
        except gdb.error as error:
            raise gdb.GdbError(str(error))
        summary = _summary(value)
        if not summary:
            raise gdb.GdbError("expected a non-null AST node")
        gdb.write(summary + "\n")


def _register_printers():
    printers = gdb.printing.RegexpCollectionPrettyPrinter("zomlang")
    printers.add_printer("Node", "^zomlang::compiler::ast::Node$", _NodePrinter)
    printers.add_printer("OwnNode", "^zc::Own<zomlang::compiler::ast::Node.*>$", _NodePrinter)
    gdb.printing.register_pretty_printer(gdb.current_objfile(), printers, replace=True)


_register_printers()
_ZomKind()
_ZomInfo()
