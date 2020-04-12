// This is based on:
// https://swtch.com/~rsc/regexp/nfa.c.txt (and associated articles)
// https://condor.depaul.edu/glancast/444class/docs/nfa2dfa.html
// Wikipedia's NFA/DFA articles
//
// g++ -std=c++2a nfa.cc && ./a.out
#include <cassert>
#include <chrono>
#include <dlfcn.h>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <optional>
#include <regex>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// Finite Automaton base class for code shared between NFA and DFA
template <typename Edge>
struct FABase {
    using StateRef = int;

    StateRef addState() {
        m_states.emplace_back();
        return m_states.size() - 1;
    }

    void setStart(StateRef start) {
        m_start = start;
    }

    void addMatch(StateRef match) {
        assert(m_match.insert(match).second);
    }

private:
    static char printHelper(std::optional<char> const& o) {
        return *o;
    }
    static char printHelper(char const& c) {
        return c;
    }
    
public:
    void print() const {
        for (size_t i = 0; i < m_states.size(); ++i) {
            std::cout << "State " << i;
            if (i == m_start) {
                std::cout << " (start)";
            }
            if (m_match.count(i)) {
                std::cout << " (match)";
            }
            std::cout << std::endl;
            for (auto& edge : m_states.at(i)) {
                std::cout << "    ";
                
                if (edge.first) {
                    std::cout << printHelper(edge.first) << "  ";
                } else {
                    std::cout << "eps";
                }
                
                std::cout << "->" << edge.second << std::endl;
            }
        }
    }

    std::vector<Edge> m_states;
    StateRef m_start = -1;
    std::unordered_set<StateRef> m_match;
};

// Deterministic Finite Automaton
struct DFA : FABase</*Edge*/std::map<char, int>> {
    using FABase</*Edge*/std::map<char, int>>::StateRef;
    void addEdge(StateRef from, char cond, StateRef to) {
        assert(m_states.at(from).insert({cond, to}).second && "duplicate edge");
    }

    bool testMatch(std::string_view const sv) const {
        assert(!m_states.empty());
        assert(m_start != -1);
        assert(!m_match.empty());

        StateRef state = m_start;

        for (char c : sv) {
            auto& edges = m_states.at(state);
            auto it = edges.find(c);
            if (it == edges.end()) {
                return false;
            } else {
                state = it->second;
            }
        }

        return m_match.count(state);
    }
};

//  Nondeterministic Finite Automaton
struct NFA : FABase</*Edge*/std::vector<std::pair<std::optional<char>, int>>> {
    using FABase</*Edge*/std::vector<std::pair<std::optional<char>, int>>>::StateRef;
    void addEdge(StateRef from, std::optional<char> cond, StateRef to) {
        m_states.at(from).push_back({cond, to});
    }

private:
    using sset = std::set<StateRef>;

    // add all edges reachable by following epsilons
    void FollowEpsilons(sset& stateset) const {
        std::function<void(StateRef)> recurse = [&](StateRef state) {
            if (stateset.count(state)) {
                return;
            }

            stateset.insert(state);

            for (auto& edge : m_states.at(state)) {
                if (!edge.first) {
                    recurse(edge.second);
                }
            }
        };

        auto copy = std::move(stateset);
        stateset.clear();
        for (auto state : copy) {
            recurse(state);
        }
    }
public:

    bool testMatch(std::string_view const sv) const {
        assert(!m_states.empty());
        assert(m_start != -1);
        assert(!m_match.empty());

        sset nextStates;
        sset currentStates = {m_start};
        FollowEpsilons(currentStates);


        for (char c : sv) {
            for (auto state : currentStates) {
                for (auto& edge : m_states.at(state)) {
                    if (edge.first && c == *edge.first) {
                        nextStates.insert(edge.second);
                    }
                }
            }

            FollowEpsilons(nextStates);
            std::swap(nextStates, currentStates);
            nextStates.clear();
        }

        for (auto state : currentStates) {
            if (m_match.count(state)) {
                return true;
            }
        }

        return false;
    }

