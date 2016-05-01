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
#include "socketwrapper.h"
#include "request.h"
#include "error.h"

#define MAX_LISTEN (128)

using namespace std;


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
