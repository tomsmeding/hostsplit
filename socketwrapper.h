#pragma once

#include <string>

using namespace std;


class Socketwrapper{
	struct Datablock{
		int refcount;
		int sock;
	};

	Datablock *datablock;

public:
	Socketwrapper(int sock);
	Socketwrapper(const Socketwrapper &other);
	Socketwrapper(Socketwrapper &&other);
	~Socketwrapper();

	operator int() const;

	void sendall(const string &s);
	void sendall(const char *buf,const int len);
};
