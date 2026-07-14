# Classes and Structures

## Classes

Classes are reference types that support inheritance, polymorphism, and encapsulation.
A class or struct member is instance-bound only when its parameter clause
explicitly declares `this`. The member body may resolve a `this` expression
only to that declared receiver parameter.

### Class Definition

```zom
class Vehicle {
    protected let make: str;
    protected let model: str;
    protected let year: i32;
    private let vin: str;

    public init(this, make: str, model: str, year: i32, vin: str) {
        this.make = make;
        this.model = model;
        this.year = year;
        this.vin = vin;
    }

    public fun getInfo(this) -> str {
        return this.year.toString() + " " + this.make + " " + this.model;
    }

    public fun start(this) {
        print("Starting " + this.getInfo());
    }

    protected fun getVin(this) -> str {
        return this.vin;
    }
}
```

### Inheritance

```zom
class Car : Vehicle {
    private let doors: i32;
    private let fuelType: str;

    public init(this, make: str, model: str, year: i32, vin: str, doors: i32, fuelType: str) {
        super(make, model, year, vin);
        this.doors = doors;
        this.fuelType = fuelType;
    }

    override public fun start(this) {
        print("Turning key...");
        super.start();
    }

    public fun getDoors(this) -> i32 {
        return this.doors;
    }
}
```

### Abstract Classes

```zom
abstract class Shape {
    protected let color: str;

    public init(this, color: str) {
        this.color = color;
    }

    // Abstract method - must be implemented by subclasses
    abstract public fun area(this) -> f64;
    abstract public fun perimeter(this) -> f64;

    // Concrete method
    public fun getColor(this) -> str {
        return this.color;
    }

    public fun describe(this) -> str {
        return "A " + this.color + " shape with area " + this.area().toString();
    }
}

class Circle : Shape {
    private let radius: f64;

    public init(this, color: str, radius: f64) {
        super(color);
        this.radius = radius;
    }

    override public fun area(this) -> f64 {
        return 3.14159 * this.radius * this.radius;
    }

    override public fun perimeter(this) -> f64 {
        return 2.0 * 3.14159 * this.radius;
    }
}
```

### Properties

```zom
class Rectangle {
    private mut _width: f64;
    private mut _height: f64;

    public init(this, width: f64, height: f64) {
        this._width = width;
        this._height = height;
    }

    // Getter property
    public get width(this) -> f64 {
        return this._width;
    }

    // Setter property
    public set width(this, value: f64) {
        if (value > 0) {
            this._width = value;
        }
    }

    // Read-only computed property
    public get area(this) -> f64 {
        return this._width * this._height;
    }

    // Property with both getter and setter
    public get height(this) -> f64 {
        return this._height;
    }

    public set height(this, value: f64) {
        if (value > 0) {
            this._height = value;
        }
    }
}
```

## Structures

Structures are value types that are copied when assigned or passed as parameters.

### Basic Struct

```zom
struct Point3D {
    x: f64,
    y: f64,
    z: f64
}

// Usage
let origin = Point3D(0.0, 0.0, 0.0);
let point = Point3D(x: 1.0, y: 2.0, z: 3.0);
```

### Struct with Methods

```zom
struct Vector3D {
    x: f64,
    y: f64,
    z: f64,

    fun length(this) -> f64 {
        return sqrt(this.x * this.x + this.y * this.y + this.z * this.z);
    }

    fun normalize(this) -> Vector3D {
        let len = this.length();
        return Vector3D(this.x / len, this.y / len, this.z / len);
    }

    fun dot(this, other: Vector3D) -> f64 {
        return this.x * other.x + this.y * other.y + this.z * other.z;
    }

    fun cross(this, other: Vector3D) -> Vector3D {
        return Vector3D(
            this.y * other.z - this.z * other.y,
            this.z * other.x - this.x * other.z,
            this.x * other.y - this.y * other.x
        );
    }
}
```

### Struct Derived-Value Methods

```zom
struct Circle {
    radius: f64,

    fun area(this) -> f64 {
        return 3.14159 * this.radius * this.radius;
    }

    fun circumference(this) -> f64 {
        return 2.0 * 3.14159 * this.radius;
    }

    fun diameter(this) -> f64 {
        return 2.0 * this.radius;
    }
}
```

### Mutable Structs

```zom
struct Counter {
    mut value: i32,

    fun increment(this) {
        this.value += 1;
    }

    fun decrement(this) {
        this.value -= 1;
    }

    fun reset(this) {
        this.value = 0;
    }
}
```
