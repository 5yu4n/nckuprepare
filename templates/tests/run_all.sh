#!/bin/sh
# 在電腦上跑 templates 的所有測試：sh tests/run_all.sh（在 templates/ 資料夾裡執行）
set -e
T=$(mktemp -d)
gcc -O2 -Wall -Wextra -Ipico-app tests/test_fixed.c -o $T/fixed -lm && $T/fixed
gcc -O2 -pthread -Ipico-app tests/test_spsc.c -o $T/spsc && $T/spsc
gcc -O2 -Wall -Wextra pico-app/main.c -o $T/pmain && $T/pmain | tail -1
(cd linux-app && gcc -O3 -Wall -Wextra -pthread main.c -o $T/lmain) && $T/lmain | tail -1
rm -rf $T
echo "ALL TEMPLATE TESTS PASSED"
