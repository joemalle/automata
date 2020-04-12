// This is based on:
// https://swtch.com/~rsc/regexp/nfa.c.txt (and associated articles)
// https://condor.depaul.edu/glancast/444class/docs/nfa2dfa.html
// Wikipedia's NFA/DFA articles
//
// g++ -std=c++2a nfa.cc && ./a.out
#include <cassert>
#include <chrono>
#include <dlfcn.h>
#include <functional>
#include <fstream>
#include <iostream>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// Finite Automaton base class for code shared between NFA and DFA
template <typename Edge>
struct FABase {
    using StateRef = std::size_t;

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

protected:
    friend class JitFunction;
    std::vector<Edge> m_states;
    StateRef m_start = -1;
    std::unordered_set<StateRef> m_match;
};

// Deterministic Finite Automaton
struct DFA : FABase</*Edge*/std::unordered_map<char, std::size_t>> {
    using FABase</*Edge*/std::unordered_map<char, std::size_t>>::StateRef;
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
struct NFA : FABase</*Edge*/std::vector<std::pair<std::optional<char>, std::size_t>>> {
    using FABase</*Edge*/std::vector<std::pair<std::optional<char>, std::size_t>>>::StateRef;
    void addEdge(StateRef from, std::optional<char> cond, StateRef to) {
        m_states.at(from).push_back({cond, to});
    }

private:
    using sset = std::unordered_set<StateRef>;

    sset EpsilonReachable(sset const& ssetIn) const {
        sset ssetOut;
        std::function<void(StateRef)> recurse = [&](StateRef state) {
            if (ssetOut.count(state)) {
                return;
            }
            for (auto& edge : m_states.at(state)) {
                if (!edge.first) {
                    ssetOut.insert(edge.second);
                    recurse(edge.second);
                }
            }
        };

        for (auto state : ssetIn) {
            recurse(state);
        }
        return ssetOut;
    }
public:

    bool testMatch(std::string_view const sv) const {
        assert(!m_states.empty());
        assert(m_start != -1);
        assert(!m_match.empty());

        sset currentStates = {m_start};
        currentStates.merge(EpsilonReachable(currentStates));

        for (char c : sv) {
            sset nextStates;

            for (auto state : currentStates) {
                for (auto& edge : m_states.at(state)) {
                    if (edge.first && c == *edge.first) {
                        nextStates.insert(edge.second);
                    }
                }
            }

            nextStates.merge(EpsilonReachable(nextStates));
            currentStates = nextStates;
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
            std::size_t hash = 14695981039346656037u;
            for (auto n : vec) {
                hash ^= n;
                hash *= 1099511628211;
            }
            return hash;
        };

        std::unordered_map<sset, StateRef, decltype(hasher)> cache(m_states.size(), hasher);
        std::function<StateRef(sset)> recurse = [&](sset states) {
            states.merge(EpsilonReachable(states));
            if (cache.count(states)) {
                return cache.at(states);
            }

            auto newState = dfa.addState();
            cache.insert({states, newState});

            std::unordered_map<char, sset> map;

            for (auto state : states) {
                if (m_match.count(state)) {
                    dfa.addMatch(newState);
                }
                for (auto& edge : m_states.at(state)) {
                    if (edge.first) {
                        map[*edge.first].insert(edge.second);
                    }
                }
            }

            for (auto [c, cstates] : map) {
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
        m_filename = "jitfunc" + std::to_string((std::size_t)&dfa);
        {
            std::ofstream outs((m_filename + ".c").c_str());

            outs
            << "int jitted(char* c, int len) { char ch;";
            for (std::size_t i = 0; i < dfa.m_states.size(); ++i) {
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
        void* lib_handle = dlopen(std::string(m_filename + ".dylib").c_str(), RTLD_LOCAL|RTLD_LAZY);

        if (!lib_handle) {
            printf("[%s] Unable to load library: %s\n", __FILE__, dlerror());
            exit(EXIT_FAILURE);
        }

        m_jitted = (decltype(m_jitted))dlsym(lib_handle, "jitted");

        if (!m_jitted) {
            printf("[%s] Unable to get symbol: %s\n", __FILE__, dlerror());
            exit(EXIT_FAILURE);
        }
    }

    ~JitFunction() {
        std::system(std::string("rm -f " + m_filename + ".c " + m_filename + ".dylib").c_str());
    }

    bool operator()(std::string_view const sv) {
        assert(m_jitted);
        return m_jitted((char*)sv.data(), (int)sv.size());
    }

    int (*m_jitted)(char* c, int len);
    std::string m_filename;
};

template <typename Func>
int benchmark(Func func) {
    std::vector<std::string> cases = {
        "aba", "abb", "aa", "ab", "a",
        "aaa", "aab", "baa", "bba", "bbb", "ba", "bb", "b", "c",
        "blah blah blah", "abaracadabara"
    };

    std::vector<std::string> tests;
    srand(0);

    for (int i = 0; i < 1000000; ++i) {
        tests.push_back(cases.at(rand() % cases.size()));
    }

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


int main() {
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
    int nfa_count =benchmark([&](auto const& str) {
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
        return dfa.testMatch(str);
    });
    std::cout << jit_count << std::endl;
}

































