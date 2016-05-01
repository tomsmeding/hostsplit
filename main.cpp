#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <utility>
#include <regex>
#include <functional>
#include <thread>
#include <stdexcept>
#include <system_error>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <sys/select.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include "util.h"

#define MAX_LISTEN (128)

using namespace std;


#define define_exception_type(name)         \
	class name : public exception{          \
		string whatstr;                     \
	public:                                 \
		name(const string &s):whatstr(s){}  \
                                            \
		const char* what() const noexcept { \
			return whatstr.data();          \
		}                                   \
	}//

define_exception_type(Error);
define_exception_type(FilenameError);

#undef define_exception_type


class Socketwrapper{
	struct Datablock{
		int refcount;
		int sock;
	};

	Datablock *datablock;

public:
	Socketwrapper(int sock):datablock(new Datablock{1,sock}){
		if(sock<0)throw Error("Negative socket value passed to Socketwrapper constructor");
	}
	Socketwrapper(const Socketwrapper &other):datablock(other.datablock){
		datablock->refcount++;
	}
	Socketwrapper(Socketwrapper &&other):datablock(other.datablock){
		other.datablock=new Datablock{1,-1};
	}
	~Socketwrapper(){
		datablock->refcount--;
		if(datablock->refcount==0){
			if(datablock->sock!=-1)close(datablock->sock);
			delete datablock;
		} else if(datablock->refcount<0){
			cout<<"Negative reference count on Socketwrapper "<<(void*)this<<", datablock "<<(void*)datablock<<" (refcount="<<datablock->refcount<<", sock="<<datablock->sock<<')'<<endl;
			terminate();
		}
	}

	operator int(){
		if(datablock->sock<0){
			cout<<"Socketwrapper with negative socket unwrapped!"<<endl;
			terminate();
		}
		return datablock->sock;
	}

	void sendall(const string &s){
		sendall(s.data(),s.size());
	}
	void sendall(const char *buf,const int len){
		int cursor=0;
		do {
			int nwr=send(datablock->sock,buf+cursor,len-cursor,0);
			if(nwr==-1)throw Error(string("Socketwrapper sendall: send: ")+strerror(errno));
			if(nwr==0)throw Error("Socketwrapper sendall: send returned 0");
			cursor+=nwr;
		} while(cursor<len);
	}
};

struct Header{
	string name,value;

	Header(const string &name,const string &value):name(name),value(value){}
};

class Request{
public:
	string method,path,version;
	vector<Header> headers;

	void addheader(const string &name,const string &value){
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

	string getheader(const string &name) const {
		string lcname=stolower(name);
		for(const Header &h : headers){
			if(h.name==lcname)return h.value;
		}
		return string();
	}
};

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


void socketpipe(Socketwrapper from,Socketwrapper to){
	const int bufsz=1024;
	char buf[bufsz];
	fd_set readset;
	FD_ZERO(&readset);
	const int nfds=max((int)from,(int)to)+1;
	while(true){
		FD_SET(from,&readset);
		FD_SET(to,&readset);
		int nready=select(nfds,&readset,nullptr,nullptr,nullptr);
		if(nready==-1){
			if(errno!=EINTR)throw Error(string("select: ")+strerror(errno));
			cout<<"select returned EINTR"<<endl;
			continue;
		}
		if(nready==0){
			cout<<"select returned 0"<<endl;
			continue;
		}
		int f,t;
		if(FD_ISSET(from,&readset)){f=from; t=to;}
		else if(FD_ISSET(to,&readset)){f=to; t=from;}
		else {
			cout<<"select returned no file descriptors!"<<endl;
			break;
		}
		int nrec=recv(f,buf,bufsz,0);
		if(nrec==-1)throw Error(string("recv error in socketpipe: ")+strerror(errno));
		if(nrec==0)break; //EOF
		(t==to?to:from).sendall(buf,nrec);
	}
}

class Target{
public:
	string name;
	int destport;
	regex methodreg,pathreg,versionreg;
	vector<pair<string,regex>> headerregs;

