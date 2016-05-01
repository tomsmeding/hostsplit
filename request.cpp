#include <cstring>
#include <sys/socket.h>
#include "request.h"
#include "error.h"
#include "util.h"

using namespace std;


Header::Header(const string &name,const string &value):name(name),value(value){}


void Request::addheader(const string &name,const string &value){
	string lcname=stolower(name);
	string lcvalue=stolower(value);
	for(int i=0;i<(int)headers.size();i++){
		if(headers[i].name==lcname){
			headers[i].value=lcvalue;
			return;
		}
	}
	headers.emplace_back(lcname,lcvalue);
}

string Request::getheader(const string &name) const {
	string lcname=stolower(name);
	for(const Header &h : headers){
		if(h.name==lcname)return h.value;
	}
	return string();
}

ostream& operator<<(ostream &os,const Request &r){
	os<<"method: <"<<r.method<<">  path: <"<<r.path<<">  version: <"<<r.version<<'>';
	for(const Header &h : r.headers){
		os<<"\nheader <"<<h.name<<"> = <"<<h.value<<'>';
	}
	return os;
}


string recvheaders(int conn){
	const int bufsz=1024;
	char buf[bufsz];
	string res;
	int cursor=0;
	int state=0; //0=searching, 1=r, 2=r?n, 3=r?nr, 4=r?nr?n
	while(true){
		int nrec=recv(conn,buf,bufsz-1,0);
		if(nrec==-1)throw Error(string("recv: ")+strerror(errno));
		if(nrec==0)throw Error("end of stream before double-lf");
		buf[nrec]='\0';
		res+=buf;
		for(;cursor<(int)res.size();cursor++){
			if(res[cursor]=='\r'){
				switch(state){
					case 0: state=1; break;
					case 1: state=0; break;
					case 2: state=3; break;
					case 3: state=0; break;
				}
			} else if(res[cursor]=='\n'){
				switch(state){
					case 0: state=2; break;
					case 1: state=2; break;
					case 2: state=4; break;
					case 3: state=4; break;
				}
			} else state=0;
			if(state==4)break;
		}
		if(state==4)break;
		if(res.size()>=102400)throw Error("Too large header block");
	}
	return res;
}

inline bool isws(char c){
	return c==' '||c=='\t';
}
inline bool islf(char c){
	return c=='\r'||c=='\n';
}

void skipws(const string &s,int &cursor){
	for(;cursor<(int)s.size();cursor++)if(!isws(s[cursor]))return;
	throw Error("No header parse: end of stream after skipws");
}
void skiptolf(const string &s,int &cursor){
	for(;cursor<(int)s.size();cursor++)if(islf(s[cursor]))return;
	throw Error("No header parse: end of stream after skiptolf");
}
void skipnowslf(const string &s,int &cursor){
	for(;cursor<(int)s.size();cursor++)if(isws(s[cursor])||islf(s[cursor]))return;
	throw Error("No header parse: end of stream after skipnowslf");
}
void skipneqnolf(const string &s,int &cursor,char c){
	for(;cursor<(int)s.size();cursor++)if(s[cursor]==c||islf(s[cursor]))break;
	if(cursor==(int)s.size())throw Error("No header parse: end of stream after skipneqnolf");
	if(s[cursor]!=c)throw Error(string("No header parse: char not found in skipneqnolf: ")+c);
}
void skipwslf(const string &s,int &cursor,bool allowend=false){
	for(;cursor<(int)s.size();cursor++)if(!isws(s[cursor]))break;
	if(cursor==(int)s.size())throw Error("No header parse: end of stream during skipwslf");
	if(islf(s[cursor])){
		cursor++;
		for(;cursor<(int)s.size();cursor++)if(!islf(s[cursor]))break;
		if(!allowend&&cursor==(int)s.size())throw Error("No header parse: end of stream after skipwslf");
	} else throw Error(string("No header parse: lf expected in skipwslf, got ")+s[cursor]);
}

Request parseheaders(const string &s){
	Request r;
	int cursor=0,start=0;
	skipnowslf(s,cursor);
	if(cursor==start)throw Error("No header parse: empty method field");
	r.method=s.substr(0,cursor);

	skipws(s,cursor);
	start=cursor;
	skipnowslf(s,cursor);
	if(cursor==start)throw Error("No header parse: empty path field");
	r.path=s.substr(start,cursor-start);
	
	skipws(s,cursor);
	start=cursor;
	skipnowslf(s,cursor);
	if(cursor==start)throw Error("No header parse: empty version field");
	r.version=s.substr(start,cursor-start);
	skipwslf(s,cursor,true);
	if(cursor==(int)s.size())return r; //no headers

	while(true){
		start=cursor;
		skipneqnolf(s,cursor,':');
		if(cursor==start)throw Error("No header parse: empty header name");
		string name=s.substr(start,cursor-start);
		cursor++;
		skipws(s,cursor);
		start=cursor;
		skiptolf(s,cursor);
		if(cursor==start)throw Error("No header parse: empty header value");
		string value=s.substr(start,cursor-start);
		r.addheader(name,value);
		skipwslf(s,cursor,true);
		if(cursor==(int)s.size())break;
	}
	return r;
}
