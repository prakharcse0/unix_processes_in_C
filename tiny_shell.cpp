// tiny_shell.cpp - A minimal Unix-like shell implementation
// Intermediate implementation with I/O redirection, background processes, and pipes

#include <iostream>
#include <string>
#include <sstream>
#include <vector>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <cstring>
#include <cerrno>
#include <algorithm>
#include <cctype>  // Added for isalnum

// ANSI color codes for shell prompt
#define COLOR_BLUE "\033[1;34m"
#define COLOR_GREEN "\033[1;32m"
#define COLOR_RESET "\033[0m"

// Structure to hold command information
struct Command {
    std::vector<std::string> args;     // Command arguments
    std::string inputFile;             // Input redirection file
    std::string outputFile;            // Output redirection file
    bool appendOutput;                 // Whether to append to output file
    bool runInBackground;              // Whether to run in background
};

// Structure to hold a pipeline of commands
struct Pipeline {
    std::vector<Command> commands;     // List of commands in the pipeline
    bool runInBackground;              // Whether to run in background
};

// Function declarations
Pipeline parsePipeline(const std::string& input);
bool executePipeline(const Pipeline& pipeline);
bool executeCommand(const Command& command, int inputFd, int outputFd);
bool executeBuiltInCommand(const std::vector<std::string>& args);
std::string getCurrentDirectory();
void displayPrompt();
void showHelp();

// List of background process IDs
std::vector<pid_t> backgroundProcesses;

int main() {
    std::string input;
    
    // Shell main loop
    while (true) {
        // Check for completed background processes
        for (auto it = backgroundProcesses.begin(); it != backgroundProcesses.end();) {
            int status;
            pid_t result = waitpid(*it, &status, WNOHANG);
            if (result > 0) {
                std::cout << "[" << *it << "] completed" << std::endl;
                it = backgroundProcesses.erase(it);
            } else {
                ++it;
            }
        }
        
        displayPrompt();
        
        // Get input from user
        if (!std::getline(std::cin, input)) {
            std::cout << std::endl;  // Proper newline on Ctrl+D
            break;
        }
        
        // Skip empty inputs
        if (input.empty()) {
            continue;
        }
        
        // Parse input into a pipeline structure
        Pipeline pipeline = parsePipeline(input);
        
        // Skip if no commands
        if (pipeline.commands.empty()) {
            continue;
        }
        
        // Check for built-in commands (only if not in a pipeline)
        if (pipeline.commands.size() == 1 && !pipeline.commands[0].args.empty()) {
            if (pipeline.commands[0].args[0] == "exit") {
                break;
            } else if (executeBuiltInCommand(pipeline.commands[0].args)) {
                // Command was a built-in and has been handled
                continue;
            }
        }
        
        // Execute the pipeline
        executePipeline(pipeline);
    }
    
    return 0;
}

// Display the shell prompt with username and current directory
void displayPrompt() {
    char hostname[1024];
    hostname[1023] = '\0';
    gethostname(hostname, 1023);
    
    char* username = getlogin();
    std::string user = username ? username : "user";
    
    std::cout << COLOR_GREEN << user << "@" << hostname << COLOR_RESET << ":" 
              << COLOR_BLUE << getCurrentDirectory() << COLOR_RESET << "$ ";
}

// Get the current working directory (for the prompt)
std::string getCurrentDirectory() {
    char cwd[1024];
    if (getcwd(cwd, sizeof(cwd)) != NULL) {
        std::string dir(cwd);
        // Get home directory to replace with ~ if needed
        char* home = getenv("HOME");
        if (home && dir.find(home) == 0) {
            return "~" + dir.substr(strlen(home));
        }
        return dir;
    } else {
        return "?";
    }
}

