# automata
Playing around with NFAs and DFAs

Sample output:

```
 $ g++ -std=c++2a nfa.cc && ./a.out
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
elapsed time: 4188.16ms
155951
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
elapsed time: 185.094ms
155951
JIT
elapsed time: 183.974ms
155951
```