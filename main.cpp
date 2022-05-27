
#define BACKWARD_HAS_BFD        1
#include "backward.h"

#include "app.h"
#include "console.h"
#include <iostream>
#include <unistd.h>
#include <sw/redis++/redis++.h>
#include <functional>
#include <filesystem>
#include <thread>

using namespace std;
namespace cr = CppReadline;
using ret = cr::Console::ReturnCode;
using namespace sw::redis;
namespace ph = std::placeholders;
namespace fs = std::filesystem;

static cr::Console *console;

backward::SignalHandling sh;

class Chat {
    Redis *redis;
    std::thread *heartbeat_t, *sub_t, *fsub_t;
    volatile bool exiting, sub_exiting;
    string user, room;
    ofstream of;

    void unsubscribe() {
      sub_exiting = true;
      usleep(200000);
      if (sub_t)
        delete sub_t;
      sub_t = 0;
    }
    void subscribe() {
      sub_exiting = false;
      sub_t = new std::thread(std::bind(&Chat::sub, this));
      sub_t->detach();
    }
    void leaveRoom() {
      if (room.size()) {
        cout << "leaving room " << room << "\n";
        unsubscribe();
        redis->srem("rooms." + room, user);
        room.clear();
      }
    }
    void fsubscribe() {
      fsub_t = new std::thread(std::bind(&Chat::fsub, this));
      fsub_t->detach();
    }
    void fsub() {
      auto sub = redis->subscriber();
      string ctrlChannel("file_ctrl." + user), dataChannel("file_data." + user);
      sub.on_message([=, this](std::string channel, std::string msg) {
        static string fname;
        if (channel == ctrlChannel) {
          if (strstr(msg.data(), "playopen:") || strstr(msg.data(), "open:")) {
            bool playing = strstr(msg.data(), "playopen:") ? true : false;
            fname = msg.data() + (playing ? 9 : 5);
            cout << "\r\nreceiving" << (playing ? " sound" : "") << " file " << fname << "\n";
            of.open(playing ? ("/tmp/" + fname) : fname);
          }
          if (strstr(msg.data(), "close") || strstr(msg.data(), "playclose")) {
            bool playing = strstr(msg.data(), "playclose") ? true : false;
            cout << "receiving" << (playing ? " sound" : "") << " file completed\n";
            of.close();
            if (playing) {
              cout << "playing... ";
              int res = system(("play /tmp/" + fname + " > /dev/null 2>&1").c_str());
              unlink(("/tmp/" + fname).c_str());
              if (res)
                cout << "play failed\n";
              else
                cout << "done\n";
            }
          }
        }
        if (channel == dataChannel) {
          of.write(msg.data(), msg.size());
        }
      });
      sub.on_pmessage([](std::string pattern, std::string channel, std::string msg) {
        // cout << "on_pmessage: [" << pattern << "] : " << channel << " => " << msg << endl;
      });
      sub.on_meta([](Subscriber::MsgType type, OptionalString channel, long long num) {
        // cout << "on_meta: type: " << int(type) << ", channel: " << channel << ", num: " << num << endl;
      });
      sub.subscribe({ ctrlChannel, dataChannel });
      while(!exiting) {
        try {
          sub.consume();
        } catch (const TimeoutError &e) { // just ignore
        } catch (const Error &e) {
            cerr << "\n\rfsub exception: " << e.what() << endl;
            if (e.what() == string("Connection is broken"))
              exit(-1);
        } catch (...) {
            cerr << "\n\rfsub general exception\n";
            exit(-1);
        }
      } // while
      sub.unsubscribe({ ctrlChannel, dataChannel });
    }
    void sub() {
      auto sub = redis->subscriber();
      sub.on_message([=, this](std::string channel, std::string msg) {
        if (channel != room)
          return;
        cerr << "\r" << msg << endl;
      });
      sub.on_pmessage([](std::string pattern, std::string channel, std::string msg) {
        // cout << "on_pmessage: [" << pattern << "] : " << channel << " => " << msg << endl;
      });
      sub.on_meta([](Subscriber::MsgType type, OptionalString channel, long long num) {
        // cout << "on_meta: type: " << int(type) << ", channel: " << channel << ", num: " << num << endl;
      });
      sub.subscribe(room);
      while(!exiting && !sub_exiting) {
        try {
          sub.consume();
        } catch (const TimeoutError &e) { // just ignore
        } catch (const Error &e) {
            cerr << "\n\rsub exception: " << e.what() << endl;
            if (e.what() == string("Connection is broken"))
              exit(-1);
        } catch (...) {
            cerr << "\n\rsub general exception\n";
            exit(-1);
        }
      } // while
      sub.unsubscribe(room);
    }
    void heartbeat() {
      time_t t0 = 0;
      while(!exiting) {
        try {
          if ((time(0) - t0)) {
            t0 = time(0);
            using namespace std::chrono;
            if (user.size() && !redis->expire("usersonline." + user, seconds(2)))
              cerr << "EXPIRE for user failed !\n";
            if (room.size() && !redis->expire("rooms." + room, seconds(60)))
              cerr << "EXPIRE for room failed !\n";
          }
        } catch (const Error &e) {
            cerr << "\n\raux exception: " << e.what() << endl;
            exit(-1);
        } catch (...) {
            cerr << "\n\raux general exception\n";
            exit(-1);
        }
        usleep(10000);
      }
    }
  public:
    Chat() : redis(0), heartbeat_t(0), sub_t(0), fsub_t(0), exiting(false), sub_exiting(false) {
      ConnectionOptions connection_options;
      connection_options.host = "127.0.0.1";
      connection_options.port = REDIS_PORT;
      connection_options.password = REDIS_PASS;
      connection_options.socket_timeout = std::chrono::milliseconds(100);
      redis = new Redis(connection_options);
      redis->ping();
      heartbeat_t = new std::thread(std::bind(&Chat::heartbeat, this));
      heartbeat_t->detach();
    }
    ~Chat() {
      exiting = true;
      leaveRoom();
      if (user.size()) {
        redis->srem("usersonline." + user, "1");
        cout << user << " logged out\n";
      }
      if (fsub_t)
        delete fsub_t;
      if (heartbeat_t)
        delete heartbeat_t;
      if (redis)
        delete redis;
    }
    // cli commands
    unsigned cliSendPlay(const vector<string> &i, bool play = false) {
      if (i.size() < 3) {
        cerr << "missing file name and/or user\n";
        return ret::Error;
      }
      if (i[2] == user) {
        cerr << "you already have this file\n";
        return ret::Error;
      }
      if (!redis->exists("usersonline." + i[2])) {
        cerr << "user " << i[2] << " is not online\n";
        return ret::Error;
      }
      ifstream fi(i[1]);
      if (!fi.good()) {
        cerr << "cant open " << i[1] << endl;
        return ret::Error;
      }
      string fname(fs::path(i[1]).filename());
      cout << (play ? "playing" : "sending") << " file " << fname << " to " << i[2] << endl;
      string ctrlChannel("file_ctrl." + i[2]), dataChannel("file_data." + i[2]);;
      redis->publish(ctrlChannel, (play ? "playopen:" : "open:") + fname);
      char buf[128 * 1024];
      while(1) {
        size_t nread = fi.readsome(buf, sizeof(buf));
        if (!nread)
          break;
        if (fi.bad()) {
          cerr << "error reading " << i[2] << endl;
          break;
        }
        string data(buf, nread);
        redis->publish(dataChannel, data);
        if (fi.eof())
          break;
      }
      redis->publish(ctrlChannel, (play ? "playclose" : "close"));
      cout << (play ? "playing" : "sending") << " file " << fname << " completed\n";
      return ret::Ok;
    }
    unsigned cliPlay(const vector<string> &i) {
      return cliSendPlay(i, true);
    }
    unsigned cliSend(const vector<string> &i) {
      return cliSendPlay(i);
    }
    unsigned cliUsers(const vector<string> &i) {
      if (i.size() < 2) {
        cerr << "missing room name\n";
        return ret::Error;
      }
      if (!redis->exists("rooms." + i[1])) {
        cout << "no such room\n";
        return ret::Error;
      }
      vector<string> users;
      redis->smembers("rooms." + i[1], std::back_inserter(users));
      if (!users.size())
        cout << "no users\n";
      else
        for (auto& u : users)
          cout << u << endl;
      return ret::Ok;
    }
    unsigned cliList(const vector<string> &i) {
      vector<string> rooms;
      redis->keys("rooms.*", back_inserter(rooms));
      if (!rooms.size())
        cout << "no rooms\n";
      else
        for (auto& r : rooms)
          cout << (r.data() + 6) << endl;
      return ret::Ok;
    }
    unsigned cliRoom(const vector<string> &i) {
      if (!user.size()) {
        cerr << "login first\n";
        return ret::Error;
      }
      if (i.size() < 2) {
        cerr << "missing room name\n";
        return ret::Error;
      }
      if (room.size())
        leaveRoom();
      bool created = !redis->exists("rooms." + i[1]);
      redis->sadd("rooms." + i[1], user);
      room = i[1];
      subscribe();
      cout << user << (created ? " created and" : "") << " joined room " << room << endl;
      console->chatMode = true;
      cout << "chat mode is on. send '!@#'' to switch back to command mode\n";
      return ret::Ok;
    }
    unsigned cliLogin(const vector<string> &i) {
      if (i.size() < 2) {
        cerr << "missing nickname\n";
        return ret::Error;
      }
      if (!redis->sismember("users", i[1])) {
        cerr << "no such user registered\n";
        return ret::Error;
      }
      if (!redis->sadd("usersonline." + i[1], "1")) {
        cerr << i[1] << " already logged in !\n";
        return ret::Error;
      }
      user = i[1];
      cout << "logged as " << user << endl;
      fsubscribe();
      return ret::Ok;
    }
    unsigned cliChat(const vector<string> &i) {
      if (!console->chatMode) {
        cerr << "Uknown command: " << i[0] << endl;
        return ret::Error;
      }
      if (i[0] == "!@#") {
        leaveRoom();
        console->chatMode = false;
        return ret::Ok;
      }
      string msg;
      for (auto& m : i)
        msg += (m + " ");
      redis->publish(room, user + "> " + msg);
      return ret::Ok;
    }
    unsigned cliRegister(const vector<string> &i) {
      if (i.size() < 2) {
        cerr << "missing nickname\n";
        return ret::Error;
      }
      if (!redis->sadd("users", i[1])) {
        cerr << "such user already exists\n";
        return ret::Error;
      }
      cout << "user " << i[1] << " created\n";
      return ret::Ok;
    }
    void cliPrintHelp() {
      cout << "\nhelp:\n\n";
      cout << "register <nickname>\t- creates new user\n";
      cout << "login <nickname>\t- login\n";
      cout << "list\t\t\t- list rooms\n";
      cout << "users <room>\t\t- list users in room\n";
      cout << "room <name>\t\t- join room. will be created if not exists\n";
      cout << "send <file> <user>\t- send file to user\n";
      cout << "play <file> <user>\t- play file to user\n";
      cout << "Ctrl-C\t\t\t- exit\n";
      cout << "\n";
    }
    unsigned cliHelp(const vector<string> &i) {
      cliPrintHelp();
      return ret::Ok;
    }
};

