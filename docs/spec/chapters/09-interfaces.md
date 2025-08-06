# Interfaces

Interfaces define contracts that types can implement, enabling polymorphism and code reuse.

### Basic Interface

```zom
interface Drawable {
    fun draw();
    fun getBounds() -> Rectangle;
}

interface Movable {
    fun move(deltaX: f64, deltaY: f64);
    fun getPosition() -> Point;
}
```

### Interface Implementation

```zom
class Button implements Drawable, Movable {
    private let position: Point;
    private let size: Size;
    private let text: str;

    public init(position: Point, size: Size, text: str) {
        this.position = position;
        this.size = size;
        this.text = text;
    }

    // Implementing Drawable
    public fun draw() {
        // Draw button implementation
        print("Drawing button: " + this.text);
    }

    public fun getBounds() -> Rectangle {
        return Rectangle(this.position, this.size);
    }

    // Implementing Movable
    public fun move(deltaX: f64, deltaY: f64) {
        this.position.x += deltaX;
        this.position.y += deltaY;
    }

    public fun getPosition() -> Point {
        return this.position;
    }
}
```

### Generic Interfaces

```zom
interface Container<T> {
    fun add(item: T);
    fun remove(item: T) -> bool;
    fun contains(item: T) -> bool;
    fun size() -> i32;
    fun isEmpty() -> bool;
    fun clear();
}

interface Iterator<T> {
    fun hasNext() -> bool;
    fun next() -> T?;
}

interface Iterable<T> {
    fun iterator() -> Iterator<T>;
}
```

### Interface Inheritance

```zom
interface ReadableStream {
    fun read(buffer: u8[], offset: i32, length: i32) -> i32;
    fun close();
}

interface WritableStream {
    fun write(buffer: u8[], offset: i32, length: i32) -> i32;
    fun flush();
    fun close();
}

interface ReadWriteStream extends ReadableStream, WritableStream {
    fun seek(position: i64);
    fun getPosition() -> i64;
}
```

### Associated Types

```zom
interface Collection<T> {
    type Iterator: Iterator<T>;

    fun iterator() -> Iterator;
    fun size() -> i32;
}

class ArrayList<T> implements Collection<T> {
    type Iterator = ArrayListIterator<T>;

    private let items: T[];

    public fun iterator() -> ArrayListIterator<T> {
        return ArrayListIterator(this.items);
    }

    public fun size() -> i32 {
        return this.items.length;
    }
}
```
