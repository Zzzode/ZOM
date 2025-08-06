# Memory Management

Zom provides automatic memory management with deterministic cleanup and manual control when needed.

### Automatic Memory Management

```zom
// Stack allocation for value types
struct Point {
    x: f64,
    y: f64
}

fun createPoint() -> Point {
    let p = Point(1.0, 2.0); // Allocated on stack
    return p; // Copied to caller
} // p is automatically cleaned up

// Heap allocation for reference types
class Node {
    value: i32,
    next: Node?
}

fun createList() -> Node {
    let head = Node(1, nil); // Allocated on heap
    head.next = Node(2, nil); // Reference counted
    return head; // Reference transferred to caller
} // Local reference cleaned up, heap objects remain
```

### Reference Counting

```zom
class Resource {
    private let name: str;

    init(name: str) {
        this.name = name;
        print("Resource " + name + " created");
    }

    deinit() {
        print("Resource " + this.name + " destroyed");
    }
}

fun useResource() {
    let resource = Resource("test"); // Reference count = 1
    let shared = resource; // Reference count = 2

    processResource(shared); // Reference passed to function

    // shared goes out of scope, reference count = 1
    // resource goes out of scope, reference count = 0
    // deinit() called automatically
}
```

### Weak References

```zom
class Parent {
    children: Child[],

    fun addChild(child: Child) {
        child.parent = weak this; // Weak reference to avoid cycles
        this.children.push(child);
    }
}

class Child {
    weak parent: Parent?, // Weak reference

    fun getParent() -> Parent? {
        return this.parent; // May return nil if parent was deallocated
    }
}
```

### Manual Memory Management

```zom
// Explicit allocation and deallocation
fun manualMemory() {
    let ptr = allocate<i32>(1000); // Allocate array of 1000 integers

    // Use the memory
    for (let i = 0; i < 1000; ++i) {
        ptr[i] = i * i;
    }

    // Must explicitly deallocate
    deallocate(ptr);
}

// RAII with custom cleanup
class FileHandle {
    private let fd: i32;

    init(filename: str) {
        this.fd = openFile(filename);
    }

    deinit() {
        closeFile(this.fd);
    }

    fun read(buffer: u8[]) -> i32 {
        return readFile(this.fd, buffer);
    }
}
```

### Memory Safety

```zom
// Compile-time checks prevent common errors
fun saftyExample() {
    let array = [1, 2, 3, 4, 5];

    // Bounds checking
    let value = array[10]; // Compile error: index out of bounds

    // Null safety
    let optional: str? = getString();
    let length = optional.length; // Compile error: optional not unwrapped
    let safeLength = optional?.length ?? 0; // Safe access

    // Use after free prevention
    let resource = createResource();
    resource.cleanup();
    resource.use(); // Compile error: use after cleanup
}
```
