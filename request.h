#pragma once

#include <iostream>
#include <vector>
#include <string>
#include <utility>

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


//pair of received block and size of actual header block
pair<string,int> recvheaders(int conn);

//parses the prefix of s of length sz
Request parseheaders(const string &s,int sz);
