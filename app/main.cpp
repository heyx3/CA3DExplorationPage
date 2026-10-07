#include "LevelGen.hpp"

#include <iostream>


int main()
{
    std::string warnings;
    CALevelGen::LayeredWolfram2D::Generator gen;
    CALevelGen::LayeredWolfram2D::State state{ gen, 128, 128, 0x5656, 0.4f, &warnings };

    if (warnings.empty())
        std::cerr << "No warnings!\n";
    else
        std::cerr << "WARNINGS: " << warnings << "\n";

    return 0;
}