// Parse input string into a pipeline structure
Pipeline parsePipeline(const std::string& input) {
    Pipeline pipeline;
    pipeline.runInBackground = false;
    
    // Check if the command should run in background
    std::string cmd = input;
    if (!cmd.empty() && cmd.back() == '&') {
        pipeline.runInBackground = true;
        cmd.pop_back();  // Remove the &
    }
    
    // Split the input by pipes
    std::vector<std::string> cmdStrings;
    std::string cmdStr;
    std::istringstream iss(cmd);
    
    while (std::getline(iss, cmdStr, '|')) {
        cmdStrings.push_back(cmdStr);
    }

    // Process each command in the pipeline
    for (const auto& cmdString : cmdStrings) {
        Command command;
        command.appendOutput = false;
        command.runInBackground = false;
        
        std::istringstream cmdStream(cmdString);
        std::string token;
        std::vector<std::string> tokens;
        
        // Tokenize the command
        while (cmdStream >> token) {
            tokens.push_back(token);
        }
        
        // Process tokens for special operators
        for (size_t i = 0; i < tokens.size(); ++i) {
            if (tokens[i] == "<") {
                // Input redirection
                if (i + 1 < tokens.size()) {
                    command.inputFile = tokens[i + 1];
                    ++i; // Skip the filename
                }
            } else if (tokens[i] == ">") {
                // Output redirection
                if (i + 1 < tokens.size()) {
                    command.outputFile = tokens[i + 1];
                    command.appendOutput = false;
                    ++i; // Skip the filename
                }
            } else if (tokens[i] == ">>") {
                // Append output redirection
                if (i + 1 < tokens.size()) {
                    command.outputFile = tokens[i + 1];
                    command.appendOutput = true;
                    ++i; // Skip the filename
                }
            } else {
                // Regular argument
                command.args.push_back(tokens[i]);
            }
        }
        
        // Add environment variable expansion code here
        // Expand environment variables in arguments
        for (size_t i = 0; i < command.args.size(); ++i) {
            std::string& arg = command.args[i];
            // Look for environment variable references ($VAR or ${VAR})
            size_t pos = 0;
            while ((pos = arg.find('$', pos)) != std::string::npos) {
                if (pos + 1 >= arg.length()) {
                    break; // $ at the end of string
                }
                // Handle ${VAR} format
                if (arg[pos + 1] == '{') {
                    size_t endPos = arg.find('}', pos + 2);
                    if (endPos != std::string::npos) {
                        std::string varName = arg.substr(pos + 2, endPos - (pos + 2));
                        const char* value = getenv(varName.c_str());
                        if (value) {
                            arg.replace(pos, endPos - pos + 1, value);
                            pos += strlen(value); // Skip past the value
                        } else {
                            // Remove the reference if not found
                            arg.replace(pos, endPos - pos + 1, "");
                        }
                    } else {
                        ++pos; // Skip past the $
                    }
                }
                // Handle $VAR format
                else {
                    size_t endPos = pos + 1;
                    while (endPos < arg.length() && (isalnum(arg[endPos]) || arg[endPos] == '_')) {
                        ++endPos;
                    }
                    std::string varName = arg.substr(pos + 1, endPos - (pos + 1));
                    if (!varName.empty()) {
                        const char* value = getenv(varName.c_str());
                        if (value) {
                            arg.replace(pos, endPos - pos, value);
                            pos += strlen(value); // Skip past the value
                        } else {
                            // Remove the reference if not found
                            arg.replace(pos, endPos - pos, "");
                        }
                    } else {
                        ++pos; // Skip past the $
                    }
                }
            }
        }
        
        // Add the command to the pipeline if it has arguments
        if (!command.args.empty()) {
            pipeline.commands.push_back(command);
        }
    }
    
    return pipeline;
}

