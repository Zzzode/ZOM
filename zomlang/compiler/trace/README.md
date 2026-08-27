# ZOM Compiler Trace Module

## Overview

The ZOM compiler trace module provides an efficient, configurable program execution tracing system for debugging, performance analysis, and program behavior monitoring. This module is designed to be low-overhead, thread-safe, and supports both compile-time and runtime configuration.

## Features

- **Low Performance Overhead**: Can be completely disabled in release mode, minimal overhead in debug mode
- **Thread Safety**: Supports concurrent tracing in multi-threaded environments
- **Category Filtering**: Supports selective tracing by module categories
- **RAII Support**: Automatically manages scope entry and exit events
- **Flexible Configuration**: Supports environment variables and compile-time configuration
- **Extensible**: Easy to add new trace categories and event types

## Quick Start

### 1. Basic Usage

```cpp
#include "zomlang/compiler/trace/trace.h"

void myFunction() {
  // Trace entire function
  ZOM_TRACE_FUNCTION(trace::TraceCategory::kParser);

  // Trace specific events
  ZOM_TRACE_EVENT(trace::TraceCategory::kParser, "Processing token", "identifier");

  // Trace scope
  {
    ZOM_TRACE_SCOPE(trace::TraceCategory::kParser, "Inner scope");
    // Some processing logic
  }

  // Trace counter
  ZOM_TRACE_COUNTER(trace::TraceCategory::kPerformance, "tokens_processed", 42);
}
```

### 2. Initialization Configuration

```cpp
#include "zomlang/compiler/trace/trace-config.h"

void initializeTracing() {
  trace::TraceConfig config;

  // Read configuration from environment variables
  config.enabled = trace::RuntimeConfig::shouldEnableFromEnvironment();
  config.categoryMask = static_cast<trace::TraceCategory>(
      trace::RuntimeConfig::getCategoryMaskFromEnvironment());

  const char* outputFile = trace::RuntimeConfig::getOutputFileFromEnvironment();
  if (outputFile != nullptr) {
    config.outputFile = zc::StringPtr(outputFile);
  }

  // Apply configuration
  trace::TraceManager::getInstance().configure(config);
}
```

### 3. Complete Usage Example

```cpp
#include "zomlang/compiler/trace/trace.h"
#include "zomlang/compiler/trace/trace-config.h"

class Parser {
public:
  void parseModule() {
    ZOM_TRACE_FUNCTION(TraceCategory::kParser);

    ZOM_TRACE_EVENT(TraceCategory::kParser, "Module parsing started");

    // Parse module items
    while (hasMoreTokens()) {
      parseModuleItem();
    }

    ZOM_TRACE_COUNTER(TraceCategory::kPerformance, "total_modules", moduleCount);
  }

private:
  void parseModuleItem() {
    ZOM_TRACE_SCOPE(TraceCategory::kParser, "parseModuleItem");

    if (ZOM_TRACE_CATEGORY_ENABLED(TraceCategory::kParser)) {
      // Only execute expensive string construction when tracing is enabled
      zc::String tokenInfo = zc::str("Current token: ", getCurrentToken().text);
      ZOM_TRACE_EVENT(TraceCategory::kParser, "Processing token", tokenInfo);
    }

    // Parsing logic...
    {
      ZOM_TRACE_SCOPE(TraceCategory::kParser, "Type analysis");
      analyzeType();
    }
  }

  void analyzeType() {
    ZOM_TRACE_FUNCTION(TraceCategory::kChecker);
    // Type analysis logic...
  }
};

// Initialize in main function
int main() {
  // Initialize tracing system
  trace::TraceConfig config;
  config.enabled = trace::RuntimeConfig::shouldEnableFromEnvironment();
  config.categoryMask = static_cast<trace::TraceCategory>(
      trace::RuntimeConfig::getCategoryMaskFromEnvironment());

  const char* outputFile = trace::RuntimeConfig::getOutputFileFromEnvironment();
  if (outputFile != nullptr) {
    config.outputFile = zc::StringPtr(outputFile);
  }

  trace::TraceManager::getInstance().configure(config);

  // Run compiler
  Parser parser;
  parser.parseModule();

  // Output trace results
  trace::TraceManager::getInstance().flush();

  return 0;
}
```

