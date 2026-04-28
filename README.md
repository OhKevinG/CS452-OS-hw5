# HW5 Driversity
- Author: Kevin Gutierrez
- Class: CS452
- Semester: Spring 2026

## Overview
A basic device driver for a scanner that splits a sequence of characters into tokens.
The split is determined by the following set of separators:
```
' ', '\t', '\n', ':'
```

These are space, tab, newline and colon.

## Building
To build the program run:
```
cd Scanner
make
```
To load the scanner run:
```
sudo make install
```

To unload the scanner run:
```
sudo make uninstall
```

## Testing
After building and loading, to run the test suite:
```
chmod +x run_tests.sh
./run_tests.sh
```

## Command log - results in transcript.txt
```
make
sudo make install
make TryScanner

printf 'a:b:c\n' | ./TryScanner 
./TryScanner < /etc/passwd

```