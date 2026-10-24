
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <string.h>
#include <signal.h>
#include <fcntl.h>
#include <time.h>
#include <glob.h>
#include <wordexp.h>
#include <vector>
#include <string>
#include <cctype>
#include <iostream>
#include <sstream>
#include "command.h"
#include "tokenizer.h"

static std::string removeQuotes(const std::string &s)
{

    std::string out;
    for (size_t i = 0; i < s.size(); i++)
        if (s[i] != '"' && s[i] != '\'')
            out += s[i];
    return out;
}

static void setFile(char *&field, const std::string &name)
{

    free(field);
    field = strdup(removeQuotes(name).c_str());
}

static bool isWord(TokenType t) { return t == TOKEN_COMMAND || t == TOKEN_ARGUMENT; }

void parse(std::vector<Token> &tokens)
{
    Command &cmd = Command::_currentCommand;
    SimpleCommand *cur = new SimpleCommand();
    const char *error = NULL;

    for (size_t i = 0; i < tokens.size() && !error; i++)
    {
        Token &t = tokens[i];

        if (isWord(t.type))
        {
            cur->insertArgument(strdup(removeQuotes(t.value).c_str()));
        }
        else if (t.type == TOKEN_PIPE)
        {
            if (cur->_numberOfArguments == 0)
                error = "missing command before '|'";
            else
            {
                cmd.insertSimpleCommand(cur);
                cur = new SimpleCommand();
            }
        }
        else if (t.type == TOKEN_BACKGROUND)
        {
            if (tokens[i + 1].type != TOKEN_EOF)
                error = "'&' must be at the end of the command";
            else
                cmd._background = 1;
        }
        else if (t.type != TOKEN_EOF)
        {
            if (!isWord(tokens[i + 1].type))
            {
                error = "missing file name after redirection";
                break;
            }
            std::string file = tokens[++i].value;

            if (t.type == TOKEN_REDIRECT)
            {
                setFile(cmd._outFile, file);
                cmd._append = 0;
            }
            else if (t.type == TOKEN_APPEND)
            {
                setFile(cmd._outFile, file);
                cmd._append = 1;
            }
            else if (t.type == TOKEN_INPUT)
            {
                setFile(cmd._inputFile, file);
            }
            else if (t.type == TOKEN_ERROR)
            {
                setFile(cmd._errFile, file);
            }
            else
            {
                setFile(cmd._outFile, file);
                setFile(cmd._errFile, file);
                cmd._out_error = 1;
                cmd._append = 1;
            }
        }
    }

    if (!error)
    {
        if (cur->_numberOfArguments > 0)
            cmd.insertSimpleCommand(cur);
        else if (cmd._numberOfSimpleCommands > 0)
            error = "missing command after '|'";
        else if (cmd._outFile || cmd._inputFile || cmd._errFile || cmd._background)
            error = "missing command";
        else
        {
            freeSimpleCommand(cur);
            return;
        }
    }

    if (error)
    {
        fprintf(stderr, "myshell: syntax error: %s\n", error);
        freeSimpleCommand(cur);
        cmd.clear();
        return;
    }

    cmd.execute();
}
SimpleCommand::SimpleCommand()
{
    _numberOfAvailableArguments = 5;
    _numberOfArguments = 0;
    _arguments = (char **)malloc(_numberOfAvailableArguments * sizeof(char *));
    _arguments[0] = NULL;
}

void SimpleCommand::insertArgument(char *argument)
{
    if (_numberOfAvailableArguments == _numberOfArguments + 1)
    {
        _numberOfAvailableArguments *= 2;
        _arguments = (char **)realloc(_arguments,
                                      _numberOfAvailableArguments * sizeof(char *));
    }

    _arguments[_numberOfArguments] = argument;
    _arguments[_numberOfArguments + 1] = NULL;

    _numberOfArguments++;
}

Command::Command()
{
    _numberOfAvailableSimpleCommands = 1;
    _numberOfSimpleCommands = 0;
    _simpleCommands = (SimpleCommand **)
        malloc(_numberOfAvailableSimpleCommands * sizeof(SimpleCommand *));

    _outFile = 0;
    _inputFile = 0;
    _errFile = 0;
    _out_error = 0;
    _background = 0;
    _append = 0;
}

void Command::insertSimpleCommand(SimpleCommand *simpleCommand)
{
    if (_numberOfAvailableSimpleCommands == _numberOfSimpleCommands)
    {
        _numberOfAvailableSimpleCommands *= 2;
        _simpleCommands = (SimpleCommand **)realloc(_simpleCommands,
                                                    _numberOfAvailableSimpleCommands * sizeof(SimpleCommand *));
    }

    _simpleCommands[_numberOfSimpleCommands] = simpleCommand;
    _numberOfSimpleCommands++;
}

void Command::clear()
{
    for (int i = 0; i < _numberOfSimpleCommands; i++)
        freeSimpleCommand(_simpleCommands[i]);

    free(_outFile);
    free(_inputFile);
    free(_errFile);

    _numberOfSimpleCommands = 0;
    _outFile = 0;
    _inputFile = 0;
    _errFile = 0;
    _out_error = 0;
    _background = 0;
    _append = 0;
}

