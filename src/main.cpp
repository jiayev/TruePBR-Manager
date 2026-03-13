#include "app/Application.h"

// Hint NVIDIA / AMD switchable-graphics drivers to prefer the discrete GPU.
extern "C"
{
    __declspec(dllexport) unsigned long NvOptimusEnablement = 1;
    __declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}

int main(int argc, char* argv[])
{
    tpbr::Application app(argc, argv);
    return app.run();
}
