#include <iostream>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <errno.h>
#include "socketwrapper.h"
#include "error.h"
#include "const.h"

using namespace std;


Socketwrapper::Socketwrapper(int sock):datablock(new Datablock{1,sock}){
	if(sock<0)throw Error("Negative socket value passed to Socketwrapper constructor");
}
Socketwrapper::Socketwrapper(const Socketwrapper &other):datablock(other.datablock){
	datablock->refcount++;
}
Socketwrapper::Socketwrapper(Socketwrapper &&other):datablock(other.datablock){
	other.datablock=new Datablock{1,-1};
}
Socketwrapper::~Socketwrapper(){
	datablock->refcount--;
	if(datablock->refcount==0){
		if(datablock->sock!=-1)close(datablock->sock);
		delete datablock;
	} else if(datablock->refcount<0){
		cout<<"Negative reference count on Socketwrapper "<<(void*)this<<", datablock "<<(void*)datablock<<" (refcount="<<datablock->refcount<<", sock="<<datablock->sock<<')'<<endl;
		terminate();
	}
}

Socketwrapper::operator int() const {
	if(datablock->sock<0){
		cout<<"Socketwrapper with negative socket unwrapped!"<<endl;
		terminate();
	}
	return datablock->sock;
}

void Socketwrapper::sendall(const string &s){
	sendall(s.data(),s.size());
}
void Socketwrapper::sendall(const char *buf,const int len){
	int cursor=0;
	do {
		int nwr=send(datablock->sock,buf+cursor,len-cursor,0);
		if(nwr==-1)throw Error(string("Socketwrapper sendall: send: ")+strerror(errno));
		if(nwr==0)throw Error("Socketwrapper sendall: send returned 0");
		cursor+=nwr;
	} while(cursor<len);
}
