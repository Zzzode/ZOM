# Concurrency

Zom provides modern concurrency features including async/await, actors, and structured concurrency.

### Async/Await

```zom
// Async function declaration
async fun fetchUserData(userId: i64) -> User {
    let response = await httpClient.get("/users/" + userId.toString());
    let userData = await response.json();
    return User.fromJson(userData);
}

// Calling async functions
async fun processUser(userId: i64) {
    let result = await fetchUserData(userId);
    match (result) {
        when Success(user) => print("User: " + user.name)
        when Failure(error) => print("Failed to fetch user: " + error.message)
    }
}
```

### Concurrent Execution

```zom
// Parallel execution
async fun fetchMultipleUsers(userIds: i64[]) -> User[] {
    let tasks = userIds.map(id => fetchUserData(id));
    return await Promise.all(tasks);
}

// Racing operations
async fun fetchWithTimeout(url: str, timeoutMs: i32) -> str {
    let fetchTask = httpClient.get(url);
    let timeoutTask = Timer.delay(timeoutMs).then(() => {
        throw TimeoutError("Request timed out");
    });

    return await Promise.race([fetchTask, timeoutTask]);
}
```

### Channels

```zom
// Channel for communication between async tasks
fun producerConsumerExample() {
    let channel = Channel<i32>(capacity: 10);

    // Producer
    async {
        for (let i = 0; i < 100; ++i) {
            await channel.send(i);
        }
        channel.close();
    };

    // Consumer
    async {
        while (let value = await channel.receive()) {
            print("Received: " + value.toString());
        }
    };
}
```

### Actors

```zom
actor Counter {
    private let value: i32 = 0;

    fun increment() -> i32 {
        this.value += 1;
        return this.value;
    }

    fun decrement() -> i32 {
        this.value -= 1;
        return this.value;
    }

    fun getValue() -> i32 {
        return this.value;
    }
}

// Usage
async fun useCounter() {
    let counter = Counter();

    // All method calls are automatically async
    let value1 = await counter.increment();
    let value2 = await counter.increment();
    let current = await counter.getValue();

    print("Current value: " + current.toString());
}
```

### Structured Concurrency

```zom
async fun structuredExample() {
    // All tasks in this scope will be cancelled if any fails
    await withTaskGroup { group in
        group.addTask {
            await longRunningTask1();
        };

        group.addTask {
            await longRunningTask2();
        };

        group.addTask {
            await longRunningTask3();
        };
    };

    print("All tasks completed");
}
```

### Thread Safety

```zom
// Atomic operations
class ThreadSafeCounter {
    private let value: Atomic<i32> = Atomic(0);

    fun increment() -> i32 {
        return this.value.fetchAdd(1) + 1;
    }

    fun decrement() -> i32 {
        return this.value.fetchSub(1) - 1;
    }

    fun getValue() -> i32 {
        return this.value.load();
    }
}

// Mutex for protecting critical sections
class BankAccount {
    private let balance: f64;
    private let mutex: Mutex = Mutex();

    fun withdraw(amount: f64) -> bool {
        return this.mutex.withLock {
            if (this.balance >= amount) {
                this.balance -= amount;
                return true;
            }
            return false;
        };
    }

    fun deposit(amount: f64) {
        this.mutex.withLock {
            this.balance += amount;
        };
    }
}
```
