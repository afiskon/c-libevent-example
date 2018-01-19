# c-libevent-example

Simple libevent-based chat server.

How to build:

```
sudo apt-get install libevent-dev cmake3
mkdir build
cd build
cmake ..
make
```

How to test:

```
cd test
go build
./test 11.22.33.44 1337 1000
```

Expected output:

```
...
Test passed, 1000 connections handled!
```
