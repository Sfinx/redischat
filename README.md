# simple redis multiuser chat 

requirements
------------

 - hiredis from https://github.com/redis/hiredis.git
 - redis++ from https://github.com/sewenew/redis-plus-plus.git

```
Redis chat app v1.0.0 production build at Wed 25 May 2022 11:08:08 AM EEST

help:

register <nickname>	- creates new user
login <nickname>	- login
list			- list rooms
users <room>		- list users in room
room <name>		- join room. will be created if not existed
send <file> <user>	- send file to user
play <file> <user>	- play file to user
Ctrl-C			- exit
```

usage
-----

```
make dbup
./redischat
```