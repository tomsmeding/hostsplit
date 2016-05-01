#include <cctype>
#include "util.h"

using namespace std;

string trim(string s){
	int L;
	for(L=0;L<(int)s.size();L++)if(!isspace(s[L]))break;
	if(L==(int)s.size())return string();
	int H;
	for(H=(int)s.size()-1;H>=L;H--)if(!isspace(s[H]))break;
	if(H<L)return string();
	return s.substr(L,H-L+1);
}

string stolower(string s){
	for(int i=0;i<(int)s.size();i++)s[i]=tolower(s[i]);
	return s;
}
