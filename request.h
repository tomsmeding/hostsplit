#pragma once

#include <iostream>
#include <vector>
#include <string>

using namespace std;


struct Header{
	string name,value;

	Header(const string &name,const string &value);
};

class Request{
public:
	string method,path,version;
	vector<Header> headers;

	void addheader(const string &name,const string &value);

	string getheader(const string &name) const;
};

ostream& operator<<(ostream &os,const Request &r);


string recvheaders(int conn);
Request parseheaders(const string &s);