## Configuration Options

### Environment Variables

| Variable Name | Description | Example Values |
|---------------|-------------|----------------|
| `ZOM_TRACE_ENABLED` | Enable/disable tracing | `1`, `true`, `on` |
| `ZOM_TRACE_CATEGORIES` | Specify trace categories | `parser,lexer,driver` |
| `ZOM_TRACE_OUTPUT` | Output file path | `/tmp/zom_trace.json` |

### Compile-time Configuration

```cmake
# Enable tracing (enabled by default in debug mode)
target_compile_definitions(your_target PRIVATE ZOM_TRACE_ENABLED=1)

# Set maximum call depth
target_compile_definitions(your_target PRIVATE ZOM_TRACE_MAX_DEPTH=500)

# Set event buffer size
target_compile_definitions(your_target PRIVATE ZOM_TRACE_BUFFER_SIZE=500000)
```

## Trace Categories

| Category | Description | Purpose |
|----------|-------------|----------|
| `kLexer` | Lexical analyzer | Trace token generation process |
| `kParser` | Syntax analyzer | Trace AST construction process |
| `kChecker` | Type checker | Trace type checking and semantic analysis |
| `kDriver` | Compilation driver | Trace compilation flow control |
| `kDiagnostics` | Diagnostic system | Trace error and warning handling |
| `kMemory` | Memory management | Trace memory allocation and deallocation |
| `kPerformance` | Performance monitoring | Trace performance-related metrics |
| `kAll` | All categories | Enable all tracing |

## Event Types

### TraceEventType Enumeration

- **kEnter**: Function or scope entry event
  - Automatically generated by `ZOM_TRACE_SCOPE` and `ZOM_TRACE_FUNCTION` during construction
  - Automatically increases the call depth of the current thread
- **kExit**: Function or scope exit event
  - Automatically generated by `ZOM_TRACE_SCOPE` and `ZOM_TRACE_FUNCTION` during destruction
  - Automatically decreases the call depth of the current thread
- **kInstant**: Instant event
  - Generated by `ZOM_TRACE_EVENT`, marks important moments in program execution
  - Does not affect call depth
- **kCounter**: Counter value event
  - Generated by `ZOM_TRACE_COUNTER`, used to record numeric metrics
  - Value is stored in the event's `details` field
- **kMetadata**: Metadata event
  - Reserved event type for recording configuration information, version information, etc.

### TraceEvent Structure

```cpp
struct TraceEvent {
  TraceEventType type;     // Event type
  TraceCategory category;  // Event category
  zc::String name;        // Event name
  zc::String details;     // Detailed information (optional)
  uint64_t timestamp;     // Timestamp (nanoseconds since epoch)
  uint32_t threadId;      // Thread ID (hash value)
  uint32_t depth;         // Call stack depth
};
```

**Field Descriptions**:

- `timestamp`: High-precision timestamp obtained using `std::chrono::high_resolution_clock`
- `threadId`: Calculated via `std::hash<std::thread::id>{}(std::this_thread::get_id())`
- `depth`: Current thread's function call depth, used to generate call stack view

## Macro Definitions

### ZOM_TRACE_CATEGORY_ENABLED(category)

**Function**: Check if tracing for the specified category is enabled

**Parameters**:

- `category`: `TraceCategory` enumeration value, specifies the trace category to check

**Return Value**: `bool` - Returns `true` if tracing for that category is enabled

**Example**:

```cpp
if (ZOM_TRACE_CATEGORY_ENABLED(TraceCategory::kParser)) {
  // Execute some expensive trace preparation work
}
```

### ZOM_TRACE_EVENT(category, name, ...)

**Function**: Record instant events to mark important moments in program execution

**Parameters**:

