#include <cstring>
#include <sys/socket.h>
#include "request.h"
#include "error.h"
#include "util.h"
#include "const.h"

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


pair<string,int> recvheaders(int conn){
	const int bufsz=1024;
	char buf[bufsz];
	string res;
	int cursor=0;
	int state=0; //0=searching, 1=r, 2=r?n, 3=r?nr, 4=r?nr?n
	try {
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
	} catch(Error e){
		cout<<"Error, received up until now:"<<endl<<"<<<"<<res<<">>>"<<endl;
		throw e;
	}
#ifdef DEBUG
	cout<<res<<endl;
#endif
	return {res,cursor+1};
}

inline bool isws(char c){
	return c==' '||c=='\t';
}
inline bool islf(char c){
	return c=='\r'||c=='\n';
}

void skipws(const string &s,int sz,int &cursor){
	for(;cursor<sz;cursor++)if(!isws(s[cursor]))return;
	throw Error("No header parse: end of stream after skipws");
}
void skiptolf(const string &s,int sz,int &cursor){
	for(;cursor<sz;cursor++)if(islf(s[cursor]))return;
	throw Error("No header parse: end of stream after skiptolf");
}
void skipnowslf(const string &s,int sz,int &cursor){
	for(;cursor<sz;cursor++)if(isws(s[cursor])||islf(s[cursor]))return;
	throw Error("No header parse: end of stream after skipnowslf");
}
void skipneqnolf(const string &s,int sz,int &cursor,char c){
	for(;cursor<sz;cursor++)if(s[cursor]==c||islf(s[cursor]))break;
	if(cursor==sz)throw Error("No header parse: end of stream after skipneqnolf");
	if(s[cursor]!=c)throw Error(string("No header parse: char not found in skipneqnolf: ")+c);
}
void skipwslf(const string &s,int sz,int &cursor,bool allowend=false){
	for(;cursor<sz;cursor++)if(!isws(s[cursor]))break;
	if(cursor==sz)throw Error("No header parse: end of stream during skipwslf");
	if(islf(s[cursor])){
		cursor++;
		for(;cursor<sz;cursor++)if(!islf(s[cursor]))break;
		if(!allowend&&cursor==sz)throw Error("No header parse: end of stream after skipwslf");
	} else throw Error(string("No header parse: lf expected in skipwslf, got ")+s[cursor]);
}

Request parseheaders(const string &s,int sz){
	Request r;
	int cursor=0,start=0;
	try {
		skipnowslf(s,sz,cursor);
		if(cursor==start)throw Error("No header parse: empty method field");
		r.method=s.substr(0,cursor);

		skipws(s,sz,cursor);
		start=cursor;
		skipnowslf(s,sz,cursor);
		if(cursor==start)throw Error("No header parse: empty path field");
		r.path=s.substr(start,cursor-start);

		skipws(s,sz,cursor);
		start=cursor;
		skipnowslf(s,sz,cursor);
		if(cursor==start)throw Error("No header parse: empty version field");
		r.version=s.substr(start,cursor-start);
		skipwslf(s,sz,cursor,true);
		if(cursor==sz)return r; //no headers

		while(true){
			start=cursor;
			skipneqnolf(s,sz,cursor,':');
			if(cursor==start)throw Error("No header parse: empty header name");
			string name=s.substr(start,cursor-start);
			cursor++;
			skipws(s,sz,cursor);
			start=cursor;
			skiptolf(s,sz,cursor);
			if(cursor==start)throw Error("No header parse: empty header value");
			string value=s.substr(start,cursor-start);
			r.addheader(name,value);
			skipwslf(s,sz,cursor,true);
			if(cursor==sz)break;
		}
	} catch(Error e){
		cout<<"Error, request up until now:"<<endl<<r<<endl
		    <<"Source string:"<<endl<<"<<<"<<s.substr(0,sz)<<">>>"<<endl
		    <<"cursor="<<cursor<<" start="<<start<<endl;
		throw e;
	}
	return r;
}
