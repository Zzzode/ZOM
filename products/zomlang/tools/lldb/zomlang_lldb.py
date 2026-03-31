import lldb


_ENUM_CACHE = {}


def _escape_text(text):
  return text.replace("\\", "\\\\").replace("\n", "\\n").replace("\r", "\\r").replace("\t", "\\t")


def _get_enum_name(target, enum_type_name, value):
  cache = _ENUM_CACHE.get(enum_type_name)
  if cache is None:
    cache = {}
    type_list = target.FindTypes(enum_type_name)
    if type_list.GetSize() > 0:
      enum_type = type_list.GetTypeAtIndex(0)
      members = enum_type.GetEnumMembers()
      for i in range(members.GetSize()):
        member = members.GetEnumMemberAtIndex(i)
        cache[member.GetValueAsUnsigned()] = member.GetName()
    _ENUM_CACHE[enum_type_name] = cache
  return cache.get(value, str(value))


def _read_range_text(process, range_value, max_len=256):
  # SourceRange contains start and end SourceLoc members
  start = range_value.GetChildMemberWithName("start")
  end = range_value.GetChildMemberWithName("end")
  if not start.IsValid() or not end.IsValid():
    return ""

  # SourceLoc contains ptr pointer
  start_ptr = start.GetChildMemberWithName("ptr")
  end_ptr = end.GetChildMemberWithName("ptr")
  if not start_ptr.IsValid() or not end_ptr.IsValid():
    return ""

  start_addr = start_ptr.GetValueAsUnsigned()
  end_addr = end_ptr.GetValueAsUnsigned()
  if end_addr <= start_addr:
    return ""

  size = end_addr - start_addr
  if size > max_len:
    size = max_len

  error = lldb.SBError()
  data = process.ReadMemory(start_addr, size, error)
  if not error.Success():
    return ""

  try:
    text = data.decode("utf-8", errors="replace")
  except Exception:
    text = data.decode("latin-1", errors="replace")

  # Remove null characters from end of string
  text = text.rstrip('\x00')

  if end_addr - start_addr > max_len:
    return _escape_text(text) + "…"
  return _escape_text(text)


def _get_value_address(valobj):
  target = valobj.GetTarget()
  val_type = valobj.GetType()
  if val_type.IsPointerType() or val_type.IsReferenceType():
    return valobj.GetValueAsUnsigned()
  return valobj.GetAddress().GetLoadAddress(target)


def _deref_if_needed(valobj):
  val_type = valobj.GetType()
  if val_type.IsPointerType() or val_type.IsReferenceType():
    return valobj.Dereference()
  return valobj


def _unwrap_node_value(valobj):
  type_name = valobj.GetType().GetName()
  # Handle zc::Own smart pointer
  if type_name.startswith("zc::Own<"):
    ptr = valobj.GetChildMemberWithName("ptr")
    if ptr.IsValid():
      return ptr
  # Handle possible reference type
  elif valobj.GetType().IsReferenceType():
    return valobj.Dereference()
  # Handle possible pointer type
  elif valobj.GetType().IsPointerType():
    return valobj
  return valobj


def _evaluate_expression(frame, expr):
  if expr.strip() == "":
    return None
  value = frame.EvaluateExpression(expr)
  if not value.IsValid():
    return None
  return value


def _value_from_command_or_default(frame, command, default_name):
  if command.strip() == "":
    value = frame.FindVariable(default_name)
    if value.IsValid():
      return value
    return None
  return _evaluate_expression(frame, command)


def _token_text_from_token_value(token_value):
  # Token class has direct getValue() method, no need to access through impl pointer
  expr = f"((const zomlang::compiler::lexer::Token*)0x{_get_value_address(token_value):x})->getValue()"
  value_value = token_value.GetFrame().EvaluateExpression(expr)
  if not value_value.IsValid():
    return ""

  # Try to get string content directly
  value_str = value_value.GetSummary()
  if value_str and value_str.startswith('"') and value_str.endswith('"'):
    return value_str[1:-1]

  # If failed, try reading through source range
  expr = f"((const zomlang::compiler::lexer::Token*)0x{_get_value_address(token_value):x})->getRange()"
  range_value = token_value.GetFrame().EvaluateExpression(expr)
  if not range_value.IsValid():
    return ""

  return _read_range_text(token_value.GetProcess(), range_value)


def _token_kind_name_from_value(token_value):
  addr = _get_value_address(token_value)
  if addr == 0:
    return ""
  expr = f"((const zomlang::compiler::lexer::Token*)0x{addr:x})->getKind()"
  kind_value = token_value.GetFrame().EvaluateExpression(expr)
  if not kind_value.IsValid():
    return ""
  kind_num = kind_value.GetValueAsUnsigned()
  return _get_enum_name(token_value.GetTarget(), "zomlang::compiler::ast::SyntaxKind", kind_num)