- `category`: `TraceCategory` enumeration value, the trace category the event belongs to
- `name`: `zc::StringPtr` or string literal, event name
- `...` (optional): `zc::StringPtr` or string literal, detailed description of the event

**Implementation Details**:

- Only executes when the corresponding category is enabled
- Creates a trace event of type `TraceEventType::kInstant`
- Automatically records timestamp, thread ID, and call depth

**Example**:

```cpp
ZOM_TRACE_EVENT(TraceCategory::kParser, "Token parsed", "identifier: myVar");
ZOM_TRACE_EVENT(TraceCategory::kLexer, "Lexing started");
```

### ZOM_TRACE_SCOPE(category, name, ...)

**Function**: Trace execution of specific scopes, using RAII to automatically manage entry and exit events

**Parameters**:

- `category`: `TraceCategory` enumeration value, the trace category the scope belongs to
- `name`: `zc::StringPtr` or string literal, scope name
- `...` (optional): `zc::StringPtr` or string literal, detailed description of the scope

**Implementation Details**:

- Creates a `ScopeTracer` object, utilizing RAII mechanism
- Records `TraceEventType::kEnter` event and increases call depth during construction
- Records `TraceEventType::kExit` event and decreases call depth during destruction
- Only activates when the corresponding category is enabled

**Example**:

```cpp
void parseExpression() {
  ZOM_TRACE_SCOPE(TraceCategory::kParser, "parseExpression", "binary operator");
  // Parsing logic...
} // Automatically records exit event
```

### ZOM_TRACE_FUNCTION(category)

**Function**: Trace execution of entire functions, a convenience macro for `ZOM_TRACE_SCOPE`

**Parameters**:

- `category`: `TraceCategory` enumeration value, the trace category the function belongs to

**Implementation Details**:

- Equivalent to `ZOM_TRACE_SCOPE(category, __FUNCTION__)`
- Uses the compiler's built-in `__FUNCTION__` macro as the function name
- Automatically records function entry and exit

**Example**:

```cpp
void lexNextToken() {
  ZOM_TRACE_FUNCTION(TraceCategory::kLexer);
  // Lexical analysis logic...
}
```

### ZOM_TRACE_COUNTER(category, name, value)

**Function**: Record counter values for performance monitoring and statistics collection

**Parameters**:

- `category`: `TraceCategory` enumeration value, the trace category the counter belongs to
- `name`: `zc::StringPtr` or string literal, counter name
- `value`: Any value convertible to string, current value of the counter

**Implementation Details**:

- Only executes when the corresponding category is enabled
- Creates a trace event of type `TraceEventType::kCounter`
- Uses `zc::str(value)` to convert the value to string and store it in the `details` field
- Automatically records timestamp, thread ID, and call depth

**Example**:

```cpp
ZOM_TRACE_COUNTER(TraceCategory::kPerformance, "tokens_processed", tokenCount);
ZOM_TRACE_COUNTER(TraceCategory::kMemory, "heap_usage_mb", heapSize / 1024 / 1024);
ZOM_TRACE_COUNTER(TraceCategory::kParser, "ast_nodes", nodeCount);
```

### Compile-time Optimization

When `ZOM_TRACE_ENABLED` is 0 (typically in release builds), all trace macros are defined as no-ops:

```cpp
#define ZOM_TRACE_EVENT(category, name, ...) do { } while (0)
#define ZOM_TRACE_SCOPE(category, name, ...) do { } while (0)
#define ZOM_TRACE_FUNCTION(category) do { } while (0)
#define ZOM_TRACE_COUNTER(category, name, value) do { } while (0)
```

This ensures completely zero performance overhead in production environments.

## TraceManager API

### Core Methods

```cpp
class TraceManager {
public:
  static TraceManager& getInstance();
  void configure(const TraceConfig& config);
  bool isEnabled(TraceCategory category) const;
  void addEvent(TraceEventType type, TraceCategory category,
                zc::StringPtr name, zc::StringPtr details = nullptr);
  void flush();
  uint32_t getCurrentDepth() const;
  void incrementDepth();
  void decrementDepth();
  void clear();
  size_t getEventCount() const;
};
```