void Command::print()
{
    printf("\n\n");
    printf("              COMMAND TABLE                \n");
    printf("\n");
    printf("  #   Simple Commands\n");
    printf("  --- ----------------------------------------------------------\n");

    for (int i = 0; i < _numberOfSimpleCommands; i++)
    {
        printf("  %-3d ", i);
        for (int j = 0; j < _simpleCommands[i]->_numberOfArguments; j++)
        {
            printf("\"%s\" \t", _simpleCommands[i]->_arguments[j]);
        }
        printf("\n");
    }

    printf("\n\n");
    printf("  Output       Input        Error        Err&Out       Background\n");
    printf("  ------------ ------------ ------------ ------------ ------------\n");
    printf("  %-12s %-12s %-12s %-12s %-12s\n", _outFile ? _outFile : "default",
           _inputFile ? _inputFile : "default", _errFile ? _errFile : "default",
           _out_error == 1 ? _errFile : "default",
           _background ? "YES" : "NO");
    printf("\n\n");
}
static void logChild(pid_t pid)
{
    char line[64];
    int n = snprintf(line, sizeof(line), "Child process %d terminated\n", (int)pid);
    if (logFd >= 0)
    {
        ssize_t r = write(logFd, line, n);
        (void)r;
    }
}

static void onChildExit(int)
{
    int saved = errno;
    int status;
    pid_t pid;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0)
        logChild(pid);
    errno = saved;
}


void Command::execute()
{
    print();

    if (_numberOfSimpleCommands == 1)
    {
        char **args = _simpleCommands[0]->_arguments;

        if (strcmp(args[0], "exit") == 0)
        {
            printf("Good bye!!\n");
            exit(0);
        }
        if (strcmp(args[0], "cd") == 0)
        {
            const char *dir = args[1] ? args[1] : getenv("HOME");
            if (dir == NULL || chdir(dir) != 0)
                fprintf(stderr, "cd: %s: %s\n", dir ? dir : "(no HOME)", strerror(errno));
            clear();
            return;
        }
    }

    int infd = -1, outfd = -1, errfd = -1;

    if (_inputFile)
    {
        infd = open(_inputFile, O_RDONLY);
        if (infd < 0)
        {
            fprintf(stderr, "myshell: %s: %s\n", _inputFile, strerror(errno));
            clear();
            return;
        }
    }
    if (_outFile)
    {
        outfd = open(_outFile, O_WRONLY | O_CREAT | (_append ? O_APPEND : O_TRUNC), 0644);
        if (outfd < 0)
        {
            fprintf(stderr, "myshell: %s: %s\n", _outFile, strerror(errno));
            clear();
            return;
        }
    }
    if (_errFile)
    {
        if (_out_error)
            errfd = dup(outfd);
        else
            errfd = open(_errFile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (errfd < 0)
        {
            fprintf(stderr, "myshell: %s: %s\n", _errFile, strerror(errno));
            clear();
            return;
        }
    }

    fflush(stdout);
    sigset_t block, old;
    sigemptyset(&block);
    sigaddset(&block, SIGCHLD);
    sigprocmask(SIG_BLOCK, &block, &old);

    std::vector<pid_t> pids;
    int prevRead = -1;

    for (int i = 0; i < _numberOfSimpleCommands; i++)
    {
        bool last = (i == _numberOfSimpleCommands - 1);
        int fd[2] = {-1, -1};

        if (!last && pipe(fd) < 0)
        {
            perror("myshell: pipe");
            break;
        }

        pid_t pid = fork();
        if (pid < 0)
        {
            perror("myshell: fork");
            break;
        }

        if (pid == 0)
        {
            sigprocmask(SIG_SETMASK, &old, NULL);
            signal(SIGCHLD, SIG_DFL);
            if (!_background)
                signal(SIGINT, SIG_DFL);

            if (i == 0 && infd >= 0)
                dup2(infd, 0);
            else if (i > 0)
                dup2(prevRead, 0);

            if (last && outfd >= 0)
                dup2(outfd, 1);
            else if (!last)
                dup2(fd[1], 1);

            if (errfd >= 0)
                dup2(errfd, 2);

            if (infd >= 0)
                close(infd);
            if (outfd >= 0)
                close(outfd);
            if (errfd >= 0)
                close(errfd);
            if (prevRead >= 0)
                close(prevRead);
            if (fd[0] >= 0)
                close(fd[0]);
            if (fd[1] >= 0)
                close(fd[1]);

            char **args = _simpleCommands[i]->_arguments;
            execvp(args[0], args);

            fprintf(stderr, "myshell: %s: command not found\n", args[0]);
            _exit(127);
        }

        pids.push_back(pid);
        if (prevRead >= 0)
            close(prevRead);
        if (fd[1] >= 0)
            close(fd[1]);
        prevRead = fd[0];
    }

    if (prevRead >= 0)
        close(prevRead);
    if (infd >= 0)
        close(infd);
    if (outfd >= 0)
        close(outfd);
    if (errfd >= 0)
        close(errfd);

    if (_background)
    {
        if (!pids.empty())
            printf("[bg] %d\n", (int)pids.back());
    }
    else
    {
        for (size_t k = 0; k < pids.size(); k++)
        {
            int status;
            if (waitpid(pids[k], &status, 0) > 0)
                logChild(pids[k]);
        }
    }

    sigprocmask(SIG_SETMASK, &old, NULL);
    clear();
}

void Command::prompt(){
    std::string input;
    while (true)
    {
        printf("myshell>");
        fflush(stdout);

        if (!std::getline(std::cin, input))
        {
            printf("\nGood bye!!\n");
            break;
        }
        std::vector<Token> tokens = tokenize(input);
        parse(tokens);
    }
}

Command Command::_currentCommand;
SimpleCommand *Command::_currentSimpleCommand;

int main(){
    signal(SIGINT, SIG_IGN);
    signal(SIGCHLD, onChildExit);
    logFd = open("myshell.log", O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);

    Command::_currentCommand.prompt();
    return 0;
}