	Target(string name="",int destport=80)
		:name(name),destport(destport),methodreg(""),pathreg(""),versionreg(""){}

	void addheaderreg(const string &name,const string &regstr){
		headerregs.emplace_back(name,regex(regstr));
	}

	bool match(const Request &r) const {
		if(!regex_search(r.method,methodreg)||
		   !regex_search(r.path,pathreg)||
		   !regex_search(r.version,versionreg))return false;
		for(const pair<string,regex> &p : headerregs){
			if(!regex_search(r.getheader(p.first),p.second))return false;
		}
		return true;
	}

	void forwardpipe(const string &buf,Socketwrapper &&conn){
		if(destport==-1)throw Error("No destport set for target "+to_string((intptr_t)this)+", name "+name);
		char portbuf[6];
		snprintf(portbuf,6,"%d",destport&0xffff);

		struct addrinfo hints,*res;
		memset(&hints,0,sizeof(hints));
		hints.ai_family=AF_UNSPEC;
		hints.ai_socktype=SOCK_STREAM;
		int ret=getaddrinfo("127.0.0.1",portbuf,&hints,&res);
		if(ret!=0)throw Error(string("getaddrinfo: ")+gai_strerror(ret));

		Socketwrapper tconn(socket(res->ai_family,res->ai_socktype,res->ai_protocol));
		if(tconn==-1)throw Error(string("socket: ")+strerror(errno));
		if(connect(tconn,res->ai_addr,res->ai_addrlen)==-1){
			throw Error(string("connect: ")+strerror(errno));
		}
		tconn.sendall(buf);
		socketpipe(conn,tconn);
	}
};

/*
Config file format:

#Hashtag in first column marks rest of line as comment
#The bind key indicates what port to listen on
listen 80
#Every target specification should start with a target key, where the first
# argument indicates the destination port, and the second argument is a name
target 80 target_name
#Then can follow the following keys: (values are regular expressions, ecma
# flavour; all are optional. When all omitted, every request goes here.)
method GET
path ^/[^?]*$
#Apparently we don't want query strings
version HTCPCP/1.0
#Header names (not values!) are plain strings, but are case-insensitive
header host coffee.com
header content-length 0
*/
class Config{
	string filename;

public:
	int listenport;
	vector<Target> targets;

	Config() = default;
	Config(const string &filename){
		reload(filename);
	}

