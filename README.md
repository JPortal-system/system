# JPortal -- Enable Intel Processor Trace on JVM

​JPortal is a control flow tracing system that can precisely reconstruct the dynamic control flow of Java bytecode from traces collected by hardware — in particular, the Intel Processor Trace (Intel PT). 

## Contents

​	jdk12-06222165c35f:           the extended Openjdk12 where online information collection is added

​	trace:                                       used to trace cpu instructions

​    decode/td2dbc:                      used to decode hardware tracing data to dynamic bytecode sequences

​    decode/dbc2sbc:                   used to project dynamic bytecode sequences to control flows

​	README:                                 this file

## Dependencies

> cmake: the cross-platform open-source build system.            http://www.cmake.org
>
> libipt:    the Intel Processor Trace (Intel PT) Decoder Library. https://github.com/intel/libipt

Notes: libipt needs to be installed in system library by `make install` .

## Building on Ubuntu

​	This part describes how to build JPortal on Ubuntu. First you need to git clone JPortal and promote to top-level source directory.

### Build offline decode module

```bash
$ cd decode
$ mkdir build
$ cd build
$ cmake ..
$ make
```

### Build jdk

​	Before building extended openjdk, run `bash configure`to check dependencies. After all dependencies installed, start to make jdk.

```bash
$ cd jdk12-06222165c35f
$ bash configure --diable-warnings-as-errors --with-debug-level=slowdebug
$ make all
```

​	For convenience, you could add ` java ` to `PATH`.

```bash
$ export PATH=/path/to/java:$PATH
```

## How to run JPortal

​	This part describles how to run JPortal.

​	First, create a working directory and enter it. For example:

```bash
$ mkdir test
$ cd test
```

​	Load `msr` kernel module so that JPortal can read/write `msr` register during execution.

```bash
$ sudo modeprobe msr
```

### Online collection

​	This part describes how to collect trace info using JPortal.

​	1. Run the program that needed to be traced on the extended JVM to collect online info. JPortal has been intergreted into JVM. So just `java` something with these arguments:

| Argument          | Meaning                       |
| ----------------- | ----------------------------- |
| -XX:+JPortalTrace | Trace program with IPT        |
| -XX:+JPortalDump  | Dump debug info and JIT code |

​	For example, you could begin like this:

```bash
$ javac Main.java
$ sudo java -XX:+JPortalTrace -XX:+JPortalDump Main
```

​	Note that `java` must be run in root mode because JPortal needs to access `msr` register.

​	2. After runing, there are 4 types of files generated:

|      File Name      |                           Meaning                            |
| :-----------------: | :----------------------------------------------------------: |
|  JPortalDump.data   | Runtime info of JVM in binaries including:<br/>jitted code, template code and code IP. |
|  JPortalTrace.data  |      Tracing data of PT for each CPU core in binaries.       |

### Offline decoding

​	This part describes how to decode the trace collected to generate dynamic bytecode sequence according to files generated after online phase.

​	1. Create a file named `class-config` to record java classes of the running program. It could be like this:

```
[
	/path/to/your/java/classes
]
```

​	2. Then run `decode` to generate dynamic bytecode traces with the following arguments:

|    Argument    |             Meaning                 |
| :------------: | :-----------------------------:     |
| --class-config | Position of class-config file.      |
|  --trace-data  | Position of JPortalTrace.data file. |
|  --dump-data   | Position of JPortalDump.data file.  |

​	For example, you could `decode`  like this:

```bash
$ decode --class-config class-config --trace-data JPortalTrace.data --dump-data JPortalDump.data
```

​	After decoding, there are 2 types of files generated:

|    File name     |                           Meaning                            |
| :--------------: | :----------------------------------------------------------: |
|      [tid]       |         Matched bytecode sequence for each thread [tid].       |
|     methods      | Method signature & its index accroding to program class files. |
