#!/bin/sh
echo "=== 1"
printf 'a:b:c\n' | ./TryScanner
echo "=== 2"
head -1 /etc/passwd | ./TryScanner