	void reload(const string &newfname){
		filename=newfname;
		reload();
	}
	void reload(void){
		ifstream f(filename);
		if(!f)throw FilenameError("Cannot open file '"+filename+"'");
		string line;
		listenport=-1;
		for(int lineidx=1;f;lineidx++){
			getline(f,line);
			if(line.size()==0)continue;
			if(line[0]=='#')continue;
			size_t idx=line.find(' ');
			if(idx==string::npos)throw Error("No space found on line "+to_string(lineidx));
			string key=line.substr(0,idx);
			idx=line.find_first_not_of(" ",idx);
			string value=idx==string::npos?string():line.substr(idx);
			try {
				if(key=="listen"){
					const char *startp=&value.front();
					char *endp;
					int v=strtol(startp,&endp,10);
					if(endp!=startp+value.size()){
						throw Error("Invalid number value on line "+to_string(lineidx));
					}
					if(listenport==-1)listenport=v;
					else throw Error("Listen port already specified before line "+to_string(lineidx));
				} else if(key=="target"){
					idx=value.find(' ');
					if(idx==string::npos){
						throw Error("No second space found on line "+to_string(lineidx));
					}
					size_t idx2=value.find_first_not_of(" ",idx);
					const char *startp=&value.front();
					char *endp;
					int destport=strtol(startp,&endp,10);
					if(endp!=startp+idx){
						throw Error("Invalid number value on line "+to_string(lineidx));
					}
					targets.emplace_back(value.substr(idx2),destport);
				} else if(targets.size()==0){
					throw Error("'target' key expected before line "+to_string(lineidx));
				} else if(key=="method")targets.back().methodreg=regex(value);
				else if(key=="path")targets.back().pathreg=regex(value);
				else if(key=="version")targets.back().versionreg=regex(value);
				else if(key=="header"){
					idx=value.find(' ');
					if(idx==string::npos){
						throw Error("No second space found on line "+to_string(lineidx));
					}
					size_t idx2=value.find_first_not_of(" ",idx);
					targets.back().addheaderreg(value.substr(0,idx),value.substr(idx2));
				} else throw Error("Unrecognised key on line "+to_string(lineidx));
			} catch(regex_error e){
				throw Error("Regex error on line "+to_string(lineidx)+": "+e.what()+" (code "+to_string(e.code())+")");
			}
		}
		if(listenport==-1){
			throw Error("No listen port specified!");
		}
		f.close();
	}
};


void handleaccept(Config config,Socketwrapper conn){
	try {
		string buf=recvheaders(conn);
		Request r=parseheaders(buf);
		bool success=false;
		for(Target &t : config.targets){
			if(t.match(r)){
				cout<<"Forwarding to target "<<t.name<<endl;
				t.forwardpipe(buf,move(conn));
				success=true;
				break;
			}
		}
		if(!success)cout<<"No matching target for request!"<<endl;
	} catch(Error e){
		cout<<"Error: "<<e.what()<<endl;
	}
}

void handleacceptasync(const Config &config,Socketwrapper conn){
	try {
		thread th(handleaccept,config,conn);
		th.detach();
	} catch(system_error e){
		throw Error(string("Cannot spawn thread to handleaccept! e = ")+e.what());
	}
}


bool shouldreloadconfig=false;

void signalhandler(int sig){
	if(sig!=SIGUSR1)return;
	cout<<"Queueing config reload"<<endl;
	shouldreloadconfig=true;
}

int main(int argc,char **argv){
	if(argc!=2){
		cout<<"Pass config file name as command-line argument."<<endl;
		return 1;
	}
	signal(SIGUSR1,signalhandler);
	Config config;
	try {
		config.reload(argv[1]);
		cout<<"Config file "<<argv[1]<<" read, "<<config.targets.size()<<" targets found ";
		cout<<'(';
		bool first=true;
		for(const Target &t : config.targets){
			if(first)first=false;
			else cout<<',';
			cout<<t.name;
		}
		cout<<')'<<endl;
	} catch(FilenameError e){
		cout<<"Filename error: "<<e.what()<<endl;
		return 1;
	} catch(Error e){
		cout<<"Error: "<<e.what()<<endl;
		return 1;
	}

	struct sockaddr_in name;
	int sock=socket(AF_INET,SOCK_STREAM,0);
	int i=1;
	if(setsockopt(sock,SOL_SOCKET,SO_REUSEADDR,&i,sizeof i)==-1){
		perror("setsockopt");
		return 1;
	}
	name.sin_family=AF_INET;
	name.sin_addr.s_addr=htonl(INADDR_ANY);
	name.sin_port=htons(config.listenport);
	if(::bind(sock,(struct sockaddr*)&name,sizeof name)==-1){
		perror("bind");
		return 1;
	}
	if(listen(sock,MAX_LISTEN)==-1){
		perror("listen");
		return 1;
	}
	cout<<"Bound on port "<<config.listenport<<'.'<<endl;

	while(true){
		fd_set readset;
		FD_SET(sock,&readset);
		struct timeval timeout;
		timeout.tv_sec=2;
		timeout.tv_usec=0;
		int ret=select(sock+1,&readset,nullptr,nullptr,&timeout);
		if(ret==-1&&errno!=EINTR){
			perror("select");
			return 1;
		}
		if(ret<=0||!FD_ISSET(sock,&readset)){
			if(shouldreloadconfig){
				shouldreloadconfig=false;
				config.reload();
				cout<<"Reloaded config"<<endl;
			}
			continue;
		}
		Socketwrapper conn(accept(sock,NULL,NULL));
		if(conn==-1){
			perror("accept");
			continue;
		}
		cout<<"Accept"<<endl;
		handleacceptasync(config,conn);
	}
}