    DFA lower() const {
        DFA dfa;

        auto hasher = [](sset const& s) {
            std::vector<StateRef> vec(s.begin(), s.end());
            std::sort(vec.begin(), vec.end());
            // https://en.wikipedia.org/wiki/Fowler–Noll–Vo_hash_function
            int hash = 2166136261;
            for (auto n : vec) {
                hash ^= n;
                hash *= 16777619;
            }
            return hash;
        };

        std::unordered_map<sset, StateRef, decltype(hasher)> cache(m_states.size(), hasher);
        std::function<StateRef(sset)> recurse = [&](sset states) {
            FollowEpsilons(states);
            if (cache.count(states)) {
                return cache.at(states);
            }

            auto newState = dfa.addState();
            cache.insert({states, newState});

            std::unordered_map<char, sset> newEdges;

            for (auto state : states) {
                if (m_match.count(state)) {
                    dfa.addMatch(newState);
                }
                for (auto& edge : m_states.at(state)) {
                    if (edge.first) {
                        newEdges[*edge.first].insert(edge.second);
                    }
                }
            }

            for (auto [c, cstates] : newEdges) {
                dfa.addEdge(newState, c, recurse(cstates));
            }

            return newState;
        };

        dfa.setStart(recurse({m_start}));
        
        return dfa;
    }
};



// JIT the DFA! WOMM
struct JitFunction {
    JitFunction(DFA const& dfa) {
        m_filename = "jitfunc" + std::to_string((std::uintptr_t)&dfa);
        {
            std::ofstream outs((m_filename + ".c").c_str());

            outs
            << "int jitted(char* c, int len) { char ch;";
            for (int i = 0; i < dfa.m_states.size(); ++i) {
                outs
                << std::endl
                << "state" << i << ":"
                << "if (!len) { return " << dfa.m_match.count(i) << "; }"
                << "ch = *c; ++c; --len;";

                for (auto& edge : dfa.m_states.at(i)) {
                    outs << "if (ch == '" << edge.first << "') goto state" << edge.second << ";";
                }

                outs << "return 0;";
            }
            outs << "}" << std::endl;
        }

        std::system(std::string("gcc -O3 -dynamiclib -undefined suppress -flat_namespace " + m_filename + ".c -o " + m_filename + ".dylib").c_str());

        // https://developer.apple.com/library/archive/documentation/DeveloperTools/Conceptual/DynamicLibraries/100-Articles/UsingDynamicLibraries.html
        m_lib_handle = dlopen(std::string(m_filename + ".dylib").c_str(), RTLD_LOCAL|RTLD_LAZY);

        if (!m_lib_handle) {
            printf("[%s] Unable to load library: %s\n", __FILE__, dlerror());
            exit(EXIT_FAILURE);
        }

        m_jitted = (decltype(m_jitted))dlsym(m_lib_handle, "jitted");

        if (!m_jitted) {
            printf("[%s] Unable to get symbol: %s\n", __FILE__, dlerror());
            exit(EXIT_FAILURE);
        }
    }

    ~JitFunction() {
        if (dlclose(m_lib_handle) != 0) {
            printf("[%s] Problem closing library: %s", __FILE__, dlerror());
        }
        std::system(std::string("rm -f " + m_filename + ".c " + m_filename + ".dylib").c_str());
    }

    bool operator()(std::string_view const sv) {
        assert(m_jitted);
        return m_jitted((char*)sv.data(), (int)sv.size());
    }

    int (*m_jitted)(char* c, int len);
    std::string m_filename;
    void* m_lib_handle;
};


struct Benchmark {
    std::vector<std::string> tests;

    Benchmark(std::vector<std::string> cases) {
        srand(0);
        for (int i = 0; i < 1000000; ++i) {
            tests.push_back(cases.at(rand() % cases.size()));
        }
    }

    template <typename Func>
    int operator()(Func func) const {
        struct TimedScope {
            TimedScope() {
                start = std::chrono::steady_clock::now();
            }

            ~TimedScope() {
                auto stop = std::chrono::steady_clock::now();
                std::cout << "elapsed time: " << std::chrono::duration<double, std::milli>(stop - start).count() << "ms" << std::endl;
            }
            std::chrono::time_point<std::chrono::steady_clock> start;
        };

        int count = 0;
        {
            TimedScope timer;
            for (auto& test : tests) {
                count += func(test);
            }
        }
        return count;
    }
};

