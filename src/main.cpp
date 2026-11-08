#include "App.h"
#include <exception>
#include <iostream>
using namesapce std;

int main() 
{
    try {
        App app;
        return app.run();
    } catch (const std::exception& e) {
        cerr << "Fatal error: " << e.what() << "\n";
        return 1;
    } catch (...) {
        cerr << "Fatal error: unknown exception.\n";
        return 1;
    }
}
