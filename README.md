# dst

[![Build Status](https://travis-ci.org/bakpakin/dst.svg?branch=master)](https://travis-ci.org/bakpakin/dst)
[![Appveyor Status](https://ci.appveyor.com/api/projects/status/32r7s2skrgm9ubva?svg=true)](https://ci.appveyor.com/project/bakpakin/dst)

Dst is a functional and imperative programming language and bytecode interpreter. It is a
modern lisp, but lists are replaced
by other data structures with better utility and performance (arrays, tables, structs, tuples).
The language can also easily bridge to native code written in C, and supports abstract datatypes
for interfacing with C. Also support meta programming with macros, and bytecode assembly for the
dst abstract machine. The bytecode vm is a register based vm loosely inspired by the LuaJIT
bytecode format, but simpler and safer (bytecode can be verified by the assembler).

There is a repl for trying out the language, as well as the ability
to run script files. This client program is separate from the core runtime, so
dst could be embedded into other programs.

Implemented in mostly standard C99, dst runs on Windows, Linux and macOS.
The few features that are not standard C (dynamic library loading, compiler specific optimizations),
are fairly straight forward. Dst can be easily ported to new platforms.

There is not much in the way of documentation yet because it is still a "personal project" and
I don't want to freeze features prematurely. You can look in the examples directory, the test directory,
or the file `src/compiler/boot.dst` to get a sense of what dst code looks like.

For syntax highlighting, there is some preliminary vim syntax highlighting in [dst.vim](https://github.com/bakpakin/dst.vim).
Generic lisp syntax highlighting should provide good results, however.

## Features

* First class closures
* Garbage collection
* First class green threads (continuations)
* Mutable and immutable arrays (array/tuple)
* Mutable and immutable hashtables (table/struct)
* Mutable and immutable strings (buffer/string)
* Lisp Macros (Code is Data, Data is Code)
* Byte code interpreter with an assembly interface, as well as bytecode verification
* Proper tail calls.
* Direct interop with C via abstract types and C functions
* Dynamically load C libraries
* Functional and imperative standard library
* Lexical scoping
* Imperative Programming as well as functional
* REPL
* Interactive Environment

## Documentation

API documentation and design documents can be found in the
[wiki](https://github.com/bakpakin/dst/wiki).

## Usage

A repl is launched when the binary is invoked with no arguments. Pass the -h flag
to display the usage information. Individual scripts can be run with `./dst myscript.dst`

```
$ ./dst
Dst 0.0.0 alpha  Copyright (C) 2017-2018 Calvin Rose
> (+ 1 2 3)
6
> (print "Hello, World!")
Hello, World!
nil
> (exit)
$ ./dst -h
usage: ./dst [options] scripts...
Options are:
-h Show this help
-v Print the version string
-r Enter the repl after running all scripts
$
```

## Compiling and Running

Dst can be built with Make or CMake.
Use Make if you are on a posix system and don't like CMake.
Use CMake if you are on Windows or like CMake.

### Make

```sh
cd somewhere/my/projects/dst
make
make test
```

### CMake

On a posix system using make as the backend, compiling and running is as follows (this is the same as
most CMake based projects).

```sh
cd somewhere/my/projects/dst
mkdir -p build
cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make
make test
```

The repl can also be run with the CMake run target.
```sh
make run
```

## Examples

See the examples directory for some example dst code.