// Execute a pipeline of commands
bool executePipeline(const Pipeline& pipeline) {
    if (pipeline.commands.empty()) {
        return false;
    }
    
    // Single command case (no pipe)
    if (pipeline.commands.size() == 1) {
        Command cmd = pipeline.commands[0];
        cmd.runInBackground = pipeline.runInBackground;
        return executeCommand(cmd, -1, -1);
    }
    
    // Multiple commands case (with pipes)
    int numCommands = pipeline.commands.size();
    std::vector<int> pipeFds((numCommands - 1) * 2);
    std::vector<pid_t> pids(numCommands);
    
    // Create pipes
    for (int i = 0; i < numCommands - 1; ++i) {
        if (pipe(&pipeFds[i * 2]) == -1) {
            std::cerr << "Pipe creation failed: " << strerror(errno) << std::endl;
            return false;
        }
    }
    
    // Create processes for each command
    for (int i = 0; i < numCommands; ++i) {
        pids[i] = fork();
        
        if (pids[i] < 0) {
            // Fork failed
            std::cerr << "Fork failed: " << strerror(errno) << std::endl;
            return false;
        } else if (pids[i] == 0) {
            // Child process
            
            // Set up stdin (from previous pipe or from input file)
            int inputFd = -1;
            if (i == 0) {
                // First command - use input file if specified
                inputFd = -1;  // Default to stdin
            } else {
                // Not first command - use previous pipe
                inputFd = pipeFds[(i - 1) * 2];
            }
            
            // Set up stdout (to next pipe or to output file)
            int outputFd = -1;
            if (i == numCommands - 1) {
                // Last command - use output file if specified
                outputFd = -1;  // Default to stdout
            } else {
                // Not last command - use next pipe
                outputFd = pipeFds[i * 2 + 1];
            }
            
            // Close all unused pipe ends
            for (int j = 0; j < (numCommands - 1) * 2; ++j) {
                // Keep read end of previous pipe (if not first command)
                // Keep write end of current pipe (if not last command)
                if (!((i > 0 && j == (i-1)*2) || 
                      (i < numCommands-1 && j == i*2+1))) {
                    close(pipeFds[j]);
                }
            }
            
            // Execute the command
            executeCommand(pipeline.commands[i], inputFd, outputFd);
            
            // Child should exit after executing
            exit(EXIT_FAILURE);
        }
    }
    
    // Parent process
    
    // Close all pipe file descriptors in the parent
    for (int i = 0; i < (numCommands - 1) * 2; ++i) {
        close(pipeFds[i]);
    }
    
    // Wait for child processes to complete, or run in background
    if (!pipeline.runInBackground) {
        // Foreground execution - wait for all processes
        for (int i = 0; i < numCommands; ++i) {
            int status;
            waitpid(pids[i], &status, 0);
        }
    } else {
        // Background execution - add to list of background processes
        std::cout << "[";
        for (int i = 0; i < numCommands; ++i) {
            backgroundProcesses.push_back(pids[i]);
            std::cout << pids[i];
            if (i < numCommands - 1) std::cout << " ";
        }
        std::cout << "] running in background" << std::endl;
    }
    
    return true;
}

// Execute built-in commands
bool executeBuiltInCommand(const std::vector<std::string>& args) {
    if (args.empty()) {
        return false;
    }

    // cd command - change directory
    if (args[0] == "cd") {
        std::string dir = args.size() > 1 ? args[1] : getenv("HOME");
        
        // Handle ~ expansion for home directory
        if (!dir.empty() && dir[0] == '~') {
            const char* home = getenv("HOME");
            if (home) {
                dir = std::string(home) + dir.substr(1);
            }
        }
        
        if (chdir(dir.c_str()) != 0) {
            std::cerr << "cd: " << dir << ": " << strerror(errno) << std::endl;
        }
        return true;
    }
    
    // pwd command - print working directory
    if (args[0] == "pwd") {
        std::cout << getCurrentDirectory() << std::endl;
        return true;
    }
    
    // help command - show help
    if (args[0] == "help") {
        showHelp();
        return true;
    }
    
    // Add new built-in commands here
    // env command - display environment variables
    if (args[0] == "env") {
        extern char** environ;
        for (char** env = environ; *env != nullptr; ++env) {
            std::cout << *env << std::endl;
        }
        return true;
    }
    
    // export command - set environment variable
    if (args[0] == "export" && args.size() >= 2) {
        for (size_t i = 1; i < args.size(); ++i) {
            std::string assignment = args[i];
            size_t equalsPos = assignment.find('=');
            if (equalsPos != std::string::npos) {
                std::string name = assignment.substr(0, equalsPos);
                std::string value = assignment.substr(equalsPos + 1);
                setenv(name.c_str(), value.c_str(), 1);
            }
        }
        return true;
    }
    
    // unset command - unset environment variable
    if (args[0] == "unset" && args.size() >= 2) {
        for (size_t i = 1; i < args.size(); ++i) {
            unsetenv(args[i].c_str());
        }
        return true;
    }
    
    return false;  // Not a built-in command
}

