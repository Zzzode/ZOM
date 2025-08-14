# ZC Library

## Overview

The `zc` library is a selectively ported and adapted version of the `kj` library from
the [Cap'n Proto project](https://github.com/capnproto/capnproto.git). The original `kj` library, located in the
`c++/src/kj` directory, has been partially ported to fit the specific needs of this project. This library retains the core
functionalities while making necessary adaptations for better integration with the ZOM project.

**ZC is Modern C++'s missing base library.** It provides a comprehensive set of utilities and abstractions that make C++ development more productive and safer, designed from the ground up for Modern C++ (C++11 and later).

### Key Features

- **Core Utilities**: RAII memory management, smart pointers (`zc::Own`, `zc::Array`), optional types (`zc::Maybe`), variant types (`zc::OneOf`)
- **String Handling**: UTF-8 aware string types (`zc::String`, `zc::StringPtr`) with convenient formatting
- **Containers**: Modern containers like `zc::Vector`, `zc::HashMap`, `zc::TreeMap` designed for C++20
- **Async Programming**: Event loop framework with Promise API for asynchronous operations
- **I/O and Networking**: Cross-platform I/O, filesystem operations, HTTP client/server, TLS support
- **Parsing**: URL parsing, JSON handling, parser combinator framework
- **Testing**: Lightweight unit testing framework (`ztest`) compatible with Google Test
- **And much more**: Threading, encoding/decoding, command-line parsing, etc.

### Core Modules

- **`core`**: Fundamental data structures, memory management, strings, I/O, threading, and debugging utilities
- **`async`**: Cross-platform coroutines and async I/O with epoll/kqueue/IOCP support
- **`http`**: HTTP/URL parsing and client/server tools
- **`parse`**: Character processing and general parsing utilities
- **`tls`**: OpenSSL-based TLS wrapper with async handshake support
- **`zip`**: Compression algorithms (gzip, brotli, etc.)
- **`ztest`**: Lightweight unit testing framework

## Documentation

- **[Getting Started Guide](doc/index.md)**: Introduction to KJ library and its philosophy
- **[Library Tour](doc/tour.md)**: Comprehensive tour through KJ's functionality with examples
- **[Style Guide](doc/style-guide.md)**: Coding conventions and design patterns used in KJ

## Acknowledgments

I would like to extend my heartfelt gratitude and admiration to the `kj` library team for their exceptional work. The `kj` library is a testament to their expertise and dedication, and it has had a significant impact on the C++ community.

Their contributions have provided a solid foundation upon which this project is built.

## License Information

The original `kj` library is licensed under the MIT License, and I have retained the original license as is.

Additionally, this project is licensed under the Apache 2.0 License. Below, you will find both the original MIT License from the `kj` library and the Apache 2.0 License for this project.

### MIT License (Original `kj` Library)

```
Copyright (c) 2013-2017 Sandstorm Development Group, Inc.; Cloudflare, Inc.;
and other contributors. Each commit is copyright by its respective author or
author's employer.

Licensed under the MIT License:

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.

```

### Apache 2.0 License (ZOM Project)

```
Copyright (c) 2024-2025 Zode.Z. All rights reserved

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
License for the specific language governing permissions and limitations under
the License.
```

## Philosophy

ZC follows several key design principles:

- **RAII Everywhere**: Resource management through constructors and destructors
- **Value vs Resource Types**: Clear distinction between copyable values and unique resources
- **Exception Safety**: Exceptions for error handling with proper cleanup guarantees
- **Modern C++**: Designed for C++20 with move semantics and smart pointers
- **No Standard Library Dependency**: Self-contained with minimal external dependencies

## Conclusion

The `zc` library aims to leverage the robust and efficient design of the `kj` library while tailoring it to meet the specific requirements of the ZOM project. It provides a modern, safe, and efficient foundation for C++ development.

For detailed usage examples and API documentation, please refer to the documentation files in the `doc/` directory.

For any questions or contributions, please feel free to contact me or submit a pull request.
