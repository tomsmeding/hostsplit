#include "error.h"
#include "const.h"


Error::Error(const string &s):whatstr(s){}

const char* Error::what() const noexcept {
	return whatstr.data();
}

FilenameError::FilenameError(const string &s):whatstr(s){}

const char* FilenameError::what() const noexcept {
	return whatstr.data();
}
