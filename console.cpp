
#include "console.h"

#include <iostream>
#include <fstream>
#include <functional>
#include <algorithm>
#include <iterator>
#include <sstream>

#include <cstdlib>
#include <readline/readline.h>
#include <readline/history.h>

namespace CppReadline {
    Console * Console::currentConsole_  = nullptr;
    void * Console::emptyHistory_       = static_cast<void*>(history_get_history_state());

    Console::Console(std::string greeting, CommandFunction _defcmd) : chatMode(false), greeting_(greeting), history_(nullptr), defcmd_(_defcmd) {
      rl_attempted_completion_function = &Console::getCommandCompletions;
    }

    Console::~Console() {
      write_history(0);
      free(history_);
    }

    void Console::registerCommand(const std::string & s, CommandFunction f) {
      commands_[s] = f;
    }

    std::vector<std::string> Console::getRegisteredCommands() const {
        std::vector<std::string> allCommands;
        for ( auto & pair : commands_ ) allCommands.push_back(pair.first);

        return allCommands;
    }

    void Console::saveState() {
        free(history_);
        history_ = static_cast<void*>(history_get_history_state());
    }

    void Console::reserveConsole() {
        if ( currentConsole_ == this ) return;

        if ( currentConsole_ )
            currentConsole_->saveState();

        if ( ! history_ )
            history_set_history_state(static_cast<HISTORY_STATE*>(emptyHistory_));
        else
            history_set_history_state(static_cast<HISTORY_STATE*>(history_));
        static bool hr;
        if (!hr) {
          read_history(0);
          hr = true;
        }
        currentConsole_ = this;
    }

    int Console::executeCommand(const std::string & command) {
        std::vector<std::string> inputs;
        {
            std::istringstream iss(command);
            std::copy(std::istream_iterator<std::string>(iss),
                    std::istream_iterator<std::string>(),
                    std::back_inserter(inputs));
        }

        if ( inputs.size() == 0 ) return ReturnCode::Ok;
        if (chatMode)
          return defcmd_(inputs);
        RegisteredCommands::iterator it;
        if ( ( it = commands_.find(inputs[0]) ) != end(commands_) ) {
            return static_cast<int>((it->second)(inputs));
        }
        return defcmd_(inputs);
    }

    int Console::executeFile(const std::string & filename) {
        std::ifstream input(filename);
        if ( ! input ) {
            std::cout << "run: Could not find the specified file to execute.\n";
            return ReturnCode::Error;
        }
        std::string command;
        int counter = 0, result;

        while ( std::getline(input, command)  ) {
            if ( command[0] == '#' ) continue; // Ignore comments
            std::cout << "[" << counter << "] " << command << '\n';
            if ( (result = executeCommand(command)) ) return result;
            ++counter; std::cout << '\n';
        }

        // If we arrived successfully at the end, all is ok
        return ReturnCode::Ok;
    }

    int Console::readLine() {
        reserveConsole();

        char * buffer = readline(greeting_.c_str());
        if ( !buffer ) {
            std::cout << '\n'; // EOF doesn't put last endline so we put that so that it looks uniform.
            return ReturnCode::Quit;
        }

        // TODO: Maybe add commands to history only if succeeded?
        if ( buffer[0] != '\0' )
            add_history(buffer);

        std::string line(buffer);
        free(buffer);

        return executeCommand(line);
    }

    char ** Console::getCommandCompletions(const char * text, int start, int) {
        char ** completionList = nullptr;

        if ( start == 0 )
            completionList = rl_completion_matches(text, &Console::commandIterator);

        return completionList;
    }

    char * Console::commandIterator(const char * text, int state) {
        static RegisteredCommands::iterator it;
        auto & commands_ = currentConsole_->commands_;

        if ( state == 0 ) it = begin(commands_);

        while ( it != end(commands_ ) ) {
            auto & command = it->first;
            ++it;
            if ( command.find(text) != std::string::npos ) {
                char * completion = new char[command.size()];
                strcpy(completion, command.c_str());
                return completion;
            }
        }
        return nullptr;
    }
}
