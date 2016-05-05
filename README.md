# Hostsplit

This is a simple, bare-bones reverse proxy, written in C++. Don't use it for
your military-grade servers, and maybe also don't use it for your small
projects. Supposedly, it works, since I use it myself. At least, at the time
this was written. Which might be some time ago, if I don't update this.


## Config file

The config file format is really simple.

	#Hashtag in first column marks rest of line as comment

	#The listen key indicates what port to listen on
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

The file essentially lists regular expressions that the request should match.
The file should contain one `listen [PORT]` statement, and can contain multiple
`target [PORT] [NAME]` statements. Under a target, you can match on the HTTP
method, the path, and the version. You can also give requirements for specific
headers with `header [HEADERNAME] [REGEX]`. Header names are case-insensitive.
`^` and `$` are NOT automatically added, so if you want to only match the whole
string, add them yourself. Regexen are in C++ ECMA format, whatever that may
be.


## Compiling

Tested with Apple clang 7.3.0 and g++ 5.3.0. Will very probably work with
earlier versions, but they should fully support the C++11 regex library. In
particular, I've read you need at least g++ 4.9 to have that.
