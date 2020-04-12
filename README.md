# automata
Playing around with NFAs and DFAs

TL;DR: my NFA simulator is pretty slow, but lowering to DFA and then simulating the DFA is much faster.  The DFA can be faster than `std::regex` (not definitive since I only tried one pattern plus `regex` handles more cases).

JITing the DFA leads to a pretty signficant speedup.

Sample output:

```
$ g++ -O3 -std=c++2a nfa.cc && ./a.out
--------------------------
Regex Tests
Regex as string: a(bb)+a
elapsed time: 750.658ms
332745
Regex as NFA:
State 0 (start)
    eps->1
State 1
    eps->2
State 2
    a  ->3
State 3
    eps->4
State 4
    eps->5
State 5
    eps->6
State 6
    eps->7
State 7
    b  ->8
State 8
    eps->9
State 9
    eps->10
State 10
    b  ->11
State 11
    eps->12
State 12
    eps->13
State 13
    eps->5
    eps->14
State 14
    eps->15
State 15
    eps->16
State 16
    a  ->17
State 17
    eps->18
State 18 (match)
elapsed time: 22714.7ms
332745
Regex as DFA:
State 0 (start)
    a  ->1
State 1
    b  ->2
State 2
    b  ->3
State 3
    b  ->2
    a  ->4
State 4 (match)
elapsed time: 157.699ms
JIT
elapsed time: 21.093ms
332745
--------------------------
Basic Tests
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
elapsed time: 3561.63ms
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
elapsed time: 47.34ms
312521
JIT
elapsed time: 12.973ms
312521
```
