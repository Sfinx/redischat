
#pragma once

#include <functional>
#include <string>
#include <vector>
#include <unordered_map>

namespace CppReadline {

    using CommandFunction = std::function<unsigned(const std::vector<std::string> &)>;
    using RegisteredCommands = std::unordered_map<std::string,CommandFunction>;

    class Console {
        public:
            volatile bool chatMode;
            enum ReturnCode {
                Quit = -1,
                Ok = 0,
                Error = 1
            };
            Console(std::string greeting, CommandFunction defcmd = 0);
            ~Console();
            void registerCommand(const std::string & s, CommandFunction f);
            std::vector<std::string> getRegisteredCommands() const;
            int executeCommand(const std::string & command);
            int executeFile(const std::string & filename);
            int readLine();
        private:
            std::string greeting_;
            RegisteredCommands commands_;
            void * history_;
            void saveState();
            void reserveConsole();
            static Console * currentConsole_;
            static void * emptyHistory_;
            using commandCompleterFunction = char**(const char * text, int start, int end);
            using commandIteratorFunction = char*(const char * text, int state);

            static commandCompleterFunction getCommandCompletions;
            static commandIteratorFunction commandIterator;
            CommandFunction defcmd_;
    };
}
