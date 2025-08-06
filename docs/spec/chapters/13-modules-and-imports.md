# Modules and Imports

Zom organizes code into modules, providing namespace management and controlled visibility.

### Module Declaration

```zom
// File: math/geometry.zom
module math.geometry;

export struct Point {
    x: f64,
    y: f64
}

export fun distance(p1: Point, p2: Point) -> f64 {
    let dx = p1.x - p2.x;
    let dy = p1.y - p2.y;
    return sqrt(dx * dx + dy * dy);
}

// Private function (not exported)
fun sqrt(value: f64) -> f64 {
    // Implementation
    return value;
}
```

### Import Statements

```zom
// Import specific symbols
import math.geometry.Point;
import math.geometry.distance;

// Import with alias
import math.geometry.Point as GeoPoint;

// Import entire module
import math.geometry;

// Import all exported symbols
import math.geometry.*;

// Conditional imports
import platform.windows.* if TARGET_OS == "windows";
import platform.linux.* if TARGET_OS == "linux";
```

### Export Declarations

```zom
// Export individual declarations
export fun publicFunction() {
    // Implementation
}

export class PublicClass {
    // Implementation
}

// Export with alias
export { internalName as publicName };

// Re-export from other modules
export { Point, distance } from math.geometry;

// Export all from module
export * from utils.helpers;
```

### Module Visibility

```zom
module myapp.core;

// Public - visible to other modules
export class PublicAPI {
    public fun doSomething() {}
}

// Module-private - only visible within this module
class InternalHelper {
    fun helperMethod() {}
}

// Private - only visible within this file
private fun secretFunction() {
    // Implementation
}
```

### Nested Modules

```zom
// File: graphics/rendering/opengl.zom
module graphics.rendering.opengl;

export class OpenGLRenderer {
    // Implementation
}

// File: graphics/rendering/vulkan.zom
module graphics.rendering.vulkan;

export class VulkanRenderer {
    // Implementation
}

// File: graphics/mod.zom
module graphics;

export { OpenGLRenderer } from graphics.rendering.opengl;
export { VulkanRenderer } from graphics.rendering.vulkan;
```