**Method Descriptions**:

- **getInstance()**: Get singleton instance, thread-safe lazy loading
- **configure(config)**: Configure tracing system, including enabled state, category mask, maximum events, etc.
- **isEnabled(category)**: Check if specified category is enabled, used internally by macros
- **addEvent(...)**: Add trace event, automatically records timestamp, thread ID, and call depth
- **flush()**: Output events to configured file (current version outputs to debug log)
- **getCurrentDepth()**: Get current thread's call depth
- **incrementDepth()/decrementDepth()**: Manage call depth, automatically called by RAII classes
- **clear()**: Clear all events and thread depth information
- **getEventCount()**: Get current number of stored events

### TraceConfig Structure

```cpp
struct TraceConfig {
  bool enabled = false;                    // Whether tracing is enabled
  TraceCategory categoryMask = TraceCategory::kAll;  // Category mask
  size_t maxEvents = 1000000;             // Maximum number of events
  bool enableTimestamps = true;           // Whether to record timestamps
  bool enableThreadInfo = true;           // Whether to record thread information
  zc::StringPtr outputFile = nullptr;     // Output file path
};
```

**Field Descriptions**:

- `enabled`: Global switch, when `false` all tracing is disabled
- `categoryMask`: Bit mask controlling which category events are recorded
- `maxEvents`: Event buffer size, deletes oldest events when exceeded (FIFO)
- `enableTimestamps`: Controls whether to record high-precision timestamps
- `enableThreadInfo`: Controls whether to record thread ID information
- `outputFile`: Output file path, outputs to debug log when `nullptr`

## Performance Considerations

1. **Compile-time Optimization**: In release mode, all trace macros are optimized away by the compiler
2. **Runtime Checks**: Tracing logic only executes when corresponding categories are enabled
3. **Memory Management**: Uses circular buffer to avoid unlimited growth
4. **Thread Safety**: Uses efficient mutexes to protect shared data

## Best Practices

### 1. Performance Optimization Recommendations

```cpp
// ✅ Good practice: Use conditional checks to avoid expensive operations
if (ZOM_TRACE_CATEGORY_ENABLED(TraceCategory::kParser)) {
  zc::String expensiveDebugInfo = buildComplexDebugString();
  ZOM_TRACE_EVENT(TraceCategory::kParser, "Complex operation", expensiveDebugInfo);
}

// ❌ Avoid: Always executing expensive operations
zc::String expensiveDebugInfo = buildComplexDebugString();  // Executes even when tracing is disabled
ZOM_TRACE_EVENT(TraceCategory::kParser, "Complex operation", expensiveDebugInfo);
```

### 2. Reasonable Category Division

```cpp
// ✅ Divide by module
ZOM_TRACE_FUNCTION(TraceCategory::kLexer);     // Lexical analysis
ZOM_TRACE_FUNCTION(TraceCategory::kParser);    // Syntax analysis
ZOM_TRACE_FUNCTION(TraceCategory::kChecker);   // Type checking

// ✅ Divide by function
ZOM_TRACE_COUNTER(TraceCategory::kPerformance, "parse_time_ms", elapsedMs);
ZOM_TRACE_COUNTER(TraceCategory::kMemory, "heap_usage", currentHeapSize);
```

### 3. Event Naming Conventions

```cpp
// ✅ Clear event names
ZOM_TRACE_EVENT(TraceCategory::kParser, "parseExpression", "binary operator");
ZOM_TRACE_EVENT(TraceCategory::kLexer, "tokenizeString", "escape sequence found");

// ❌ Vague event names
ZOM_TRACE_EVENT(TraceCategory::kParser, "process", "something");
```

### 4. Memory Management

```cpp
// Periodically clean events to avoid excessive memory usage
void periodicCleanup() {
  auto& manager = trace::TraceManager::getInstance();
  if (manager.getEventCount() > 500000) {
    manager.flush();  // Output to file
    manager.clear();  // Clear events in memory
  }
}
```

