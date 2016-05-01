#pragma once

#include <string>
#include <stdexcept>

using namespace std;

class Error : public exception{
	string whatstr;

public:
	Error(const string &s);

	const char* what() const noexcept;
};

class FilenameError : public exception{
	string whatstr;

public:
	FilenameError(const string &s);

	const char* what() const noexcept;
};