static Chat *chat;

void cleanup() {
  cout << "\nBye..\n";
  if (chat)
    delete chat;
  if (console)
    delete console;
}

void sighandler(int sig) {
  cleanup();
  ::_exit(0);
}

void set_signals() {
 signal(SIGALRM, sighandler);
 signal(SIGINT, sighandler);
 signal(SIGTERM, sighandler);
 signal(SIGQUIT, sighandler);
}

int main(int argc, char **argv) {
  set_signals();
  cout << "\nRedis chat app"<< APP_VERSION << " " << APP_BULD_TYPE << " build at " << APP_BUILD_STAMP << endl;
  try {
    chat = new Chat();
    console = new cr::Console("redischat> ", std::bind(&Chat::cliChat, chat, ph::_1));
    console->registerCommand("?", std::bind(&Chat::cliHelp, chat, ph::_1));
    console->registerCommand("register", std::bind(&Chat::cliRegister, chat, ph::_1));
    console->registerCommand("login", std::bind(&Chat::cliLogin, chat, ph::_1));
    console->registerCommand("list", std::bind(&Chat::cliList, chat, ph::_1));
    console->registerCommand("users", std::bind(&Chat::cliUsers, chat, ph::_1));
    console->registerCommand("room", std::bind(&Chat::cliRoom, chat, ph::_1));
    console->registerCommand("send", std::bind(&Chat::cliSend, chat, ph::_1));
    console->registerCommand("play", std::bind(&Chat::cliPlay, chat, ph::_1));
    if (argc > 1)
      console->executeFile(argv[1]);
    else
      chat->cliPrintHelp();
    while (console->readLine() != ret::Quit)
    usleep(100000);
  } catch (string err) {
      cerr << "\r\nmain::exception: " << err << endl;
  } catch (const Error &e) {
      cerr << "\r\nError: " << e.what() << endl;
  } catch (...) {
      cerr << "\r\nGeneric exception" << endl;
  }
  cleanup();
  return 0;
}