// Show help for the shell
void showHelp() {
    std::cout << "Tiny Shell Help\n"
              << "----------------\n"
              << "Built-in commands:\n"
              << "  cd [dir]       Change directory\n"
              << "  pwd            Print current directory\n"
              << "  help           Show this help\n"
              << "  exit           Exit the shell\n"
              << "  env            Display environment variables\n"
              << "  export NAME=VAL Set environment variable\n"
              << "  unset NAME     Unset environment variable\n"
              << "\n"
              << "Features:\n"
              << "  command &      Run in background\n"
              << "  cmd1 | cmd2    Pipe output of cmd1 to cmd2\n"
              << "  cmd < file     Input redirection\n"
              << "  cmd > file     Output redirection\n"
              << "  cmd >> file    Append output redirection\n"
              << "  $VAR or ${VAR} Environment variable expansion\n";
}

// Execute a single command (in a child process)
bool executeCommand(const Command& command, int inputFd, int outputFd) {
    if (command.args.empty()) {
        return false;
    }
    
    // Handle built-in commands directly if this is the parent process
    if (getpid() == getppid() + 1 && executeBuiltInCommand(command.args)) {
        return true;
    }
    
    // Create a child process to execute the command
    pid_t pid = getpid() == getppid() + 1 ? fork() : 0;
    
    if (pid < 0) {
        // Fork failed
        std::cerr << "Fork failed: " << strerror(errno) << std::endl;
        return false;
    } else if (pid == 0) {
        // Child process
        
        // Handle input redirection
        if (!command.inputFile.empty()) {
            int fd = open(command.inputFile.c_str(), O_RDONLY);
            if (fd == -1) {
                std::cerr << "Cannot open input file: " << command.inputFile << ": "
                          << strerror(errno) << std::endl;
                exit(EXIT_FAILURE);
            }
            dup2(fd, STDIN_FILENO);
            close(fd);
        } else if (inputFd != -1) {
            dup2(inputFd, STDIN_FILENO);
            close(inputFd);
        }
        
        // Handle output redirection
        if (!command.outputFile.empty()) {
            int flags = O_WRONLY | O_CREAT;
            if (command.appendOutput) {
                flags |= O_APPEND;
            } else {
                flags |= O_TRUNC;
            }
            int fd = open(command.outputFile.c_str(), flags, 0644);
            if (fd == -1) {
                std::cerr << "Cannot open output file: " << command.outputFile << ": "
                          << strerror(errno) << std::endl;
                exit(EXIT_FAILURE);
            }
            dup2(fd, STDOUT_FILENO);
            close(fd);
        } else if (outputFd != -1) {
            dup2(outputFd, STDOUT_FILENO);
            close(outputFd);
        }
        
        // Prepare arguments for execvp
        std::vector<char*> argv(command.args.size() + 1);
        for (size_t i = 0; i < command.args.size(); ++i) {
            argv[i] = const_cast<char*>(command.args[i].c_str());
        }
        argv[command.args.size()] = nullptr;
        
        // Execute the command
        execvp(argv[0], argv.data());
        
        // If execvp returns, it must have failed
        std::cerr << command.args[0] << ": " << strerror(errno) << std::endl;
        exit(EXIT_FAILURE);
    } else {
        // Parent process
        
        if (!command.runInBackground) {
            // Wait for the child process to complete
            int status;
            waitpid(pid, &status, 0);
        } else {
            // Add to list of background processes
            backgroundProcesses.push_back(pid);
            std::cout << "[" << pid << "] running in background" << std::endl;
        }
    }
    
    return true;
}