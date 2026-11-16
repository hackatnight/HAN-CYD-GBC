Runs the play-history code (`#define HIST_PATH` ... in `src/main.cpp`) on a PC:

    python3 -c "s=open('../../src/main.cpp').read(); a=s.index('#define HIST_PATH'); open('hist.inc','w').write(s[a:s.index('static void listDir() {', a)])"
    g++ -std=c++17 -o t test.cpp && ./t
