
include .env
export

RELEASE=1

COMMON_FLAGS	= -fPIC -fPIE -Werror
DEBUG		= -gdwarf-5 -flto -pipe -fstack-protector-all -fstack-clash-protection -fstack-protector-strong -fcf-protection=full
ifeq ($(RELEASE),0)
$(info [ Debug app ])
DEBUG           += -DAPP_DEBUG -D_GLIBCXX_ASSERTIONS -D_GLIBCXX_VERBOSE_ASSERT -fsanitize=undefined -fsanitize=address -fsanitize=leak -fvar-tracking-assignments # LSAN_OPTIONS ASAN_OPTIONS="help=1"
else
$(info [ Release app ])
DEBUG		+= -O3 -D_FORTIFY_SOURCE=2 -DNDEBUG
endif
#STATIC		= -static
DEFINES 	= -D__USE_REENTRANT -D_REENTRANT -D_THREAD_SAFE -D_LIBC_REENTRANT -D_GNU_SOURCE
INCLUDES	=
CFLAGS		= -Qn -Wall $(COMMON_FLAGS) $(DEFINES) $(INCLUDES) $(DEBUG) -Werror=format-security -ffunction-sections -fdata-sections
CPPFLAGS        = $(COMMONCPP_CFLAGS) $(CFLAGS) -std=gnu++20
BACKTRACE_LIBS	+= -lbfd
ifeq ($(RELEASE),0)
BACKTRACE_LIBS  += -lubsan -lasan
endif
LIBS		= -L/usr/local/lib -lhiredis -lredis++ -lreadline -lhistory $(BACKTRACE_LIBS) -ldl -lpthread
# ld
LD_LTO		= -fwhole-program -flto=8 -Wl,--unresolved-symbols=report-all -Wl,--warn-common
# gold
#LD_LTO		= -fwhole-program -flto=8 -fuse-ld=gold -Wl,--threads -Wl,--unresolved-symbols=report-all -Wl,--warn-common -Wl,--warn-execstack
LDFLAGS		= $(STATIC) $(DEBUG) -Wl,-O6 -Wl,--start-group $(LIBS) -Wl,--end-group -Wl,--gc-sections $(LD_LTO)

# 16 digits
#   YYYYMMDDHHMMSS
# 0020150218125222
BUILD_TYPE := 00
BUILD_TIMESTAMP := $(shell date +%Y%m%d%H%M%S)
COMMIT_ID := $(shell git describe --abbrev=24 --always)
SECLDFLAGS	+= -Wl,-z,relro,-z,now,-z,defs -pie
LDFLAGS         += $(SECLDFLAGS) -Wl,--build-id=0x$(COMMIT_ID)$(BUILD_TYPE)$(BUILD_TIMESTAMP)

TARGET		= redischat
DEVICE		= b

OBJS = main.o console.o

all:	do-it-all

ifeq (.depend,$(wildcard .depend))
include .depend
do-it-all:	stamp $(TARGET)
else
do-it-all:	stamp depend $(TARGET)
endif

stamp:
	@cp -f app.tmpl app.h
	@echo "#define APP_BUILD_STAMP \"`LC_TIME=\"\" date`\"" >> app.h
	@echo "#define REDIS_PASS \"`grep REDIS_PASS .env | cut -d '=' -f 2`\"" >> app.h
	@echo "#define REDIS_PORT `grep REDIS_PORT .env | cut -d '=' -f 2`" >> app.h

$(TARGET):	$(OBJS)
	$(CXX) $(OBJS) -o $(TARGET) $(LDFLAGS)
ifeq ($(RELEASE),1)
	@strip -R .comment -R .note.gnu.gold-version -R .note.ABI-tag $(TARGET)
	@strip -s $(TARGET)
endif

.cpp.o:
	$(CXX) $(CPPFLAGS) -c $< -o $@

.c.o:
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	@rm -rf db; rm -f .gdb_history peda-session*txt DEADJOE .depend $(OBJS) *~ *core $(TARGET) *out *.upx *.000 app.h && echo Clean Ok.
                
dep depend: stamp
	$(CC) $(CFLAGS) -M *.cpp > .depend

dist:	clean dep all

dbup:
	@mkdir -p db
	@docker compose -f redis.yml up -d

dbdown:
	@docker compose -f redis.yml down --rmi local

dbsh:
	@docker exec -ti redis /bin/bash

dbcli:
	@docker exec -ti redis /usr/local/bin/redis-cli -a $(REDIS_PASS)

test1:
	@./$(TARGET) tests/test1

test2:
	@./$(TARGET) tests/test2

dbbackup:
	@tar cvfz backup.tgz db

checksec:
	@checksec $(TARGET)