def _node_kind_name_from_value(node_value):
  addr = _get_value_address(node_value)
  if addr == 0:
    return ""
  expr = f"((const zomlang::compiler::ast::Node*)0x{addr:x})->getKind()"
  kind_value = node_value.GetFrame().EvaluateExpression(expr)
  if not kind_value.IsValid():
    return ""
  kind_num = kind_value.GetValueAsUnsigned()
  return _get_enum_name(node_value.GetTarget(), "zomlang::compiler::ast::SyntaxKind", kind_num)


def _node_text_from_value(node_value):
  addr = _get_value_address(node_value)
  if addr == 0:
    return ""
  expr = f"((const zomlang::compiler::ast::Node*)0x{addr:x})->getSourceRange()"
  range_value = node_value.GetFrame().EvaluateExpression(expr)
  if not range_value.IsValid():
    return ""
  return _read_range_text(node_value.GetProcess(), range_value)


def _format_summary(kind_name, text):
  if kind_name == "" and text == "":
    return ""
  if text == "":
    return kind_name
  if kind_name == "":
    return text
  return f"{kind_name} \"{text}\""


def token_summary(valobj, internal_dict):
  kind_name = _token_kind_name_from_value(valobj)
  text = _token_text_from_token_value(valobj)
  return _format_summary(kind_name, text)


def lexer_summary(valobj, internal_dict):
  # Lexer uses Pimpl pattern, need to get impl pointer first
  impl = valobj.GetChildMemberWithName("impl")
  if not impl.IsValid():
    return ""

  ptr = impl.GetChildMemberWithName("ptr")
  if not ptr.IsValid() or ptr.GetValueAsUnsigned() == 0:
    return ""

  # Get state inside Lexer::Impl
  impl_value = ptr.Dereference()
  state = impl_value.GetChildMemberWithName("state")
  if not state.IsValid():
    return ""

  # LexerState contains current token
  token_value = state.GetChildMemberWithName("token")
  if not token_value.IsValid():
    return ""

  kind_name = _token_kind_name_from_value(token_value)
  text = _token_text_from_token_value(token_value)
  return _format_summary(kind_name, text)


def node_summary(valobj, internal_dict):
  kind_name = _node_kind_name_from_value(valobj)
  text = _node_text_from_value(valobj)
  return _format_summary(kind_name, text)


def own_ast_summary(valobj, internal_dict):
  ptr = valobj.GetChildMemberWithName("ptr")
  if not ptr.IsValid() or ptr.GetValueAsUnsigned() == 0:
    return ""
  kind_name = _node_kind_name_from_value(ptr)
  text = _node_text_from_value(ptr)
  return _format_summary(kind_name, text)


def zomkind(debugger, command, result, internal_dict):
  target = debugger.GetSelectedTarget()
  process = target.GetProcess()
  thread = process.GetSelectedThread()
  frame = thread.GetSelectedFrame()
  if not frame.IsValid():
    return
  value = _value_from_command_or_default(frame, command, "expression")
  if value is None:
    return
  node_value = _unwrap_node_value(value)
  kind_name = _node_kind_name_from_value(node_value)
  if kind_name == "":
    kind_name = _token_kind_name_from_value(node_value)
  result.PutCString(kind_name)


def zominfo(debugger, command, result, internal_dict):
  target = debugger.GetSelectedTarget()
  process = target.GetProcess()
  thread = process.GetSelectedThread()
  frame = thread.GetSelectedFrame()
  if not frame.IsValid():
    return
  value = _value_from_command_or_default(frame, command, "expression")
  if value is None:
    return
  node_value = _unwrap_node_value(value)
  kind_name = _node_kind_name_from_value(node_value)
  text = _node_text_from_value(node_value)
  if kind_name == "" and text == "":
    token_value = _deref_if_needed(node_value)
    kind_name = _token_kind_name_from_value(token_value)
    text = _token_text_from_token_value(token_value)
  result.PutCString(_format_summary(kind_name, text))


def __lldb_init_module(debugger, internal_dict):
  debugger.HandleCommand("type category define zomlang")
  debugger.HandleCommand(
      "type summary add -w zomlang -F zomlang_lldb.token_summary "
      '"zomlang::compiler::lexer::Token"')
  debugger.HandleCommand(
      "type summary add -w zomlang -F zomlang_lldb.lexer_summary "
      '"zomlang::compiler::lexer::Lexer"')
  debugger.HandleCommand(
      "type summary add -w zomlang -F zomlang_lldb.node_summary "
      '"zomlang::compiler::ast::Node"')
  debugger.HandleCommand(
      "type summary add -w zomlang -F zomlang_lldb.own_ast_summary "
      '-x "zc::Own<zomlang::compiler::ast::.*>"')
  debugger.HandleCommand("command script add -f zomlang_lldb.zomkind zomkind")
  debugger.HandleCommand("command script add -f zomlang_lldb.zominfo zominfo")
  debugger.HandleCommand("command alias zk zomkind")
  debugger.HandleCommand("command alias zi zominfo")
  debugger.HandleCommand("type category enable zomlang")