// Helpers to make regex/NFA from parser

struct Char {
    Char(char c) : c(c) {};
    char c;

    std::string toStr() const {
        return std::string({c});
    }

    NFA toNFA() const {
        NFA nfa;

        auto start = nfa.addState();
        nfa.setStart(start);

        auto match = nfa.addState();
        nfa.addMatch(match);

        nfa.addEdge(start, c, match);

        return nfa;
    }
};

// insert src into dst at dstref.  Creates a state that indicates a match of src in dst, and returns a ref.
NFA::StateRef merge(NFA& dst, NFA::StateRef dstref, NFA&& src) {
    // this map could just be addition but this is fine
    std::unordered_map</*src*/ NFA::StateRef, /*dst*/NFA::StateRef> newEdges;
    for (NFA::StateRef i = 0; i < src.m_states.size(); ++i) {
        newEdges[i] = dst.addState();
    }

    for (NFA::StateRef i = 0; i < src.m_states.size(); ++i) {
        for (auto& edge : src.m_states.at(i)) {
            dst.addEdge(newEdges.at(i), edge.first, newEdges.at(edge.second));
        }
    }

    // map dstRef to the start node
    dst.addEdge(dstref, std::nullopt, newEdges.at(src.m_start));

    // map the matching nodes to one node
    auto matchsrc = dst.addState();

    for (auto srcstate : src.m_match) {
        dst.addEdge(newEdges.at(srcstate), std::nullopt, matchsrc);
    }

    return matchsrc;
}

template <typename A, typename B>
struct And {
    And(A a, B b) : a(a), b(b) {}
    A a;
    B b;

    std::string toStr() const {
        return a.toStr() + b.toStr();
    }

    NFA toNFA() const {
        NFA nfa;

        auto start = nfa.addState();
        nfa.setStart(start);

        auto mid = merge(nfa, start, a.toNFA());
        auto match = merge(nfa, mid, b.toNFA());

        nfa.addMatch(match);

        return nfa;
    }
};

template <typename A, typename B>
struct Or {
    Or(A a, B b) : a(a), b(b) {}
    A a;
    B b;

    std::string toStr() const {
        return "(" + a.toStr() + ")|(" + b.toStr() + ")";
    }

    NFA toNFA() const {
        NFA nfa;

        auto start = nfa.addState();
        nfa.setStart(start);

        auto matcha = merge(nfa, start, a.toNFA());
        auto matchb = merge(nfa, start, b.toNFA());

        nfa.addMatch(matcha);
        nfa.addMatch(matchb);

        return nfa;
    }
};

template <typename A>
struct Maybe {
    Maybe(A a) : a(a) {}
    A a;

    std::string toStr() const {
        return "(" + a.toStr() + ")?";
    }

    NFA toNFA() const {
        NFA nfa;

        auto start = nfa.addState();
        nfa.setStart(start);

        auto matchit = merge(nfa, start, a.toNFA());

        nfa.addMatch(matchit);
        nfa.addEdge(start, std::nullopt, matchit);

        return nfa;
    }
};

template <typename A>
struct OneOrMore {
    OneOrMore(A a) : a(a) {}
    A a;

    std::string toStr() const {
        return "(" + a.toStr() + ")+";
    }

    NFA toNFA() const {
        NFA nfa;

        auto start = nfa.addState();
        nfa.setStart(start);

        auto matchit = merge(nfa, start, a.toNFA());

        nfa.addMatch(matchit);
        nfa.addEdge(matchit, std::nullopt, start);

        return nfa;
    }
};