## Advanced Usage

### 1. Custom Trace Category Combinations

```cpp
// Combine multiple categories
constexpr auto FRONTEND_CATEGORIES =
    static_cast<uint32_t>(TraceCategory::kLexer) |
    static_cast<uint32_t>(TraceCategory::kParser);

// Use in environment variables
// export ZOM_TRACE_CATEGORIES="lexer,parser"
```

### 2. Conditional Tracing

```cpp
class ConditionalTracer {
public:
  ConditionalTracer(bool condition, TraceCategory category, zc::StringPtr name)
      : active_(condition && ZOM_TRACE_CATEGORY_ENABLED(category)) {
    if (active_) {
      category_ = category;
      name_ = zc::str(name);
      trace::TraceManager::getInstance().addEvent(
          trace::TraceEventType::kEnter, category_, name_);
    }
  }

  ~ConditionalTracer() {
    if (active_) {
      trace::TraceManager::getInstance().addEvent(
          trace::TraceEventType::kExit, category_, name_);
    }
  }

private:
  bool active_;
  TraceCategory category_;
  zc::String name_;
};

// Usage example
void complexFunction(bool enableDetailedTrace) {
  ConditionalTracer tracer(enableDetailedTrace, TraceCategory::kParser, "complexFunction");
  // Function logic...
}
```

### 3. Performance Analysis Assistant

```cpp
class PerformanceTracker {
public:
  PerformanceTracker(TraceCategory category, zc::StringPtr operation)
      : category_(category), operation_(zc::str(operation)) {
    start_ = std::chrono::high_resolution_clock::now();
    ZOM_TRACE_EVENT(category_, "performance_start", operation_);
  }

  ~PerformanceTracker() {
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start_);

    ZOM_TRACE_COUNTER(category_, zc::str(operation_, "_duration_us"), duration.count());
  }

private:
  TraceCategory category_;
  zc::String operation_;
  std::chrono::high_resolution_clock::time_point start_;
};

#define ZOM_TRACE_PERFORMANCE(category, operation) \
  PerformanceTracker ZC_UNIQUE_NAME(_perf_tracker_)(category, operation)

// Usage example
void expensiveOperation() {
  ZOM_TRACE_PERFORMANCE(TraceCategory::kPerformance, "expensiveOperation");
  // Time-consuming operations...
}
```

## Debugging Infinite Loops

When the parser encounters infinite loops, you can use the trace module as follows:

```bash
# Enable parser tracing
export ZOM_TRACE_ENABLED=1
export ZOM_TRACE_CATEGORIES=parser
export ZOM_TRACE_OUTPUT=/tmp/parser_trace.json

# Run compiler
./zomc input.zom

# Analyze trace results
cat /tmp/parser_trace.json | grep -A5 -B5 "parseModuleItem"
```

## Extension Guide

### Adding New Trace Categories

1. Add new values to the `TraceCategory` enumeration in `trace.h`
2. Add parsing logic to `getCategoryMaskFromEnvironment()` in `trace-config.cc`
3. Use the new category in corresponding modules

### Adding New Event Types

1. Add new types to the `TraceEventType` enumeration
2. Add handling logic to `TraceManager::addEvent()`
3. Add corresponding convenience macros

## Troubleshooting

### Tracing Not Enabled

- Check `ZOM_TRACE_ENABLED` environment variable
- Confirm compile-time `ZOM_TRACE_ENABLED` macro definition
- Verify category mask settings

### Performance Issues

- Reduce trace categories
- Increase buffer size
- Disable tracing in release mode

### High Memory Usage

- Reduce `maxEvents` configuration
- Periodically call `flush()` and `clear()`
- Check for memory leaks

## Future Improvements

- [ ] Support Chrome Tracing format output
- [ ] Add real-time trace viewer
- [ ] Support remote tracing
- [ ] Add more performance metrics
- [ ] Support conditional tracing
- [ ] Integrate performance analysis tools
