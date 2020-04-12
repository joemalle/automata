# automata
Playing around with NFAs and DFAs

Sample output:

```
$ g++ -O3 -std=c++2a nfa.cc && ./a.out
NFA
State 0 (start)
    eps->1
    a  ->2
State 1
    a  ->3
    a  ->4
State 2
    b  ->3
State 3
    a  ->4
    b  ->4
State 4 (match)
elapsed time: 2704.7ms
312521
DFA
State 0 (start)
    a  ->1
State 1 (match)
    b  ->3
    a  ->2
State 2 (match)
State 3 (match)
    a  ->2
    b  ->2
elapsed time: 42.83ms
312521
JIT
elapsed time: 41.926ms
312521
```