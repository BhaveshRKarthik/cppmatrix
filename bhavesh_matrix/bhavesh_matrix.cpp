// bhavesh_matrix.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include <iostream>
#include <string>
#include <vector>
#include "bhavesh_matrix_v1.h"

using bhavesh::matrix;

consteval auto f() {
    auto m1 = matrix<float>{ 2, 2, {
        {1, 2},
        {3, 4}
    } };
    matrix<int> m2 = (m1.operator*=(1.1));
    auto m3 = (m1.operator*(m2)).transpose_inplace();
    std::array<std::array<float, 2>, 2> ans{
        m3[0][0], m3[0][1],
        m3[1][0], m3[1][1]
    };
    return ans;
}

constexpr static auto v = f();

int main() {
    matrix<int> m1 = matrix<int>(1, 2, { 1, 2 });
    matrix<int> m2 = matrix<int>(1, 2, { 1, 2 });
    auto m3 = std::move(m1).operator+(std::move(m2));
}

// Run program: Ctrl + F5 or Debug > Start Without Debugging menu
// Debug program: F5 or Debug > Start Debugging menu

// Tips for Getting Started: 
//   1. Use the Solution Explorer window to add/manage files
//   2. Use the Team Explorer window to connect to source control
//   3. Use the Output window to see build output and other messages
//   4. Use the Error List window to view errors
//   5. Go to Project > Add New Item to create new code files, or Project > Add Existing Item to add existing code files to the project
//   6. In the future, to open this project again, go to File > Open > Project and select the .sln file