bool basicTests() {
    std::cout << "--------------------------" << std::endl;
    std::cout << "Basic Tests" << std::endl;

    Benchmark benchmark({
        "aba", "abb", "aa", "ab", "a",
        "aaa", "aab", "baa", "bba", "bbb", "ba", "bb", "b", "c",
        "blah blah blah", "abaracadabara"
    });

    NFA nfa;

    auto s1 = nfa.addState();
    auto s2 = nfa.addState();
    auto s3 = nfa.addState();
    auto s4 = nfa.addState();
    auto s5 = nfa.addState();

    nfa.addEdge(s1, std::nullopt, s2);
    nfa.addEdge(s1, 'a', s3);
    nfa.addEdge(s2, 'a', s4);
    nfa.addEdge(s2, 'a', s5);
    nfa.addEdge(s3, 'b', s4);
    nfa.addEdge(s4, 'a', s5);
    nfa.addEdge(s4, 'b', s5);

    nfa.setStart(s1);
    nfa.addMatch(s5);

    assert(nfa.testMatch("a"));
    assert(nfa.testMatch("ab"));
    assert(nfa.testMatch("abb"));
    assert(!nfa.testMatch("c"));
    assert(!nfa.testMatch("abbb"));

    std::cout << "NFA" << std::endl;
    nfa.print();
    int nfa_count = benchmark([&](auto const& str) {
        return nfa.testMatch(str);
    });
    std::cout << nfa_count << std::endl;

    DFA dfa = nfa.lower();

    assert(dfa.testMatch("a"));
    assert(dfa.testMatch("ab"));
    assert(dfa.testMatch("abb"));
    assert(!dfa.testMatch("c"));
    assert(!dfa.testMatch("abbb"));

    std::cout << "DFA" << std::endl;
    dfa.print();
    int dfa_count = benchmark([&](auto const& str) {
        return dfa.testMatch(str);
    });
    std::cout << dfa_count << std::endl;

    JitFunction jfn(dfa);

    assert(jfn("a"));
    assert(jfn("ab"));
    assert(jfn("abb"));
    assert(!jfn("c"));
    assert(!jfn("abbb"));

    std::cout << "JIT" << std::endl;
    int jit_count = benchmark([&](auto const& str) {
        return jfn(str);
    });
    std::cout << jit_count << std::endl;

    return jit_count == dfa_count && dfa_count == nfa_count;
}

bool regexTests() {
    // a(bb)+a (the same example as the article linked at the top of this file)

    std::cout << "--------------------------" << std::endl;
    std::cout << "Regex Tests" << std::endl;
    
    Benchmark benchmark({
        "aa", "aba", "abba", "abbba", "abbbba", "abbbbbbbbbbbbbbbbbbbba", "abbbbbbbbbbbbbbbbbba"
        "blah blah blah", "abaracadabara", "crapola"
    });

    auto parser = And(And(Char('a') , OneOrMore(And(Char('b'), Char('b')))), Char('a'));

    auto str = parser.toStr();
    std::cout << "Regex as string: " << str << std::endl;

    const std::regex stl_regex(str);
    
    int stl_count = benchmark([&](auto const& str) {
        return std::regex_match(str, stl_regex);
    });
    std::cout << stl_count << std::endl;

    auto nfa = parser.toNFA();

    assert(!nfa.testMatch("aa"));
    assert(!nfa.testMatch("aba"));
    assert(nfa.testMatch("abba"));
    assert(!nfa.testMatch("abbba"));
    assert(nfa.testMatch("abbbba"));

    std::cout << "Regex as NFA:" << std::endl;
    // You can see in this printout that this NFA has a lot of unnecessary epsilon transitions
    nfa.print();
    int nfa_count = benchmark([&](auto const& str) {
        return nfa.testMatch(str);
    });
    std::cout << nfa_count << std::endl;

    auto dfa = nfa.lower();

    assert(!dfa.testMatch("aa"));
    assert(!dfa.testMatch("aba"));
    assert(dfa.testMatch("abba"));
    assert(!dfa.testMatch("abbba"));
    assert(dfa.testMatch("abbbba"));


    std::cout << "Regex as DFA:" << std::endl;
    dfa.print();
    int dfa_count = benchmark([&](auto const& str) {
        return dfa.testMatch(str);
    });
    
    JitFunction jfn(dfa);


    assert(!jfn("aa"));
    assert(!jfn("aba"));
    assert(jfn("abba"));
    assert(!jfn("abbba"));
    assert(jfn("abbbba"));

    std::cout << "JIT" << std::endl;
    int jit_count = benchmark([&](auto const& str) {
        return jfn(str);
    });
    std::cout << jit_count << std::endl;

    return jit_count == dfa_count && dfa_count == nfa_count && nfa_count == stl_count;
}

int main() {
    assert(basicTests());
    assert(regexTests());
}































