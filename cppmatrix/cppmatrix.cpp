// cppmatrix.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include <iostream>
#define BHAVESH_NO_DEDUCE_DEBUG
#include "cppmatrix.h"
#include <string>
#include <ranges>
#include <vector>
using namespace bhavesh;
using namespace bhavesh::detail;
using namespace std;
using t = bhavesh::iterators::matrix_colwise_iterable<int>;
constexpr bool x = std::ranges::view<t>;
int main()
{
	partial_alloc<int> v(3);
	v.emplace_back();
	v.emplace_back();
	v.emplace_back();
	auto p = v.release();
	cout << *p;
	bhavesh::detail::deallocate(p, 3);
	cin.get();
	return 0;
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
